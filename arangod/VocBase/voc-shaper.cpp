////////////////////////////////////////////////////////////////////////////////
/// @brief json shaper used to compute the shape of an json object
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Martin Schoenert
/// @author Copyright 2006-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "voc-shaper.h"

#include "BasicsC/associative.h"
#include "BasicsC/hashes.h"
#include "BasicsC/locks.h"
#include "BasicsC/logging.h"
#include "BasicsC/tri-strings.h"
#include "BasicsC/utf8-helper.h"
#include "VocBase/document-collection.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief attribute weight
////////////////////////////////////////////////////////////////////////////////

typedef struct attribute_weight_s {
  TRI_shape_aid_t              _aid;
  int64_t                      _weight;
  char*                        _attribute;
  struct attribute_weight_s*   _next;
}
attribute_weight_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief attribute weights
////////////////////////////////////////////////////////////////////////////////

typedef struct attribute_weights_s {
  attribute_weight_t*          _first;
  attribute_weight_t*          _last;
  size_t                       _length;
}
attribute_weights_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief weighted attribute
////////////////////////////////////////////////////////////////////////////////

typedef struct weighted_attribute_s {
  TRI_shape_aid_t              _aid;
  int64_t                      _weight;  
  TRI_shaped_json_t            _value;
} 
weighted_attribute_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief collection-based shaper
////////////////////////////////////////////////////////////////////////////////

typedef struct voc_shaper_s {
  TRI_shaper_t                 base;

  TRI_associative_synced_t     _attributeNames;
  TRI_associative_synced_t     _attributeIds;
  TRI_associative_synced_t     _shapeDictionary;
  TRI_associative_synced_t     _shapeIds;

  TRI_associative_pointer_t    _accessors;

  TRI_vector_pointer_t         _sortedAttributes;
  TRI_associative_pointer_t    _weightedAttributes;
  attribute_weights_t          _weights;

  TRI_shape_aid_t              _nextAid;
  TRI_shape_sid_t              _nextSid;

  TRI_document_collection_t*   _collection;

  TRI_mutex_t                  _shapeLock;
  TRI_mutex_t                  _attributeLock;
  TRI_mutex_t                  _accessorLock;
}
voc_shaper_t;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the attribute name of a key
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyAttributeName (TRI_associative_synced_t* array, void const* key) {
  char const* k = (char const*) key;

  return TRI_FnvHashString(k);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashs the attribute name of an element
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementAttributeName (TRI_associative_synced_t* array, void const* element) {
  char const* e = (char const*) element;

  return TRI_FnvHashString(e + sizeof(TRI_df_attribute_marker_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares an attribute name and an attribute
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyAttributeName (TRI_associative_synced_t* array, void const* key, void const* element) {
  char const* k = (char const*) key;
  char const* e = (char const*) element;

  return TRI_EqualString(k, e + sizeof(TRI_df_attribute_marker_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares two attribute strings stored in the attribute marker
///
/// returns 0 if the strings are equal
/// returns < 0 if the left string compares less than the right string
/// returns > 0 if the left string compares more than the right string
////////////////////////////////////////////////////////////////////////////////

static int CompareNameAttributeWeight (const void* leftItem,
                                       const void* rightItem) {
  const attribute_weight_t* l = (const attribute_weight_t*)(leftItem);
  const attribute_weight_t* r = (const attribute_weight_t*)(rightItem);

  assert(l);
  assert(r);

  return TRI_compare_utf8(l->_attribute, r->_attribute);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares two attribute strings stored in the attribute marker
////////////////////////////////////////////////////////////////////////////////

static int CompareNameAttributeWeightPointer (const void* leftItem,
                                              const void* rightItem) {
  const attribute_weight_t* l = *((const attribute_weight_t**)(leftItem));
  const attribute_weight_t* r = *((const attribute_weight_t**)(rightItem));

  assert(l);
  assert(r);

  return TRI_compare_utf8(l->_attribute, r->_attribute);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief search for attribute
///
/// Performs a binary search on a list of attributes (attribute markers) and
/// returns the position where a given attribute would be inserted
////////////////////////////////////////////////////////////////////////////////

static int64_t SortedIndexOf (voc_shaper_t* shaper, 
                              attribute_weight_t* item) {
  int64_t leftPos;
  int64_t rightPos;
  int64_t midPos;

  leftPos  = 0;
  rightPos = ((int64_t) shaper->_sortedAttributes._length) - 1;

  while (leftPos <= rightPos)  {
    midPos = (leftPos + rightPos) / 2;
  
    int compareResult = CompareNameAttributeWeight(TRI_AtVectorPointer(&(shaper->_sortedAttributes), midPos), (void*) item);
    if (compareResult < 0) {
      leftPos = midPos + 1;
    }
    else if (compareResult > 0) {
      rightPos = midPos - 1;
    }
    else {
      // should never happen since we do not allow duplicates here
      return -1;
    }
  }
  return leftPos; // insert it to the left of this position
}
 
////////////////////////////////////////////////////////////////////////////////
/// @brief sets the attribute weight
///
/// helper method to store a list of attributes and their corresponding weights.
////////////////////////////////////////////////////////////////////////////////

static void SetAttributeWeight (voc_shaper_t* shaper,
                                attribute_weight_t* item,
                                int64_t* searchResult,
                                bool* weighted) {
  int64_t itemWeight;
  attribute_weight_t* leftItem;  // nearest neighbour items
  attribute_weight_t* rightItem; // nearest neighbour items
  const int64_t resolution = 100;
  void* result;

  *weighted = false;

  assert(item != nullptr);
  assert(shaper != nullptr);

  *searchResult = SortedIndexOf(shaper, item);

  if (*searchResult < 0 || 
      *searchResult > (int64_t) shaper->_sortedAttributes._length) { // oops
    assert(false);
    return;
  }

  switch ( (shaper->_sortedAttributes)._length) {
    case 0: {
      item->_weight = 0;
      *weighted = true;
      break;
    }

    case 1: {
      if (*searchResult == 0) {
        item->_weight = 0;
        rightItem = (attribute_weight_t*)(TRI_AtVectorPointer(&(shaper->_sortedAttributes), 0));
        rightItem->_weight = resolution;
      }
      else {
        leftItem  = (attribute_weight_t*)(TRI_AtVectorPointer(&(shaper->_sortedAttributes), 0));
        leftItem->_weight = 0;
        item->_weight = resolution;
      }
      *weighted = true;
      break;
    }

    default: {
      if (*searchResult == 0) {
        rightItem = (attribute_weight_t*)(TRI_AtVectorPointer(&(shaper->_sortedAttributes), 0));
        item->_weight = rightItem->_weight - resolution;
        *weighted = true;
      }
      else if (*searchResult == (int64_t) (shaper->_sortedAttributes)._length) {
        leftItem  = (attribute_weight_t*)(TRI_AtVectorPointer(&(shaper->_sortedAttributes), (shaper->_sortedAttributes)._length - 1));
        item->_weight = leftItem->_weight + resolution;
        *weighted = true;
      }
      else {
        leftItem  = (attribute_weight_t*)(TRI_AtVectorPointer(&(shaper->_sortedAttributes), *searchResult - 1));
        rightItem = (attribute_weight_t*)(TRI_AtVectorPointer(&(shaper->_sortedAttributes), *searchResult));
        itemWeight = (rightItem->_weight + leftItem->_weight) / 2;

        if (leftItem->_weight != itemWeight && 
            rightItem->_weight != itemWeight) {
          item->_weight = itemWeight;
          *weighted = true;
        }
      }
      break;
    }
  } // end of switch statement

  result = TRI_InsertKeyAssociativePointer(&(shaper->_weightedAttributes), &(item->_aid), item, false);

  if (result != nullptr) {
    LOG_ERROR("attribute weight could not be inserted into associative array");
    *searchResult = -1;
    return;
  }

  // ...........................................................................
  // Obtain the pointer of the weighted attribute structure which is stored
  // in the associative array. We need to pass this pointer to the Vector Pointer
  // ...........................................................................

  item = static_cast<attribute_weight_t*>(TRI_LookupByKeyAssociativePointer(&(shaper->_weightedAttributes), &(item->_aid)));

  if (item == nullptr) {
    LOG_ERROR("attribute weight could not be located immediately after insert into associative array");
    *searchResult = -1;
    return;
  }

  TRI_InsertVectorPointer(&(shaper->_sortedAttributes), item, (size_t) (*searchResult));
}
 
////////////////////////////////////////////////////////////////////////////////
/// @brief sets the attribute weight
////////////////////////////////////////////////////////////////////////////////

static void FullSetAttributeWeight (voc_shaper_t* shaper) {
  int64_t startWeight;

  startWeight = 0;

  for (size_t j = 0; j < shaper->_sortedAttributes._length; ++j) {
    attribute_weight_t* item = (attribute_weight_t*) TRI_AtVectorPointer(&(shaper->_sortedAttributes), j);
    item->_weight = startWeight;
    startWeight += 100;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds an attribute identifier by name
////////////////////////////////////////////////////////////////////////////////

static TRI_shape_aid_t LookupAttributeByName (TRI_shaper_t* shaper, 
                                              char const* name) {
  voc_shaper_t* s;
  void const* p;

  assert(name != nullptr);

  s = (voc_shaper_t*) shaper;
  p = TRI_LookupByKeyAssociativeSynced(&s->_attributeNames, name);

  if (p != nullptr) {
    return ((TRI_df_attribute_marker_t const*) p)->_aid;
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds an attribute identifier by name
////////////////////////////////////////////////////////////////////////////////

static TRI_shape_aid_t FindOrCreateAttributeByName (TRI_shaper_t* shaper, 
                                                    char const* name,
                                                    bool isLocked) { 
  char* mem;
  TRI_df_attribute_marker_t* marker;
  TRI_df_marker_t* result;
  TRI_df_attribute_marker_t* markerResult;
  TRI_doc_datafile_info_t* dfi;
  TRI_voc_fid_t fid;
  int res;
  size_t n;
  voc_shaper_t* s;
  TRI_shape_aid_t aid;
  TRI_voc_size_t totalSize;
  void const* p;
  void* f;
  int64_t searchResult;
  bool weighted;

  assert(name != nullptr);

  s = (voc_shaper_t*) shaper;
  p = TRI_LookupByKeyAssociativeSynced(&s->_attributeNames, name);

  if (p != nullptr) {
    return ((TRI_df_attribute_marker_t const*) p)->_aid;
  }

  // create a new attribute name
  n = strlen(name) + 1;
  
  totalSize = (TRI_voc_size_t) (sizeof(TRI_df_attribute_marker_t) + n);
  mem = (char*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, totalSize, false);

  if (mem == nullptr) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return 0;
  }

  // marker points to mem, but has a different type
  marker = (TRI_df_attribute_marker_t*) mem;
  assert(marker != nullptr);

  // init attribute marker
  TRI_InitMarker(mem, TRI_DF_MARKER_ATTRIBUTE, totalSize);
  
  // copy attribute name into marker
  memcpy(mem + sizeof(TRI_df_attribute_marker_t), name, n);
  marker->_size = n;

  // lock the index and check that the element is still missing
  TRI_LockMutex(&s->_attributeLock);

  p = TRI_LookupByKeyAssociativeSynced(&s->_attributeNames, name);

  // if the element appeared, return the aid
  if (p != nullptr) {
    TRI_UnlockMutex(&s->_attributeLock);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, mem);

    return ((TRI_df_attribute_marker_t const*) p)->_aid;
  }

  // get next attribute id and write into marker
  aid = s->_nextAid++;
  marker->_aid = aid;
  
  if (! isLocked) {
    // write-lock
    s->_collection->base.beginWrite(&s->_collection->base);
  }

  // write attribute into the collection
  res = TRI_WriteMarkerDocumentCollection(s->_collection, &marker->base, totalSize, &fid, &result, false);
  
  if (! isLocked) {
    s->_collection->base.endWrite(&s->_collection->base);
  }
 
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, mem);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_UnlockMutex(&s->_attributeLock);

    LOG_ERROR("an error occurred while writing attribute data into shapes collection: %s", 
              TRI_errno_string(res));
    return 0;
  }
  
  assert(result != nullptr);

  // update datafile info
  dfi = TRI_FindDatafileInfoPrimaryCollection(&s->_collection->base, fid, true);

  if (dfi != nullptr) {
    dfi->_numberAttributes++;
    dfi->_sizeAttributes += (int64_t) TRI_DF_ALIGN_BLOCK(totalSize);
  }
  
  f = TRI_InsertKeyAssociativeSynced(&s->_attributeIds, &aid, result, false);
  assert(f == nullptr);
  
  // enter into the dictionaries
  f = TRI_InsertKeyAssociativeSynced(&s->_attributeNames, name, result, false);
  assert(f == nullptr);

  // ...........................................................................
  // Each attribute has an associated integer as a weight. This
  // weight corresponds to the natural ordering of the attribute strings
  // ...........................................................................

  markerResult = (TRI_df_attribute_marker_t*) result;

  attribute_weight_t* weightedAttribute = static_cast<attribute_weight_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(attribute_weight_t), false));

  if (weightedAttribute != nullptr) {
    weightedAttribute->_aid       = markerResult->_aid;
    weightedAttribute->_weight    = TRI_VOC_UNDEFINED_ATTRIBUTE_WEIGHT;
    weightedAttribute->_attribute = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, name);
    weightedAttribute->_next      = nullptr;

    // ..........................................................................
    // Save the new attribute weight in the linked list.
    // ..........................................................................

    if ((s->_weights)._last == nullptr) {
      (s->_weights)._first = weightedAttribute;
    }
    else {
      ((s->_weights)._last)->_next = weightedAttribute;
    }

    (s->_weights)._last = weightedAttribute;  
    (s->_weights)._length += 1;

    SetAttributeWeight(s, weightedAttribute, &searchResult, &weighted);
    assert(searchResult > -1);

    if (! weighted) {  
      FullSetAttributeWeight(s);
    }
  }

  else {
    LOG_WARNING("FindAttributeByName could not allocate memory, attribute is NOT weighted");
  }

  // ...........................................................................
  // and release the lock
  // ...........................................................................

  TRI_UnlockMutex(&s->_attributeLock);

  return aid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes the attribute id
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyAttributeId (TRI_associative_synced_t* array, void const* key) {
  TRI_shape_aid_t const* k = static_cast<TRI_shape_aid_t const*>(key);

  return TRI_FnvHashPointer(k, sizeof(TRI_shape_aid_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes the attribute
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementAttributeId (TRI_associative_synced_t* array, void const* element) {
  TRI_df_attribute_marker_t const* e = static_cast<TRI_df_attribute_marker_t const*>(element);

  return TRI_FnvHashPointer(&e->_aid, sizeof(TRI_shape_aid_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares an attribute name and an attribute
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyAttributeId (TRI_associative_synced_t* array, void const* key, void const* element) {
  TRI_shape_aid_t const* k = static_cast<TRI_shape_aid_t const*>(key);
  TRI_df_attribute_marker_t const* e = static_cast<TRI_df_attribute_marker_t const*>(element);

  return *k == e->_aid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an attribute name by identifier
////////////////////////////////////////////////////////////////////////////////

static char const* LookupAttributeId (TRI_shaper_t* shaper, 
                                      TRI_shape_aid_t aid) {
  voc_shaper_t* s = (voc_shaper_t*) shaper;

  void const* p = TRI_LookupByKeyAssociativeSynced(&s->_attributeIds, &aid);

  if (p == nullptr) {
    return nullptr;
  }

  return static_cast<char const*>(p) + sizeof(TRI_df_attribute_marker_t);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an attribute weight by identifier
////////////////////////////////////////////////////////////////////////////////

static int64_t LookupAttributeWeight (TRI_shaper_t* shaper, 
                                      TRI_shape_aid_t aid) {
  voc_shaper_t* s = (voc_shaper_t*) shaper;
  const attribute_weight_t* item;

  item = (const attribute_weight_t*)(TRI_LookupByKeyAssociativePointer(&s->_weightedAttributes, &aid));

  if (item == nullptr) {
    // ........................................................................
    // return -9223372036854775807L 2^63-1 to indicate that the attribute is
    // weighted to be the lowest possible
    // ........................................................................
    LOG_WARNING("LookupAttributeWeight returned NULL weight");
    return TRI_VOC_UNDEFINED_ATTRIBUTE_WEIGHT;
  }

  if (item->_aid != aid) {
    // ........................................................................
    // return -9223372036854775807L 2^63-1 to indicate that the attribute is
    // weighted to be the lowest possible
    // ........................................................................
    LOG_WARNING("LookupAttributeWeight returned an UNDEFINED weight");
    return TRI_VOC_UNDEFINED_ATTRIBUTE_WEIGHT;
  }

  return item->_weight;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes the shapes
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementShape (TRI_associative_synced_t* array, 
                                  void const* element) {
  char const* e = static_cast<char const*>(element);
  TRI_shape_t const* ee = static_cast<TRI_shape_t const*>(element);

  return TRI_FnvHashPointer(e + + sizeof(TRI_shape_sid_t), ee->_size - sizeof(TRI_shape_sid_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares shapes
////////////////////////////////////////////////////////////////////////////////

static bool EqualElementShape (TRI_associative_synced_t* array, 
                               void const* left, 
                               void const* right) {
  char const* l = static_cast<char const*>(left);
  char const* r = static_cast<char const*>(right);

  TRI_shape_t const* ll = static_cast<TRI_shape_t const*>(left);
  TRI_shape_t const* rr = static_cast<TRI_shape_t const*>(right);

  return (ll->_size == rr->_size)
    && memcmp(l + sizeof(TRI_shape_sid_t),
              r + sizeof(TRI_shape_sid_t),
              ll->_size - sizeof(TRI_shape_sid_t)) == 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds a shape
/// if the function returns non-nullptr, the return value is a pointer to an
/// already existing shape and the value must not be freed
/// if the function returns nullptr, it has not found the shape and was not able
/// to create it. The value must then be freed by the caller
////////////////////////////////////////////////////////////////////////////////

static TRI_shape_t const* FindShape (TRI_shaper_t* shaper, 
                                     TRI_shape_t* shape,
                                     bool create,
                                     bool isLocked) {
  char* mem;
  TRI_df_marker_t* result;
  TRI_df_shape_marker_t* marker;
  TRI_doc_datafile_info_t* dfi;
  TRI_voc_fid_t fid;
  TRI_voc_size_t totalSize;
  TRI_shape_t const* found;
  TRI_shape_t* l;
  int res;
  voc_shaper_t* s;
  void* f;

  s = (voc_shaper_t*) shaper;

  found = TRI_LookupBasicShapeShaper(shape);

  if (found == nullptr) {
    found = static_cast<TRI_shape_t const*>(TRI_LookupByElementAssociativeSynced(&s->_shapeDictionary, shape));
  }

  // shape found, free argument and return
  if (found != nullptr) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, shape);

    return found;
  }

  // not found
  if (! create) {
    return 0;
  }

  // initialise a new shape marker
  totalSize = (TRI_voc_size_t) (sizeof(TRI_df_shape_marker_t) + shape->_size);
  mem = (char*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, totalSize, false);

  if (mem == nullptr) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return nullptr;
  }

  marker = (TRI_df_shape_marker_t*) mem;
  assert(marker != nullptr);

  TRI_InitMarker(mem, TRI_DF_MARKER_SHAPE, totalSize);
  
  // copy shape into the marker
  memcpy(mem + sizeof(TRI_df_shape_marker_t), shape, shape->_size);


  // lock the index and check the element is still missing
  TRI_LockMutex(&s->_shapeLock);

  found = static_cast<TRI_shape_t const*>(TRI_LookupByElementAssociativeSynced(&s->_shapeDictionary, shape));

  if (found != 0) {
    TRI_UnlockMutex(&s->_shapeLock);

    TRI_Free(TRI_UNKNOWN_MEM_ZONE, shape);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, mem);

    return found;
  }

  // get next shape number and write into marker
  ((TRI_shape_t*) (mem + sizeof(TRI_df_shape_marker_t)))->_sid = s->_nextSid++;

  if (! isLocked) {
    // write-lock
    s->_collection->base.beginWrite(&s->_collection->base);
  }

  // write shape into the collection
  res = TRI_WriteMarkerDocumentCollection(s->_collection, &marker->base, totalSize, &fid, &result, false);

  if (! isLocked) {
    // write-unlock
    s->_collection->base.endWrite(&s->_collection->base);
  }

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_UnlockMutex(&s->_shapeLock);

    LOG_ERROR("an error occurred while writing shape data into shapes collection: %s", 
              TRI_errno_string(res));
  
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, mem);

    return nullptr;
  }
  
  assert(result != nullptr);
  
  // update datafile info
  dfi = TRI_FindDatafileInfoPrimaryCollection(&s->_collection->base, fid, true);

  if (dfi != nullptr) {
    dfi->_numberShapes++;
    dfi->_sizeShapes += (int64_t) TRI_DF_ALIGN_BLOCK(totalSize);
  }
    
  // enter into the dictionaries
  l = (TRI_shape_t*) (((char*) result) + sizeof(TRI_df_shape_marker_t));
  
  f = TRI_InsertKeyAssociativeSynced(&s->_shapeIds, &l->_sid, l, false);
  assert(f == nullptr);

  f = TRI_InsertElementAssociativeSynced(&s->_shapeDictionary, l, false);
  assert(f == nullptr);

  TRI_UnlockMutex(&s->_shapeLock);
  
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, mem);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, shape);

  return l;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares to weighted attributes
////////////////////////////////////////////////////////////////////////////////

static int AttributeWeightCompareFunction (const void* leftItem, const void* rightItem) {
  const weighted_attribute_t* l = (const weighted_attribute_t*) leftItem;
  const weighted_attribute_t* r = (const weighted_attribute_t*) rightItem;

  if (l->_weight < r->_weight) { 
    return -1; 
  }
  if (l->_weight > r->_weight) { 
    return  1; 
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free weighted attribute
///
/// Helper method to deal with freeing memory associated with the weight 
/// comparisions.
////////////////////////////////////////////////////////////////////////////////

static void FreeShapeTypeJsonArrayHelper (weighted_attribute_t** leftWeightedList, 
                                          weighted_attribute_t** rightWeightedList) {
  if (*leftWeightedList != nullptr) {                                         
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, *leftWeightedList);
    *leftWeightedList = nullptr;
  }  

  if (*rightWeightedList != nullptr) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, *rightWeightedList);
    *rightWeightedList = nullptr;
  }                                           
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of entries
////////////////////////////////////////////////////////////////////////////////

static int CompareShapeTypeJsonArrayHelper (const TRI_shape_t* shape,
                                            const TRI_shaper_t* shaper, 
                                            const TRI_shaped_json_t* shapedJson,
                                            weighted_attribute_t** attributeArray) {
  char* charShape = (char*)(shape);
  TRI_shape_size_t fixedEntries;     // the number of entries in the JSON array whose value is of a fixed size
  TRI_shape_size_t variableEntries;  // the number of entries in the JSON array whose value is not of a known fixed size
  TRI_shape_size_t j;
  const TRI_shape_aid_t* aids;
  const TRI_shape_sid_t* sids;
  const TRI_shape_size_t* offsets;
  
  // .............................................................................
  // Ensure we return an empty array - in case of funny business below
  // .............................................................................
  
  *attributeArray = nullptr;
  
  // .............................................................................
  // Determine the number of fixed sized values
  // .............................................................................
  
  charShape = charShape + sizeof(TRI_shape_t);           
  fixedEntries = *((TRI_shape_size_t*)(charShape));

  // .............................................................................
  // Determine the number of variable sized values
  // .............................................................................
  
  charShape = charShape + sizeof(TRI_shape_size_t);
  variableEntries = *((TRI_shape_size_t*)(charShape));

  // .............................................................................
  // It may happen that the shaped_json_array is 'empty {}'
  // .............................................................................
  
  if ((fixedEntries + variableEntries) == 0) {
    return 0;
  }  
  
  // .............................................................................
  // Allocate memory to hold the attribute information required for comparison
  // .............................................................................
  
  *attributeArray = static_cast<weighted_attribute_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, (sizeof(weighted_attribute_t) * (fixedEntries + variableEntries)), false));

  if (*attributeArray == nullptr) {
    return -1;
  }

  // .............................................................................
  // Determine the list of shape identifiers
  // .............................................................................
  
  charShape = charShape + sizeof(TRI_shape_size_t);
  sids = (const TRI_shape_sid_t*)(charShape);
  
  charShape = charShape + (sizeof(TRI_shape_sid_t) * (fixedEntries + variableEntries));
  aids = (const TRI_shape_aid_t*)(charShape);

  charShape = charShape + (sizeof(TRI_shape_aid_t) * (fixedEntries + variableEntries));
  offsets = (const TRI_shape_size_t*)(charShape);
  
  for (j = 0; j < fixedEntries; ++j) {
    (*attributeArray)[j]._aid                = aids[j];
    (*attributeArray)[j]._weight             = shaper->lookupAttributeWeight((TRI_shaper_t*)(shaper),aids[j]);
    (*attributeArray)[j]._value._sid         = sids[j];
    (*attributeArray)[j]._value._data.data   = shapedJson->_data.data + offsets[j];
    (*attributeArray)[j]._value._data.length = (uint32_t) (offsets[j + 1] - offsets[j]);
  }
  
  offsets = (const TRI_shape_size_t*)(shapedJson->_data.data);
  for (j = 0; j < variableEntries; ++j) {
    TRI_shape_size_t jj = j + fixedEntries;
    (*attributeArray)[jj]._aid                = aids[jj];
    (*attributeArray)[jj]._weight             = shaper->lookupAttributeWeight((TRI_shaper_t*)(shaper),aids[jj]);
    (*attributeArray)[jj]._value._sid         = sids[jj];
    (*attributeArray)[jj]._value._data.data   = shapedJson->_data.data + offsets[j];
    (*attributeArray)[jj]._value._data.length = (uint32_t) (offsets[j + 1] - offsets[j]);
  }

  return (int) (fixedEntries + variableEntries);  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes the shape id
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyShapeId (TRI_associative_synced_t* array, 
                                void const* key) {
  TRI_shape_sid_t const* k = static_cast<TRI_shape_sid_t const*>(key);

  return TRI_FnvHashPointer(k, sizeof(TRI_shape_sid_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes the shape
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementShapeId (TRI_associative_synced_t* array, 
                                    void const* element) {
  TRI_shape_t const* e = static_cast<TRI_shape_t const*>(element);

  return TRI_FnvHashPointer(&e->_sid, sizeof(TRI_shape_sid_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a shape id and a shape
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyShapeId (TRI_associative_synced_t* array, 
                             void const* key, 
                             void const* element) {
  TRI_shape_sid_t const* k = static_cast<TRI_shape_sid_t const*>(key);
  TRI_shape_t const* e = static_cast<TRI_shape_t const*>(element);

  return *k == e->_sid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a shape by identifier
////////////////////////////////////////////////////////////////////////////////

static TRI_shape_t const* LookupShapeId (TRI_shaper_t* shaper, 
                                         TRI_shape_sid_t sid) {
  TRI_shape_t const* shape = TRI_LookupSidBasicShapeShaper(sid);

  if (shape == nullptr) {
    voc_shaper_t* s = (voc_shaper_t*) shaper;
    shape = static_cast<TRI_shape_t const*>(TRI_LookupByKeyAssociativeSynced(&s->_shapeIds, &sid));
  }

  return shape;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes the accessor
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementAccessor (TRI_associative_pointer_t* array, void const* element) {
  TRI_shape_access_t const* ee = static_cast<TRI_shape_access_t const*>(element);
  uint64_t v[2];

  v[0] = ee->_sid;
  v[1] = ee->_pid;

  return TRI_FnvHashPointer(v, sizeof(v));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares an accessor
////////////////////////////////////////////////////////////////////////////////

static bool EqualElementAccessor (TRI_associative_pointer_t* array, void const* left, void const* right) {
  TRI_shape_access_t const* ll = static_cast<TRI_shape_access_t const*>(left);
  TRI_shape_access_t const* rr = static_cast<TRI_shape_access_t const*>(right);

  return ll->_sid == rr->_sid && ll->_pid == rr->_pid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Hashes a weighted attribute
////////////////////////////////////////////////////////////////////////////////

static bool EqualKeyElementWeightedAttribute (TRI_associative_pointer_t* array, const void* key, const void* element) {
  TRI_shape_aid_t* aid = (TRI_shape_aid_t*)(key);
  attribute_weight_t* item = (attribute_weight_t*)(element);
  return (*aid == item->_aid);
}

static uint64_t HashKeyWeightedAttribute (TRI_associative_pointer_t* array, const void* key) {
  TRI_shape_aid_t* aid = (TRI_shape_aid_t*)(key);
  return TRI_FnvHashBlock(TRI_FnvHashBlockInitial(), (char*) aid, sizeof(TRI_shape_aid_t));
}

static uint64_t HashElementWeightedAttribute (TRI_associative_pointer_t* array, const void* element) {
  attribute_weight_t* item = (attribute_weight_t*)(element);
  TRI_shape_aid_t* aid = &(item->_aid);
  return TRI_FnvHashBlock(TRI_FnvHashBlockInitial(), (char*) aid, sizeof(TRI_shape_aid_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a shaper
////////////////////////////////////////////////////////////////////////////////

static int InitStep1VocShaper (voc_shaper_t* shaper) {
  int res;

  shaper->base.findOrCreateAttributeByName = FindOrCreateAttributeByName;
  shaper->base.lookupAttributeByName       = LookupAttributeByName;
  shaper->base.lookupAttributeId           = LookupAttributeId;
  shaper->base.findShape                   = FindShape;
  shaper->base.lookupShapeId               = LookupShapeId;

  res = TRI_InitAssociativeSynced(&shaper->_attributeNames,
                                  TRI_UNKNOWN_MEM_ZONE,
                                  HashKeyAttributeName,
                                  HashElementAttributeName,
                                  EqualKeyAttributeName,
                                  0);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  res = TRI_InitAssociativeSynced(&shaper->_attributeIds,
                                  TRI_UNKNOWN_MEM_ZONE,
                                  HashKeyAttributeId,
                                  HashElementAttributeId,
                                  EqualKeyAttributeId,
                                  0);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativeSynced(&shaper->_attributeNames);

    return res;
  }

  res = TRI_InitAssociativeSynced(&shaper->_shapeDictionary,
                                  TRI_UNKNOWN_MEM_ZONE,
                                  0,
                                  HashElementShape,
                                  0,
                                  EqualElementShape);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativeSynced(&shaper->_attributeIds);
    TRI_DestroyAssociativeSynced(&shaper->_attributeNames);

    return res;
  }

  res = TRI_InitAssociativeSynced(&shaper->_shapeIds,
                                  TRI_UNKNOWN_MEM_ZONE,
                                  HashKeyShapeId,
                                  HashElementShapeId,
                                  EqualKeyShapeId,
                                  0);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativeSynced(&shaper->_shapeDictionary);
    TRI_DestroyAssociativeSynced(&shaper->_attributeIds);
    TRI_DestroyAssociativeSynced(&shaper->_attributeNames);

    return res;
  }

  res = TRI_InitAssociativePointer(&shaper->_accessors,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   0,
                                   HashElementAccessor,
                                   0,
                                   EqualElementAccessor);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativeSynced(&shaper->_shapeIds);
    TRI_DestroyAssociativeSynced(&shaper->_shapeDictionary);
    TRI_DestroyAssociativeSynced(&shaper->_attributeIds);
    TRI_DestroyAssociativeSynced(&shaper->_attributeNames);

    return res;
  }

  res = TRI_InitAssociativePointer(&shaper->_weightedAttributes,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   HashKeyWeightedAttribute,
                                   HashElementWeightedAttribute,
                                   EqualKeyElementWeightedAttribute,
                                   nullptr);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativePointer(&shaper->_accessors);
    TRI_DestroyAssociativeSynced(&shaper->_shapeIds);
    TRI_DestroyAssociativeSynced(&shaper->_shapeDictionary);
    TRI_DestroyAssociativeSynced(&shaper->_attributeIds);
    TRI_DestroyAssociativeSynced(&shaper->_attributeNames);

    return res;
  }
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a shaper
////////////////////////////////////////////////////////////////////////////////

static int InitStep2VocShaper (voc_shaper_t* shaper) { 
  TRI_InitMutex(&shaper->_shapeLock);
  TRI_InitMutex(&shaper->_attributeLock);
  TRI_InitMutex(&shaper->_accessorLock);

  shaper->_nextAid = 1;
  shaper->_nextSid = 7; // TODO!!!!!!!!!!!!!!!!!

  // ..........................................................................
  // Attribute weight
  // ..........................................................................

  shaper->base.lookupAttributeWeight = LookupAttributeWeight;

  TRI_InitVectorPointer(&shaper->_sortedAttributes, TRI_UNKNOWN_MEM_ZONE);

  // ..........................................................................
  // We require a place to store the weights -- a linked list is as good as
  // anything for now. Later try and store in a smart 2D array.
  // ..........................................................................

  (shaper->_weights)._first  = nullptr;
  (shaper->_weights)._last   = nullptr;
  (shaper->_weights)._length = 0;

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises a shaper
////////////////////////////////////////////////////////////////////////////////

static int InitStep3VocShaper (voc_shaper_t* shaper) { 
  // .............................................................................
  // Sort all the attributes using the attribute string
  // .............................................................................
  
  qsort(shaper->_sortedAttributes._buffer, 
        shaper->_sortedAttributes._length, 
        sizeof(attribute_weight_t*), 
        CompareNameAttributeWeightPointer);
  
  
  // .............................................................................
  // re-weigh all of the attributes
  // .............................................................................

  FullSetAttributeWeight(shaper);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a shaper
////////////////////////////////////////////////////////////////////////////////

TRI_shaper_t* TRI_CreateVocShaper (TRI_vocbase_t* vocbase,
                                   TRI_document_collection_t* document) {
  voc_shaper_t* shaper = static_cast<voc_shaper_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(voc_shaper_t), false));

  if (shaper == nullptr) {
    // out of memory
    return nullptr;
  }

  shaper->_collection = document;

  int res = TRI_InitShaper(&shaper->base, TRI_UNKNOWN_MEM_ZONE);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, shaper);

    return nullptr;
  }

  res = InitStep1VocShaper(shaper);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeShaper(&shaper->base);

    return nullptr;
  }

  res = InitStep2VocShaper(shaper);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeVocShaper(&shaper->base);

    return nullptr;
  }

  // and return
  return &shaper->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a shaper, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyVocShaper (TRI_shaper_t* s) {
  voc_shaper_t* shaper = (voc_shaper_t*) s;
  attribute_weight_t* weightedAttribute;
  attribute_weight_t* nextWeightedAttribute;
  size_t i;

  assert(shaper != nullptr);

  TRI_DestroyAssociativeSynced(&shaper->_attributeNames);
  TRI_DestroyAssociativeSynced(&shaper->_attributeIds);
  TRI_DestroyAssociativeSynced(&shaper->_shapeDictionary);
  TRI_DestroyAssociativeSynced(&shaper->_shapeIds);

  for (i = 0; i < shaper->_accessors._nrAlloc; ++i) {
    TRI_shape_access_t* accessor = (TRI_shape_access_t*) shaper->_accessors._table[i];
    if (accessor != nullptr) {
      TRI_FreeShapeAccessor(accessor);
    }
  }
  TRI_DestroyAssociativePointer(&shaper->_accessors);

  TRI_DestroyMutex(&shaper->_shapeLock);
  TRI_DestroyMutex(&shaper->_attributeLock);

  // ..........................................................................
  // Attribute weight
  // ..........................................................................

  TRI_DestroyVectorPointer(&shaper->_sortedAttributes);
  TRI_DestroyAssociativePointer(&shaper->_weightedAttributes);

  weightedAttribute = (shaper->_weights)._first;
  while (weightedAttribute != nullptr) {
    nextWeightedAttribute = weightedAttribute->_next;

    // free attribute name
    if (weightedAttribute->_attribute != nullptr) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, weightedAttribute->_attribute);
    }

    TRI_Free(TRI_UNKNOWN_MEM_ZONE, weightedAttribute);
    weightedAttribute = nextWeightedAttribute;
  }

  TRI_DestroyShaper(s);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a shaper and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeVocShaper (TRI_shaper_t* shaper) {
  TRI_DestroyVocShaper(shaper);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, shaper);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a shaper, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

int TRI_InitVocShaper (TRI_shaper_t* s) {
  voc_shaper_t* shaper = (voc_shaper_t*) s;

  return InitStep3VocShaper(shaper);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief move a shape marker, called during compaction
////////////////////////////////////////////////////////////////////////////////

int TRI_MoveMarkerVocShaper (TRI_shaper_t* s, 
                             TRI_df_marker_t* marker) {
  voc_shaper_t* shaper = (voc_shaper_t*) s;

  if (marker->_type == TRI_DF_MARKER_SHAPE) {
    char* p = ((char*) marker) + sizeof(TRI_df_shape_marker_t);
    TRI_shape_t* l = (TRI_shape_t*) p;
    void* f;

    TRI_LockMutex(&shaper->_shapeLock);

    // remove the old marker
    // and re-insert the marker with the new pointer
    f = TRI_InsertKeyAssociativeSynced(&shaper->_shapeIds, &l->_sid, l, true);
    assert(f != nullptr);
  
    // same for the shape dictionary
    // delete and re-insert 
    f = TRI_InsertElementAssociativeSynced(&shaper->_shapeDictionary, l, true);
    assert(f != nullptr);
    
    TRI_UnlockMutex(&shaper->_shapeLock);
  }
  else if (marker->_type == TRI_DF_MARKER_ATTRIBUTE) {
    TRI_df_attribute_marker_t* m = (TRI_df_attribute_marker_t*) marker;
    char* p = ((char*) m) + sizeof(TRI_df_attribute_marker_t);
    void* f;
    
    TRI_LockMutex(&shaper->_attributeLock);
 
    // remove attribute by name (p points to new location of name, but names
    // are identical in old and new marker) 
    // and re-insert same attribute with adjusted pointer
    f = TRI_InsertKeyAssociativeSynced(&shaper->_attributeNames, p, m, true);
    assert(f != nullptr);

    // same for attribute ids
    // delete and re-insert same attribute with adjusted pointer
    f = TRI_InsertKeyAssociativeSynced(&shaper->_attributeIds, &m->_aid, m, true);
    assert(f != nullptr);
    
    TRI_UnlockMutex(&shaper->_attributeLock);
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief insert a shape, called when opening a collection
////////////////////////////////////////////////////////////////////////////////

int TRI_InsertShapeVocShaper (TRI_shaper_t* s,
                              TRI_df_marker_t const* marker) {
  voc_shaper_t* shaper = (voc_shaper_t*) s;
  char* p = ((char*) marker) + sizeof(TRI_df_shape_marker_t);
  TRI_shape_t* l = (TRI_shape_t*) p;
  void* f;

  LOG_TRACE("found shape %lu", (unsigned long) l->_sid);

  f = TRI_InsertElementAssociativeSynced(&shaper->_shapeDictionary, l, false);
  assert(f == nullptr);

  f = TRI_InsertKeyAssociativeSynced(&shaper->_shapeIds, &l->_sid, l, false);
  assert(f == nullptr);

  if (shaper->_nextSid <= l->_sid) {
    shaper->_nextSid = l->_sid + 1;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief insert an attribute, called when opening a collection
////////////////////////////////////////////////////////////////////////////////

int TRI_InsertAttributeVocShaper (TRI_shaper_t* s,
                                  TRI_df_marker_t const* marker) {
  voc_shaper_t* shaper = (voc_shaper_t*) s;
  TRI_df_attribute_marker_t* m = (TRI_df_attribute_marker_t*) marker;
  char* p = ((char*) m) + sizeof(TRI_df_attribute_marker_t);
  void* f;

  LOG_TRACE("found attribute '%s', aid: %lu", p, (unsigned long) m->_aid);

  f = TRI_InsertKeyAssociativeSynced(&shaper->_attributeNames, p, m, false);

  if (f != nullptr) {
    char const* name = shaper->_collection->base.base._info._name;

#ifdef TRI_ENABLE_MAINTAINER_MODE
    LOG_WARNING("found duplicate attribute name '%s' in collection '%s'", p, name);
#else
    LOG_TRACE("found duplicate attribute name '%s' in collection '%s'", p, name); 
#endif    
  }

  f = TRI_InsertKeyAssociativeSynced(&shaper->_attributeIds, &m->_aid, m, false);
  
  if (f != nullptr) {
    char const* name = shaper->_collection->base.base._info._name;

#ifdef TRI_ENABLE_MAINTAINER_MODE
    LOG_WARNING("found duplicate attribute id '%llu' in collection '%s'", (unsigned long long) m->_aid, name);
#else
    LOG_TRACE("found duplicate attribute id '%llu' in collection '%s'", (unsigned long long) m->_aid, name);
#endif    
  }

  // no lock is necessary here as we are the only users of the shaper at this time
  if (shaper->_nextAid <= m->_aid) {
    shaper->_nextAid = m->_aid + 1;
  }


  // .........................................................................
  // Add the attributes to the 'sorted' vector. These are in random order
  // at the moment, however they will be sorted once all the attributes
  // have been loaded into memory.
  // .........................................................................

  attribute_weight_t* weightedAttribute = static_cast<attribute_weight_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(attribute_weight_t), false));

  if (weightedAttribute != nullptr) {
    attribute_weight_t* result;

    weightedAttribute->_aid       = m->_aid;
    weightedAttribute->_weight    = TRI_VOC_UNDEFINED_ATTRIBUTE_WEIGHT;
    weightedAttribute->_attribute = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, (char*) m + sizeof(TRI_df_attribute_marker_t));
    weightedAttribute->_next      = nullptr;

    // ..........................................................................
    // Save the new attribute weight in the linked list.
    // ..........................................................................

    if ((shaper->_weights)._last == nullptr) {
      (shaper->_weights)._first = weightedAttribute;
    }
    else {
      ((shaper->_weights)._last)->_next = weightedAttribute;
    }

    (shaper->_weights)._last = weightedAttribute;
    (shaper->_weights)._length += 1;

    result = (attribute_weight_t*) TRI_InsertKeyAssociativePointer(&(shaper->_weightedAttributes), &(weightedAttribute->_aid), weightedAttribute, false);

    if (result == nullptr) {
      attribute_weight_t* weightedItem = static_cast<attribute_weight_t*>(TRI_LookupByKeyAssociativePointer(&(shaper->_weightedAttributes), &(weightedAttribute->_aid)));

      if (weightedItem == nullptr ||
          weightedItem->_aid != weightedAttribute->_aid) {
        LOG_ERROR("attribute weight could not be located immediately after insert into associative array");
      }
      else {
        TRI_PushBackVectorPointer(&shaper->_sortedAttributes, weightedItem);
      }
    
      return TRI_ERROR_NO_ERROR;
    }
    
    LOG_WARNING("weighted attribute could not be inserted into associative array");
  }
  else {
    LOG_WARNING("OpenIterator could not allocate memory, attribute is NOT weighted");
  }
  
  return TRI_ERROR_OUT_OF_MEMORY;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finds an accessor for a shaper
////////////////////////////////////////////////////////////////////////////////

TRI_shape_access_t const* TRI_FindAccessorVocShaper (TRI_shaper_t* s,
                                                     TRI_shape_sid_t sid,
                                                     TRI_shape_pid_t pid) {
  voc_shaper_t* shaper = (voc_shaper_t*) s;
  TRI_shape_access_t search;
  TRI_shape_access_t* accessor;

  search._sid = sid;
  search._pid = pid;

  TRI_LockMutex(&shaper->_accessorLock);
  TRI_shape_access_t const* found = static_cast<TRI_shape_access_t const*>(TRI_LookupByElementAssociativePointer(&shaper->_accessors, &search));

  if (found == nullptr) {
    found = accessor = TRI_ShapeAccessor(&shaper->base, sid, pid);

    // TRI_ShapeAccessor can return a nullptr pointer
    if (found != nullptr) {
      TRI_InsertElementAssociativePointer(&shaper->_accessors, accessor, true);
    }
  }

  TRI_UnlockMutex(&shaper->_accessorLock);

  return found;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts a sub-shape
////////////////////////////////////////////////////////////////////////////////

bool TRI_ExtractShapedJsonVocShaper (TRI_shaper_t* shaper,
                                     TRI_shaped_json_t const* document,
                                     TRI_shape_sid_t sid,
                                     TRI_shape_pid_t pid,
                                     TRI_shaped_json_t* result,
                                     TRI_shape_t const** shape) {
  TRI_shape_access_t const* accessor;
  bool ok;

  accessor = TRI_FindAccessorVocShaper(shaper, document->_sid, pid);

  if (accessor == nullptr) {
    LOG_TRACE("failed to get accessor for sid %lu and path %lu",
              (unsigned long) document->_sid,
              (unsigned long) pid);

    return false;
  }

  if (accessor->_resultSid == TRI_SHAPE_ILLEGAL) {
    LOG_TRACE("expecting any object for path %lu, got nothing",
              (unsigned long) pid);

    *shape = nullptr;

    return sid == TRI_SHAPE_ILLEGAL;
  }

  *shape = shaper->lookupShapeId(shaper, accessor->_resultSid);

  if (*shape == nullptr) {
    LOG_TRACE("expecting any object for path %lu, got unknown shape id %lu",
              (unsigned long) pid,
              (unsigned long) accessor->_resultSid);

    *shape = nullptr;

    return sid == TRI_SHAPE_ILLEGAL;
  }

  if (sid != 0 && sid != accessor->_resultSid) {
    LOG_TRACE("expecting sid %lu for path %lu, got sid %lu",
              (unsigned long) sid,
              (unsigned long) pid,
              (unsigned long) accessor->_resultSid);

    return false;
  }

  ok = TRI_ExecuteShapeAccessor(accessor, document, result);

  if (! ok) {
    LOG_TRACE("failed to get accessor for sid %lu and path %lu",
              (unsigned long) document->_sid,
              (unsigned long) pid);

    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief helper method for recursion for comparison
///
/// You must either supply (leftDocument, leftObject) or leftShaped.
/// You must either supply (rightDocument, rightObject) or rightShaped.
////////////////////////////////////////////////////////////////////////////////

int TRI_CompareShapeTypes (TRI_doc_mptr_t* leftDocument,
                           TRI_shaped_sub_t* leftObject,
                           TRI_shaped_json_t const* leftShaped,
                           TRI_doc_mptr_t* rightDocument,
                           TRI_shaped_sub_t* rightObject,
                           TRI_shaped_json_t const* rightShaped,
                           TRI_shaper_t* leftShaper,
                           TRI_shaper_t* rightShaper) {
  
  TRI_shape_t const* leftShape;
  TRI_shape_t const* rightShape;
  TRI_shaped_json_t left;
  TRI_shape_type_t leftType;
  TRI_shape_type_t rightType;
  TRI_shaped_json_t leftElement;
  TRI_shaped_json_t right;
  TRI_shaped_json_t rightElement;
  char const* ptr;
  int result;
  size_t listLength;
  weighted_attribute_t* leftWeightedList;
  weighted_attribute_t* rightWeightedList;
  
  // left is either a shaped json or a shaped sub object
  if (leftDocument != nullptr) {
    ptr = (char const*) leftDocument->_data;

    left._sid = leftObject->_sid;
    left._data.length = (uint32_t) leftObject->_length;
    left._data.data = const_cast<char*>(ptr) + leftObject->_offset;
  }
  else {
    left = *leftShaped;
  }
    
  // right is either a shaped json or a shaped sub object
  if (rightDocument != nullptr) {
    ptr = (char const*) rightDocument->_data;

    right._sid = rightObject->_sid;
    right._data.length = (uint32_t) rightObject->_length;
    right._data.data = const_cast<char*>(ptr) + rightObject->_offset;
  }
  else {
    right = *rightShaped;
  }
    
  // get shape and type
  if (leftShaper == rightShaper && left._sid == right._sid) {
    // identical collection and shape
    leftShape = rightShape = leftShaper->lookupShapeId(leftShaper, left._sid);
  }
  else {
    // different shapes
    leftShape  = leftShaper->lookupShapeId(leftShaper, left._sid);
    rightShape = rightShaper->lookupShapeId(rightShaper, right._sid);
  }

  if (leftShape == nullptr || rightShape == nullptr) {
    LOG_ERROR("shape not found");
    assert(false);
  }

  leftType   = leftShape->_type;
  rightType  = rightShape->_type;

  // .............................................................................
  // check ALL combinations of leftType and rightType
  // .............................................................................

  switch (leftType) {

    // .............................................................................
    // illegal type
    // .............................................................................

    case TRI_SHAPE_ILLEGAL: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: {
          return 0;
        }
        case TRI_SHAPE_NULL:
        case TRI_SHAPE_BOOLEAN:
        case TRI_SHAPE_NUMBER:
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING:
        case TRI_SHAPE_ARRAY:
        case TRI_SHAPE_LIST:
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: {
          return -1;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_ILLEGAL

    // .............................................................................
    // nullptr
    // .............................................................................

    case TRI_SHAPE_NULL: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: {
          return 1;
        }
        case TRI_SHAPE_NULL: {
          return 0;
        }
        case TRI_SHAPE_BOOLEAN:
        case TRI_SHAPE_NUMBER:
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING:
        case TRI_SHAPE_ARRAY:
        case TRI_SHAPE_LIST:
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: {
          return -1;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_NULL

    // .............................................................................
    // BOOLEAN
    // .............................................................................

    case TRI_SHAPE_BOOLEAN: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: 
        case TRI_SHAPE_NULL: {
          return 1;
        }
        case TRI_SHAPE_BOOLEAN: {
          // check which is false and which is true!
          if ( *((TRI_shape_boolean_t*)(left._data.data)) == *((TRI_shape_boolean_t*)(right._data.data)) ) {
            return 0;          
          }  
          if ( *((TRI_shape_boolean_t*)(left._data.data)) < *((TRI_shape_boolean_t*)(right._data.data)) ) {
            return -1;          
          }  
          return 1;
        }
        case TRI_SHAPE_NUMBER:
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING:
        case TRI_SHAPE_ARRAY:
        case TRI_SHAPE_LIST:
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: {
          return -1;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_BOOLEAN

    // .............................................................................
    // NUMBER
    // .............................................................................

    case TRI_SHAPE_NUMBER: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: 
        case TRI_SHAPE_NULL:
        case TRI_SHAPE_BOOLEAN: {
          return 1;
        }
        case TRI_SHAPE_NUMBER: {
          // compare the numbers
          if ( *((TRI_shape_number_t*)(left._data.data)) == *((TRI_shape_number_t*)(right._data.data)) ) {
            return 0;          
          }  
          if ( *((TRI_shape_number_t*)(left._data.data)) < *((TRI_shape_number_t*)(right._data.data)) ) {
            return -1;          
          }  
          return 1;
        }
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING:
        case TRI_SHAPE_ARRAY:
        case TRI_SHAPE_LIST:
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: {
          return -1;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_NUMBER
    
    // .............................................................................
    // STRING
    // .............................................................................

    case TRI_SHAPE_SHORT_STRING: 
    case TRI_SHAPE_LONG_STRING: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: 
        case TRI_SHAPE_NULL:
        case TRI_SHAPE_BOOLEAN:
        case TRI_SHAPE_NUMBER: {
          return 1;
        }
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING: {
          char* leftString;
          char* rightString;

          // compare strings
          // extract the strings
          if (leftType == TRI_SHAPE_SHORT_STRING) {
            leftString = (char*)(sizeof(TRI_shape_length_short_string_t) + left._data.data);
          }
          else {
            leftString = (char*)(sizeof(TRI_shape_length_long_string_t) + left._data.data);
          }          
          
          if (rightType == TRI_SHAPE_SHORT_STRING) {
            rightString = (char*)(sizeof(TRI_shape_length_short_string_t) + right._data.data);
          }
          else {
            rightString = (char*)(sizeof(TRI_shape_length_long_string_t) + right._data.data);
          }         
          
          return TRI_compare_utf8(leftString, rightString);
        }
        case TRI_SHAPE_ARRAY:
        case TRI_SHAPE_LIST:
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: {
          return -1;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_LONG/SHORT_STRING 

    
    // .............................................................................
    // HOMOGENEOUS LIST
    // .............................................................................

    case TRI_SHAPE_HOMOGENEOUS_LIST: 
    case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: 
    case TRI_SHAPE_LIST: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: 
        case TRI_SHAPE_NULL:
        case TRI_SHAPE_BOOLEAN:
        case TRI_SHAPE_NUMBER:
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING: {
          return 1;
        }
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: 
        case TRI_SHAPE_LIST: {
          // unfortunately recursion: check the types of all the entries
          size_t leftListLength = *((TRI_shape_length_list_t*) left._data.data);
          size_t rightListLength = *((TRI_shape_length_list_t*) right._data.data);
          
          // determine the smallest list
          if (leftListLength > rightListLength) {
            listLength = rightListLength;
          }
          else {
            listLength = leftListLength;
          }
          
          for (size_t j = 0; j < listLength; ++j) {
            if (leftType == TRI_SHAPE_HOMOGENEOUS_LIST) {
              TRI_AtHomogeneousListShapedJson((const TRI_homogeneous_list_shape_t*)(leftShape),
                                              &left,
                                              j,
                                              &leftElement);
            }            
            else if (leftType == TRI_SHAPE_HOMOGENEOUS_SIZED_LIST) {
              TRI_AtHomogeneousSizedListShapedJson((const TRI_homogeneous_sized_list_shape_t*)(leftShape),
                                                   &left,
                                                   j,
                                                   &leftElement);
            }
            else {
              TRI_AtListShapedJson((const TRI_list_shape_t*)(leftShape),
                                   &left,
                                   j,
                                   &leftElement);
            }
            
            
            if (rightType == TRI_SHAPE_HOMOGENEOUS_LIST) {
              TRI_AtHomogeneousListShapedJson((const TRI_homogeneous_list_shape_t*)(rightShape),
                                              &right,
                                              j,
                                              &rightElement);
            }            
            else if (rightType == TRI_SHAPE_HOMOGENEOUS_SIZED_LIST) {
              TRI_AtHomogeneousSizedListShapedJson((const TRI_homogeneous_sized_list_shape_t*)(rightShape),
                                                   &right,
                                                   j,
                                                   &rightElement);
            }
            else {
              TRI_AtListShapedJson((const TRI_list_shape_t*)(rightShape),
                                   &right,
                                   j,
                                   &rightElement);
            }
            
            result = TRI_CompareShapeTypes(nullptr,
                                           nullptr,
                                           &leftElement,
                                           nullptr,
                                           nullptr,
                                           &rightElement,
                                           leftShaper,
                                           rightShaper);

            if (result != 0) { 
              return result;
            }  
          }          
          
          // up to listLength everything matches
          if (leftListLength < rightListLength) {
            return -1;
          }
          else if (leftListLength > rightListLength) {
            return 1;
          }  
          return 0;
        }
        
        
        case TRI_SHAPE_ARRAY:
        {
          return -1;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_LIST ... 
    
    // .............................................................................
    // ARRAY
    // .............................................................................

    case TRI_SHAPE_ARRAY: {
      switch (rightType) {
        case TRI_SHAPE_ILLEGAL: 
        case TRI_SHAPE_NULL:
        case TRI_SHAPE_BOOLEAN:
        case TRI_SHAPE_NUMBER:
        case TRI_SHAPE_SHORT_STRING:
        case TRI_SHAPE_LONG_STRING:
        case TRI_SHAPE_HOMOGENEOUS_LIST:
        case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST: 
        case TRI_SHAPE_LIST: {
          return 1;
        }
        
        case TRI_SHAPE_ARRAY: {

          // ............................................................................  
          // We are comparing a left JSON array with another JSON array on the right
          // The comparison works as follows:
          //
          //   Suppose that leftShape has m key/value pairs and that the 
          //   rightShape has n key/value pairs
          //
          //   Extract the m key aids (attribute identifiers) from the leftShape
          //   Extract the n key aids (attribute identifiers) from the rightShape
          //
          //   Sort the key aids for both the left and right shape
          //   according to the weight of the key (attribute) 
          //
          //   Let lw_j denote the weight of the jth key from the sorted leftShape key list
          //   and rw_j the corresponding rightShape.
          //
          //   If lw_j < rw_j return -1
          //   If lw_j > rw_j return 1
          //   If lw_j == rw_j, then we extract the values and compare the values
          //   using recursion. 
          //   
          //   If lv_j < rv_j return -1
          //   If lv_j > rv_j return 1
          //   If lv_j == rv_j, then repeat the process with j+1.
          // ............................................................................  
          
          // ............................................................................
          // generate the left and right lists.
          // ............................................................................

          int leftNumWeightedList  = CompareShapeTypeJsonArrayHelper(leftShape, leftShaper, &left, &leftWeightedList);
          int rightNumWeightedList = CompareShapeTypeJsonArrayHelper(rightShape, rightShaper, &right, &rightWeightedList);

          // ............................................................................
          // If the left and right both resulted in errors, we return equality for want
          // of something better.
          // ............................................................................
          
          if ( (leftNumWeightedList < 0) && (rightNumWeightedList < 0) )  { // probably out of memory error        
            FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
            return 0;            
          }
          
          // ............................................................................
          // If the left had an error, we rank the left as the smallest item in the order
          // ............................................................................
          
          if (leftNumWeightedList < 0) { // probably out of memory error        
            FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
            return -1; // attempt to compare as low as possible
          }
         
          // ............................................................................
          // If the right had an error, we rank the right as the largest item in the order
          // ............................................................................
          
          if (rightNumWeightedList < 0) {
            FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
            return 1;
          }

          // ............................................................................
          // Are we comparing two empty shaped_json_arrays?
          // ............................................................................
          
          if ( (leftNumWeightedList == 0) && (rightNumWeightedList == 0) ) {
            FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
            return 0; 
          }

          // ............................................................................
          // If the left is empty, then it is smaller than the right, right?
          // ............................................................................

          if  (leftNumWeightedList == 0) {
            FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
            return -1; 
          }
         
          // ............................................................................
          // ...and the opposite of the above.
          // ............................................................................

          if  (rightNumWeightedList == 0) {
            FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
            return 1; 
          }

          // ..............................................................................
          // We now have to sort the left and right weighted list according to attribute weight
          // ..............................................................................

          qsort(leftWeightedList, leftNumWeightedList, sizeof(weighted_attribute_t), AttributeWeightCompareFunction);
          qsort(rightWeightedList, rightNumWeightedList, sizeof(weighted_attribute_t), AttributeWeightCompareFunction);                         

          // ..............................................................................
          // check the weight and if equal check the values. Notice that numWeightedList
          // below MUST be greater or equal to 1.
          // ..............................................................................

          int numWeightedList = (leftNumWeightedList < rightNumWeightedList ? leftNumWeightedList : rightNumWeightedList);          
          
          result = 0;

          for (int i = 0; i < numWeightedList; ++i) {
            if (leftWeightedList[i]._weight != rightWeightedList[i]._weight) {
              result = (leftWeightedList[i]._weight < rightWeightedList[i]._weight ? -1: 1);
              break;
            }
            
            result = TRI_CompareShapeTypes(nullptr,
                                           nullptr,
                                           &(leftWeightedList[i]._value),
                                           nullptr,
                                           nullptr,
                                           &(rightWeightedList[i]._value),
                                           leftShaper,
                                           rightShaper);

            if (result != 0) { 
              break;
            }  
            
            // the attributes are equal now check for the values 
            /* start oreste debug
            const char* name = leftShaper->lookupAttributeId(leftShaper,leftWeightedList[i]._aid);
            printf("%s:%u:w=%ld:%s\n",__FILE__,__LINE__,leftWeightedList[i]._weight,name);
            const char* name = rightShaper->lookupAttributeId(rightShaper,rightWeightedList[i]._aid);
            printf("%s:%u:w=%ld:%s\n",__FILE__,__LINE__,rightWeightedList[i]._weight,name);
            end oreste debug */
          }        
          
          if (result == 0) {
            // ............................................................................
            // The comparisions above indicate that the shaped_json_arrays are equal, 
            // however one more check to determine if the number of elements in the arrays
            // are equal.
            // ............................................................................
            if (leftNumWeightedList < rightNumWeightedList) {
              result = -1;
            }
            else if (leftNumWeightedList > rightNumWeightedList) {
              result = 1;
            }
          }
          
          
          // ..............................................................................
          // Deallocate any memory for the comparisions and return the result
          // ..............................................................................
          
          FreeShapeTypeJsonArrayHelper(&leftWeightedList, &rightWeightedList);
          return result;
        }
      } // end of switch (rightType) 
    } // end of case TRI_SHAPE_ARRAY
  } // end of switch (leftType)
  
  assert(false);
  return 0; //shut the vc++ up
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
