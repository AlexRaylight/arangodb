////////////////////////////////////////////////////////////////////////////////
/// @brief index
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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "index.h"

#include "BasicsC/conversions.h"
#include "BasicsC/files.h"
#include "BasicsC/json.h"
#include "BasicsC/json-utilities.h"
#include "BasicsC/linked-list.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/tri-strings.h"
#include "BasicsC/utf8-helper.h"
#include "BasicsC/fasthash.h"
#include "BasicsC/xxhash.h"
#include "CapConstraint/cap-constraint.h"
#include "GeoIndex/geo-index.h"
#include "FulltextIndex/fulltext-index.h"
#include "FulltextIndex/fulltext-wordlist.h"
#include "GeoIndex/geo-index.h"
#include "HashIndex/hash-index.h"
#include "ShapedJson/shape-accessor.h"
#include "ShapedJson/shaped-json.h"
#include "VocBase/document-collection.h"
#include "VocBase/edge-collection.h"
#include "VocBase/replication-logger.h"
#include "VocBase/server.h"
#include "VocBase/voc-shaper.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                             INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise basic index properties
////////////////////////////////////////////////////////////////////////////////

void TRI_InitIndex (TRI_index_t* idx, 
                    TRI_idx_iid_t iid,
                    TRI_idx_type_e type, 
                    TRI_document_collection_t* document,
                    bool unique) {
  TRI_ASSERT(idx != nullptr);

  if (iid > 0) {
    // use iid if specified
    idx->_iid = iid;
  }
  else if (type == TRI_IDX_TYPE_PRIMARY_INDEX) {
    // override iid
    idx->_iid = 0;
  }
  else {
    idx->_iid = TRI_NewTickServer();
  }

  idx->_type              = type;
  idx->_collection        = document;
  idx->_unique            = unique;
  
  // init common functions
  idx->memory            = nullptr;
  idx->removeIndex       = nullptr;
  idx->cleanup           = nullptr;
  idx->sizeHint          = nullptr;
  idx->postInsert        = nullptr;

  LOG_TRACE("initialising index of type %s", TRI_TypeNameIndex(idx->_type));
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not an index needs full coverage
////////////////////////////////////////////////////////////////////////////////

bool TRI_NeedsFullCoverageIndex (TRI_idx_type_e type) {
  switch (type) {
    case TRI_IDX_TYPE_PRIMARY_INDEX:
    case TRI_IDX_TYPE_EDGE_INDEX:
    case TRI_IDX_TYPE_HASH_INDEX:
    case TRI_IDX_TYPE_SKIPLIST_INDEX:
    case TRI_IDX_TYPE_FULLTEXT_INDEX:
    case TRI_IDX_TYPE_GEO1_INDEX:
    case TRI_IDX_TYPE_GEO2_INDEX:
    case TRI_IDX_TYPE_CAP_CONSTRAINT:
      return true;
    case TRI_IDX_TYPE_BITARRAY_INDEX:
      return false;
    case TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX:
    case TRI_IDX_TYPE_UNKNOWN:
      return false;
  }

  // unknown type...
  TRI_ASSERT(false);
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the name of an index type
////////////////////////////////////////////////////////////////////////////////

TRI_idx_type_e TRI_TypeIndex (char const* type) {
  if (TRI_EqualString(type, "primary")) {
    return TRI_IDX_TYPE_PRIMARY_INDEX;
  }
  else if (TRI_EqualString(type, "edge")) {
    return TRI_IDX_TYPE_EDGE_INDEX;
  }
  else if (TRI_EqualString(type, "hash")) {
    return TRI_IDX_TYPE_HASH_INDEX;
  }
  else if (TRI_EqualString(type, "skiplist")) {
    return TRI_IDX_TYPE_SKIPLIST_INDEX;
  }
  else if (TRI_EqualString(type, "fulltext")) {
    return TRI_IDX_TYPE_FULLTEXT_INDEX;
  }
  else if (TRI_EqualString(type, "bitarray")) {
    return TRI_IDX_TYPE_BITARRAY_INDEX;
  }
  else if (TRI_EqualString(type, "cap")) {
    return TRI_IDX_TYPE_CAP_CONSTRAINT;
  }
  else if (TRI_EqualString(type, "geo1")) {
    return TRI_IDX_TYPE_GEO1_INDEX;
  }
  else if (TRI_EqualString(type, "geo2")) {
    return TRI_IDX_TYPE_GEO2_INDEX;
  }

  return TRI_IDX_TYPE_UNKNOWN;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the name of an index type
////////////////////////////////////////////////////////////////////////////////

char const* TRI_TypeNameIndex (TRI_idx_type_e type) {
  switch (type) {
    case TRI_IDX_TYPE_PRIMARY_INDEX:
      return "primary";
    case TRI_IDX_TYPE_GEO1_INDEX:
      return "geo1";
    case TRI_IDX_TYPE_GEO2_INDEX:
      return "geo2";
    case TRI_IDX_TYPE_HASH_INDEX:
      return "hash";
    case TRI_IDX_TYPE_EDGE_INDEX:
      return "edge";
    case TRI_IDX_TYPE_FULLTEXT_INDEX:
      return "fulltext";
    case TRI_IDX_TYPE_SKIPLIST_INDEX:
      return "skiplist";
    case TRI_IDX_TYPE_BITARRAY_INDEX:
      return "bitarray";
    case TRI_IDX_TYPE_CAP_CONSTRAINT:
      return "cap";
    case TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX:
    case TRI_IDX_TYPE_UNKNOWN:
    default: {
    }
  }

  return "";
}

////////////////////////////////////////////////////////////////////////////////
/// @brief validate an index id
////////////////////////////////////////////////////////////////////////////////

bool TRI_ValidateIdIndex (char const* key) {
  char const* p = key;

  while (1) {
    const char c = *p;

    if (c == '\0') {
      return (p - key) > 0;
    }
    if (c >= '0' && c <= '9') {
      ++p;
      continue;
    }

    return false;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief validate an index id (collection name + / + index id)
////////////////////////////////////////////////////////////////////////////////

bool TRI_ValidateIndexIdIndex (char const* key,
                               size_t* split) {
  char const* p = key;
  char c = *p;

  // extract collection name
        
  if (! (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
    return false;
  }

  ++p;

  while (1) {
    c = *p;

    if ((c == '_') || (c == '-') || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      ++p;
      continue;
    }

    if (c == '/') {
      break;
    }

    return false;
  }

  if (p - key > TRI_COL_NAME_LENGTH) {
    return false;
  }

  // store split position
  *split = p - key;
  ++p;
 
  // validate index id
  return TRI_ValidateIdIndex(p);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free an index
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeIndex (TRI_index_t* idx) {
  TRI_ASSERT(idx);

  LOG_TRACE("freeing index");

  switch (idx->_type) {
    case TRI_IDX_TYPE_GEO1_INDEX:
    case TRI_IDX_TYPE_GEO2_INDEX:
      TRI_FreeGeoIndex(idx);
      break;

    case TRI_IDX_TYPE_BITARRAY_INDEX:
      TRI_FreeBitarrayIndex(idx);
      break;

    case TRI_IDX_TYPE_HASH_INDEX:
      TRI_FreeHashIndex(idx);
      break;

    case TRI_IDX_TYPE_EDGE_INDEX:
      TRI_FreeEdgeIndex(idx);
      break;

    case TRI_IDX_TYPE_SKIPLIST_INDEX:
      TRI_FreeSkiplistIndex(idx);
      break;

    case TRI_IDX_TYPE_FULLTEXT_INDEX:
      TRI_FreeFulltextIndex(idx);
      break;

    case TRI_IDX_TYPE_CAP_CONSTRAINT:
      TRI_FreeCapConstraint(idx);
      break;

    case TRI_IDX_TYPE_PRIMARY_INDEX:
      TRI_FreePrimaryIndex(idx);
      break;

    default:
      // no action necessary
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes an index file
////////////////////////////////////////////////////////////////////////////////

bool TRI_RemoveIndexFile (TRI_document_collection_t* collection, TRI_index_t* idx) {
  char* filename;
  char* name;
  char* number;
  int res;

  // construct filename
  number = TRI_StringUInt64(idx->_iid);

  if (number == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    LOG_ERROR("out of memory when creating index number");
    return false;
  }

  name = TRI_Concatenate3String("index-", number, ".json");

  if (name == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    TRI_FreeString(TRI_CORE_MEM_ZONE, number);
    LOG_ERROR("out of memory when creating index name");
    return false;
  }

  filename = TRI_Concatenate2File(collection->base._directory, name);

  if (filename == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    TRI_FreeString(TRI_CORE_MEM_ZONE, number);
    TRI_FreeString(TRI_CORE_MEM_ZONE, name);
    LOG_ERROR("out of memory when creating index filename");
    return false;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, name);
  TRI_FreeString(TRI_CORE_MEM_ZONE, number);

  res = TRI_UnlinkFile(filename);
  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  if (res != TRI_ERROR_NO_ERROR) {
    LOG_ERROR("cannot remove index definition: %s", TRI_last_error());
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves an index
////////////////////////////////////////////////////////////////////////////////

int TRI_SaveIndex (TRI_document_collection_t* document,
                   TRI_index_t* idx,
                   TRI_server_id_t generatingServer) {
  TRI_json_t* json;
  TRI_vocbase_t* vocbase;
  char* filename;
  char* name;
  char* number;
  bool ok;

  // convert into JSON
  json = idx->json(idx);

  if (json == NULL) {
    LOG_TRACE("cannot save index definition: index cannot be jsonified");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // construct filename
  number   = TRI_StringUInt64(idx->_iid);
  name     = TRI_Concatenate3String("index-", number, ".json");
  filename = TRI_Concatenate2File(document->base._directory, name);

  TRI_FreeString(TRI_CORE_MEM_ZONE, name);
  TRI_FreeString(TRI_CORE_MEM_ZONE, number);

  vocbase = document->base._vocbase;

  // and save
  ok = TRI_SaveJson(filename, json, vocbase->_settings.forceSyncProperties);
  
  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  if (! ok) {
    LOG_ERROR("cannot save index definition: %s", TRI_last_error());
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

    return TRI_errno();
  }

  // it is safe to use _name as we hold a read-lock on the collection status
  TRI_LogCreateIndexReplication(vocbase, 
                                document->base._info._cid, 
                                document->base._info._name, 
                                idx->_iid, 
                                json,
                                generatingServer);

  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an index identifier
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_LookupIndex (TRI_document_collection_t* document, 
                              TRI_idx_iid_t iid) {
  for (size_t i = 0;  i < document->_allIndexes._length;  ++i) {
    TRI_index_t* idx = static_cast<TRI_index_t*>(document->_allIndexes._buffer[i]);

    if (idx->_iid == iid) {
      return idx;
    }
  }

  TRI_set_errno(TRI_ERROR_ARANGO_NO_INDEX);

  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a basic index description as JSON
/// this only contains the common index fields and needs to be extended by the
/// specialised index 
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_JsonIndex (TRI_memory_zone_t* zone, 
                           TRI_index_t const* idx) {
  TRI_json_t* json;

  json = TRI_CreateArrayJson(zone);

  if (json != NULL) {
    char* number;
    
    number = TRI_StringUInt64(idx->_iid);
    TRI_Insert3ArrayJson(zone, json, "id", TRI_CreateStringCopyJson(zone, number));
    TRI_Insert3ArrayJson(zone, json, "type", TRI_CreateStringCopyJson(zone, TRI_TypeNameIndex(idx->_type)));
    TRI_Insert3ArrayJson(zone, json, "unique", TRI_CreateBooleanJson(zone, idx->_unique));

    TRI_FreeString(TRI_CORE_MEM_ZONE, number);
  }

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a result set returned by a hash index query
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyIndexResult (TRI_index_result_t* result) {
  if (result->_documents != NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, result->_documents);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief copies a path vector
////////////////////////////////////////////////////////////////////////////////

void TRI_CopyPathVector (TRI_vector_t* dst, TRI_vector_t* src) {
  size_t j;

  TRI_InitVector(dst, TRI_CORE_MEM_ZONE, sizeof(TRI_shape_pid_t));

  for (j = 0;  j < src->_length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(src,j)));

    TRI_PushBackVector(dst, &shape);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a path vector into a field list
///
/// Note that you must free the field list itself, but not the fields. The
/// belong to the shaper.
////////////////////////////////////////////////////////////////////////////////

char const** TRI_FieldListByPathList (TRI_shaper_t const* shaper,
                                      TRI_vector_t const* paths) {
  // .............................................................................
  // Allocate sufficent memory for the field list
  // .............................................................................

  char const** fieldList = static_cast<char const**>(TRI_Allocate(TRI_CORE_MEM_ZONE, (sizeof(char const*) * paths->_length), false));

  // ..........................................................................  
  // Convert the attributes (field list of the hash index) into strings
  // ..........................................................................  

  for (size_t j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths, j)));
    TRI_shape_path_t const* path = shaper->lookupAttributePathByPid(const_cast<TRI_shaper_t*>(shaper), shape);

    if (path == NULL) {
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
      TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);
      return NULL;
    }

    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }

  return fieldList;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                     PRIMARY INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief insert methods does nothing
////////////////////////////////////////////////////////////////////////////////

static int InsertPrimary (TRI_index_t* idx, 
                          TRI_doc_mptr_t const* doc, 
                          const bool isRollback) {
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove methods does nothing
////////////////////////////////////////////////////////////////////////////////

static int RemovePrimary (TRI_index_t* idx, 
                          TRI_doc_mptr_t const* doc,
                          const bool isRollback) {
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the memory used by the index
////////////////////////////////////////////////////////////////////////////////

static size_t MemoryPrimary (TRI_index_t const* idx) { 
  return idx->_collection->_primaryIndex._nrAlloc * sizeof(void*); 
}

////////////////////////////////////////////////////////////////////////////////
/// @brief JSON description of a primary index
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonPrimary (TRI_index_t const* idx) { 
  TRI_json_t* json = TRI_JsonIndex(TRI_CORE_MEM_ZONE, idx);
  
  if (json == nullptr) {
    return nullptr;
  }

  TRI_json_t* fields = TRI_CreateListJson(TRI_CORE_MEM_ZONE);
  TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, "_id"));
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "fields", fields);

  return json;
}

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create the primary index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreatePrimaryIndex (TRI_document_collection_t* document) {
  // create primary index
  TRI_index_t* idx = static_cast<TRI_index_t*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_index_t), false));

  if (idx == nullptr) {
    return nullptr;
  }

  char* id = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, "_id");
  TRI_InitVectorString(&idx->_fields, TRI_CORE_MEM_ZONE);
  TRI_PushBackVectorString(&idx->_fields, id);

  TRI_InitIndex(idx, 0, TRI_IDX_TYPE_PRIMARY_INDEX, document, true);

  idx->memory   = MemoryPrimary;
  idx->json     = JsonPrimary;
  idx->insert   = InsertPrimary;
  idx->remove   = RemovePrimary;

  return idx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a primary index
////////////////////////////////////////////////////////////////////////////////

void TRI_FreePrimaryIndex (TRI_index_t* idx) {
  TRI_DestroyVectorString(&idx->_fields);
  TRI_Free(TRI_CORE_MEM_ZONE, idx);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                        EDGE INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes an edge key 
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementKey (TRI_multi_pointer_t* array, void const* data) {
  TRI_edge_header_t const* h = static_cast<TRI_edge_header_t const*>(data);
  char const* key = h->_key;

  uint64_t hash = h->_cid;
  hash ^=  (uint64_t) fasthash64(key, strlen(key), 0x87654321);

  return fasthash64(&hash, sizeof(hash), 0x56781234);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes an edge (_from case)
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementEdgeFrom (TRI_multi_pointer_t* array, 
                                     void const* data,
                                     bool byKey) {
  uint64_t hash;

  if (! byKey) {
    hash = (uint64_t) data;
  }
  else {
    TRI_doc_mptr_t const* mptr = static_cast<TRI_doc_mptr_t const*>(data);
    TRI_doc_edge_key_marker_t const* edge = static_cast<TRI_doc_edge_key_marker_t const*>(mptr->getDataPtr());  // ONLY IN INDEX
    char const* key = (char const*) edge + edge->_offsetFromKey;

    hash = edge->_fromCid;
    hash ^= (uint64_t) fasthash64(key, strlen(key), 0x87654321);
  }

  return fasthash64(&hash, sizeof(hash), 0x56781234);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes an edge (_to case)
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementEdgeTo (TRI_multi_pointer_t* array, 
                                   void const* data,
                                   bool byKey) {
  uint64_t hash;

  if (! byKey) {
    hash = (uint64_t) data;
  }
  else {
    TRI_doc_mptr_t const* mptr = static_cast<TRI_doc_mptr_t const*>(data);
    TRI_doc_edge_key_marker_t const* edge = static_cast<TRI_doc_edge_key_marker_t const*>(mptr->getDataPtr());  // ONLY IN INDEX
    char const* key = (char const*) edge + edge->_offsetToKey;

    hash = edge->_toCid;
    hash ^= (uint64_t) fasthash64(key, strlen(key), 0x87654321);
  }

  return fasthash64(&hash, sizeof(hash), 0x56781234);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if key and element match (_from case)
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualKeyEdgeFrom (TRI_multi_pointer_t* array,
                                void const* left,
                                void const* right) {
  // left is a key
  // right is an element, that is a master pointer
  TRI_edge_header_t const* l = static_cast<TRI_edge_header_t const*>(left);
  char const* lKey = l->_key;

  TRI_doc_mptr_t const* rMptr = static_cast<TRI_doc_mptr_t const*>(right);
  TRI_doc_edge_key_marker_t const* rEdge = static_cast<TRI_doc_edge_key_marker_t const*>(rMptr->getDataPtr());  // ONLY IN INDEX
  char const* rKey = (char const*) rEdge + rEdge->_offsetFromKey;

  return (strcmp(lKey, rKey) == 0) && l->_cid == rEdge->_fromCid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if key and element match (_to case)
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualKeyEdgeTo (TRI_multi_pointer_t* array,
                              void const* left,
                              void const* right) {
  // left is a key
  // right is an element, that is a master pointer
  TRI_edge_header_t const* l = static_cast<TRI_edge_header_t const*>(left);
  char const* lKey = l->_key;

  TRI_doc_mptr_t const* rMptr = static_cast<TRI_doc_mptr_t const*>(right);
  TRI_doc_edge_key_marker_t const* rEdge = static_cast<TRI_doc_edge_key_marker_t const*>(rMptr->getDataPtr());  // ONLY IN INDEX
  char const* rKey = (char const*) rEdge + rEdge->_offsetToKey;

  return (strcmp(lKey, rKey) == 0) && l->_cid == rEdge->_toCid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks for elements are equal (_from case)
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualElementEdgeFrom (TRI_multi_pointer_t* array,
                                    void const* left,
                                    void const* right,
                                    bool byKey) {
  if (! byKey) {
    return left == right;
  }
  else {
    TRI_doc_mptr_t const* lMptr = static_cast<TRI_doc_mptr_t const*>(left);
    TRI_doc_mptr_t const* rMptr = static_cast<TRI_doc_mptr_t const*>(right);
    TRI_doc_edge_key_marker_t const* lEdge = static_cast<TRI_doc_edge_key_marker_t const*>(lMptr->getDataPtr());  // ONLY IN INDEX
    TRI_doc_edge_key_marker_t const* rEdge = static_cast<TRI_doc_edge_key_marker_t const*>(rMptr->getDataPtr());  // ONLY IN INDEX
    char const* lKey = (char const*) lEdge + lEdge->_offsetFromKey;
    char const* rKey = (char const*) rEdge + rEdge->_offsetFromKey;

    return strcmp(lKey, rKey) == 0 && lEdge->_fromCid == rEdge->_fromCid;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks for elements are equal (_to case)
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualElementEdgeTo (TRI_multi_pointer_t* array,
                                  void const* left,
                                  void const* right,
                                  bool byKey) {
  if (! byKey) {
    return left == right;
  }
  else {
    TRI_doc_mptr_t const* lMptr = static_cast<TRI_doc_mptr_t const*>(left);
    TRI_doc_mptr_t const* rMptr = static_cast<TRI_doc_mptr_t const*>(right);
    TRI_doc_edge_key_marker_t const* lEdge = static_cast<TRI_doc_edge_key_marker_t const*>(lMptr->getDataPtr());  // ONLY IN INDEX
    TRI_doc_edge_key_marker_t const* rEdge = static_cast<TRI_doc_edge_key_marker_t const*>(rMptr->getDataPtr());  // ONLY IN INDEX
    char const* lKey = (char const*) lEdge + lEdge->_offsetToKey;
    char const* rKey = (char const*) rEdge + rEdge->_offsetToKey;

    return strcmp(lKey, rKey) == 0 && lEdge->_toCid == rEdge->_toCid;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief insert method for edges
////////////////////////////////////////////////////////////////////////////////

static int InsertEdge (TRI_index_t* idx, 
                       TRI_doc_mptr_t const* mptr,
                       const bool isRollback) {

  TRI_multi_pointer_t* edgesIndex;

  // OUT
  edgesIndex = &(((TRI_edge_index_t*) idx)->_edges_from);
  TRI_InsertElementMultiPointer(edgesIndex, CONST_CAST(mptr), true, isRollback);
  // IN
  edgesIndex = &(((TRI_edge_index_t*) idx)->_edges_to);
  TRI_InsertElementMultiPointer(edgesIndex, CONST_CAST(mptr), true, isRollback);
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove an edge
////////////////////////////////////////////////////////////////////////////////

static int RemoveEdge (TRI_index_t* idx, 
                       TRI_doc_mptr_t const* mptr,
                       const bool isRollback) {

  TRI_multi_pointer_t* edgesIndex;

  // OUT
  edgesIndex = &(((TRI_edge_index_t*) idx)->_edges_from);
  TRI_RemoveElementMultiPointer(edgesIndex, mptr);
  // IN
  edgesIndex = &(((TRI_edge_index_t*) idx)->_edges_to);
  TRI_RemoveElementMultiPointer(edgesIndex, mptr);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the memory used by the index
////////////////////////////////////////////////////////////////////////////////

static size_t MemoryEdge (TRI_index_t const* idx) {
  return TRI_MemoryUsageMultiPointer(&(((TRI_edge_index_t*) idx)->_edges_from)) + 
         TRI_MemoryUsageMultiPointer(&(((TRI_edge_index_t*) idx)->_edges_to));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief JSON description of edge index
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonEdge (TRI_index_t const* idx) {
  TRI_json_t* json = TRI_JsonIndex(TRI_CORE_MEM_ZONE, idx);

  if (json == nullptr) {
    return nullptr;
  }

  TRI_json_t* fields = TRI_CreateListJson(TRI_CORE_MEM_ZONE);
  TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, TRI_VOC_ATTRIBUTE_FROM));
  TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, TRI_VOC_ATTRIBUTE_TO));
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "fields", fields);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief provides a size hint for the edge index
////////////////////////////////////////////////////////////////////////////////

static int SizeHintEdge (TRI_index_t* idx,
                         size_t size) {

  int err;

  TRI_multi_pointer_t* edgesIndex = &(((TRI_edge_index_t*) idx)->_edges_from);

  // we assume this is called when setting up the index and the index 
  // is still empty
  TRI_ASSERT(edgesIndex->_nrUsed == 0);

  // set an initial size for the index for some new nodes to be created
  // without resizing
  err = TRI_ResizeMultiPointer(edgesIndex, size + 2049);

  if (err != TRI_ERROR_NO_ERROR) {
    return err;
  }

  edgesIndex = &(((TRI_edge_index_t*) idx)->_edges_to);

  // we assume this is called when setting up the index and the index 
  // is still empty
  TRI_ASSERT(edgesIndex->_nrUsed == 0);

  // set an initial size for the index for some new nodes to be created
  // without resizing
  return TRI_ResizeMultiPointer(edgesIndex, size + 2049);
}

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create the edge index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateEdgeIndex (TRI_document_collection_t* document,
                                  TRI_idx_iid_t iid) {
  TRI_index_t* idx;
  char* id;
  int res;

  // create index
  TRI_edge_index_t* edgeIndex = static_cast<TRI_edge_index_t*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_edge_index_t), false));

  if (edgeIndex == nullptr) {
    return nullptr;
  }
 
  res = TRI_InitMultiPointer(&edgeIndex->_edges_from,
                             TRI_UNKNOWN_MEM_ZONE,
                             HashElementKey,
                             HashElementEdgeFrom,
                             IsEqualKeyEdgeFrom,
                             IsEqualElementEdgeFrom);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(TRI_CORE_MEM_ZONE, edgeIndex);

    return nullptr;
  }
  res = TRI_InitMultiPointer(&edgeIndex->_edges_to,
                             TRI_UNKNOWN_MEM_ZONE,
                             HashElementKey,
                             HashElementEdgeTo,
                             IsEqualKeyEdgeTo,
                             IsEqualElementEdgeTo);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyMultiPointer(&edgeIndex->_edges_from);
    TRI_Free(TRI_CORE_MEM_ZONE, edgeIndex);

    return nullptr;
  }

  idx = &edgeIndex->base;

  TRI_InitVectorString(&idx->_fields, TRI_CORE_MEM_ZONE);
  id = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, TRI_VOC_ATTRIBUTE_FROM);
  TRI_PushBackVectorString(&idx->_fields, id);
 
  TRI_InitIndex(idx, iid, TRI_IDX_TYPE_EDGE_INDEX, document, false); 

  idx->memory   = MemoryEdge;
  idx->json     = JsonEdge;
  idx->insert   = InsertEdge;
  idx->remove   = RemoveEdge;

  idx->sizeHint = SizeHintEdge;

  return idx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys the edge index, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyEdgeIndex (TRI_index_t* idx) {
  TRI_edge_index_t* edgesIndex;

  edgesIndex = (TRI_edge_index_t*) idx;

  LOG_TRACE("destroying edge index");

  TRI_DestroyMultiPointer(&edgesIndex->_edges_to);
  TRI_DestroyMultiPointer(&edgesIndex->_edges_from);

  TRI_DestroyVectorString(&idx->_fields);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the edge index
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeEdgeIndex (TRI_index_t* idx) {
  TRI_DestroyEdgeIndex(idx);
  TRI_Free(TRI_CORE_MEM_ZONE, idx);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    SKIPLIST INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

// .............................................................................
// Helper function for TRI_LookupSkiplistIndex
// .............................................................................

static int FillLookupSLOperator (TRI_index_operator_t* slOperator, 
                                 TRI_document_collection_t* document) {
  if (slOperator == nullptr) {
    return TRI_ERROR_INTERNAL;
  }

  switch (slOperator->_type) {
    case TRI_AND_INDEX_OPERATOR:
    case TRI_NOT_INDEX_OPERATOR:
    case TRI_OR_INDEX_OPERATOR: {
      TRI_logical_index_operator_t* logicalOperator = (TRI_logical_index_operator_t*) slOperator;
      int result = FillLookupSLOperator(logicalOperator->_left, document);

      if (result == TRI_ERROR_NO_ERROR) {
        result = FillLookupSLOperator(logicalOperator->_right, document);
      }
      if (result != TRI_ERROR_NO_ERROR) {
        return result;
      }
      break;
    }

    case TRI_EQ_INDEX_OPERATOR:
    case TRI_GE_INDEX_OPERATOR:
    case TRI_GT_INDEX_OPERATOR:
    case TRI_NE_INDEX_OPERATOR:
    case TRI_LE_INDEX_OPERATOR:
    case TRI_LT_INDEX_OPERATOR: {
      TRI_relation_index_operator_t* relationOperator = (TRI_relation_index_operator_t*) slOperator;
      relationOperator->_numFields = relationOperator->_parameters->_value._objects._length;
      relationOperator->_fields = static_cast<TRI_shaped_json_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * relationOperator->_numFields, false));

      if (relationOperator->_fields != NULL) {
        for (size_t j = 0; j < relationOperator->_numFields; ++j) {
          TRI_json_t* jsonObject = (TRI_json_t*) TRI_AtVector(&(relationOperator->_parameters->_value._objects), j);

          if ((TRI_IsListJson(jsonObject) || TRI_IsArrayJson(jsonObject)) && 
              slOperator->_type != TRI_EQ_INDEX_OPERATOR) {
            // non-equality operator used on complex data type, this is disallowed
            return TRI_ERROR_BAD_PARAMETER;
          }

          TRI_shaped_json_t* shapedObject = TRI_ShapedJsonJson(document->_shaper, jsonObject, false, false);

          if (shapedObject != NULL) {
            relationOperator->_fields[j] = *shapedObject; // shallow copy here is ok
            TRI_Free(TRI_UNKNOWN_MEM_ZONE, shapedObject); // don't require storage anymore
          }
          else {
            return TRI_RESULT_ELEMENT_NOT_FOUND; 
          }
        }
      }
      else {
        relationOperator->_numFields = 0; // out of memory?
      }
      break;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief attempts to locate an entry in the skip list index
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Note: this function will destroy the passed slOperator before it returns
// Warning: who ever calls this function is responsible for destroying
// TRI_skiplist_iterator_t* results
// .............................................................................

TRI_skiplist_iterator_t* TRI_LookupSkiplistIndex (TRI_index_t* idx, 
                                                  TRI_index_operator_t* slOperator) {
  TRI_skiplist_index_t*    skiplistIndex;
  TRI_skiplist_iterator_t* iteratorResult;
  int                      errorResult;

  skiplistIndex = (TRI_skiplist_index_t*)(idx);

  // .........................................................................
  // fill the relation operators which may be embedded in the slOperator with
  // additional information. Recall the slOperator is what information was
  // received from a user for query the skiplist.
  // .........................................................................

  errorResult = FillLookupSLOperator(slOperator, skiplistIndex->base._collection);

  if (errorResult != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(errorResult);

    return nullptr;
  }

  iteratorResult = SkiplistIndex_find(skiplistIndex->_skiplistIndex, 
                                      &skiplistIndex->_paths, 
                                      slOperator);

  // .........................................................................
  // we must deallocate any memory we allocated in FillLookupSLOperator
  // .........................................................................

  TRI_FreeIndexOperator(slOperator);

  return iteratorResult;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief helper for skiplist methods
////////////////////////////////////////////////////////////////////////////////

static int SkiplistIndexHelper (const TRI_skiplist_index_t* skiplistIndex,
                                TRI_skiplist_index_element_t* skiplistElement,
                                const TRI_doc_mptr_t* document) {
  TRI_shaped_json_t shapedObject;
  TRI_shaped_json_t shapedJson;
  char const* ptr;
  size_t j;
  
  // ..........................................................................
  // Assign the document to the SkiplistIndexElement structure so that it can 
  // be retrieved later.
  // ..........................................................................
    
  TRI_ASSERT(document != NULL); 
  TRI_ASSERT(document->getDataPtr() != NULL);   // ONLY IN INDEX
    
  TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->getDataPtr());  // ONLY IN INDEX

  if (shapedJson._sid == 0) {
    LOG_WARNING("encountered invalid marker with shape id 0");
    
    return TRI_ERROR_INTERNAL;
  }

  skiplistElement->_document = const_cast<TRI_doc_mptr_t*>(document);
  ptr = (char const*) skiplistElement->_document->getDataPtr();  // ONLY IN INDEX
    
  for (j = 0; j < skiplistIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&skiplistIndex->_paths, j)));

    // ..........................................................................
    // Determine if document has that particular shape 
    // ..........................................................................

    TRI_shape_access_t const* acc = TRI_FindAccessorVocShaper(skiplistIndex->base._collection->_shaper, shapedJson._sid, shape);

    if (acc == NULL || acc->_resultSid == 0) {
      return TRI_ERROR_ARANGO_INDEX_DOCUMENT_ATTRIBUTE_MISSING;
    }  
      
      
    // ..........................................................................
    // Extract the field
    // ..........................................................................    

    if (! TRI_ExecuteShapeAccessor(acc, &shapedJson, &shapedObject)) {
      return TRI_ERROR_INTERNAL;
    }

    // .........................................................................
    // Store the field
    // .........................................................................    

    skiplistElement->_subObjects[j]._sid = shapedObject._sid;
    skiplistElement->_subObjects[j]._length = shapedObject._data.length;
    skiplistElement->_subObjects[j]._offset = ((char const*) shapedObject._data.data) - ptr;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into a skip list index
////////////////////////////////////////////////////////////////////////////////

static int InsertSkiplistIndex (TRI_index_t* idx, 
                                TRI_doc_mptr_t const* doc,
                                const bool isRollback) {
  TRI_skiplist_index_element_t skiplistElement;
  TRI_skiplist_index_t* skiplistIndex;
  int res;
  
  // ...........................................................................
  // Obtain the skip listindex structure
  // ...........................................................................

  skiplistIndex = (TRI_skiplist_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in InsertSkiplistIndex");
    return TRI_ERROR_INTERNAL;
  }

  // ...........................................................................
  // Allocate storage to shaped json objects stored as a simple list.
  // These will be used for comparisions
  // ...........................................................................

  skiplistElement._subObjects = static_cast<TRI_shaped_sub_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_sub_t) * skiplistIndex->_paths._length, false));
  
  if (skiplistElement._subObjects == NULL) {
    LOG_WARNING("out-of-memory in InsertSkiplistIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
  
  res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, doc);
  // ...........................................................................
  // most likely the cause of this error is that the 'shape' of the document
  // does not match the 'shape' of the index structure -- so the document
  // is ignored. So not really an error at all.
  // ...........................................................................

  if (res != TRI_ERROR_NO_ERROR) {

    // ..........................................................................
    // Deallocated the memory already allocated to skiplistElement.fields
    // ..........................................................................
      
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement._subObjects);
    
    // .........................................................................
    // It may happen that the document does not have the necessary
    // attributes to be included within the hash index, in this case do
    // not report back an error.
    // .........................................................................

    if (res == TRI_ERROR_ARANGO_INDEX_DOCUMENT_ATTRIBUTE_MISSING) {
      return TRI_ERROR_NO_ERROR;
    }

    return res;
  }    

  // ...........................................................................
  // Fill the json field list from the document for skiplist index
  // ...........................................................................

  res = SkiplistIndex_insert(skiplistIndex->_skiplistIndex, &skiplistElement);

  // ...........................................................................
  // Memory which has been allocated to skiplistElement.fields remains allocated
  // contents of which are stored in the hash array.
  // ...........................................................................
      
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement._subObjects);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the memory used by the index
////////////////////////////////////////////////////////////////////////////////

static size_t MemorySkiplistIndex (TRI_index_t const* idx) {
  TRI_skiplist_index_t const* skiplistIndex = (TRI_skiplist_index_t const*) idx;

  if (skiplistIndex == nullptr) {
    return 0;
  }

  return SkiplistIndex_memoryUsage(skiplistIndex->_skiplistIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a skiplist index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonSkiplistIndex (TRI_index_t const* idx) {
  TRI_json_t* json;
  TRI_json_t* fields;
  char const** fieldList;
  size_t j;

  // ..........................................................................
  // Recast as a skiplist index
  // ..........................................................................
  
  TRI_skiplist_index_t const* skiplistIndex = (TRI_skiplist_index_t const*) idx;

  if (skiplistIndex == nullptr) {
    return nullptr;
  }

  TRI_document_collection_t* document = idx->_collection;

  // ..........................................................................
  // Allocate sufficent memory for the field list
  // ..........................................................................

  fieldList = static_cast<char const**>(TRI_Allocate(TRI_CORE_MEM_ZONE, (sizeof(char*) * skiplistIndex->_paths._length) , false));

  // ..........................................................................
  // Convert the attributes (field list of the skiplist index) into strings
  // ..........................................................................

  for (j = 0; j < skiplistIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*) TRI_AtVector(&skiplistIndex->_paths, j));
    const TRI_shape_path_t* path = document->_shaper->lookupAttributePathByPid(document->_shaper, shape);

    if (path == nullptr) {
      TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);

      return NULL;
    }
    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }

  // ..........................................................................
  // create json object and fill it
  // ..........................................................................

  json = TRI_JsonIndex(TRI_CORE_MEM_ZONE, idx);

  fields = TRI_CreateListJson(TRI_CORE_MEM_ZONE);
  for (j = 0; j < skiplistIndex->_paths._length; ++j) {
    TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, fieldList[j]));
  }
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "fields", fields);

  TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a skiplist index
////////////////////////////////////////////////////////////////////////////////

static int RemoveSkiplistIndex (TRI_index_t* idx, 
                                TRI_doc_mptr_t const* doc,
                                const bool isRollback) {
  TRI_skiplist_index_element_t skiplistElement;
  int res;

  // ...........................................................................
  // Obtain the skiplist index structure
  // ...........................................................................

  TRI_skiplist_index_t* skiplistIndex = (TRI_skiplist_index_t*) idx;

  // ...........................................................................
  // Allocate some memory for the SkiplistIndexElement structure
  // ...........................................................................

  skiplistElement._subObjects = static_cast<TRI_shaped_sub_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_sub_t) * skiplistIndex->_paths._length, false));
  
  if (skiplistElement._subObjects == nullptr) {
    LOG_WARNING("out-of-memory in InsertSkiplistIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
  
  // ..........................................................................
  // Fill the json field list from the document
  // ..........................................................................
  
  res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, doc);
  
  // ..........................................................................
  // Error returned generally implies that the document never was part of the 
  // skiplist index
  // ..........................................................................
  
  if (res != TRI_ERROR_NO_ERROR) {
  
    // ........................................................................
    // Deallocate memory allocated to skiplistElement.fields above
    // ........................................................................
    
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement._subObjects);
    
    // ........................................................................
    // It may happen that the document does not have the necessary attributes
    // to have particpated within the hash index. In this case, we do not
    // report an error to the calling procedure.
    // ........................................................................
    
    if (res == TRI_ERROR_ARANGO_INDEX_DOCUMENT_ATTRIBUTE_MISSING) { 
      return TRI_ERROR_NO_ERROR;
    }

    return res;  
  }
  
  // ...........................................................................
  // Attempt the removal for skiplist indexes
  // ...........................................................................
  
  res = SkiplistIndex_remove(skiplistIndex->_skiplistIndex, &skiplistElement);
  
  // ...........................................................................
  // Deallocate memory allocated to skiplistElement.fields above
  // ...........................................................................
    
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement._subObjects);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a skiplist index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateSkiplistIndex (TRI_document_collection_t* document,
                                      TRI_idx_iid_t iid,
                                      TRI_vector_pointer_t* fields,
                                      TRI_vector_t* paths,
                                      bool unique) {
  TRI_skiplist_index_t* skiplistIndex = static_cast<TRI_skiplist_index_t*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_skiplist_index_t), false));

  if (skiplistIndex == nullptr) {
    return nullptr;
  }

  TRI_index_t* idx = &skiplistIndex->base;

  TRI_InitIndex(idx, iid, TRI_IDX_TYPE_SKIPLIST_INDEX, document, unique);

  idx->memory   = MemorySkiplistIndex;
  idx->json     = JsonSkiplistIndex;
  idx->insert   = InsertSkiplistIndex;
  idx->remove   = RemoveSkiplistIndex;
  
  // ...........................................................................
  // Copy the contents of the shape list vector into a new vector and store this
  // ...........................................................................

  TRI_InitVector(&skiplistIndex->_paths, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shape_pid_t));

  for (size_t j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths,j)));

    TRI_PushBackVector(&skiplistIndex->_paths, &shape);
  }

  TRI_InitVectorString(&idx->_fields, TRI_CORE_MEM_ZONE);

  for (size_t j = 0;  j < fields->_length;  ++j) {
    char const* name = static_cast<char const*>(fields->_buffer[j]);
    char* copy = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, name);
    TRI_PushBackVectorString(&idx->_fields, copy);
  }

  skiplistIndex->_skiplistIndex = SkiplistIndex_new(document, 
                                                    paths->_length, 
                                                    unique,
                                                    false);
  // Note that the last argument is the "sparse" flag. This will be
  // implemented soon but has no consequences as of now.

  if (skiplistIndex->_skiplistIndex == nullptr) {
    TRI_DestroyVector(&skiplistIndex->_paths);
    TRI_DestroyVectorString(&idx->_fields);
    TRI_Free(TRI_CORE_MEM_ZONE, skiplistIndex);
    LOG_WARNING("skiplist index creation failed -- internal error when "
                "creating skiplist structure");
    return nullptr;
  }

  // ...........................................................................
  // Assign the function calls used by the query engine
  // ...........................................................................

  int result = SkiplistIndex_assignMethod(&(idx->indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || SkiplistIndex_assignMethod(&(idx->indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || SkiplistIndex_assignMethod(&(idx->indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);

  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVector(&skiplistIndex->_paths);
    TRI_DestroyVectorString(&idx->_fields);
    SkiplistIndex_free(skiplistIndex->_skiplistIndex);
    TRI_Free(TRI_CORE_MEM_ZONE, skiplistIndex);
    LOG_WARNING("skiplist index creation failed -- internal error when assigning function calls");
    return nullptr;
  }

  return idx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroySkiplistIndex (TRI_index_t* idx) {
  if (idx == nullptr) {
    return;
  }

  LOG_TRACE("destroying skiplist index");
  TRI_DestroyVectorString(&idx->_fields);

  TRI_skiplist_index_t* sl = (TRI_skiplist_index_t*) idx;
  TRI_DestroyVector(&sl->_paths);

  SkiplistIndex_free(sl->_skiplistIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeSkiplistIndex (TRI_index_t* idx) {
  if (idx == nullptr) {
    return;
  }

  TRI_DestroySkiplistIndex(idx);
  TRI_Free(TRI_CORE_MEM_ZONE, idx);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    FULLTEXT INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief callback function called by the fulltext index to determine the
/// words to index for a specific document
////////////////////////////////////////////////////////////////////////////////

static TRI_fulltext_wordlist_t* GetWordlist (TRI_index_t* idx,
                                             TRI_doc_mptr_t const* document) {
  TRI_fulltext_index_t* fulltextIndex;
  TRI_fulltext_wordlist_t* wordlist;
  TRI_shaped_json_t shaped;
  TRI_shaped_json_t shapedJson;
  TRI_shape_t const* shape;
  char* text;
  size_t textLength;
  TRI_vector_string_t* words;
  bool ok;

  fulltextIndex = (TRI_fulltext_index_t*) idx;

  // extract the shape
  TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, document->getDataPtr());  // ONLY IN INDEX
  ok = TRI_ExtractShapedJsonVocShaper(fulltextIndex->base._collection->_shaper, &shaped, 0, fulltextIndex->_attribute, &shapedJson, &shape);

  if (! ok || shape == nullptr) {
    return nullptr;
  }

  // extract the string value for the indexed attribute
  ok = TRI_StringValueShapedJson(shape, shapedJson._data.data, &text, &textLength);

  if (! ok) {
    return nullptr;
  }

  // parse the document text
  words = TRI_get_words(text, textLength, (size_t) fulltextIndex->_minWordLength, (size_t) TRI_FULLTEXT_MAX_WORD_LENGTH, true);

  if (words == nullptr) {
    return nullptr;
  }

  wordlist = TRI_CreateWordlistFulltextIndex(words->_buffer, words->_length);

  if (wordlist == nullptr) {
    TRI_FreeVectorString(TRI_UNKNOWN_MEM_ZONE, words);
    return nullptr;
  }

  // this really is a hack, but it works well:
  // make the word list vector think it's empty and free it
  // this does not free the word list, that we have already over the result
  words->_length = 0;
  words->_buffer = nullptr;
  TRI_FreeVectorString(TRI_UNKNOWN_MEM_ZONE, words);

  return wordlist;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into the fulltext index
////////////////////////////////////////////////////////////////////////////////

static int InsertFulltextIndex (TRI_index_t* idx, 
                                TRI_doc_mptr_t const* doc,
                                const bool isRollback) {
  TRI_fulltext_index_t* fulltextIndex;
  TRI_fulltext_wordlist_t* wordlist;
  int res;

  fulltextIndex = (TRI_fulltext_index_t*) idx;

  if (idx == nullptr) {
    LOG_WARNING("internal error in InsertFulltextIndex");
    return TRI_ERROR_INTERNAL;
  }

  res = TRI_ERROR_NO_ERROR;

  wordlist = GetWordlist(idx, doc);

  if (wordlist == nullptr) {
    // TODO: distinguish the cases "empty wordlist" and "out of memory"
    // LOG_WARNING("could not build wordlist");
    return res;
  }

  if (wordlist->_numWords > 0) {
    // TODO: use status codes
    if (! TRI_InsertWordsFulltextIndex(fulltextIndex->_fulltextIndex, (TRI_fulltext_doc_t) ((uintptr_t) doc), wordlist)) {
      LOG_ERROR("adding document to fulltext index failed");
      res = TRI_ERROR_INTERNAL;
    }
  }

  TRI_FreeWordlistFulltextIndex(wordlist);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the memory used by the index
////////////////////////////////////////////////////////////////////////////////

static size_t MemoryFulltextIndex (TRI_index_t const* idx) {
  TRI_fulltext_index_t const* fulltextIndex = (TRI_fulltext_index_t const*) idx;

  return TRI_MemoryFulltextIndex(fulltextIndex->_fulltextIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a fulltext index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonFulltextIndex (TRI_index_t const* idx) {
  TRI_json_t* json;
  TRI_json_t* fields;
  TRI_shape_path_t const* path;
  char const* attributeName;

  TRI_fulltext_index_t const* fulltextIndex = (TRI_fulltext_index_t const*) idx;

  if (fulltextIndex == nullptr) {
    return nullptr;
  }

  TRI_document_collection_t* document = idx->_collection;

  // convert attribute to string
  path = document->_shaper->lookupAttributePathByPid(document->_shaper, fulltextIndex->_attribute);

  if (path == 0) {
    return nullptr;
  }

  attributeName = ((char const*) path) + sizeof(TRI_shape_path_t) + (path->_aidLength * sizeof(TRI_shape_aid_t));

  json = TRI_JsonIndex(TRI_CORE_MEM_ZONE, idx);

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "minLength", TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) fulltextIndex->_minWordLength));

  fields = TRI_CreateListJson(TRI_CORE_MEM_ZONE);
  TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, attributeName));
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "fields", fields);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a fulltext index
////////////////////////////////////////////////////////////////////////////////

static int RemoveFulltextIndex (TRI_index_t* idx, 
                                TRI_doc_mptr_t const* doc,
                                const bool isRollback) {
  TRI_fulltext_index_t* fulltextIndex = (TRI_fulltext_index_t*) idx;

  TRI_DeleteDocumentFulltextIndex(fulltextIndex->_fulltextIndex, (TRI_fulltext_doc_t) ((uintptr_t) doc));

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief cleanup function for the fulltext index
///
/// This will incrementally clean the index by removing document/word pairs
/// for deleted documents
////////////////////////////////////////////////////////////////////////////////

static int CleanupFulltextIndex (TRI_index_t* idx) {
  LOG_TRACE("fulltext cleanup called");

  TRI_fulltext_index_t* fulltextIndex = (TRI_fulltext_index_t*) idx;
  int res = TRI_ERROR_NO_ERROR;

  // check whether we should do a cleanup at all
  if (! TRI_CompactFulltextIndex(fulltextIndex->_fulltextIndex)) {
    res = TRI_ERROR_INTERNAL;
  }

  return res;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a fulltext index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateFulltextIndex (TRI_document_collection_t* document,
                                      TRI_idx_iid_t iid,
                                      const char* attributeName,
                                      const bool indexSubstrings,
                                      int minWordLength) {
  TRI_index_t* idx;
  TRI_fts_index_t* fts;
  TRI_shaper_t* shaper;
  char* copy;
  TRI_shape_pid_t attribute;

  // look up the attribute
  shaper = document->_shaper;
  attribute = shaper->findOrCreateAttributePathByName(shaper, attributeName, true);

  if (attribute == 0) {
    return nullptr;  
  }

  copy = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, attributeName);
  TRI_fulltext_index_t* fulltextIndex = static_cast<TRI_fulltext_index_t*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_fulltext_index_t), false));

  fts = TRI_CreateFtsIndex(2048, 1, 1);

  if (fts == nullptr) {
    TRI_Free(TRI_CORE_MEM_ZONE, fulltextIndex);
    return nullptr;
  }

  idx = &fulltextIndex->base;

  TRI_InitIndex(idx, iid, TRI_IDX_TYPE_FULLTEXT_INDEX, document, false);

  idx->memory   = MemoryFulltextIndex;
  idx->json     = JsonFulltextIndex;
  idx->insert   = InsertFulltextIndex;
  idx->remove   = RemoveFulltextIndex;
  idx->cleanup  = CleanupFulltextIndex;

  fulltextIndex->_fulltextIndex   = fts;
  fulltextIndex->_indexSubstrings = indexSubstrings;
  fulltextIndex->_attribute       = attribute;
  fulltextIndex->_minWordLength   = (minWordLength > 0 ? minWordLength : 1);

  TRI_InitVectorString(&idx->_fields, TRI_CORE_MEM_ZONE);
  TRI_PushBackVectorString(&idx->_fields, copy);

  return idx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyFulltextIndex (TRI_index_t* idx) {
  if (idx == nullptr) {
    return;
  }

  TRI_fulltext_index_t* fulltextIndex = (TRI_fulltext_index_t*) idx;

  TRI_DestroyVectorString(&idx->_fields);
  LOG_TRACE("destroying fulltext index");
  TRI_FreeFtsIndex(fulltextIndex->_fulltextIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeFulltextIndex (TRI_index_t* idx) {
  if (idx == nullptr) {
    return;
  }

  TRI_DestroyFulltextIndex(idx);
  TRI_Free(TRI_CORE_MEM_ZONE, idx);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    BITARRAY INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

// .............................................................................
// Helper function for TRI_LookupBitarrayIndex
// .............................................................................

static int FillLookupBitarrayOperator (TRI_index_operator_t* indexOperator, TRI_document_collection_t* collection) {
  if (indexOperator == nullptr) {
    return TRI_ERROR_INTERNAL;
  }

  switch (indexOperator->_type) {
    case TRI_AND_INDEX_OPERATOR:
    case TRI_NOT_INDEX_OPERATOR:
    case TRI_OR_INDEX_OPERATOR: {
      TRI_logical_index_operator_t* logicalOperator = (TRI_logical_index_operator_t*) indexOperator;
      FillLookupBitarrayOperator(logicalOperator->_left, collection);
      FillLookupBitarrayOperator(logicalOperator->_right,collection);
      break;
    }

    case TRI_EQ_INDEX_OPERATOR:
    case TRI_GE_INDEX_OPERATOR:
    case TRI_GT_INDEX_OPERATOR:
    case TRI_NE_INDEX_OPERATOR:
    case TRI_LE_INDEX_OPERATOR:
    case TRI_LT_INDEX_OPERATOR: {
      TRI_relation_index_operator_t* relationOperator = (TRI_relation_index_operator_t*) indexOperator;
      relationOperator->_numFields  = relationOperator->_parameters->_value._objects._length;
      relationOperator->_fields     = NULL; // bitarray indexes need only the json representation of values

      // even tough we use the json representation of the values sent by the client
      // for a bitarray index, we still require the shaped_json values for later
      // if we intend to force a bitarray index to return a result set irrespective
      // of whether the index can do this efficiently, then we will require the shaped json
      // representation of the values to apply any filter condition. Note that
      // for skiplist indexes, we DO NOT use the json representation, rather the shaped json
      // representation of the values is used since for skiplists we are ALWAYS required to
      // go to the document and make comparisons with the document values and the client values


      // when you are ready to use the shaped json values -- uncomment the follow
      /*
      relationOperator->_fields     = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * relationOperator->_numFields, false);
      if (relationOperator->_fields != NULL) {
        int j;
        TRI_json_t* jsonObject;
        TRI_shaped_json_t* shapedObject;
        for (j = 0; j < relationOperator->_numFields; ++j) {
          jsonObject   = (TRI_json_t*) (TRI_AtVector(&(relationOperator->_parameters->_value._objects),j));
          shapedObject = TRI_ShapedJsonJson(collection->_shaper, jsonObject);
          if (shapedObject) {
            relationOperator->_fields[j] = *shapedObject; // shallow copy here is ok
            TRI_Free(TRI_UNKNOWN_MEM_ZONE, shapedObject); // don't require storage anymore
          }
        }
      }
      else {
        relationOperator->_numFields = 0; // out of memory?
      }
      */

      break;

    }

  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief attempts to locate an entry in the bitarray index
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Note: this function will destroy the passed index operator before it returns
// Warning: who ever calls this function is responsible for destroying
// TRI_index_iterator_t* results
// .............................................................................


TRI_index_iterator_t* TRI_LookupBitarrayIndex (TRI_index_t* idx,
                                               TRI_index_operator_t* indexOperator,
                                               bool (*filter) (TRI_index_iterator_t*)) {
  TRI_bitarray_index_t* baIndex;
  TRI_index_iterator_t* iteratorResult;
  int                   errorResult;

  baIndex = (TRI_bitarray_index_t*)(idx);

  // .........................................................................
  // fill the relation operators which may be embedded in the indexOperator
  // with additional information. Recall the indexOperator is what information
  // was received from a client for querying the bitarray index.
  // .........................................................................
  
  errorResult = FillLookupBitarrayOperator(indexOperator, baIndex->base._collection); 

  if (errorResult != TRI_ERROR_NO_ERROR) {
    return NULL;
  }  
  
  iteratorResult = BitarrayIndex_find(baIndex->_bitarrayIndex,
                                      indexOperator,
                                      &baIndex->_paths,
                                      baIndex,
                                      NULL);

  TRI_FreeIndexOperator(indexOperator);

  return iteratorResult;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief helper for bitarray methods
////////////////////////////////////////////////////////////////////////////////

static int BitarrayIndexHelper(const TRI_bitarray_index_t* baIndex, 
                               TRI_bitarray_index_key_t* element,
                               const TRI_doc_mptr_t* document,
                               const TRI_shaped_json_t* shapedDoc) {

  TRI_shaped_json_t shapedObject;
  TRI_shape_access_t const* acc;
  size_t j;

  // ...........................................................................
  // For the structure element->fields, memory will have been allocated for this
  // by the calling procedure -- DO NOT deallocate the memory here -- it is the
  // responsibility of the calling procedure.
  // ...........................................................................


  if (shapedDoc != NULL) {

    // ..........................................................................
    // Attempting to locate a entry using TRI_shaped_json_t object. Use this
    // when we wish to remove a entry and we only have the "keys" rather than
    // having the document (from which the keys would follow).
    // ..........................................................................

    element->data = NULL;


    for (j = 0; j < baIndex->_paths._length; ++j) {
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&baIndex->_paths,j)));

      // ..........................................................................
      // Determine if document has that particular shape
      // ..........................................................................

      acc = TRI_FindAccessorVocShaper(baIndex->base._collection->_shaper, shapedDoc->_sid, shape);

      if (acc == NULL || acc->_resultSid == 0) {
        return TRI_ERROR_ARANGO_INDEX_BITARRAY_UPDATE_ATTRIBUTE_MISSING;
      }


      // ..........................................................................
      // Extract the field
      // ..........................................................................

      if (! TRI_ExecuteShapeAccessor(acc, shapedDoc, &shapedObject)) {
        return TRI_ERROR_INTERNAL;
      }

      // ..........................................................................
      // Store the json shaped Object -- this is what will be used by index to
      // whatever it requires to be determined.
      // ..........................................................................

      element->fields[j] = shapedObject;
    }
  }

  else if (document != NULL) {

    // ..........................................................................
    // Assign the document to the element structure so that it can
    // be retreived later.
    // ..........................................................................

    element->data = const_cast<TRI_doc_mptr_t*>(document);

    for (j = 0; j < baIndex->_paths._length; ++j) {
      TRI_shaped_json_t shapedJson;
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&baIndex->_paths,j)));

      // ..........................................................................
      // Determine if document has that particular shape
      // ..........................................................................

      TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->getDataPtr());  // ONLY IN INDEX

      acc = TRI_FindAccessorVocShaper(baIndex->base._collection->_shaper, shapedJson._sid, shape);

      if (acc == NULL || acc->_resultSid == 0) {
        return TRI_ERROR_ARANGO_INDEX_DOCUMENT_ATTRIBUTE_MISSING;
      }

      // ..........................................................................
      // Extract the field
      // ..........................................................................

      if (! TRI_ExecuteShapeAccessor(acc, &shapedJson, &shapedObject)) {
        return TRI_ERROR_INTERNAL;
      }

      // ..........................................................................
      // Store the field
      // ..........................................................................

      element->fields[j] = shapedObject;
    }
  }

  else {
    return TRI_ERROR_INTERNAL;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into a bitarray list index
////////////////////////////////////////////////////////////////////////////////

static int InsertBitarrayIndex (TRI_index_t* idx, 
                                TRI_doc_mptr_t const* doc,
                                const bool isRollback) {
  TRI_bitarray_index_key_t element;
  TRI_bitarray_index_t* baIndex;
  int result;

  // ............................................................................
  // Obtain the bitarray index structure
  // ............................................................................

  baIndex = (TRI_bitarray_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in InsertBitarrayIndex");
    return TRI_ERROR_INTERNAL;
  }



  // ............................................................................
  // Allocate storage to shaped json objects stored as a simple list.
  // These will be used for comparisions
  // ............................................................................

  element.numFields   = baIndex->_paths._length;
  element.fields      = static_cast<TRI_shaped_json_t*>(TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * element.numFields, false));
  element.collection  = baIndex->base._collection;

  if (element.fields == NULL) {
    LOG_WARNING("out-of-memory in InsertBitarrayIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  // ............................................................................
  // For each attribute we have defined in the index obtain its corresponding
  // value.
  // ............................................................................

  result = BitarrayIndexHelper(baIndex, &element, doc, NULL);

  // ............................................................................
  // most likely the cause of this error is that the 'shape' of the document
  // does not match the 'shape' of the index structure -- so the document
  // is ignored.
  // ............................................................................

  if (result != TRI_ERROR_NO_ERROR) {

    // ..........................................................................
    // Deallocated the memory already allocated to element.fields
    // ..........................................................................

    TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
    element.numFields = 0;


    // ..........................................................................
    // It may happen that the document does not have the necessary attributes to
    // be included within the bitarray index, in this case do not report back an error.
    // ..........................................................................

    if (result == TRI_ERROR_ARANGO_INDEX_DOCUMENT_ATTRIBUTE_MISSING) {
      if (! baIndex->_supportUndef) {
        return TRI_ERROR_NO_ERROR;
      }


      // ........................................................................
      // This insert means that the document does NOT have the index attributes
      // defined, however, we still insert it into aspecial 'undefined' column
      // ........................................................................

      result = BitarrayIndex_insert(baIndex->_bitarrayIndex, &element);
    }

    return result;
  }


  // ............................................................................
  // This insert means that the document has ALL attributes which have been defined
  // in the index. However, it may happen that one or more attribute VALUES are
  // unsupported within the index -- in this case the function below will return
  // an error and insertion of the document is rolled back.
  // ............................................................................

  result = BitarrayIndex_insert(baIndex->_bitarrayIndex, &element);

  // ............................................................................
  // Since we have allocated memory to element.fields above, we have to deallocate
  // this here.
  // ............................................................................

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a bitarray index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonBitarrayIndex (TRI_index_t const* idx) {
  TRI_json_t* json;      // the json object we return describing the index
  TRI_json_t* keyValues; // a list of attributes and their associated values
  size_t j;

  // ..........................................................................
  // Recast index as bitarray index
  // ..........................................................................

  TRI_bitarray_index_t const* baIndex = (TRI_bitarray_index_t const*) idx;

  if (baIndex == NULL) {
    return NULL;
  }

  TRI_document_collection_t* document = idx->_collection;

  // ..........................................................................
  // Allocate sufficent memory for the field list
  // ..........................................................................

  char const** fieldList = static_cast<char const**>(TRI_Allocate( TRI_CORE_MEM_ZONE, (sizeof(char*) * baIndex->_paths._length) , false));

  // ..........................................................................
  // Convert the attributes (field list of the bitarray index) into strings
  // ..........................................................................

  for (j = 0; j < baIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&baIndex->_paths,j)));
    const TRI_shape_path_t* path = document->_shaper->lookupAttributePathByPid(document->_shaper, shape);

    if (path == NULL) {
      TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);
      return NULL;
    }

    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }


  // ..........................................................................
  // create the json object representing the index and proceed to fill it
  // ..........................................................................

  json = TRI_JsonIndex(TRI_CORE_MEM_ZONE, idx);

  // ..........................................................................
  // Create json list which will hold the key value pairs. Assuming that the
  // index is constructed with 3 fields "a","b" % "c", these pairs are stored as follows:
  // [ ["a", [value-a1,...,value-aN]], ["b", [value-b1,...,value-bN]], ["c", [value-c1,...,value-cN]] ]
  // ..........................................................................


  // ..........................................................................
  // Create the key value list
  // ..........................................................................

  keyValues = TRI_CreateListJson(TRI_CORE_MEM_ZONE);

  for (j = 0; j < baIndex->_paths._length; ++j) {
    TRI_json_t* keyValue;
    TRI_json_t* key;
    TRI_json_t* value;

    // ........................................................................
    // Create the list to store the pairs
    // ........................................................................

    keyValue = TRI_CreateListJson(TRI_CORE_MEM_ZONE);

    if (keyValue == NULL) {
      TRI_FreeJson(TRI_CORE_MEM_ZONE, keyValues);
      TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
      TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);
      return NULL;
    }

    // ........................................................................
    // Create the key json object (copy the string)
    // ........................................................................

    key = TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, fieldList[j]);

    if (key == NULL) {
      TRI_FreeJson(TRI_CORE_MEM_ZONE, keyValues);
      TRI_FreeJson(TRI_CORE_MEM_ZONE, keyValue);
      TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
      TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);
      return NULL;
    }

    // ........................................................................
    // Create the list of values and fill it from the values stored in the
    // bit array index structure
    // ........................................................................

    value = TRI_CreateListJson(TRI_CORE_MEM_ZONE);

    if (value == NULL) {
      TRI_FreeJson(TRI_CORE_MEM_ZONE, keyValues);
      TRI_FreeJson(TRI_CORE_MEM_ZONE, key);
      TRI_FreeJson(TRI_CORE_MEM_ZONE, keyValue);
      TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
      TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);
      return NULL;
    }

    TRI_CopyToJson(TRI_CORE_MEM_ZONE, value, (TRI_json_t*)(TRI_AtVector(&baIndex->_values,j)));


    // ........................................................................
    // insert the key first followed by the list of values
    // ........................................................................

    TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, keyValue, key);
    TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, keyValue, value);


    // ........................................................................
    // insert the key value pair into the list of such pairs
    // ........................................................................

    TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, keyValues, keyValue);
  }

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "fields", keyValues);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "undefined", TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, baIndex->_supportUndef));

  TRI_Free(TRI_CORE_MEM_ZONE, (void*) fieldList);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a bitarray index
////////////////////////////////////////////////////////////////////////////////

static int RemoveBitarrayIndex (TRI_index_t* idx, 
                                TRI_doc_mptr_t const* doc,
                                const bool isRollback) {
  TRI_bitarray_index_key_t element;
  TRI_bitarray_index_t* baIndex;
  int result;

  // ............................................................................
  // Obtain the bitarray index structure
  // ............................................................................

  baIndex = (TRI_bitarray_index_t*) idx;

  // ............................................................................
  // Allocate some memory for the element structure
  // ............................................................................

  element.numFields  = baIndex->_paths._length;
  element.fields     = static_cast<TRI_shaped_json_t*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_shaped_json_t) * element.numFields, false));
  element.collection = baIndex->base._collection;
  
  // ..........................................................................
  // Fill the json field list with values from the document
  // ..........................................................................

  result = BitarrayIndexHelper(baIndex, &element, doc, NULL);

  // ..........................................................................
  // Error returned generally implies that the document never was part of the
  // index -- however for a bitarray index we support docs which do not have
  // such an index key(s).
  // ..........................................................................

  if (result != TRI_ERROR_NO_ERROR) {

    // ........................................................................
    // Check what type of error we received. If 'bad' error, then return
    // ........................................................................

    if (result != TRI_ERROR_ARANGO_INDEX_DOCUMENT_ATTRIBUTE_MISSING) {

      // ......................................................................
      // Deallocate memory allocated to element.fields above
      // ......................................................................
    
      TRI_Free(TRI_CORE_MEM_ZONE, element.fields);
      return result;    
    }    

    // ........................................................................
    // If we support undefined documents in the index, then pass this on,
    // otherwise return an error. Note that, eventually it may be slightly
    // more efficient to simply pass these undefined documents straight to
    // the index without using the BitarrayIndexHelper function above.
    // ........................................................................

    if (! baIndex->_supportUndef) {

      // ......................................................................
      // Deallocate memory allocated to element.fields above
      // ......................................................................
    
      TRI_Free(TRI_CORE_MEM_ZONE, element.fields);
    
      return TRI_ERROR_NO_ERROR;    
    }
  }
  
  // ............................................................................
  // Attempt to remove document from index
  // ............................................................................
  
  result = BitarrayIndex_remove(baIndex->_bitarrayIndex, &element);

  // ............................................................................
  // Deallocate memory allocated to element.fields above
  // ............................................................................
    
  TRI_Free(TRI_CORE_MEM_ZONE, element.fields);
  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a bitarray index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateBitarrayIndex (TRI_document_collection_t* document,
                                      TRI_idx_iid_t iid,
                                      TRI_vector_pointer_t* fields,
                                      TRI_vector_t* paths,
                                      TRI_vector_pointer_t* values,
                                      bool supportUndef, 
                                      int* errorNum, 
                                      char** errorStr) {
  TRI_index_t* idx;
  size_t i,j,k;
  int result;
  void* createContext;
  int cardinality;


  // ...........................................................................
  // Before we start moving things about, ensure that the attributes have
  // not been repeated
  // ...........................................................................

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t* leftShape = (TRI_shape_pid_t*)(TRI_AtVector(paths,j));
    for (i = j + 1; i < paths->_length;  ++i) {
      TRI_shape_pid_t* rightShape = (TRI_shape_pid_t*)(TRI_AtVector(paths,i));
      if (*leftShape == *rightShape) {
        LOG_WARNING("bitarray index creation failed -- duplicate keys in index");
        *errorNum = TRI_ERROR_ARANGO_INDEX_BITARRAY_CREATION_FAILURE_DUPLICATE_ATTRIBUTES;
        *errorStr = TRI_DuplicateString("bitarray index creation failed -- duplicate keys in index");
        return NULL;
      }
    }
  }

  // ...........................................................................
  // For each key (attribute) ensure that the list of supported values are
  // unique
  // ...........................................................................

  for (k = 0;  k < paths->_length;  ++k) {
    TRI_json_t* valueList = (TRI_json_t*)(TRI_AtVectorPointer(values,k));

    if (valueList == NULL || valueList->_type != TRI_JSON_LIST) {
      LOG_WARNING("bitarray index creation failed -- list of values for index undefined");
      *errorNum = TRI_ERROR_BAD_PARAMETER;
      *errorStr = TRI_DuplicateString("bitarray index creation failed -- list of values for index undefined");
      return NULL;
    }

    for (j = 0; j < valueList->_value._objects._length; ++j) {
      TRI_json_t* leftValue = (TRI_json_t*)(TRI_AtVector(&(valueList->_value._objects), j));
      for (i = j + 1; i < valueList->_value._objects._length; ++i) {
        TRI_json_t* rightValue = (TRI_json_t*)(TRI_AtVector(&(valueList->_value._objects), i));
        if (TRI_EqualJsonJson(leftValue, rightValue)) {
          LOG_WARNING("bitarray index creation failed -- duplicate values in value list for an attribute");
          *errorNum = TRI_ERROR_ARANGO_INDEX_BITARRAY_CREATION_FAILURE_DUPLICATE_VALUES;
          *errorStr = TRI_DuplicateString("bitarray index creation failed -- duplicate values in value list for an attribute");
          return NULL;
        }
      }
    }
  }

  // ...........................................................................
  // attempt to allocate memory for the bit array index structure
  // ...........................................................................

  TRI_bitarray_index_t* baIndex = static_cast<TRI_bitarray_index_t*>(TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_bitarray_index_t), false));
  idx = &baIndex->base;

  TRI_InitIndex(idx, iid, TRI_IDX_TYPE_BITARRAY_INDEX, document, false);

  idx->json     = JsonBitarrayIndex;
  idx->insert   = InsertBitarrayIndex;
  idx->remove   = RemoveBitarrayIndex;
  
  baIndex->_supportUndef  = supportUndef;
  baIndex->_bitarrayIndex = NULL;
  
  // ...........................................................................
  // Copy the contents of the shape list vector into a new vector and store this
  // Do the same for the values associated with the attributes
  // ...........................................................................

  TRI_InitVector(&baIndex->_paths, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shape_pid_t));
  TRI_InitVector(&baIndex->_values, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_json_t));

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_json_t value;
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths,j)));
    TRI_PushBackVector(&baIndex->_paths, &shape);
    TRI_CopyToJson(TRI_UNKNOWN_MEM_ZONE, &value, (TRI_json_t*)(TRI_AtVectorPointer(values,j)));
    TRI_PushBackVector(&baIndex->_values, &value);
  }

  // ...........................................................................
  // Store the list of fields (attributes based on the paths above) as simple
  // c strings - saves us looking these up at a latter stage
  // ...........................................................................

  TRI_InitVectorString(&idx->_fields, TRI_CORE_MEM_ZONE);

  for (j = 0;  j < fields->_length;  ++j) {
    char const* name = static_cast<char const*>(fields->_buffer[j]);
    char* copy = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, name);

    TRI_PushBackVectorString(&idx->_fields, copy);
  }

  // ...........................................................................
  // Currently there is no creation context -- todo later
  // ...........................................................................

  createContext = NULL;

  // ...........................................................................
  // Check that the attributes have not been repeated
  // ...........................................................................

  // ...........................................................................
  // Determine the cardinality of the Bitarray index (that is, the number of
  // columns which constitute the index)
  // ...........................................................................

  cardinality = 0;

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_json_t* value = (TRI_json_t*) TRI_AtVector(&baIndex->_values,j);
    size_t numValues;

    if (value == NULL) {
      TRI_DestroyVector(&baIndex->_paths);
      TRI_DestroyVector(&baIndex->_values);
      TRI_Free(TRI_CORE_MEM_ZONE, baIndex);
      LOG_WARNING("bitarray index creation failed -- list of values for index undefined");
      return NULL;
    }

    numValues = value->_value._objects._length;


    // .........................................................................
    // value is a list json type -- the number of entries informs us how many
    // different possible values there are
    // .........................................................................

    cardinality += (int) numValues;
  }

  // ...........................................................................
  // for the moment we restrict the cardinality to 64
  // ...........................................................................

  if (cardinality > 64) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_Free(TRI_CORE_MEM_ZONE, baIndex);
    LOG_WARNING("bitarray index creation failed -- more than 64 possible values");
    return NULL;
  }


  if (cardinality < 1 ) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_Free(TRI_CORE_MEM_ZONE, baIndex);
    LOG_WARNING("bitarray index creation failed -- no index values defined");
    return NULL;
  }

  // ...........................................................................
  // Assign the function calls used by the query engine
  // ...........................................................................

  result = BittarrayIndex_assignMethod(&(idx->indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || BittarrayIndex_assignMethod(&(idx->indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || BittarrayIndex_assignMethod(&(idx->indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);

  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_Free(TRI_CORE_MEM_ZONE, baIndex);
    LOG_WARNING("bitarray index creation failed -- internal error when assigning function calls");
    return NULL;
  }

  // ...........................................................................
  // attempt to create a new bitarray index
  // ...........................................................................

  result = BitarrayIndex_new(&(baIndex->_bitarrayIndex), TRI_UNKNOWN_MEM_ZONE, (size_t) cardinality,
                             &baIndex->_values, supportUndef, createContext);
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_FreeBitarrayIndex(idx);
    LOG_WARNING("bitarray index creation failed -- your guess as good as mine");
    return NULL;
  }

  return idx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyBitarrayIndex (TRI_index_t* idx) {
  if (idx == nullptr) {
    return;
  }

  LOG_TRACE("destroying bitarray index");
  TRI_DestroyVectorString(&idx->_fields);

  TRI_bitarray_index_t* baIndex = (TRI_bitarray_index_t*) idx;

  for (size_t j = 0;  j < baIndex->_values._length;  ++j) {
    TRI_DestroyJson(TRI_UNKNOWN_MEM_ZONE, (TRI_json_t*)(TRI_AtVector(&(baIndex->_values),j)));
  }

  TRI_DestroyVector(&baIndex->_paths);
  TRI_DestroyVector(&baIndex->_values);
  BitarrayIndex_free(baIndex->_bitarrayIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeBitarrayIndex (TRI_index_t* idx) {
  if (idx == nullptr) {
    return;
  }

  TRI_DestroyBitarrayIndex(idx);
  TRI_Free(TRI_CORE_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief index comparator, used by the coordinator to detect if two index
/// contents are the same
////////////////////////////////////////////////////////////////////////////////

bool IndexComparator (TRI_json_t const* lhs,
                      TRI_json_t const* rhs) {
  TRI_json_t* typeJson = TRI_LookupArrayJson(lhs, "type");
  TRI_ASSERT(TRI_IsStringJson(typeJson));

  // type must be identical
  if (! TRI_CheckSameValueJson(typeJson, TRI_LookupArrayJson(rhs, "type"))) {
    return false;
  }
  
  TRI_idx_type_e type = TRI_TypeIndex(typeJson->_value._string.data);


  // unique must be identical if present
  TRI_json_t* value = TRI_LookupArrayJson(lhs, "unique");
  if (TRI_IsBooleanJson(value)) {
    if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "unique"))) {
      return false;
    }
  }

 
  if (type == TRI_IDX_TYPE_GEO1_INDEX) {
    // geoJson must be identical if present
    value = TRI_LookupArrayJson(lhs, "geoJson");
    if (TRI_IsBooleanJson(value)) {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "geoJson"))) {
        return false;
      }
    }
    value = TRI_LookupArrayJson(lhs, "ignoreNull");
    if (TRI_IsBooleanJson(value)) {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "ignoreNull"))) {
        return false;
      }
    }
  }
  else if (type == TRI_IDX_TYPE_GEO2_INDEX) {
    value = TRI_LookupArrayJson(lhs, "ignoreNull");
    if (TRI_IsBooleanJson(value)) {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "ignoreNull"))) {
        return false;
      }
    }
  }
  else if (type == TRI_IDX_TYPE_FULLTEXT_INDEX) {
    // minLength
    value = TRI_LookupArrayJson(lhs, "minLength");
    if (TRI_IsNumberJson(value)) {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "minLength"))) {
        return false;
      }
    }
  }
  else if (type == TRI_IDX_TYPE_CAP_CONSTRAINT) {
    // size, byteSize
    value = TRI_LookupArrayJson(lhs, "size");
    if (TRI_IsNumberJson(value)) {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "size"))) {
        return false;
      }
    }
    
    value = TRI_LookupArrayJson(lhs, "byteSize");
    if (TRI_IsNumberJson(value)) {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "byteSize"))) {
        return false;
      }
    }
  }
  
  if (type == TRI_IDX_TYPE_BITARRAY_INDEX) {
    // bitarray indexes are considered identical if they are based on the same attributes
    TRI_json_t const* r = TRI_LookupArrayJson(rhs, "fields");
    value = TRI_LookupArrayJson(lhs, "fields");

    if (TRI_IsListJson(value) && 
        TRI_IsListJson(r) && 
        value->_value._objects._length == r->_value._objects._length) {

      for (size_t i = 0; i < value->_value._objects._length; ++i) {
        TRI_json_t const* l1 = TRI_LookupListJson(value, i);
        TRI_json_t const* r1 = TRI_LookupListJson(r, i);

        if (TRI_IsListJson(l1) && 
            TRI_IsListJson(r1) && 
            l1->_value._objects._length == 2 && 
            r1->_value._objects._length == 2) {
        
          // element at position 0 is the attribute name
          if (! TRI_CheckSameValueJson(TRI_LookupListJson(l1, 0), TRI_LookupListJson(r1, 0))) {
            return false;
          }
        }

      }
    }

    // we must always exit here to avoid the "regular" fields comparison
    return true;
  }
    
  // other index types: fields must be identical if present
  value = TRI_LookupArrayJson(lhs, "fields");

  if (TRI_IsListJson(value)) {
    if (type == TRI_IDX_TYPE_HASH_INDEX) {
      // compare fields in arbitrary order
      TRI_json_t const* r = TRI_LookupArrayJson(rhs, "fields");

      if (! TRI_IsListJson(r) || 
          value->_value._objects._length != r->_value._objects._length) {
        return false;
      }
   
      for (size_t i = 0; i < value->_value._objects._length; ++i) {
        TRI_json_t const* v = TRI_LookupListJson(value, i);

        bool found = false;
        
        for (size_t j = 0; j < r->_value._objects._length; ++j) {
          if (TRI_CheckSameValueJson(v, TRI_LookupListJson(r, j))) {
            found = true;
            break;
          }
        }

        if (! found) {
          return false;
        }
      }
    }
    else {
      if (! TRI_CheckSameValueJson(value, TRI_LookupArrayJson(rhs, "fields"))) {
        return false;
      }
    }

  }

  return true;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
