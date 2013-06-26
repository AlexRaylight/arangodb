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

  json = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (json == NULL) {
    // should not happen in CORE_MEM_ZONE, but you never know
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, eventName));
  if (tid == 0) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "tid", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, "0"));
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
  logger->_lastId = mptr._rid;
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
  TRI_voc_rid_t oldRev;
  TRI_voc_key_t key;
  
  if (type == TRI_VOC_DOCUMENT_OPERATION_INSERT) {
    oldRev = 0;
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_UPDATE) {
    oldRev = oldHeader->_rid;
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_REMOVE) {
    oldRev = oldHeader->_rid;
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
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
  }
  else if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    key = ((char*) m) + m->_offsetKey;
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
    APPEND_STRING(buffer, "\"_key\":\"");
    APPEND_STRING(buffer, key);
    APPEND_STRING(buffer, "\",\"_rev\":\"");
    APPEND_UINT64(buffer, marker->_tick);
    APPEND_CHAR(buffer, '"');

    if (marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
      TRI_doc_edge_key_marker_t const* e = (TRI_doc_edge_key_marker_t const*) marker;
      TRI_voc_key_t fromKey = ((char*) e) + e->_offsetFromKey;
      TRI_voc_key_t toKey = ((char*) e) + e->_offsetToKey;

      APPEND_STRING(buffer, ",\"_from\":\"");
      APPEND_UINT64(buffer, e->_fromCid);
      APPEND_CHAR(buffer, '/');
      APPEND_STRING(buffer, fromKey);
      APPEND_STRING(buffer, "\",\"_to\":\"");
      APPEND_UINT64(buffer, e->_toCid);
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
/// @brief start the replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int StartReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_transaction_t* trx;
  TRI_vocbase_col_t* collection;
  TRI_vocbase_t* vocbase;
  TRI_voc_cid_t cid;
  int res;
  
  if (logger->_active) {
    return TRI_ERROR_INTERNAL;
  }

  assert(logger->_trx == NULL);
  assert(logger->_trxCollection == NULL);
  assert(logger->_lastId == 0);

  vocbase = logger->_vocbase;
  collection = TRI_LookupCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION);

  if (collection == NULL) {
    LOG_ERROR("could not open collection '" TRI_COL_NAME_REPLICATION "'");

    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  cid = collection->_cid;

  // TODO: do we need to establish a barrier against unloading the collection??
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
  assert(logger->_active == false);

  logger->_lastId = (TRI_voc_tick_t) ((TRI_collection_t*) collection->_collection)->_info._tick; 
  logger->_active = true;
  
  LOG_INFO("started replication logger for database '%s', last id: %llu", 
           logger->_databaseName,
           (unsigned long long) logger->_lastId);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int StopReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_string_buffer_t* buffer;
  TRI_voc_tick_t lastId;
  int res;

  if (! logger->_active) {
    return TRI_ERROR_INTERNAL;
  }

  TRI_LockSpin(&logger->_idLock);
  lastId = logger->_lastId;
  TRI_UnlockSpin(&logger->_idLock);

  assert(logger->_trx != NULL);
  assert(logger->_trxCollection != NULL);

  buffer = GetBuffer(logger);
  
  if (! StringifyStopReplication(buffer, lastId)) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, OPERATION_REPLICATION_STOP, buffer); 
  
  TRI_FreeTransaction(logger->_trx);

  // TODO: free barrier if we established one
  
  LOG_INFO("stopped replication logger for database '%s', last id: %llu", 
           logger->_databaseName,
           (unsigned long long) logger->_lastId);


  logger->_trx           = NULL;
  logger->_trxCollection = NULL;
  logger->_lastId        = 0;
  logger->_active        = false;

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

  logger->_vocbase       = vocbase;
  logger->_trx           = NULL;
  logger->_trxCollection = NULL;
  logger->_lastId        = 0;
  logger->_active        = false;
  logger->_logSize       = vocbase->_replicationLogSize;
  logger->_waitForSync   = vocbase->_replicationWaitForSync;
  logger->_databaseName  = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, vocbase->_name);

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

  if (! logger->_active) {
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

  if (logger->_active) {
    res = StopReplicationLogger(logger);
  }

  TRI_WriteUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                              public log functions
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
  
      // write "commit"  
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

int TRI_TransactionReplication (TRI_vocbase_t* vocbase,
                                TRI_transaction_t const* trx) {
  TRI_replication_logger_t* logger;
  int res;
   
  assert(trx->_replicate);
  assert(trx->_hasOperations);

  res = TRI_ERROR_NO_ERROR;

  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (logger->_active) {
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

int TRI_CreateCollectionReplication (TRI_vocbase_t* vocbase,
                                     TRI_voc_cid_t cid,
                                     TRI_json_t const* json) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;

  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_active) {
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

int TRI_DropCollectionReplication (TRI_vocbase_t* vocbase,
                                   TRI_voc_cid_t cid) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_active) {
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

int TRI_RenameCollectionReplication (TRI_vocbase_t* vocbase,
                                     TRI_voc_cid_t cid,
                                     char const* name) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_active) {
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

int TRI_ChangePropertiesCollectionReplication (TRI_vocbase_t* vocbase,
                                               TRI_voc_cid_t cid,
                                               TRI_json_t const* json) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_active) {
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

int TRI_CreateIndexReplication (TRI_vocbase_t* vocbase,
                                TRI_voc_cid_t cid,
                                TRI_idx_iid_t iid,
                                TRI_json_t const* json) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_active) {
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

int TRI_DropIndexReplication (TRI_vocbase_t* vocbase,
                              TRI_voc_cid_t cid,
                              TRI_idx_iid_t iid) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  logger = vocbase->_replicationLogger;
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_active) {
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

int TRI_DocumentReplication (TRI_vocbase_t* vocbase,
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

  if (! logger->_active) {
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

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
