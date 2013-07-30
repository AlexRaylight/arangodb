////////////////////////////////////////////////////////////////////////////////
/// @brief replication logger
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

#include "replication-logger.h"

#include "BasicsC/conversions.h"
#include "BasicsC/files.h"
#include "BasicsC/json.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/tri-strings.h"

#include "VocBase/collection.h"
#include "VocBase/datafile.h"
#include "VocBase/document-collection.h"
#include "VocBase/server-id.h"
#include "VocBase/transaction.h"
#include "VocBase/vocbase.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                REPLICATION LOGGER
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                   CLIENT HANDLING
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief struct to hold a client action
////////////////////////////////////////////////////////////////////////////////

typedef struct logger_client_s {
  TRI_server_id_t _serverId;
  char*           _url;
  char            _stamp[24];
}
logger_client_t;

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
/// @brief hashes a client id
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashKeyClient (TRI_associative_pointer_t* array, 
                               void const* key) {
  TRI_server_id_t const* k = key;

  return (uint64_t) *k;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes a client struct
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementClient (TRI_associative_pointer_t* array, 
                                   void const* element) {
  logger_client_t const* e = element;
  
  return (uint64_t) e->_serverId;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compares a client key and a client struct
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualKeyClient (TRI_associative_pointer_t* array, 
                              void const* key, 
                              void const* element) {
  TRI_server_id_t const* k = key;
  logger_client_t const* e = element;

  return *k == e->_serverId;
}
      
////////////////////////////////////////////////////////////////////////////////
/// @brief free a single registered client
////////////////////////////////////////////////////////////////////////////////

static void FreeClient (logger_client_t* client) {
  if (client->_url != NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, client->_url);
  }

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, client);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free registered clients
////////////////////////////////////////////////////////////////////////////////

static void FreeClients (TRI_replication_logger_t* logger) {
  uint32_t i, n;
  
  n = logger->_clients._nrAlloc;

  for (i = 0; i < n; ++i) {
    logger_client_t* client = logger->_clients._table[i];

    if (client != NULL) {
      FreeClient(client);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                           LOGGING
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
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief number of pre-allocated string buffers for logging
////////////////////////////////////////////////////////////////////////////////

static size_t NumBuffers = 16;

////////////////////////////////////////////////////////////////////////////////
/// @brief pre-allocated size for each log buffer
////////////////////////////////////////////////////////////////////////////////

static size_t BufferSize = 256;

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

static TRI_replication_operation_e TranslateDocumentOperation (TRI_voc_document_operation_e type,
                                                               TRI_document_collection_t const* document) {
  if (type == TRI_VOC_DOCUMENT_OPERATION_INSERT || type == TRI_VOC_DOCUMENT_OPERATION_UPDATE) {
    const bool isEdgeCollection = (document->base.base._info._type == TRI_COL_TYPE_EDGE);

    return isEdgeCollection ? MARKER_EDGE : MARKER_DOCUMENT;
  }
  else if (type == TRI_VOC_DOCUMENT_OPERATION_REMOVE) {
    return MARKER_REMOVE;
  }

  return REPLICATION_INVALID;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a buffer to write an event in
////////////////////////////////////////////////////////////////////////////////

static TRI_string_buffer_t* GetBuffer (TRI_replication_logger_t* logger) {
  TRI_string_buffer_t* buffer;
  size_t n;
   
  assert(logger != NULL);
  buffer = NULL;

  // locked section
  // ---------------------------------------
  TRI_LockSpin(&logger->_bufferLock);

  n = logger->_buffers._length;

  if (n > 0) {
    buffer = TRI_RemoveVectorPointer(&logger->_buffers, (size_t) (n - 1));
  }

  TRI_UnlockSpin(&logger->_bufferLock);
  // ---------------------------------------
  // locked section end

  assert(buffer != NULL);

  return buffer;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a buffer to the list of available buffers
////////////////////////////////////////////////////////////////////////////////

static void ReturnBuffer (TRI_replication_logger_t* logger,
                          TRI_string_buffer_t* buffer) {
  assert(logger != NULL);
  assert(buffer != NULL);

  // make the buffer usable again
  if (buffer->_buffer == NULL) {
    TRI_InitSizedStringBuffer(buffer, TRI_CORE_MEM_ZONE, BufferSize);
  }
  else {
    TRI_ResetStringBuffer(buffer);
  }

  // locked section
  // ---------------------------------------
  TRI_LockSpin(&logger->_bufferLock);

  TRI_PushBackVectorPointer(&logger->_buffers, buffer);
  assert(logger->_buffers._length <= NumBuffers);
  
  TRI_UnlockSpin(&logger->_bufferLock);
  // ---------------------------------------
  // locked section end
}

////////////////////////////////////////////////////////////////////////////////
/// @brief logs a replication event contained in the buffer
/// the function will always free the buffer passed
////////////////////////////////////////////////////////////////////////////////

static int LogEvent (TRI_replication_logger_t* logger,
                     TRI_voc_tid_t tid,
                     bool isStandaloneOperation,
                     TRI_replication_operation_e type,
                     TRI_string_buffer_t* buffer) {
  TRI_primary_collection_t* primary;
  TRI_shaped_json_t* shaped;
  TRI_json_t json;
  TRI_doc_mptr_t mptr;
  size_t len;
  int res;
  bool forceSync;
  bool withTid;

  assert(logger != NULL);
  assert(buffer != NULL);

  len = TRI_LengthStringBuffer(buffer);

  if (len < 1) {
    // buffer is empty
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_NO_ERROR;
  }

  // do we have a transaction id?
  withTid = (tid > 0);

  // this type of operation will be synced. all other operations will not be synced.
  forceSync = (type == REPLICATION_STOP);
 
  // TODO: instead of using JSON here, we should directly use ShapedJson.
  // this will be a performance optimisation
  TRI_InitArray2Json(TRI_CORE_MEM_ZONE, &json, withTid ? 3 : 2);

  // add "type" attribute
  {
    TRI_json_t typeAttribute;
    TRI_InitNumberJson(&typeAttribute, (double) type);

    TRI_Insert4ArrayJson(TRI_CORE_MEM_ZONE, 
                         &json, 
                         "type", 
                         4, // strlen("type")
                         &typeAttribute,
                         true);
  }

  // "tid" attribute
  if (withTid) {
    TRI_json_t tidAttribute;
    TRI_InitStringJson(&tidAttribute, TRI_StringUInt64(tid));

    TRI_Insert4ArrayJson(TRI_CORE_MEM_ZONE, 
                         &json, 
                         "tid", 
                         3, // strlen("tid")
                         &tidAttribute,
                         true);
  }

  // "data" attribute
  {
    TRI_json_t dataAttribute;
    // pass the string-buffer buffer pointer to the JSON
    TRI_InitStringReference2Json(&dataAttribute, TRI_BeginStringBuffer(buffer), TRI_LengthStringBuffer(buffer));

    TRI_Insert4ArrayJson(TRI_CORE_MEM_ZONE, 
                         &json, 
                         "data", 
                         4, // strlen("data")
                         &dataAttribute,
                         true);
  }

  LOG_TRACE("logging replication event, type: %d, tid: %llu, sync: %d, data: %s", 
            (int) type,
            (unsigned long long) tid,
            (int) forceSync,
            TRI_BeginStringBuffer(buffer));
  
  primary = logger->_trxCollection->_collection->_collection;
  shaped = TRI_ShapedJsonJson(primary->_shaper, &json);
  TRI_DestroyJson(TRI_CORE_MEM_ZONE, &json);
  
  ReturnBuffer(logger, buffer);

  if (shaped == NULL) {
    return TRI_ERROR_ARANGO_SHAPER_FAILED;
  }

  res = primary->insert(logger->_trxCollection, 
                        NULL, 
                        0,
                        &mptr, 
                        TRI_DOC_MARKER_KEY_DOCUMENT, 
                        shaped, 
                        NULL, 
                        isStandaloneOperation, 
                        forceSync);

  TRI_FreeShapedJson(primary->_shaper, shaped);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  // assert the write was successful
  assert(mptr._data != NULL);
  
  // update the last tick that we've logged
  TRI_LockSpin(&logger->_idLock);
  logger->_state._lastLogTick = ((TRI_df_marker_t*) mptr._data)->_tick;
  TRI_UnlockSpin(&logger->_idLock);

  return TRI_ERROR_NO_ERROR;
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
/// @brief stringify a "replication" operation with a tick
////////////////////////////////////////////////////////////////////////////////

static bool StringifyTickReplication (TRI_string_buffer_t* buffer,
                                      TRI_voc_tick_t tick) {
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "{\"lastTick\":\"");
  APPEND_UINT64(buffer, (uint64_t) tick);
  APPEND_STRING(buffer, "\"}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "create collection" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyCreateCollection (TRI_string_buffer_t* buffer,
                                       TRI_voc_cid_t cid,
                                       TRI_json_t const* json) {
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "{\"cid\":\"");
  APPEND_UINT64(buffer, (uint64_t) cid);
  APPEND_STRING(buffer, "\",\"collection\":");
  APPEND_JSON(buffer, json);
  APPEND_CHAR(buffer, '}');

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "drop collection" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyDropCollection (TRI_string_buffer_t* buffer,
                                     TRI_voc_cid_t cid) {
  if (buffer == NULL) {
    return false;
  }

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
  
  if (buffer == NULL) {
    return false;
  }

  APPEND_CHAR(buffer, '{');

  if (! StringifyCollection(buffer, cid)) {
    return false;
  }

  APPEND_STRING(buffer, ",\"collection\":{\"name\":\"");
  // name is user-defined, but does not need escaping as collection names are "safe"
  APPEND_STRING(buffer, name);
  APPEND_STRING(buffer, "\"}}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stringify a "create index" operation
////////////////////////////////////////////////////////////////////////////////

static bool StringifyCreateIndex (TRI_string_buffer_t* buffer,
                                  TRI_voc_cid_t cid,
                                  TRI_json_t const* json) {
  if (buffer == NULL) {
    return false;
  }

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
  if (buffer == NULL) {
    return false;
  }

  APPEND_CHAR(buffer, '{');
  
  if (! StringifyCollection(buffer, cid)) {
    return false;
  }
  
  APPEND_STRING(buffer, ",\"id\":\"");
  APPEND_UINT64(buffer, (uint64_t) iid);
  APPEND_STRING(buffer, "\"}");

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
  
  if (buffer == NULL) {
    return false;
  }

  if (TRI_ReserveStringBuffer(buffer, 256) != TRI_ERROR_NO_ERROR) {
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
  APPEND_STRING(buffer, "\",\"rev\":\""); 
  APPEND_UINT64(buffer, (uint64_t) rid); 

  if (oldRev > 0) {
    APPEND_STRING(buffer, "\",\"oldRev\":\""); 
    APPEND_UINT64(buffer, (uint64_t) oldRev); 
  }

  // document
  if (marker->_type == TRI_DOC_MARKER_KEY_DOCUMENT ||
      marker->_type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_document_key_marker_t const* m = (TRI_doc_document_key_marker_t const*) marker; 
    TRI_shaped_json_t shaped;
    
    APPEND_STRING(buffer, "\",\"data\":{");
    
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
  
  if (buffer == NULL) {
    return false;
  }

  APPEND_STRING(buffer, "{\"collections\":[");

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
    
    if (TRI_ExcludeCollectionReplication(trxCollection->_collection->_name)) {
      // collection is excluded from replication
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
    APPEND_STRING(buffer, "\",\"name\":\"");
    // no escaping needed for collection name
    APPEND_STRING(buffer, document->base.base._info._name);
    APPEND_STRING(buffer, "\",\"operations\":");
    APPEND_UINT64(buffer, (uint64_t) trxCollection->_operations->_length);
    APPEND_CHAR(buffer, '}');
  }
  APPEND_STRING(buffer, "]}");

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get current state from the replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int GetStateReplicationLogger (TRI_replication_logger_t* logger,
                                      TRI_replication_logger_state_t* dst) {
  assert(logger->_state._active);

  TRI_LockSpin(&logger->_idLock);
  memcpy(dst, &logger->_state, sizeof(TRI_replication_logger_state_t));
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
  TRI_transaction_hint_t hint;
  TRI_string_buffer_t* buffer;
  TRI_voc_cid_t cid;
  int res;
  
  if (logger->_state._active) {
    return TRI_ERROR_INTERNAL;
  }

  assert(logger->_trx == NULL);
  assert(logger->_trxCollection == NULL);
  assert(logger->_state._lastLogTick == 0);

  vocbase = logger->_vocbase;
  collection = TRI_LookupCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION);

  if (collection == NULL) {
    // try to create _replication collection on the fly
    TRI_col_info_t parameter;
    
    TRI_InitCollectionInfo(vocbase, 
                           &parameter, 
                           TRI_COL_NAME_REPLICATION,
                           (TRI_col_type_e) TRI_COL_TYPE_DOCUMENT, 
                           (TRI_voc_size_t) vocbase->_defaultMaximalSize, 
                           NULL);
    
    parameter._isSystem = true;

    collection = TRI_CreateCollectionVocBase(vocbase, &parameter, 0, TRI_GetServerId());
    TRI_FreeCollectionInfoOptions(&parameter);
    
    if (collection != NULL) {
      LOG_INFO("created collection '" TRI_COL_NAME_REPLICATION "'");
    }
  }

  if (collection == NULL) {
    LOG_ERROR("could not open collection '" TRI_COL_NAME_REPLICATION "'");

    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  cid = collection->_cid;

  trx = TRI_CreateTransaction(vocbase->_transactionContext, TRI_GetServerId(), false, 0.0, false);

  if (trx == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = TRI_AddCollectionTransaction(trx, cid, TRI_TRANSACTION_WRITE, TRI_TRANSACTION_TOP_LEVEL);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeTransaction(trx);

    return TRI_ERROR_INTERNAL;
  }

  // the SINGLE_OPERATION hint is actually a hack:
  // the logger does not write just one operation, but it is used to prevent locking the collection
  // for the entire duration of the transaction
  hint = (TRI_transaction_hint_t) TRI_TRANSACTION_HINT_SINGLE_OPERATION;
  res = TRI_BeginTransaction(trx, hint, TRI_TRANSACTION_TOP_LEVEL);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeTransaction(trx);
    
    return TRI_ERROR_INTERNAL;
  }

  logger->_trxCollection = TRI_GetCollectionTransaction(trx, cid, TRI_TRANSACTION_WRITE);
  logger->_trx = trx;

  assert(logger->_trxCollection != NULL);
  assert(logger->_state._active == false);

  logger->_state._lastLogTick = ((TRI_collection_t*) collection->_collection)->_info._tick; 
  logger->_state._active = true;
  
  LOG_INFO("started replication logger for database '%s', last tick: %llu", 
           logger->_databaseName,
           (unsigned long long) logger->_state._lastLogTick);
  
  buffer = GetBuffer(logger);
  
  if (! StringifyTickReplication(buffer, logger->_state._lastLogTick)) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  res = LogEvent(logger, 0, true, REPLICATION_START, buffer); 

  return res;
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
  lastTick = logger->_state._lastLogTick;
  TRI_UnlockSpin(&logger->_idLock);

  assert(logger->_trx != NULL);
  assert(logger->_trxCollection != NULL);

  buffer = GetBuffer(logger);
  
  if (! StringifyTickReplication(buffer, lastTick)) {
    ReturnBuffer(logger, buffer);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, REPLICATION_STOP, buffer); 

  TRI_CommitTransaction(logger->_trx, 0);
  TRI_FreeTransaction(logger->_trx);
  
  LOG_INFO("stopped replication logger for database '%s', last tick: %llu", 
           logger->_databaseName,
           (unsigned long long) lastTick);


  logger->_trx                = NULL;
  logger->_trxCollection      = NULL;
  logger->_state._lastLogTick = 0;
  logger->_state._active      = false;

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the state of the _replication collection for a non-running
/// replication logger
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int GetStateInactive (TRI_vocbase_t* vocbase,
                             TRI_replication_logger_state_t* dst) {
  TRI_vocbase_col_t* col;
  TRI_primary_collection_t* primary;
 
  col = TRI_UseCollectionByNameVocBase(vocbase, TRI_COL_NAME_REPLICATION);

  if (col == NULL || col->_collection == NULL) {
    LOG_ERROR("could not open collection '" TRI_COL_NAME_REPLICATION "'");

    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  primary = (TRI_primary_collection_t*) col->_collection;

  dst->_active       = false;
  dst->_lastLogTick  = primary->base._info._tick;

  TRI_ReleaseCollectionVocBase(vocbase, col);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free all allocated buffers
////////////////////////////////////////////////////////////////////////////////

static void FreeBuffers (TRI_replication_logger_t* logger) {
  size_t i, n;

  LOG_TRACE("freeing buffers");

  n = logger->_buffers._length;

  for (i = 0; i < n; ++i) {
    TRI_string_buffer_t* buffer = (TRI_string_buffer_t*) TRI_AtVectorPointer(&logger->_buffers, i);

    assert(buffer != NULL);
    TRI_FreeStringBuffer(TRI_CORE_MEM_ZONE, buffer);
  }

  TRI_DestroyVectorPointer(&logger->_buffers);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise buffers
////////////////////////////////////////////////////////////////////////////////

static int InitBuffers (TRI_replication_logger_t* logger) {
  size_t i;
  int res;

  assert(NumBuffers > 0);
  
  LOG_TRACE("initialising buffers");

  res = TRI_InitVectorPointer2(&logger->_buffers, TRI_CORE_MEM_ZONE, NumBuffers); 

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  for (i = 0; i < NumBuffers; ++i) {
    TRI_string_buffer_t* buffer = TRI_CreateSizedStringBuffer(TRI_CORE_MEM_ZONE, BufferSize);

    if (buffer == NULL) {
      FreeBuffers(logger);

      return TRI_ERROR_OUT_OF_MEMORY;
    }

    TRI_PushBackVectorPointer(&logger->_buffers, buffer);
  }

  assert(logger->_buffers._length == NumBuffers);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether a transaction has relevant operations 
////////////////////////////////////////////////////////////////////////////////

static bool HasRelevantOperations (TRI_transaction_t const* trx) {
  size_t i, n;
  
  n = trx->_collections._length;

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* trxCollection;

    trxCollection = TRI_AtVectorPointer(&trx->_collections, i);
    assert(trxCollection != NULL);

    if (trxCollection->_operations == NULL ||
        trxCollection->_operations->_length == 0) {
      // no markers available for collection
      continue;
    }

    if (TRI_ExcludeCollectionReplication(trxCollection->_collection->_name)) {
      // collection is excluded from replication
      continue;
    }

    // found something!
    return true;
  }

  // transaction is not relevant for replication
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handle logging of a transaction
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
   
  res = LogEvent(logger, trx->_id, false, TRI_TRANSACTION_START, buffer);

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
    
    if (TRI_ExcludeCollectionReplication(trxCollection->_collection->_name)) {
      // collection is excluded from replication
      continue;
    }

    document = (TRI_document_collection_t*) trxCollection->_collection->_collection;
    k = trxCollection->_operations->_length;

    assert(k > 0);

    for (j = 0; j < k; ++j) {
      TRI_transaction_operation_t* trxOperation;
      TRI_replication_operation_e type;

      trxOperation = TRI_AtVector(trxCollection->_operations, j);
  
      buffer = GetBuffer(logger);

      if (! StringifyDocumentOperation(buffer, 
                                       document, 
                                       trxOperation->_type, 
                                       trxOperation->_marker, 
                                       trxOperation->_oldHeader, 
                                       true)) {
        ReturnBuffer(logger, buffer);

        return false;
      }

      type = TranslateDocumentOperation(trxOperation->_type, document);

      if (type == REPLICATION_INVALID) {
        ReturnBuffer(logger, buffer);

        return TRI_ERROR_INTERNAL;
      }

      res = LogEvent(logger, trx->_id, false, type, buffer);

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
  
  res = LogEvent(logger, trx->_id, false, TRI_TRANSACTION_COMMIT, buffer);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief lock the logger and check for inclusion of an event
/// if this function returns true, it has acquired a read-lock that the caller
/// must unlock!
////////////////////////////////////////////////////////////////////////////////

static bool CheckAndLock (TRI_replication_logger_t* logger,
                          TRI_server_id_t generatingServer) {
  // acquire read-lock
  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (! logger->_state._active) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return false;
  }

  if (generatingServer != 0 &&
      generatingServer != logger->_localServerId &&
      ! logger->_configuration._logRemoteChanges) {
    // read-unlock the read-lock
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return false;
  }

  // we'll keep the lock!!

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create a replication logger
////////////////////////////////////////////////////////////////////////////////

TRI_replication_logger_t* TRI_CreateReplicationLogger (TRI_vocbase_t* vocbase,
                                                       TRI_vocbase_defaults_t* defaults) {
  TRI_replication_logger_t* logger;
  int res;

  logger = TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_replication_logger_t), false);

  if (logger == NULL) {
    return NULL;
  }
 
  // init string buffers 
  res = InitBuffers(logger);

  if (res != TRI_ERROR_NO_ERROR) {
    // out of memory
    TRI_Free(TRI_CORE_MEM_ZONE, logger);

    return NULL;
  }
  
  res = TRI_InitAssociativePointer(&logger->_clients,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   HashKeyClient,
                                   HashElementClient,
                                   IsEqualKeyClient,
                                   NULL);

  if (res != TRI_ERROR_NO_ERROR) {
    // out of memory
    FreeBuffers(logger);
    TRI_Free(TRI_CORE_MEM_ZONE, logger);

    return NULL;
  }

  TRI_InitReadWriteLock(&logger->_statusLock);
  TRI_InitReadWriteLock(&logger->_clientsLock);
  TRI_InitSpin(&logger->_idLock);
  TRI_InitSpin(&logger->_bufferLock);

  logger->_vocbase                         = vocbase;
  logger->_trx                             = NULL;
  logger->_trxCollection                   = NULL;

  logger->_state._lastLogTick              = 0;
  logger->_state._active                   = false;
  logger->_configuration._logRemoteChanges = defaults->replicationLogRemoteChanges;

  logger->_localServerId                   = TRI_GetServerId();
  logger->_databaseName                    = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, vocbase->_name);

  assert(logger->_databaseName != NULL);

  return logger;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a replication logger
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_StopReplicationLogger(logger);

  FreeBuffers(logger);
 
  FreeClients(logger);
  TRI_DestroyAssociativePointer(&logger->_clients);

  TRI_FreeString(TRI_CORE_MEM_ZONE, logger->_databaseName);
  TRI_DestroySpin(&logger->_bufferLock);
  TRI_DestroySpin(&logger->_idLock);
  TRI_DestroyReadWriteLock(&logger->_clientsLock);
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
/// @brief get a JSON representation of the replication logger configuration
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_JsonConfigurationReplicationLogger (TRI_replication_logger_configuration_t const* config) {
  TRI_json_t* json;

  json = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (json == NULL) {
    return NULL;
  }

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE,
                       json,
                       "logRemoteChanges",
                       TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, config->_logRemoteChanges));

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure the replication logger
////////////////////////////////////////////////////////////////////////////////

int TRI_ConfigureReplicationLogger (TRI_replication_logger_t* logger,
                                    TRI_replication_logger_configuration_t const* config) {
  int res;

  res = TRI_ERROR_NO_ERROR;

  TRI_WriteLockReadWriteLock(&logger->_statusLock);
  logger->_configuration._logRemoteChanges = config->_logRemoteChanges;
  TRI_WriteUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief copy a logger configuration
////////////////////////////////////////////////////////////////////////////////

void TRI_CopyConfigurationReplicationLogger (TRI_replication_logger_configuration_t const* src,
                                             TRI_replication_logger_configuration_t* dst) {
  memcpy(dst, src, sizeof(TRI_replication_logger_configuration_t));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the list of clients as a JSON array
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_JsonClientsReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_json_t* json;
  uint32_t i, n;

  json = TRI_CreateListJson(TRI_CORE_MEM_ZONE);

  TRI_ReadLockReadWriteLock(&logger->_clientsLock);

  n = logger->_clients._nrAlloc;

  for (i = 0; i < n; ++i) {
    logger_client_t* client = logger->_clients._table[i];

    if (client != NULL) {
      TRI_json_t* element = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

      if (element != NULL) {
        char* serverId = TRI_StringUInt64(client->_serverId);

        if (serverId != NULL) {
          TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, element, "serverId", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, serverId));
        }

        if (client->_url != NULL) {
          TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, element, "url", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, client->_url));
        }
          
        TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, element, "time", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, client->_stamp));

        TRI_PushBack3ListJson(TRI_CORE_MEM_ZONE, json, element);
      }
    }
  }

  TRI_ReadUnlockReadWriteLock(&logger->_clientsLock);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief insert an applier action into an action list
////////////////////////////////////////////////////////////////////////////////

void TRI_UpdateClientReplicationLogger (TRI_replication_logger_t* logger,
                                        TRI_server_id_t serverId,
                                        char const* url) {

  logger_client_t* client;
  void* found;
  
  client = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(logger_client_t), false);

  if (client == NULL) {
    return;
  }

  TRI_GetTimeStampReplication(client->_stamp, sizeof(client->_stamp) - 1);

  client->_serverId = serverId;
  client->_url = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, url);

  if (client->_url == NULL) {
    // OOM
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, client);
    return;
  }

  TRI_WriteLockReadWriteLock(&logger->_clientsLock);

  found = TRI_RemoveKeyAssociativePointer(&logger->_clients, &client->_serverId);

  if (found != NULL) {
    FreeClient((logger_client_t*) found); 
  }

  client = TRI_InsertKeyAssociativePointer(&logger->_clients, &client->_serverId, (void*) client, false);

  TRI_WriteUnlockReadWriteLock(&logger->_clientsLock);
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
/// @brief get the current replication logger state
////////////////////////////////////////////////////////////////////////////////

int TRI_StateReplicationLogger (TRI_replication_logger_t* logger,
                                TRI_replication_logger_state_t* state) {
  int res;

  res = TRI_ERROR_NO_ERROR;

  TRI_ReadLockReadWriteLock(&logger->_statusLock);

  if (logger->_state._active) {
    // use state from logger
    res = GetStateReplicationLogger(logger, state);
  }
  else {
    // read first/last directly from collection
    res = GetStateInactive(logger->_vocbase, state);
  }

  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
} 

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of a logger state
////////////////////////////////////////////////////////////////////////////////
  
TRI_json_t* TRI_JsonStateReplicationLogger (TRI_replication_logger_state_t const* state) {
  TRI_json_t* json; 
  char* lastString;
  char timeString[24];

  json = TRI_CreateArray2Json(TRI_CORE_MEM_ZONE, 3);

  // add replication state
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "running", TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, state->_active));
  
  lastString = TRI_StringUInt64(state->_lastLogTick);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "lastLogTick", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastString));

  TRI_GetTimeStampReplication(timeString, sizeof(timeString) - 1);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "time", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, timeString));
  
  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return a JSON representation of the replication logger
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_JsonReplicationLogger (TRI_replication_logger_t* logger) {
  TRI_replication_logger_state_t state;
  TRI_json_t* json;
  TRI_json_t* server;
  TRI_json_t* clients;
  int res;
  
  res = TRI_StateReplicationLogger(logger, &state);
  
  if (res != TRI_ERROR_NO_ERROR) {
    return NULL;
  }

  json = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (json == NULL) {
    return NULL;
  }
    
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "state", TRI_JsonStateReplicationLogger(&state)); 
    
  // add server info
  server = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (server != NULL) {
    TRI_server_id_t serverId;

    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, server, "version", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, TRIAGENS_VERSION));

    serverId = TRI_GetServerId();  
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, server, "serverId", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, TRI_StringUInt64(serverId)));

    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "server", server);
  }
    
  clients = TRI_JsonClientsReplicationLogger(logger);

  if (clients != NULL) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "clients", clients);
  }

  return json;
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

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a transaction
////////////////////////////////////////////////////////////////////////////////

int TRI_LogTransactionReplication (TRI_vocbase_t* vocbase,
                                   TRI_transaction_t const* trx,
                                   TRI_server_id_t generatingServer) {
  TRI_replication_logger_t* logger;
  int res;
   
  assert(trx->_replicate);
  assert(trx->_hasOperations);

  res = TRI_ERROR_NO_ERROR;

  logger = vocbase->_replicationLogger;
  
  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }

  if (HasRelevantOperations(trx)) {
    TRI_primary_collection_t* primary;

    primary = logger->_trxCollection->_collection->_collection;

    assert(primary != NULL);

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
                                        char const* name,
                                        TRI_json_t const* json,
                                        TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  if (TRI_ExcludeCollectionReplication(name)) {
    return TRI_ERROR_NO_ERROR;
  }

  logger = vocbase->_replicationLogger;
  
  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyCreateCollection(buffer, cid, json)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, COLLECTION_CREATE, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "drop collection" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogDropCollectionReplication (TRI_vocbase_t* vocbase,
                                      TRI_voc_cid_t cid,
                                      char const* name,
                                      TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;

  if (TRI_ExcludeCollectionReplication(name)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  logger = vocbase->_replicationLogger;

  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyDropCollection(buffer, cid)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, COLLECTION_DROP, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "rename collection" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogRenameCollectionReplication (TRI_vocbase_t* vocbase,
                                        TRI_voc_cid_t cid,
                                        char const* name,
                                        TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  if (TRI_ExcludeCollectionReplication(name)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  logger = vocbase->_replicationLogger;

  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyRenameCollection(buffer, cid, name)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, COLLECTION_RENAME, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "change collection properties" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogChangePropertiesCollectionReplication (TRI_vocbase_t* vocbase,
                                                  TRI_voc_cid_t cid,
                                                  char const* name,
                                                  TRI_json_t const* json,
                                                  TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  if (TRI_ExcludeCollectionReplication(name)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  logger = vocbase->_replicationLogger;

  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyCreateCollection(buffer, cid, json)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, COLLECTION_CHANGE, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "create index" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogCreateIndexReplication (TRI_vocbase_t* vocbase,
                                   TRI_voc_cid_t cid,
                                   char const* name,
                                   TRI_idx_iid_t iid,
                                   TRI_json_t const* json,
                                   TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  if (TRI_ExcludeCollectionReplication(name)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  logger = vocbase->_replicationLogger;
  
  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyCreateIndex(buffer, cid, json)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, INDEX_CREATE, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a "drop index" operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogDropIndexReplication (TRI_vocbase_t* vocbase,
                                 TRI_voc_cid_t cid,
                                 char const* name,
                                 TRI_idx_iid_t iid,
                                 TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  int res;
  
  if (TRI_ExcludeCollectionReplication(name)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  logger = vocbase->_replicationLogger;
  
  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  buffer = GetBuffer(logger);

  if (! StringifyDropIndex(buffer, cid, iid)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, INDEX_DROP, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replicate a document operation
////////////////////////////////////////////////////////////////////////////////

int TRI_LogDocumentReplication (TRI_vocbase_t* vocbase,
                                TRI_document_collection_t* document,
                                TRI_voc_document_operation_e docType,
                                TRI_df_marker_t const* marker,
                                TRI_doc_mptr_t const* oldHeader,
                                TRI_server_id_t generatingServer) {
  TRI_string_buffer_t* buffer;
  TRI_replication_logger_t* logger;
  TRI_replication_operation_e type;
  int res;
  
  if (TRI_ExcludeCollectionReplication(document->base.base._info._name)) {
    return TRI_ERROR_NO_ERROR;
  }

  logger = vocbase->_replicationLogger;
  type = TranslateDocumentOperation(docType, document);
  
  if (! CheckAndLock(logger, generatingServer)) {
    return TRI_ERROR_NO_ERROR;
  }
  
  if (type == REPLICATION_INVALID) {
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_INTERNAL;
  }

  buffer = GetBuffer(logger);

  if (! StringifyDocumentOperation(buffer, 
                                   document, 
                                   docType, 
                                   marker, 
                                   oldHeader, 
                                   true)) {
    ReturnBuffer(logger, buffer);
    TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

    return TRI_ERROR_OUT_OF_MEMORY;
  }

  res = LogEvent(logger, 0, true, type, buffer);
  TRI_ReadUnlockReadWriteLock(&logger->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
