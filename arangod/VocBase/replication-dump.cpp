////////////////////////////////////////////////////////////////////////////////
/// @brief replication dump functions
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
/// @author Jan Steemann
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "replication-dump.h"

#include "BasicsC/conversions.h"
#include "BasicsC/files.h"
#include "BasicsC/json.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/tri-strings.h"

#include "VocBase/collection.h"
#include "VocBase/datafile.h"
#include "VocBase/document-collection.h"
#include "VocBase/transaction.h"
#include "VocBase/vocbase.h"
#include "VocBase/voc-shaper.h"
#include "Utils/transactions.h"
#include "Wal/Marker.h"

using namespace triagens;

// -----------------------------------------------------------------------------
// --SECTION--                                                       REPLICATION
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                   private defines
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut function
////////////////////////////////////////////////////////////////////////////////

#define FAIL_IFNOT(func, buffer, val)                                     \
  if (func(buffer, val) != TRI_ERROR_NO_ERROR) {                          \
    return false;                                                         \
  }

////////////////////////////////////////////////////////////////////////////////
/// @brief create a string-buffer function name
////////////////////////////////////////////////////////////////////////////////

#define APPEND_FUNC(name) TRI_ ## name ## StringBuffer

////////////////////////////////////////////////////////////////////////////////
/// @brief append a character to a string-buffer or fail
////////////////////////////////////////////////////////////////////////////////

#define APPEND_CHAR(buffer, c)      FAIL_IFNOT(APPEND_FUNC(AppendChar), buffer, c)

////////////////////////////////////////////////////////////////////////////////
/// @brief append a string to a string-buffer or fail
////////////////////////////////////////////////////////////////////////////////

#define APPEND_STRING(buffer, str)  FAIL_IFNOT(APPEND_FUNC(AppendString), buffer, str)

////////////////////////////////////////////////////////////////////////////////
/// @brief append uint64 to a string-buffer or fail
////////////////////////////////////////////////////////////////////////////////

#define APPEND_UINT64(buffer, val)  FAIL_IFNOT(APPEND_FUNC(AppendUInt64), buffer, val)

////////////////////////////////////////////////////////////////////////////////
/// @brief append json to a string-buffer or fail
////////////////////////////////////////////////////////////////////////////////

#define APPEND_JSON(buffer, json)   FAIL_IFNOT(TRI_StringifyJson, buffer, json)

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief a datafile descriptor
////////////////////////////////////////////////////////////////////////////////
  
typedef struct df_entry_s {
  TRI_datafile_t* _data;
  TRI_voc_tick_t  _dataMin;
  TRI_voc_tick_t  _dataMax;
  TRI_voc_tick_t  _tickMax;
  bool            _isJournal;
}
df_entry_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief container for a resolved collection name (cid => name)
////////////////////////////////////////////////////////////////////////////////

typedef struct resolved_name_s {
  TRI_voc_cid_t   _cid;
  char*           _name;
}
resolved_name_t;

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
/// @brief hashes a collection id
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyCid (TRI_associative_pointer_t* array, 
                            void const* key) {
  TRI_voc_cid_t const* k = static_cast<TRI_voc_cid_t const*>(key);

  return *k;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes a collection name
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementCid (TRI_associative_pointer_t* array, 
                                void const* element) {
  resolved_name_t const* e = static_cast<resolved_name_t const*>(element);

  return e->_cid;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a collection
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualKeyElementCid (TRI_associative_pointer_t* array, 
                                  void const* key, 
                                  void const* element) {
  TRI_voc_cid_t const* k = static_cast<TRI_voc_cid_t const*>(key);
  resolved_name_t const* e = static_cast<resolved_name_t const*>(element);

  return *k == e->_cid;
}
    
////////////////////////////////////////////////////////////////////////////////
/// @brief lookup a collection name
////////////////////////////////////////////////////////////////////////////////

static bool LookupCollectionName (TRI_replication_dump_t* dump,
                                  TRI_voc_cid_t cid,
                                  char** result) {

  TRI_ASSERT(cid > 0);
  
  resolved_name_t* found = static_cast<resolved_name_t*>(TRI_LookupByKeyAssociativePointer(&dump->_collectionNames, &cid));

  if (found == NULL) {
    found = static_cast<resolved_name_t*>(TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(resolved_name_t), false));

    if (found == NULL) {
      // out of memory;
      return false;
    }

    found->_cid = cid;
    // name can be NULL if collection is not found. 
    // but we will still cache a NULL result!
    found->_name = TRI_GetCollectionNameByIdVocBase(dump->_vocbase, cid);
    
    TRI_InsertKeyAssociativePointer(&dump->_collectionNames, &found->_cid, found, false); 
  }

  *result = found->_name;
  
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief append a collection name or id to a string buffer
////////////////////////////////////////////////////////////////////////////////

static bool AppendCollection (TRI_replication_dump_t* dump,
                              TRI_voc_cid_t cid,
                              bool translateCollectionIds) {
  if (translateCollectionIds) {
    if (cid > 0) {
      char* name;

      if (! LookupCollectionName(dump, cid, &name)) {
        return false;
      }

      if (name != NULL) {
        APPEND_STRING(dump->_buffer, name);
        return true;
      }
    }
    
    APPEND_STRING(dump->_buffer, "_unknown");
  }
  else {
    APPEND_UINT64(dump->_buffer, (uint64_t) cid);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief iterate over a vector of datafiles and pick those with a specific
/// data range
////////////////////////////////////////////////////////////////////////////////

static int IterateDatafiles (TRI_vector_pointer_t const* datafiles,
                             TRI_vector_t* result,
                             TRI_voc_tick_t dataMin,
                             TRI_voc_tick_t dataMax,
                             bool isJournal) {
  
  int res = TRI_ERROR_NO_ERROR;

  size_t const n = datafiles->_length;

  for (size_t i = 0; i < n; ++i) {
    TRI_datafile_t* df = static_cast<TRI_datafile_t*>(TRI_AtVectorPointer(datafiles, i));

    df_entry_t entry = { 
      df,
      df->_dataMin,
      df->_dataMax,
      df->_tickMax,
      isJournal
    };
    
    LOG_TRACE("checking datafile %llu with data range %llu - %llu, tick max: %llu", 
              (unsigned long long) df->_fid,
              (unsigned long long) df->_dataMin, 
              (unsigned long long) df->_dataMax,
              (unsigned long long) df->_tickMax);
    
    if (df->_dataMin == 0 || df->_dataMax == 0) {
      // datafile doesn't have any data
      continue;
    }

    TRI_ASSERT(df->_tickMin <= df->_tickMax);
    TRI_ASSERT(df->_dataMin <= df->_dataMax);

    if (dataMax < df->_dataMin) {
      // datafile is newer than requested range
      continue;
    }

    if (dataMin > df->_dataMax) {
      // datafile is older than requested range
      continue;
    }
     
    res = TRI_PushBackVector(result, &entry);

    if (res != TRI_ERROR_NO_ERROR) {
      break;
    }
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the datafiles of a collection for a specific tick range
////////////////////////////////////////////////////////////////////////////////

static TRI_vector_t GetRangeDatafiles (TRI_document_collection_t* document,
                                       TRI_voc_tick_t dataMin,
                                       TRI_voc_tick_t dataMax) {
  TRI_vector_t datafiles;

  LOG_TRACE("getting datafiles in data range %llu - %llu", 
            (unsigned long long) dataMin, 
            (unsigned long long) dataMax);

  // determine the datafiles of the collection
  TRI_InitVector(&datafiles, TRI_CORE_MEM_ZONE, sizeof(df_entry_t));

  TRI_READ_LOCK_DATAFILES_DOC_COLLECTION(document);

  IterateDatafiles(&document->_datafiles, &datafiles, dataMin, dataMax, false);
  IterateDatafiles(&document->_journals, &datafiles, dataMin, dataMax, true);
  
  TRI_READ_UNLOCK_DATAFILES_DOC_COLLECTION(document);

  return datafiles;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a raw marker from a datafile for a collection dump
////////////////////////////////////////////////////////////////////////////////

static bool StringifyMarkerDump (TRI_replication_dump_t* dump,
                                 TRI_document_collection_t* document,
                                 TRI_df_marker_t const* marker,
                                 bool withTicks,
                                 bool translateCollectionIds) {
  // This covers two cases:
  //   1. document is not nullptr and marker points into a data file
  //   2. document is a nullptr and marker points into a WAL file
  // no other combinations are allowed.

  TRI_string_buffer_t* buffer;
  TRI_replication_operation_e type;
  TRI_voc_key_t key;
  TRI_voc_rid_t rid;
  bool haveData = true;
  bool isWal = false;

  buffer = dump->_buffer; 

  if (buffer == NULL) {
    return false;
  }

  switch (marker->_type) {
    case TRI_DOC_MARKER_KEY_DELETION: {
      TRI_ASSERT(nullptr != document);
      auto m = reinterpret_cast<TRI_doc_deletion_key_marker_t const*>(marker);
      key = ((char*) m) + m->_offsetKey;
      type = MARKER_REMOVE;
      rid = m->_rid;
      haveData = false;
      break;
    }
    case TRI_DOC_MARKER_KEY_DOCUMENT: {
      TRI_ASSERT(nullptr != document);
      auto m = reinterpret_cast<TRI_doc_document_key_marker_t const*>(marker);
      key = ((char*) m) + m->_offsetKey;
      type = MARKER_DOCUMENT;
      rid = m->_rid;
      break;
    }
    case TRI_DOC_MARKER_KEY_EDGE: {
      TRI_ASSERT(nullptr != document);
      auto m = reinterpret_cast<TRI_doc_document_key_marker_t const*>(marker);
      key = ((char*) m) + m->_offsetKey;
      type = MARKER_EDGE;
      rid = m->_rid;
      break;
    }
    case TRI_WAL_MARKER_REMOVE: {
      TRI_ASSERT(nullptr == document);
      auto m = static_cast<wal::remove_marker_t const*>(marker);
      key = ((char*) m) + sizeof(wal::remove_marker_t);
      type = MARKER_REMOVE;
      rid = m->_revisionId;
      haveData = false;
      isWal = true;
      break;
    }
    case TRI_WAL_MARKER_DOCUMENT: {
      TRI_ASSERT(nullptr == document);
      auto m = static_cast<wal::document_marker_t const*>(marker);
      key = ((char*) m) + m->_offsetKey;
      type = MARKER_DOCUMENT;
      rid = m->_revisionId;
      isWal = true;
      break;
    }
    case TRI_WAL_MARKER_EDGE: {
      TRI_ASSERT(nullptr == document);
      auto m = static_cast<wal::edge_marker_t const*>(marker);
      key = ((char*) m) + m->_offsetKey;
      type = MARKER_EDGE;
      rid = m->_revisionId;
      isWal = true;
      break;
    }
    default: {
      return false;
    }
  }
  
  if (withTicks) {
    APPEND_STRING(buffer, "{\"tick\":\"");
    APPEND_UINT64(buffer, (uint64_t) marker->_tick);
    APPEND_STRING(buffer, "\",\"type\":"); 
  }
  else {
    APPEND_STRING(buffer, "{\"type\":");
  }

  APPEND_UINT64(buffer, (uint64_t) type); 
  APPEND_STRING(buffer, ",\"key\":\""); 
  // key is user-defined, but does not need escaping
  APPEND_STRING(buffer, key); 
  APPEND_STRING(buffer, "\",\"rev\":\""); 
  APPEND_UINT64(buffer, (uint64_t) rid); 

  // document
  if (haveData) {
    APPEND_STRING(buffer, "\",\"data\":{");
    
    // common document meta-data
    APPEND_STRING(buffer, "\"" TRI_VOC_ATTRIBUTE_KEY "\":\"");
    APPEND_STRING(buffer, key);
    APPEND_STRING(buffer, "\",\"" TRI_VOC_ATTRIBUTE_REV "\":\"");
    APPEND_UINT64(buffer, (uint64_t) rid);
    APPEND_CHAR(buffer, '"');

    // Is it an edge marker?
    if (marker->_type == TRI_DOC_MARKER_KEY_EDGE ||
        marker->_type == TRI_WAL_MARKER_EDGE) {
      TRI_voc_key_t fromKey;
      TRI_voc_key_t toKey;
      TRI_voc_cid_t fromCid;
      TRI_voc_cid_t toCid;

      if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
        auto e = reinterpret_cast<TRI_doc_edge_key_marker_t const*>(marker);
        fromKey = ((char*) e) + e->_offsetFromKey;
        toKey = ((char*) e) + e->_offsetToKey;
        fromCid = e->_fromCid;
        toCid = e->_toCid;
      }
      else {  // TRI_WAL_MARKER_EDGE
        auto e = reinterpret_cast<wal::edge_marker_t const*>(marker);
        fromKey = ((char*) e) + e->_offsetFromKey;
        toKey = ((char*) e) + e->_offsetToKey;
        fromCid = e->_fromCid;
        toCid = e->_toCid;
      }

      APPEND_STRING(buffer, ",\"" TRI_VOC_ATTRIBUTE_FROM "\":\"");
      if (! AppendCollection(dump, fromCid, translateCollectionIds)) {
        return false;
      }
      APPEND_STRING(buffer, "\\/");
      APPEND_STRING(buffer, fromKey);
      APPEND_STRING(buffer, "\",\"" TRI_VOC_ATTRIBUTE_TO "\":\"");
      if (! AppendCollection(dump, toCid, translateCollectionIds)) {
        return false;
      }
      APPEND_STRING(buffer, "\\/");
      APPEND_STRING(buffer, toKey);
      APPEND_CHAR(buffer, '"');
    }

    // the actual document data
    TRI_shaped_json_t shaped;
    if (! isWal) {
      auto m = reinterpret_cast<TRI_doc_document_key_marker_t const*>(marker);
      TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, m);
      TRI_StringifyArrayShapedJson(document->getShaper(), buffer, &shaped, true); // ONLY IN DUMP, PROTECTED by fake trx above
    }
    else {   // isWal == true
      auto m = static_cast<wal::document_marker_t const*>(marker);
      TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, m);
      basics::LegendReader legendReader
                 (reinterpret_cast<char const*>(m) + m->_offsetLegend);
      TRI_StringifyArrayShapedJson(&legendReader, buffer, &shaped, true);
    }

    APPEND_STRING(buffer, "}}\n");
  }
  else {  // deletion marker, so no data
    APPEND_STRING(buffer, "\"}\n");
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief iterate over the attributes of a replication log marker (shaped json)
////////////////////////////////////////////////////////////////////////////////

static bool IterateShape (TRI_shaper_t* shaper,
                          TRI_shape_t const* shape,
                          char const* name,
                          char const* data,
                          uint64_t size,
                          void* ptr) {
  bool append   = false;
  bool withName = false;

  if (TRI_EqualString(name, "data")) {
    append   = true;
    withName = false;
  }
  else if (TRI_EqualString(name, "type") ||
           TRI_EqualString(name, "tid")) {
    append = true;
    withName = true;
  }

  if (append) {
    TRI_replication_dump_t* dump;
    TRI_string_buffer_t* buffer;
    int res;
  
    dump   = (TRI_replication_dump_t*) ptr;
    buffer = dump->_buffer;

    // append ,
    res = TRI_AppendCharStringBuffer(buffer, ',');
    
    if (res != TRI_ERROR_NO_ERROR) {
      dump->_failed = true;
      return false;
    }

    if (withName) {
      // append attribute name and value
      res = TRI_AppendCharStringBuffer(buffer, '"');

      if (res != TRI_ERROR_NO_ERROR) {
        dump->_failed = true;
        return false;
      }

      res = TRI_AppendStringStringBuffer(buffer, name);

      if (res != TRI_ERROR_NO_ERROR) {
        dump->_failed = true;
        return false;
      }

      res = TRI_AppendStringStringBuffer(buffer, "\":");

      if (shape->_type == TRI_SHAPE_NUMBER) {
        if (! TRI_StringifyJsonShapeData(shaper, buffer, shape, data, size)) {
          res = TRI_ERROR_OUT_OF_MEMORY;
        }
      }
      else if (shape->_type == TRI_SHAPE_SHORT_STRING ||
               shape->_type == TRI_SHAPE_LONG_STRING) {
        char* value;
        size_t length;

        res = TRI_AppendCharStringBuffer(buffer, '"');
      
        if (res != TRI_ERROR_NO_ERROR) {
          dump->_failed = true;
          return false;
        }

        TRI_StringValueShapedJson(shape, data, &value, &length);

        if (value != NULL && length > 0) {
          res = TRI_AppendString2StringBuffer(dump->_buffer, value, length);

          if (res != TRI_ERROR_NO_ERROR) {
            dump->_failed = true;
            return false;
          }
        }
    
        res = TRI_AppendCharStringBuffer(buffer, '"');
      }
    }
    else {
      // append raw value
      char* value;
      size_t length;

      TRI_StringValueShapedJson(shape, data, &value, &length);

      if (value != NULL && length > 2) {
        res = TRI_AppendString2StringBuffer(dump->_buffer, value + 1, length - 2);
      }
    }


    if (res != TRI_ERROR_NO_ERROR) {
      dump->_failed = true;
      return false;
    }
  }

  // continue iterating
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a raw marker from a datafile for a log dump
////////////////////////////////////////////////////////////////////////////////

static bool StringifyMarkerLog (TRI_replication_dump_t* dump,
                                TRI_document_collection_t* document,
                                TRI_df_marker_t const* marker) {

  TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
  TRI_shaper_t* shaper;
  TRI_shaped_json_t shaped;
  
  TRI_ASSERT(marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT);
  shaper = document->getShaper();  // ONLY IN DUMP, PROTECTED by a fake trx from above

  TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, m);

  if (shaped._sid != 0) {
    TRI_shape_t const* shape;

    if (shaped._sid != dump->_lastSid || dump->_lastShape == NULL) {
      shape = shaper->lookupShapeId(shaper, shaped._sid);
      dump->_lastSid   = shaped._sid;
      dump->_lastShape = shape;
    }
    else {
      shape            = dump->_lastShape;
    }
  
    APPEND_STRING(dump->_buffer, "{\"tick\":\"");
    APPEND_UINT64(dump->_buffer, (uint64_t) marker->_tick);
    APPEND_CHAR(dump->_buffer, '"');
    TRI_IterateShapeDataArray(shaper, shape, shaped._data.data, &IterateShape, dump); 
    APPEND_STRING(dump->_buffer, "}\n");
  }
  else {
    return false;
  }
  
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from a collection
////////////////////////////////////////////////////////////////////////////////

static int DumpCollection (TRI_replication_dump_t* dump, 
                           TRI_document_collection_t* document,
                           TRI_voc_tick_t dataMin,
                           TRI_voc_tick_t dataMax,
                           uint64_t chunkSize,
                           bool withTicks,
                           bool translateCollectionIds) {
  TRI_vector_t datafiles;
  TRI_string_buffer_t* buffer;
  TRI_voc_tick_t lastFoundTick;
  TRI_voc_tid_t lastTid;
  size_t i, n; 
  int res;
  bool hasMore;
  bool bufferFull;
  bool ignoreMarkers;
    
  // The following fake transaction allows us to access data pointers
  // and shapers, essentially disabling the runtime checks. This is OK,
  // since the dump only considers data files (and not WAL files), so 
  // the collector has no trouble. Also, the data files of the collection
  // are protected from the compactor by a barrier and the dump only goes
  // until a certain tick.
  triagens::arango::TransactionBase trx(true);

  LOG_TRACE("dumping collection %llu, tick range %llu - %llu, chunk size %llu", 
            (unsigned long long) document->_info._cid,
            (unsigned long long) dataMin,
            (unsigned long long) dataMax,
            (unsigned long long) chunkSize);

  buffer         = dump->_buffer;
  datafiles      = GetRangeDatafiles(document, dataMin, dataMax);
 
  // setup some iteration state
  lastFoundTick  = 0;
  lastTid        = 0;
  res            = TRI_ERROR_NO_ERROR;
  hasMore        = true;
  bufferFull     = false;
  ignoreMarkers  = false;

  n = datafiles._length;

  for (i = 0; i < n; ++i) {
    df_entry_t* e = (df_entry_t*) TRI_AtVector(&datafiles, i);
    TRI_datafile_t* datafile = e->_data;
    char const* ptr;
    char const* end;

    // we are reading from a journal that might be modified in parallel
    // so we must read-lock it
    if (e->_isJournal) {
      TRI_READ_LOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(document);
    }
    else {
      TRI_ASSERT(datafile->_isSealed);
    }
    
    ptr = datafile->_data;

    if (res == TRI_ERROR_NO_ERROR) { 
      // no error so far. start iterating
      end = ptr + datafile->_currentSize;
    }
    else {
      // some error occurred. don't iterate
      end = ptr;
    }

    while (ptr < end) {
      TRI_df_marker_t* marker = (TRI_df_marker_t*) ptr;
      TRI_voc_tick_t foundTick;
      TRI_voc_tid_t tid;

      if (marker->_size == 0 || marker->_type <= TRI_MARKER_MIN) {
        // end of datafile
        break;
      }
      
      ptr += TRI_DF_ALIGN_BLOCK(marker->_size);
      
      if (marker->_type == TRI_DF_MARKER_ATTRIBUTE ||
          marker->_type == TRI_DF_MARKER_SHAPE) {
        // fully ignore these marker types. they don't need to be replicated,
        // but we also cannot stop iteration if we find one of these
        continue;
      }
          
      // get the marker's tick and check whether we should include it
      foundTick = marker->_tick;
      
      if (foundTick <= dataMin) {
        // marker too old
        continue;
      }

      if (foundTick > dataMax) {
        // marker too new
        hasMore = false;
        goto NEXT_DF;
      }

      if (marker->_type != TRI_DOC_MARKER_KEY_DOCUMENT &&
          marker->_type != TRI_DOC_MARKER_KEY_EDGE &&
          marker->_type != TRI_DOC_MARKER_KEY_DELETION) {

        // found a non-data marker...

        // check if we can abort searching
        if (foundTick >= dataMax || 
            (foundTick >= e->_tickMax && i == (n - 1))) {
          // fetched the last available marker
          hasMore = false;
          goto NEXT_DF;
        }

        continue;
      }

      // note the last tick we processed
      lastFoundTick = foundTick;


      // handle aborted/unfinished transactions

      if (document->_failedTransactions == nullptr) {
        // there are no failed transactions
        ignoreMarkers = false;
      }
      else {
        // get transaction id of marker
        if (marker->_type == TRI_DOC_MARKER_KEY_DELETION) {
          tid = ((TRI_doc_deletion_key_marker_t const*) marker)->_tid;
        }
        else {
          tid = ((TRI_doc_document_key_marker_t const*) marker)->_tid;
        }

        // check if marker is from an aborted transaction
        if (tid > 0) {
          if (tid != lastTid) {
            ignoreMarkers = (document->_failedTransactions->find(tid) != document->_failedTransactions->end());
          }

          lastTid = tid;
        } 

        if (ignoreMarkers) {
          continue;
        }
      }


      if (! StringifyMarkerDump(dump, document, marker, withTicks, translateCollectionIds)) {
        res = TRI_ERROR_INTERNAL;

        goto NEXT_DF;
      }
      
      if (foundTick >= dataMax || 
          (foundTick >= e->_tickMax && i == (n - 1))) {
        // fetched the last available marker
        hasMore = false;
        goto NEXT_DF;
      }

      if ((uint64_t) TRI_LengthStringBuffer(buffer) > chunkSize) {
        // abort the iteration
        bufferFull = true;

        goto NEXT_DF;
      }
    }

NEXT_DF:
    if (e->_isJournal) {
      // read-unlock the journal
      TRI_READ_UNLOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(document);
    }

    if (res != TRI_ERROR_NO_ERROR || ! hasMore || bufferFull) {
      break;
    }
  }
  
  TRI_DestroyVector(&datafiles);

  if (res == TRI_ERROR_NO_ERROR) {
    if (lastFoundTick > 0) {
      // data available for requested range
      dump->_lastFoundTick = lastFoundTick;
      dump->_hasMore       = hasMore;
      dump->_bufferFull    = bufferFull;
    }
    else {
      // no data available for requested range
      dump->_lastFoundTick = 0;
      dump->_hasMore       = false;
      dump->_bufferFull    = false;
    }
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from the replication log
////////////////////////////////////////////////////////////////////////////////

static int DumpLog (TRI_replication_dump_t* dump, 
                    TRI_document_collection_t* document,
                    TRI_voc_tick_t dataMin,
                    TRI_voc_tick_t dataMax,
                    uint64_t chunkSize) {
  TRI_vector_t datafiles;
  TRI_string_buffer_t* buffer;
  TRI_voc_tick_t lastFoundTick;
  size_t i, n; 
  int res;
  bool hasMore;
  bool bufferFull;
    
  LOG_TRACE("dumping collection %llu, tick range %llu - %llu, chunk size %llu", 
            (unsigned long long) document->_info._cid,
            (unsigned long long) dataMin,
            (unsigned long long) dataMax,
            (unsigned long long) chunkSize);

  buffer         = dump->_buffer;
  datafiles      = GetRangeDatafiles(document, dataMin, dataMax);
 
  // setup some iteration state
  lastFoundTick  = 0;
  res            = TRI_ERROR_NO_ERROR;
  hasMore        = true;
  bufferFull     = false;

  n = datafiles._length;

  for (i = 0; i < n; ++i) {
    df_entry_t* e = (df_entry_t*) TRI_AtVector(&datafiles, i);
    TRI_datafile_t* datafile = e->_data;
    char const* ptr;
    char const* end;

    // we are reading from a journal that might be modified in parallel
    // so we must read-lock it
    if (e->_isJournal) {
      TRI_READ_LOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(document);
    }
    else {
      TRI_ASSERT(datafile->_isSealed);
    }
    
    ptr = datafile->_data;
    end = ptr + datafile->_currentSize;

    while (ptr < end) {
      TRI_df_marker_t* marker = (TRI_df_marker_t*) ptr;
      TRI_voc_tick_t foundTick;

      if (marker->_size == 0 || marker->_type <= TRI_MARKER_MIN) {
        // end of datafile
        break;
      }
      
      ptr += TRI_DF_ALIGN_BLOCK(marker->_size);
      
      // get the marker's tick and check whether we should include it
      foundTick = marker->_tick;
      
      if (foundTick <= dataMin) {
        // marker too old
        continue;
      }
      
      if (foundTick > dataMax) {
        // marker too new
        hasMore = false;
        goto NEXT_DF;
      }
          
      if (marker->_type != TRI_DOC_MARKER_KEY_DOCUMENT) {
        // we're only interested in document markers here
        // the replication collection does not contain any edge markers
        // and deletion markers in the replication collection
        // will not be replicated

        // check if we can abort searching
        if (foundTick >= dataMax || 
            (foundTick >= e->_tickMax && i == (n - 1))) {
          // fetched the last available marker
          hasMore = false;
          goto NEXT_DF;
        }

        continue;
      }

      // note the last tick we processed
      lastFoundTick = foundTick;

      if (! StringifyMarkerLog(dump, document, marker)) {
        res = TRI_ERROR_INTERNAL;

        goto NEXT_DF;
      }

      if (foundTick >= dataMax || 
          (foundTick >= e->_dataMax && i == (n - 1))) {
        // fetched the last available marker
        hasMore = false;
        goto NEXT_DF;
      }

      if ((uint64_t) TRI_LengthStringBuffer(buffer) > chunkSize) {
        // abort the iteration
        bufferFull = true;

        goto NEXT_DF;
      }
    }

NEXT_DF:
    if (e->_isJournal) {
      // read-unlock the journal
      TRI_READ_UNLOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(document);
    }

    if (res != TRI_ERROR_NO_ERROR || ! hasMore || bufferFull) {
      break;
    }
  }
  
  TRI_DestroyVector(&datafiles);

  if (res == TRI_ERROR_NO_ERROR) {
    if (lastFoundTick > 0) {
      // data available for requested range
      dump->_lastFoundTick = lastFoundTick;
      dump->_hasMore       = hasMore;
      dump->_bufferFull    = bufferFull;
    }
    else {
      // no data available for requested range
      dump->_lastFoundTick = 0;
      dump->_hasMore       = false;
      dump->_bufferFull    = false;
    }
  }

  return res;
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
/// @brief dump data from a collection
////////////////////////////////////////////////////////////////////////////////

int TRI_DumpCollectionReplication (TRI_replication_dump_t* dump,
                                   TRI_vocbase_col_t* col,
                                   TRI_voc_tick_t dataMin,
                                   TRI_voc_tick_t dataMax,
                                   uint64_t chunkSize,
                                   bool withTicks,
                                   bool translateCollectionIds) {
  TRI_barrier_t* b;
  int res;

  TRI_ASSERT(col != nullptr);
  TRI_ASSERT(col->_collection != nullptr);

  TRI_document_collection_t* document = col->_collection;

  // create a barrier so the underlying collection is not unloaded
  b = TRI_CreateBarrierReplication(&document->_barrierList);

  if (b == nullptr) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  // block compaction
  TRI_ReadLockReadWriteLock(&document->_compactionLock);

  res = DumpCollection(dump, document, dataMin, dataMax, chunkSize, withTicks, translateCollectionIds);
  
  TRI_ReadUnlockReadWriteLock(&document->_compactionLock);

  TRI_FreeBarrier(b);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from the replication log
////////////////////////////////////////////////////////////////////////////////

int TRI_DumpLogReplication (TRI_vocbase_t* vocbase,
                            TRI_replication_dump_t* dump,
                            TRI_voc_tick_t dataMin,
                            TRI_voc_tick_t dataMax,
                            uint64_t chunkSize) {
  TRI_vocbase_col_t* col;
  TRI_barrier_t* b;
  int res;
 
  TRI_vocbase_col_status_e status;
  col = TRI_UseCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION, status);

  if (col == NULL || col->_collection == NULL) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  TRI_document_collection_t* document = col->_collection;

  // create a barrier so the underlying collection is not unloaded
  b = TRI_CreateBarrierReplication(&document->_barrierList);

  if (b == NULL) {
    TRI_ReleaseCollectionVocBase(vocbase, col);

    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  // block compaction
  TRI_ReadLockReadWriteLock(&document->_compactionLock);

  res = DumpLog(dump, document, dataMin, dataMax, chunkSize);
  
  TRI_ReadUnlockReadWriteLock(&document->_compactionLock);

  TRI_FreeBarrier(b);
  
  TRI_ReleaseCollectionVocBase(vocbase, col);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a replication dump container
////////////////////////////////////////////////////////////////////////////////

int TRI_InitDumpReplication (TRI_replication_dump_t* dump,
                             TRI_vocbase_t* vocbase,
                             size_t bufferSize) {
  int res;

  TRI_ASSERT(vocbase != NULL);

  dump->_vocbase       = vocbase;
  dump->_lastFoundTick = 0;
  dump->_lastSid       = 0;
  dump->_lastShape     = NULL;
  dump->_failed        = false;
  dump->_bufferFull    = false;
  dump->_hasMore       = false;

  dump->_buffer = TRI_CreateSizedStringBuffer(TRI_CORE_MEM_ZONE, bufferSize);

  if (dump->_buffer == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = TRI_InitAssociativePointer(&dump->_collectionNames,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   HashKeyCid,
                                   HashElementCid,
                                   IsEqualKeyElementCid,
                                   NULL);
 
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeStringBuffer(TRI_CORE_MEM_ZONE, dump->_buffer);
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a replication dump container
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyDumpReplication (TRI_replication_dump_t* dump) {
  for (size_t i = 0; i < dump->_collectionNames._nrAlloc; ++i) {
    resolved_name_t* found = static_cast<resolved_name_t*>(dump->_collectionNames._table[i]);

    if (found != NULL) {
      if (found->_name != NULL) {
        // name can be NULL
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, found->_name);
      }
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, found);
    }
  }

  TRI_DestroyAssociativePointer(&dump->_collectionNames);
  TRI_FreeStringBuffer(TRI_CORE_MEM_ZONE, dump->_buffer);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
