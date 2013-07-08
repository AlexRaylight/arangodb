////////////////////////////////////////////////////////////////////////////////
/// @brief replication functions
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

#include "replication.h"

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


#ifdef TRI_ENABLE_REPLICATION

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
/// @brief replication operations
////////////////////////////////////////////////////////////////////////////////

#define OPERATION_REPLICATION_STOP   "replication-stop"

////////////////////////////////////////////////////////////////////////////////
/// @brief collection operations
////////////////////////////////////////////////////////////////////////////////

#define OPERATION_COLLECTION_CREATE  "collection-create"
#define OPERATION_COLLECTION_DROP    "collection-drop"
#define OPERATION_COLLECTION_RENAME  "collection-rename"
#define OPERATION_COLLECTION_CHANGE  "collection-change"

////////////////////////////////////////////////////////////////////////////////
/// @brief index operations
////////////////////////////////////////////////////////////////////////////////

#define OPERATION_INDEX_CREATE       "index-create"
#define OPERATION_INDEX_DROP         "index-drop"

////////////////////////////////////////////////////////////////////////////////
/// @brief transaction control operations
////////////////////////////////////////////////////////////////////////////////

#define OPERATION_TRANSACTION_START  "transaction-start"
#define OPERATION_TRANSACTION_COMMIT "transaction-commit"

////////////////////////////////////////////////////////////////////////////////
/// @brief document operations
////////////////////////////////////////////////////////////////////////////////

#define OPERATION_DOCUMENT_INSERT    "document-insert"
#define OPERATION_DOCUMENT_UPDATE    "document-update"
#define OPERATION_DOCUMENT_REMOVE    "document-remove"

////////////////////////////////////////////////////////////////////////////////
/// @brief marker types 
////////////////////////////////////////////////////////////////////////////////

#define OPERATION_MARKER_DOCUMENT    "marker-document"
#define OPERATION_MARKER_EDGE        "marker-edge" 
#define OPERATION_MARKER_DELETE      "marker-deletion"

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
  
typedef struct {
  TRI_datafile_t* _data;
  TRI_voc_tick_t  _tickMin;
  TRI_voc_tick_t  _tickMax;
  bool            _isJournal;
}
df_entry_t;

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
/// @brief get the datafiles of a collection for a specific tick range
////////////////////////////////////////////////////////////////////////////////

static TRI_vector_t GetRangeDatafiles (TRI_primary_collection_t* primary,
                                       TRI_voc_tick_t tickMin,
                                       TRI_voc_tick_t tickMax) {
  TRI_vector_t datafiles;
  size_t i;

  LOG_TRACE("getting datafiles in tick range %llu - %llu", 
            (unsigned long long) tickMin, 
            (unsigned long long) tickMax);

  // determine the datafiles of the collection
  TRI_InitVector(&datafiles, TRI_CORE_MEM_ZONE, sizeof(df_entry_t));

  TRI_READ_LOCK_DATAFILES_DOC_COLLECTION(primary);

  for (i = 0; i < primary->base._datafiles._length; ++i) {
    TRI_datafile_t* df = TRI_AtVectorPointer(&primary->base._datafiles, i);

    df_entry_t entry = { 
      df,
      df->_tickMin,
      df->_tickMax,
      false 
    };
  
    LOG_TRACE("checking datafile with tick range %llu - %llu", 
              (unsigned long long) df->_tickMin, 
              (unsigned long long) df->_tickMax);
    
    if (tickMax < df->_tickMin) {
      // datafile is newer than requested range
      continue;
    }

    if (tickMin > df->_tickMax) {
      // datafile is older than requested range
      continue;
    }
     
    TRI_PushBackVector(&datafiles, &entry);
  }

  for (i = 0; i < primary->base._journals._length; ++i) {
    TRI_datafile_t* df = TRI_AtVectorPointer(&primary->base._journals, i);

    df_entry_t entry = { 
      df,
      df->_tickMin,
      df->_tickMax,
      true 
    };
    
    LOG_TRACE("checking journal with tick range %llu - %llu", 
              (unsigned long long) df->_tickMin, 
              (unsigned long long) df->_tickMax);
    
    if (tickMax < df->_tickMin) {
      // datafile is newer than requested range
      continue;
    }

    if (tickMin > df->_tickMax) {
      // datafile is older than requested range
      continue;
    }
     
    TRI_PushBackVector(&datafiles, &entry);
  }

  TRI_READ_UNLOCK_DATAFILES_DOC_COLLECTION(primary);

  return datafiles;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief translate a document operation
////////////////////////////////////////////////////////////////////////////////

static const char* TranslateDocumentOperation (TRI_voc_document_operation_e type) {
  if (type == TRI_VOC_DOCUMENT_OPERATION_INSERT) {
    return OPERATION_DOCUMENT_INSERT;
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_UPDATE) {
    return OPERATION_DOCUMENT_UPDATE;
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_REMOVE) {
    return OPERATION_DOCUMENT_REMOVE;
  }
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a buffer to write an event in
/// TODO: some optimisations can go here so that we do not create new buffers
/// over and over...
////////////////////////////////////////////////////////////////////////////////

static TRI_string_buffer_t* GetBuffer (TRI_replication_logger_t* logger) {
  TRI_string_buffer_t* buffer;

  assert(logger != NULL);
 
  buffer = TRI_CreateStringBuffer(TRI_CORE_MEM_ZONE);
  return buffer;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a buffer
/// TODO: some optimisations can go here so that we do not dispose unused 
/// buffers but recycle them
////////////////////////////////////////////////////////////////////////////////

static void ReturnBuffer (TRI_replication_logger_t* logger,
                          TRI_string_buffer_t* buffer) {
  assert(logger != NULL);

  if (buffer == NULL) {
    return;
  }

  TRI_FreeStringBuffer(TRI_CORE_MEM_ZONE, buffer);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief logs a replication event contained in the buffer
////////////////////////////////////////////////////////////////////////////////

static int LogEvent (TRI_replication_logger_t* logger,
                     TRI_voc_tid_t tid,
                     bool lock,
                     const char* eventName,
                     TRI_string_buffer_t* buffer) {
  TRI_primary_collection_t* primary;
  TRI_shaped_json_t* shaped;
  TRI_json_t* json;
  TRI_doc_mptr_t mptr;
  size_t len;
  int res;

  assert(logger != NULL);
  assert(buffer != NULL);

  len = TRI_LengthStringBuffer(buffer);

  if (len < 1) {
    // buffer is empty
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_NO_ERROR;
  }
 
  // printf("REPLICATION: %s: tid: %llu, %s\n", eventName, (unsigned long long) tid, buffer->_buffer);

  // TODO: instead of using JSON here, we could directly use ShapedJson.
  // this will be a performance optimisation
  json = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (json == NULL) {
    // should not happen in CORE_MEM_ZONE, but you never know
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, eventName));
  if (tid == 0) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "tid", TRI_CreateString2CopyJson(TRI_CORE_MEM_ZONE, "0", 1));
  }
  else {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "tid", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, TRI_StringUInt64(tid)));
  }

  // pass the string-buffer buffer pointer to the JSON
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "data", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, buffer->_buffer));
  
  // this will make sure we won't double-free the buffer
  buffer->_buffer = NULL;

  primary = logger->_trxCollection->_collection->_collection;
  shaped = TRI_ShapedJsonJson(primary->_shaper, json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  if (shaped == NULL) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_ARANGO_SHAPER_FAILED;
  }

  res = primary->insert(logger->_trxCollection, 
                        NULL, 
                        0,
                        &mptr, 
                        TRI_DOC_MARKER_KEY_DOCUMENT, 
                        shaped, 
                        NULL, 
                        lock, 
                        false);

  TRI_FreeShapedJson(primary->_shaper, shaped);
  ReturnBuffer(logger, buffer);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }
  
  // note the last id that we've logged
  TRI_LockSpin(&logger->_idLock);
  logger->_state._lastTick = (TRI_voc_tick_t) mptr._rid;
  TRI_UnlockSpin(&logger->_idLock);
  

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify the id of a transaction
////////////////////////////////////////////////////////////////////////////////

static bool StringifyIdTransaction (TRI_string_buffer_t* buffer,
                                    const TRI_voc_tid_t tid) {
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "\"tid\":\"");
  APPEND_UINT64(buffer, (uint64_t) tid);
  APPEND_CHAR(buffer, '"');

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify an index context
////////////////////////////////////////////////////////////////////////////////

static bool StringifyIndex (TRI_string_buffer_t* buffer,
                            const TRI_idx_iid_t iid) {
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "\"index\":{\"id\":\"");
  APPEND_UINT64(buffer, (uint64_t) iid);
  APPEND_STRING(buffer, "\"}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a collection context 
////////////////////////////////////////////////////////////////////////////////

static bool StringifyCollection (TRI_string_buffer_t* buffer,
                                 const TRI_voc_cid_t cid) {
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "\"cid\":\"");
  APPEND_UINT64(buffer, (uint64_t) cid);
  APPEND_CHAR(buffer, '"');

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "stop replication" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyStopReplication (TRI_string_buffer_t* buffer,
                                      TRI_voc_tick_t id) {
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "{\"lastId\":\"");
  APPEND_UINT64(buffer, (uint64_t) id);
  APPEND_STRING(buffer, "\"}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "create collection" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyCreateCollection (TRI_string_buffer_t* buffer,
                                       TRI_json_t const* json) {
  APPEND_STRING(buffer, "{\"collection\":");
  APPEND_JSON(buffer, json);
  APPEND_CHAR(buffer, '}');

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "drop collection" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyDropCollection (TRI_string_buffer_t* buffer,
                                     TRI_voc_cid_t cid) {
  APPEND_CHAR(buffer, '{');

  if (! StringifyCollection(buffer, cid)) {
    return false;
  }
  
  APPEND_CHAR(buffer, '}');

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "rename collection" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyRenameCollection (TRI_string_buffer_t* buffer,
                                       TRI_voc_cid_t cid,
                                       char const* name) {
  
  APPEND_CHAR(buffer, '{');

  if (! StringifyCollection(buffer, cid)) {
    return false;
  }

  APPEND_STRING(buffer, ",\"name\":\"");
  // name is user-defined, but does not need escaping as collection names are "safe"
  APPEND_STRING(buffer, name);
  APPEND_STRING(buffer, "\"}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "create index" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyCreateIndex (TRI_string_buffer_t* buffer,
                                  TRI_voc_cid_t cid,
                                  TRI_json_t const* json) {
  APPEND_CHAR(buffer, '{');
  
  if (! StringifyCollection(buffer, cid)) {
    return false;
  }

  APPEND_STRING(buffer, ",\"index\":");
  APPEND_JSON(buffer, json);
  APPEND_CHAR(buffer, '}'); 

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "drop index" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyDropIndex (TRI_string_buffer_t* buffer,
                                TRI_voc_cid_t cid,
                                TRI_idx_iid_t iid) {
  APPEND_CHAR(buffer, '{');
  
  if (! StringifyCollection(buffer, cid)) {
    return false;
  }
  
  APPEND_CHAR(buffer, ','); 

  if (! StringifyIndex(buffer, iid)) {
    return false;
  }
  
  APPEND_CHAR(buffer, '}'); 

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a document operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyDocumentOperation (TRI_string_buffer_t* buffer,
                                        TRI_document_collection_t* document,
                                        TRI_voc_document_operation_e type,
                                        TRI_df_marker_t const* marker,
                                        TRI_doc_mptr_t const* oldHeader,
                                        bool withCid) {
  TRI_voc_key_t key;
  TRI_voc_rid_t oldRev;
  TRI_voc_rid_t rid;

  if (! TRI_ReserveStringBuffer(buffer, 256)) {
    return false;
  }
  
  if (type == TRI_VOC_DOCUMENT_OPERATION_INSERT) {
    oldRev = 0;
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_UPDATE) {
    oldRev = 0;
    if (oldHeader != NULL) {
      oldRev = oldHeader->_rid;
    }
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_REMOVE) {
    oldRev = 0;
    if (oldHeader != NULL) {
      oldRev = oldHeader->_rid;
    }
  }
  else {
    return false;
  }
  
  APPEND_CHAR(buffer, '{');
  
  if (withCid) {
    if (! StringifyCollection(buffer, document->base.base._info._cid)) {
      return false;
    }
    APPEND_CHAR(buffer, ',');
  }
  
  if (marker->_type == TRI_DOC_MARKER_KEY_DELETION) {
    TRI_doc_deletion_key_marker_t const* m = (TRI_doc_deletion_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
    rid = m->_rid;
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
    rid = m->_rid;
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
    rid = m->_rid;
  }
  else {
    return false;
  }

  APPEND_STRING(buffer, "\"key\":\""); 
  // key is user-defined, but does not need escaping
  APPEND_STRING(buffer, key); 

  if (oldRev > 0) {
    APPEND_STRING(buffer, "\",\"oldRev\":\""); 
    APPEND_UINT64(buffer, (uint64_t) oldRev); 
  }

  // document
  if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT ||
      marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    TRI_shaped_json_t shaped;
    
    APPEND_STRING(buffer, "\",\"doc\":{");
    
    // common document meta-data
    APPEND_STRING(buffer, "\"" TRI_VOC_ATTRIBUTE_KEY "\":\"");
    APPEND_STRING(buffer, key);
    APPEND_STRING(buffer, "\",\"" TRI_VOC_ATTRIBUTE_REV "\":\"");
    APPEND_UINT64(buffer, (uint64_t) rid);
    APPEND_CHAR(buffer, '"');

    if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
      TRI_doc_edge_key_marker_t const* e = (TRI_doc_edge_key_marker_t const*) marker;
      TRI_voc_key_t fromKey = ((char*) e) + e->_offsetFromKey;
      TRI_voc_key_t toKey = ((char*) e) + e->_offsetToKey;

      APPEND_STRING(buffer, ",\"" TRI_VOC_ATTRIBUTE_FROM "\":\"");
      APPEND_UINT64(buffer, (uint64_t) e->_fromCid);
      APPEND_CHAR(buffer, '/');
      APPEND_STRING(buffer, fromKey);
      APPEND_STRING(buffer, "\",\"" TRI_VOC_ATTRIBUTE_TO "\":\"");
      APPEND_UINT64(buffer, (uint64_t) e->_toCid);
      APPEND_CHAR(buffer, '/');
      APPEND_STRING(buffer, toKey);
      APPEND_CHAR(buffer, '"');
    }

    // the actual document data
    TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, m);
    TRI_StringifyArrayShapedJson(document->base._shaper, buffer, &shaped, true);

    APPEND_STRING(buffer, "}}");
  }
  else {
    APPEND_STRING(buffer, "\"}");
  }
  
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify meta data about a transaction operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyMetaTransaction (TRI_string_buffer_t* buffer,
                                      TRI_transaction_t const* trx) {
  size_t i, n;
  bool printed;

  APPEND_CHAR(buffer, '{');
   
  if (! StringifyIdTransaction(buffer, trx->_id)) {
    return false;
  }

  APPEND_STRING(buffer, ",\"collections\":[");

  printed = false;
  n = trx->_collections._length;

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* trxCollection;
    TRI_document_collection_t* document;

    trxCollection = TRI_AtVectorPointer(&trx->_collections, i);

    if (trxCollection->_operations == NULL) {
      // no markers available for collection
      continue;
    }
    
    document = (TRI_document_collection_t*) trxCollection->_collection->_collection;
      
    if (printed) {
      APPEND_CHAR(buffer, ',');
    }
    else {
      printed = true;
    }
  
    APPEND_STRING(buffer, "{\"cid\":\"");
    APPEND_UINT64(buffer, (uint64_t) document->base.base._info._cid);
    APPEND_STRING(buffer, "\",\"operations\":");
    APPEND_UINT64(buffer, (uint64_t) trxCollection->_operations->_length);
    APPEND_CHAR(buffer, '}');
  }
  APPEND_STRING(buffer, "]}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a raw marker from a datafile
////////////////////////////////////////////////////////////////////////////////

static bool StringifyMarkerReplication (TRI_string_buffer_t* buffer,
                                        TRI_document_collection_t* document,
                                        TRI_df_marker_t const* marker) {
  const char* typeName;
  TRI_voc_key_t key;
  TRI_voc_rid_t rid;

  APPEND_CHAR(buffer, '{');
  
  if (marker->_type == TRI_DOC_MARKER_KEY_DELETION) {
    TRI_doc_deletion_key_marker_t const* m = (TRI_doc_deletion_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
    typeName = OPERATION_MARKER_DELETE;
    rid = m->_rid;
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
    typeName = OPERATION_MARKER_DOCUMENT;
    rid = m->_rid;
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
    typeName = OPERATION_MARKER_EDGE;
    rid = m->_rid;
  }
  else {
    return false;
  }

  APPEND_STRING(buffer, "\"type\":\""); 
  APPEND_STRING(buffer, typeName); 
  APPEND_STRING(buffer, "\",\"key\":\""); 
  // key is user-defined, but does not need escaping
  APPEND_STRING(buffer, key); 

  // document
  if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT ||
      marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    TRI_shaped_json_t shaped;
    
    APPEND_STRING(buffer, "\",\"doc\":{");
    
    // common document meta-data
    APPEND_STRING(buffer, "\"" TRI_VOC_ATTRIBUTE_KEY "\":\"");
    APPEND_STRING(buffer, key);
    APPEND_STRING(buffer, "\",\"" TRI_VOC_ATTRIBUTE_REV "\":\"");
    APPEND_UINT64(buffer, (uint64_t) rid);
    APPEND_CHAR(buffer, '"');

    if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
      TRI_doc_edge_key_marker_t const* e = (TRI_doc_edge_key_marker_t const*) marker;
      TRI_voc_key_t fromKey = ((char*) e) + e->_offsetFromKey;
      TRI_voc_key_t toKey = ((char*) e) + e->_offsetToKey;

      APPEND_STRING(buffer, ",\"" TRI_VOC_ATTRIBUTE_FROM "\":\"");
      APPEND_UINT64(buffer, (uint64_t) e->_fromCid);
      APPEND_CHAR(buffer, '/');
      APPEND_STRING(buffer, fromKey);
      APPEND_STRING(buffer, "\",\"" TRI_VOC_ATTRIBUTE_TO "\":\"");
      APPEND_UINT64(buffer, (uint64_t) e->_toCid);
      APPEND_CHAR(buffer, '/');
      APPEND_STRING(buffer, toKey);
      APPEND_CHAR(buffer, '"');
    }

    // the actual document data
    TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, m);
    TRI_StringifyArrayShapedJson(document->base._shaper, buffer, &shaped, true);

    APPEND_STRING(buffer, "}}\n");
  }
  else {
    APPEND_STRING(buffer, "\"}\n");
  }
  
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief check if a transaction id is contained in the list of failed
/// transactions
////////////////////////////////////////////////////////////////////////////////

static bool InFailedList (TRI_vector_t const* list, TRI_voc_tid_t search) {
  size_t n;

  assert(list != NULL);

  n = list->_length;
 
  // decide how to search based on size of list
  if (n == 0) {
    // simple case: list is empty
    return false;
  }

  else if (n < 16) {
    // list is small: use a linear search
    size_t i;

    for (i = 0; i < n; ++i) {
      TRI_voc_tid_t* tid = tid = TRI_AtVector(list, i);

      if (*tid == search) {
        return true;
      }
    }

    return false;
  }

  else {
    // list is somewhat bigger, use a binary search
    size_t l = 0;
    size_t r = (size_t) (n - 1);

    while (true) {
      // determine midpoint
      TRI_voc_tid_t* tid;
      size_t m;

      m = l + ((r - l) / 2);
      tid = TRI_AtVector(list, m);

      if (*tid == search) {
        return true;
      }

      if (*tid > search) {
        if (m == 0) {
          // we must abort because the following subtraction would
          // make the size_t underflow
          return false;
        }
        
        r = m - 1;
      }
      else {
        l = m + 1;
      }

      if (r < l) {
        return false;
      }
    }
  }

  // we should never get here
  assert(false);
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from a collection
////////////////////////////////////////////////////////////////////////////////

static int DumpCollection (TRI_replication_dump_t* dump, 
                           TRI_primary_collection_t* primary,
                           TRI_voc_tick_t tickMin,
                           TRI_voc_tick_t tickMax,
                           uint64_t chunkSize) {
  TRI_vector_t datafiles;
  TRI_document_collection_t* document;
  TRI_string_buffer_t* buffer;
  TRI_voc_tick_t lastFoundTick;
  TRI_voc_tid_t lastTid;
  size_t i; 
  int res;
  bool hasMore;
  bool bufferFull;
  bool ignoreMarkers;
    
  LOG_TRACE("dumping collection %llu, tick range %llu - %llu, chunk size %llu", 
            (unsigned long long) primary->base._info._cid,
            (unsigned long long) tickMin,
            (unsigned long long) tickMax,
            (unsigned long long) chunkSize);

  buffer         = dump->_buffer;
  datafiles      = GetRangeDatafiles(primary, tickMin, tickMax);
  document       = (TRI_document_collection_t*) primary;
 
  // setup some iteration state
  lastFoundTick  = 0;
  lastTid        = 0;
  res            = TRI_ERROR_NO_ERROR;
  hasMore        = true;
  bufferFull     = false;
  ignoreMarkers  = false;

  for (i = 0; i < datafiles._length; ++i) {
    df_entry_t* e = (df_entry_t*) TRI_AtVector(&datafiles, i);
    TRI_datafile_t* datafile = e->_data;
    TRI_vector_t* failedList;
    char const* ptr;
    char const* end;

    failedList    = NULL;

    // we are reading from a journal that might be modified in parallel
    // so we must read-lock it
    if (e->_isJournal) {
      TRI_READ_LOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(primary);

      if (document->_failedTransactions._length > 0) {
        // there are failed transactions. just reference them
        failedList = &document->_failedTransactions;
      }
    }
    else {
      assert(datafile->_isSealed);

      TRI_READ_LOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(primary);
      
      if (document->_failedTransactions._length > 0) {
        // there are failed transactions. copy the list of ids
        failedList = TRI_CopyVector(TRI_UNKNOWN_MEM_ZONE, &document->_failedTransactions);

        if (failedList == NULL) {
          res = TRI_ERROR_OUT_OF_MEMORY;
        }
      }

      TRI_READ_UNLOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(primary);
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
      TRI_voc_tid_t  tid;

      if (marker->_size == 0 || marker->_type <= TRI_MARKER_MIN) {
        // end of datafile
        break;
      }
      
      ptr += TRI_DF_ALIGN_BLOCK(marker->_size);
          
      if (marker->_type != TRI_DOC_MARKER_KEY_DOCUMENT &&
          marker->_type != TRI_DOC_MARKER_KEY_EDGE &&
          marker->_type != TRI_DOC_MARKER_KEY_DELETION) {
        continue;
      }

      // get the marker's tick and check whether we should include it
      foundTick = marker->_tick;
      
      if (foundTick <= tickMin) {
        // marker too old
        continue;
      }

      if (foundTick > tickMax) {
        // marker too new
        hasMore = false;
        goto NEXT_DF;
      }

      // note the last tick we processed
      lastFoundTick = foundTick;


      // handle aborted/unfinished transactions

      if (failedList == NULL) {
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
            ignoreMarkers = InFailedList(failedList, tid);
          }

          lastTid = tid;
        } 

        if (ignoreMarkers) {
          continue;
        }
      }

      if (! StringifyMarkerReplication(buffer, document, marker)) {
        res = TRI_ERROR_INTERNAL;

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
      TRI_READ_UNLOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(primary);
    }
    else {
      // free our copy of the failed list
      if (failedList != NULL) {
        TRI_FreeVector(TRI_UNKNOWN_MEM_ZONE, failedList);
      }
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
                    TRI_primary_collection_t* primary,
                    TRI_voc_tick_t tickMin,
                    TRI_voc_tick_t tickMax,
                    uint64_t chunkSize) {
  TRI_document_collection_t* document;
  TRI_doc_mptr_t* mptr;
  TRI_string_buffer_t* buffer;
  int res;
    
  LOG_TRACE("dumping log %llu, tick range %llu - %llu, chunk size %llu", 
            (unsigned long long) primary->base._info._cid,
            (unsigned long long) tickMin,
            (unsigned long long) tickMax,
            (unsigned long long) chunkSize);

  // setup some iteration state
  buffer              = dump->_buffer;
  dump->_bufferFull   = false;
  dump->_hasMore      = false;
  dump->_lastFoundTick= 0;
  
  res                 = TRI_ERROR_NO_ERROR;
  document            = (TRI_document_collection_t*) primary;
  
  TRI_READ_LOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(primary);

  mptr = document->_headers->front(document->_headers);

  while (mptr != NULL) {
    TRI_df_marker_t* marker;
    TRI_voc_tick_t foundTick;
    
    marker  = (TRI_df_marker_t*) mptr->_data;
    
    // get the marker's tick and check whether we should include it
    foundTick = marker->_tick;
      
    // note the last tick we processed
    dump->_lastFoundTick = foundTick;

    if (foundTick > tickMax) {
      // marker too new
      dump->_hasMore = false;
      break;
    }
    
    if (foundTick > tickMin) {
      // marker should be included
      if (! StringifyMarkerReplication(buffer, document, marker)) {
        res = TRI_ERROR_INTERNAL;
        break;
      }

      if ((uint64_t) TRI_LengthStringBuffer(buffer) > chunkSize) {
        // abort the iteration
        dump->_bufferFull = true;
        break;
      }
    }

    mptr = mptr->_next;
  }

  TRI_READ_UNLOCK_DOCUMENTS_INDEXES_PRIMARY_COLLECTION(primary);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get current state from the replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int GetStateReplicationLogger (TRI_replication_logger_t* logger,
                                      TRI_replication_log_state_t* dst) {
  assert(logger->_state._active);

  TRI_LockSpin(&logger->_idLock);
  memcpy(dst, &logger->_state, sizeof(TRI_replication_log_state_t));
  TRI_UnlockSpin(&logger->_idLock);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int StartReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_transaction_t* trx;
  TRI_vocbase_col_t* collection;
  TRI_vocbase_t* vocbase;
  TRI_voc_cid_t cid;
  int res;
  
  if (logger->_state._active) {
    return TRI_ERROR_INTERNAL;
  }

  assert(logger->_trx == NULL);
  assert(logger->_trxCollection == NULL);
  assert(logger->_state._lastTick == 0);

  vocbase = logger->_vocbase;
  collection = TRI_LookupCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION);

  if (collection == NULL) {
    LOG_ERROR("could not open collection '" TRI_COL_NAME_REPLICATION "'");

    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  cid = collection->_cid;

  trx = TRI_CreateTransaction(vocbase->_transactionContext, false, 0.0, false);

  if (trx == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = TRI_AddCollectionTransaction(trx, cid, TRI_TRANSACTION_WRITE, TRI_TRANSACTION_TOP_LEVEL);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeTransaction(trx);

    return TRI_ERROR_INTERNAL;
  }
    
  res = TRI_BeginTransaction(trx, (TRI_transaction_hint_t) TRI_TRANSACTION_HINT_SINGLE_OPERATION, TRI_TRANSACTION_TOP_LEVEL);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeTransaction(trx);
    
    return TRI_ERROR_INTERNAL;
  }

  logger->_trxCollection = TRI_GetCollectionTransaction(trx, cid, TRI_TRANSACTION_WRITE);
  logger->_trx = trx;

  assert(logger->_trxCollection != NULL);
  assert(logger->_state._active == false);

  logger->_state._lastTick = (TRI_voc_tick_t) ((TRI_collection_t*) collection->_collection)->_info._tick; 
  logger->_state._active   = true;
  
  LOG_INFO("started replication logger for database '%s', last id: %llu", 
           logger->_databaseName,
           (unsigned long long) logger->_state._lastTick);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int StopReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_string_buffer_t* buffer;
  TRI_voc_tick_t lastTick;
  int res;

  if (! logger->_state._active) {
    return TRI_ERROR_INTERNAL;
  }

  TRI_LockSpin(&logger->_idLock);
  lastTick = logger->_state._lastTick;
  TRI_UnlockSpin(&logger->_idLock);

  assert(logger->_trx != NULL);
  assert(logger->_trxCollection != NULL);

  buffer = GetBuffer(logger);
  
  if (! StringifyStopReplication(buffer, lastTick)) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_REPLICATION_STOP, buffer); 
  
  TRI_CommitTransaction(logger->_trx, 0);
  TRI_FreeTransaction(logger->_trx);
  
  LOG_INFO("stopped replication logger for database '%s', last id: %llu", 
           logger->_databaseName,
           (unsigned long long) lastTick);


  logger->_trx             = NULL;
  logger->_trxCollection   = NULL;
  logger->_state._lastTick = 0;
  logger->_state._active   = false;

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the state of the _replication collection for a non-running
/// replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int GetStateInactive (TRI_vocbase_t* vocbase,
                             TRI_replication_log_state_t* dst) {
  TRI_vocbase_col_t* col;
  TRI_primary_collection_t* primary;
 
  col = TRI_UseCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION);

  if (col == NULL || col->_collection == NULL) {
    LOG_ERROR("could not open collection '" TRI_COL_NAME_REPLICATION "'");

    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  primary = (TRI_primary_collection_t*) col->_collection;

  dst->_active    = false;
  dst->_firstTick = 0;
  dst->_lastTick  = primary->base._info._tick;

  TRI_ReleaseCollectionVocBase(vocbase, col);

  return TRI_ERROR_NO_ERROR;
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
/// @brief create a replication logger
////////////////////////////////////////////////////////////////////////////////

TRI_replication_logger_t* TRI_CreateReplicationLogger (TRI_vocbase_t* vocbase) {
  TRI_replication_logger_t* logger;

  logger = TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_replication_logger_t), false);

  if (logger == NULL) {
    return NULL;
  }

  TRI_InitReadWriteLock(&logger->_statusLock);
  TRI_InitSpin(&logger->_idLock);

  logger->_vocbase          = vocbase;
  logger->_trx              = NULL;
  logger->_trxCollection    = NULL;
  logger->_state._firstTick = 0;
  logger->_state._lastTick  = 0;
  logger->_state._active    = false;
  logger->_logSize          = vocbase->_replicationLogSize;
  logger->_waitForSync      = vocbase->_replicationWaitForSync;
  logger->_databaseName     = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, vocbase->_name);

  assert(logger->_databaseName != NULL);

  return logger;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a replication logger
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_StopReplicationLogger(logger);
 
  TRI_FreeString(TRI_CORE_MEM_ZONE, logger->_databaseName);
  TRI_DestroySpin(&logger->_idLock);
  TRI_DestroyReadWriteLock(&logger->_statusLock);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a replication logger
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_DestroyReplicationLogger(logger);
  TRI_Free(TRI_CORE_MEM_ZONE, logger);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication logger
////////////////////////////////////////////////////////////////////////////////

int TRI_StartReplicationLogger (TRI_replication_logger_t* logger) {
  int res;
  
  res = TRI_ERROR_NO_ERROR;

  TRI_WriteLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    res = StartReplicationLogger(logger);
  }

  TRI_WriteUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication logger
////////////////////////////////////////////////////////////////////////////////

int TRI_StopReplicationLogger (TRI_replication_logger_t* logger) {
  int res;

  res = TRI_ERROR_NO_ERROR;
  
  TRI_WriteLockReadWriteLock(&logger->_statusLock);

  if (logger->_state._active) {
    res = StopReplicationLogger(logger);
  }

  TRI_WriteUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the current replication state
////////////////////////////////////////////////////////////////////////////////

int TRI_StateReplicationLogger (TRI_replication_logger_t* logger,
                                TRI_replication_log_state_t* state) {
  int res;

  res = TRI_ERROR_NO_ERROR;
  
  TRI_WriteLockReadWriteLock(&logger->_statusLock);

  if (logger->_state._active) {
    // use state from logger
    res = GetStateReplicationLogger(logger, state);
  }
  else {
    // read first/last directly from collection
    res = GetStateInactive(logger->_vocbase, state);
  }

  TRI_WriteUnlockReadWriteLock(&logger->_statusLock);

  return res;
} 

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                     log functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

static int HandleTransaction (TRI_replication_logger_t* logger,
                              TRI_transaction_t const* trx) {
  TRI_string_buffer_t* buffer;
  size_t i, n;
  int res;

  // write "start"
  buffer = GetBuffer(logger);
  
  if (! StringifyMetaTransaction(buffer, trx)) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_OUT_OF_MEMORY;
  }
   
  res = LogEvent(logger, trx->_id, false, OPERATION_TRANSACTION_START, buffer);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }


  // write the individual operations
  n = trx->_collections._length;
  assert(n > 0);

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* trxCollection;
    TRI_document_collection_t* document;
    size_t j, k;

    trxCollection = TRI_AtVectorPointer(&trx->_collections, i);

    if (trxCollection->_operations == NULL) {
      // no markers available for collection
      continue;
    }
    
    document = (TRI_document_collection_t*) trxCollection->_collection->_collection;
    k = trxCollection->_operations->_length;

    for (j = 0; j < k; ++j) {
      TRI_transaction_operation_t* trxOperation;
      const char* typeName;

      trxOperation = TRI_AtVector(trxCollection->_operations, j);
  
      buffer = GetBuffer(logger);

      if (! StringifyDocumentOperation(buffer, 
                                       document, 
                                       trxOperation->_type, 
                                       trxOperation->_marker, 
                                       trxOperation->_oldHeader, 
                                       false)) {
        ReturnBuffer(logger, buffer);

        return false;
      }

      typeName = TranslateDocumentOperation(trxOperation->_type);

      if (typeName == NULL) {
        ReturnBuffer(logger, buffer);

        return TRI_ERROR_INTERNAL;
      }

      res = LogEvent(logger, trx->_id, false, typeName, buffer);

      if (res != TRI_ERROR_NO_ERROR) {
        return res;
      }
    }
  }


  // write "commit"  
  buffer = GetBuffer(logger);

  if (! StringifyMetaTransaction(buffer, trx)) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  res = LogEvent(logger, trx->_id, false, OPERATION_TRANSACTION_COMMIT, buffer);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a transaction
////////////////////////////////////////////////////////////////////////////////

int TRI_LogTransactionReplication (TRI_vocbase_t* vocbase,
                                   TRI_transaction_t const* trx) {
  TRI_replication_logger_t* logger;
  int res;
   
  assert(trx->_replicate);
  assert(trx->_hasOperations);

  res = TRI_ERROR_NO_ERROR;

  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (logger->_state._active) {
    TRI_primary_collection_t* primary;

    primary = logger->_trxCollection->_collection->_collection;

    // set a lock around all individual operations
    // so a transaction is logged as an uninterrupted sequence
    primary->beginWrite(primary);
    res = HandleTransaction(logger, trx);
    primary->endWrite(primary);
  }

  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "create collection" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogCreateCollectionReplication (TRI_vocbase_t* vocbase,
                                        TRI_voc_cid_t cid,
                                        TRI_json_t const* json) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;

  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyCreateCollection(buffer, json)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_COLLECTION_CREATE, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "drop collection" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogDropCollectionReplication (TRI_vocbase_t* vocbase,
                                      TRI_voc_cid_t cid) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyDropCollection(buffer, cid)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_COLLECTION_DROP, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "rename collection" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogRenameCollectionReplication (TRI_vocbase_t* vocbase,
                                        TRI_voc_cid_t cid,
                                        char const* name) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyRenameCollection(buffer, cid, name)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_COLLECTION_RENAME, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "change collection properties" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogChangePropertiesCollectionReplication (TRI_vocbase_t* vocbase,
                                                  TRI_voc_cid_t cid,
                                                  TRI_json_t const* json) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyCreateCollection(buffer, json)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_COLLECTION_CHANGE, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "create index" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogCreateIndexReplication (TRI_vocbase_t* vocbase,
                                   TRI_voc_cid_t cid,
                                   TRI_idx_iid_t iid,
                                   TRI_json_t const* json) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyCreateIndex(buffer, cid, json)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_INDEX_CREATE, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "drop index" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogDropIndexReplication (TRI_vocbase_t* vocbase,
                                 TRI_voc_cid_t cid,
                                 TRI_idx_iid_t iid) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyDropIndex(buffer, cid, iid)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_INDEX_DROP, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a document operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogDocumentReplication (TRI_vocbase_t* vocbase,
                                TRI_document_collection_t* document,
                                TRI_voc_document_operation_e type,
                                TRI_df_marker_t const* marker,
                                TRI_doc_mptr_t const* oldHeader) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  const char* typeName;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_NO_ERROR;
  }
  
  typeName = TranslateDocumentOperation(type);
  if (typeName == NULL) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_INTERNAL;
  }

  buffer = GetBuffer(logger);

  if (! StringifyDocumentOperation(buffer, document, type, marker, oldHeader, true)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, typeName, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    dump functions
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
                                   TRI_voc_tick_t tickMin,
                                   TRI_voc_tick_t tickMax,
                                   uint64_t chunkSize) {
  TRI_primary_collection_t* primary;
  TRI_barrier_t* b;
  int res;

  assert(col != NULL);
  assert(col->_collection != NULL);

  primary = (TRI_primary_collection_t*) col->_collection;

  // create a barrier so the underlying collection is not unloaded
  b = TRI_CreateBarrierReplication(&primary->_barrierList);

  if (b == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  // block compaction
  TRI_ReadLockReadWriteLock(&primary->_compactionLock);

  res = DumpCollection(dump, primary, tickMin, tickMax, chunkSize);
  
  TRI_ReadUnlockReadWriteLock(&primary->_compactionLock);

  TRI_FreeBarrier(b);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from the replication log
////////////////////////////////////////////////////////////////////////////////

int TRI_DumpLogReplication (TRI_vocbase_t* vocbase,
                            TRI_replication_dump_t* dump,
                            TRI_voc_tick_t tickMin,
                            TRI_voc_tick_t tickMax,
                            uint64_t chunkSize) {
  TRI_vocbase_col_t* col;
  TRI_primary_collection_t* primary;
  TRI_barrier_t* b;
  int res;

  col = TRI_UseCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION);

  if (col == NULL || col->_collection == NULL) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  primary = (TRI_primary_collection_t*) col->_collection;

  // create a barrier so the underlying collection is not unloaded
  b = TRI_CreateBarrierReplication(&primary->_barrierList);

  if (b == NULL) {
    TRI_ReleaseCollectionVocBase(vocbase, col);

    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  // block compaction
  TRI_ReadLockReadWriteLock(&primary->_compactionLock);

  res = DumpLog(dump, primary, tickMin, tickMax, chunkSize);
  
  TRI_ReadUnlockReadWriteLock(&primary->_compactionLock);

  TRI_FreeBarrier(b);
  
  TRI_ReleaseCollectionVocBase(vocbase, col);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                           REPLICATION APPLICATION
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief get the filename of the replication application file
////////////////////////////////////////////////////////////////////////////////

static char* GetApplyStateFilename (TRI_vocbase_t* vocbase) {
  return TRI_Concatenate2File(vocbase->_path, "REPLICATION");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of the replication apply state
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* ApplyStateToJson (TRI_replication_apply_state_t const* state) {
  TRI_json_t* json;
  char* serverId;
  char* lastTick;

  json = TRI_CreateArray2Json(TRI_CORE_MEM_ZONE, 2);

  if (json == NULL) {
    return NULL;
  }

  serverId = TRI_StringUInt64(state->_serverId);
  lastTick = TRI_StringUInt64(state->_lastTick);

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "serverId", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, serverId));
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "lastTick", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastTick));

  return json;
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
/// @brief initialise a master info struct
////////////////////////////////////////////////////////////////////////////////

void TRI_InitMasterInfoReplication (TRI_replication_master_info_t* info,
                                    const char* endpoint) {
  info->_endpoint         = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, endpoint);
  info->_serverId         = 0;
  info->_majorVersion     = 0;
  info->_minorVersion     = 0;
  info->_state._firstTick = 0;
  info->_state._lastTick  = 0;
  info->_state._active    = false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a master info struct
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyMasterInfoReplication (TRI_replication_master_info_t* info) {
  if (info->_endpoint != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, info->_endpoint);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief log information about the master state
////////////////////////////////////////////////////////////////////////////////
      
void TRI_LogMasterInfoReplication (TRI_replication_master_info_t const* info,
                                   const char* prefix) {
  LOG_INFO("%s master at %s, id %llu, tick range: %llu - %llu, version %d.%d", 
           prefix,
           info->_endpoint,
           (unsigned long long) info->_serverId,
           (unsigned long long) info->_state._firstTick,
           (unsigned long long) info->_state._lastTick,
           info->_majorVersion,
           info->_minorVersion);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise an apply state struct
////////////////////////////////////////////////////////////////////////////////

void TRI_InitApplyStateReplication (TRI_replication_apply_state_t* state) {
  state->_serverId = 0;
  state->_lastTick = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief save the replication application state to a file
////////////////////////////////////////////////////////////////////////////////

int TRI_SaveApplyStateReplication (TRI_vocbase_t* vocbase,
                                   TRI_replication_apply_state_t const* state,
                                   bool sync) {
  TRI_json_t* json;
  char* filename;
  int res;

  json = ApplyStateToJson(state);

  if (json == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  filename = GetApplyStateFilename(vocbase);

  if (! TRI_SaveJson(filename, json, sync)) {
    res = TRI_ERROR_INTERNAL;
  }
  else {
    res = TRI_ERROR_NO_ERROR;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief load the replication application state from a file
////////////////////////////////////////////////////////////////////////////////

int TRI_LoadApplyStateReplication (TRI_vocbase_t* vocbase,
                                   TRI_replication_apply_state_t* state) {
  TRI_json_t* json;
  TRI_json_t* serverId;
  TRI_json_t* lastTick;
  char* filename;
  char* error;
  int res;
   
  filename = GetApplyStateFilename(vocbase);

  if (! TRI_ExistsFile(filename)) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

    return TRI_ERROR_FILE_NOT_FOUND;
  }

  error = NULL;
  json  = TRI_JsonFile(TRI_CORE_MEM_ZONE, filename, &error);

  if (json == NULL || json->_type != TRI_JSON_ARRAY) {
    if (error != NULL) {
      TRI_Free(TRI_CORE_MEM_ZONE, error);
    }

    return TRI_ERROR_INTERNAL;
  }

  res = TRI_ERROR_NO_ERROR;

  // read the server id
  serverId = TRI_LookupArrayJson(json, "serverId");

  if (serverId == NULL || 
      serverId->_type != TRI_JSON_STRING || 
      serverId->_value._string.data == NULL) {
    res = TRI_ERROR_INTERNAL;
  }
  else {
    state->_serverId = TRI_UInt64String(serverId->_value._string.data);
  }

  // read the last tick
  lastTick = TRI_LookupArrayJson(json, "lastTick");

  if (lastTick == NULL || 
      lastTick->_type != TRI_JSON_STRING || 
      lastTick->_value._string.data == NULL) {
    res = TRI_ERROR_INTERNAL;
  }
  else {
    state->_lastTick = TRI_UInt64String(lastTick->_value._string.data);
  }

  TRI_Free(TRI_CORE_MEM_ZONE, json);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  HELPER FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief determine whether a collection should be included in replication
////////////////////////////////////////////////////////////////////////////////

bool TRI_ExcludeCollectionReplication (const char* name) {
  if (TRI_EqualString(name, TRI_COL_NAME_DATABASES)) {
    return true;
  }
  
  if (TRI_EqualString(name, TRI_COL_NAME_ENDPOINTS)) {
    return true;
  }
  
  if (TRI_EqualString(name, TRI_COL_NAME_PREFIXES)) {
    return true;
  }

  if (TRI_EqualString(name, TRI_COL_NAME_REPLICATION)) {
    return true;
  }

  if (TRI_EqualString(name, TRI_COL_NAME_TRANSACTION)) {
    return true;
  }
  
  if (TRI_EqualString(name, TRI_COL_NAME_USERS)) {
    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
