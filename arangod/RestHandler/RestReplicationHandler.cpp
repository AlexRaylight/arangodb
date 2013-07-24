////////////////////////////////////////////////////////////////////////////////
/// @brief replication request handler
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
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "RestReplicationHandler.h"

#include "build.h"
#include "Basics/JsonHelper.h"
#include "BasicsC/conversions.h"
#include "BasicsC/files.h"
#include "Logger/Logger.h"
#include "HttpServer/HttpServer.h"
#include "Rest/HttpRequest.h"
#include "VocBase/replication-applier.h"
#include "VocBase/replication-dump.h"
#include "VocBase/replication-logger.h"
#include "VocBase/server-id.h"

#ifdef TRI_ENABLE_REPLICATION

using namespace std;
using namespace triagens::basics;
using namespace triagens::rest;
using namespace triagens::arango;

  
const uint64_t RestReplicationHandler::minChunkSize = 512 * 1024;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

RestReplicationHandler::RestReplicationHandler (HttpRequest* request, 
                                                TRI_vocbase_t* vocbase)
  : RestVocbaseBaseHandler(request, vocbase) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

RestReplicationHandler::~RestReplicationHandler () {
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                   Handler methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool RestReplicationHandler::isDirect () {
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

string const& RestReplicationHandler::queue () const {
  static string const client = "STANDARD";

  return client;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

Handler::status_e RestReplicationHandler::execute() {
  // extract the request type
  const HttpRequest::HttpRequestType type = _request->requestType();
  
  vector<string> const& suffix = _request->suffix();

  const size_t len = suffix.size();

  if (len == 1) {
    const string& command = suffix[0];

    if (command == "logger-start") {
      if (type != HttpRequest::HTTP_REQUEST_PUT) {
        goto BAD_CALL;
      }
      handleCommandLoggerStart();
    }
    else if (command == "logger-stop") {
      if (type != HttpRequest::HTTP_REQUEST_PUT) {
        goto BAD_CALL;
      }
      handleCommandLoggerStop();
    }
    else if (command == "logger-state") {
      if (type != HttpRequest::HTTP_REQUEST_GET) {
        goto BAD_CALL;
      }
      handleCommandLoggerState();
    }
    else if (command == "logger-follow") {
      if (type != HttpRequest::HTTP_REQUEST_GET) {
        goto BAD_CALL;
      }
      handleCommandLoggerFollow(); 
    }
    else if (command == "inventory") {
      if (type != HttpRequest::HTTP_REQUEST_GET) {
        goto BAD_CALL;
      }
      handleCommandInventory();
    }
    else if (command == "dump") {
      if (type != HttpRequest::HTTP_REQUEST_GET) {
        goto BAD_CALL;
      }
      handleCommandDump(); 
    }
    else if (command == "applier-config") {
      if (type == HttpRequest::HTTP_REQUEST_GET) {
        handleCommandApplierGetConfig();
      }
      else {
        if (type != HttpRequest::HTTP_REQUEST_PUT) {
          goto BAD_CALL;
        }
        handleCommandApplierSetConfig();
      }
    }
    else if (command == "applier-start") {
      if (type != HttpRequest::HTTP_REQUEST_PUT) {
        goto BAD_CALL;
      }
      handleCommandApplierStart();
    }
    else if (command == "applier-stop") {
      if (type != HttpRequest::HTTP_REQUEST_PUT) {
        goto BAD_CALL;
      }
      handleCommandApplierStop();
    }
    else if (command == "applier-state") {
      if (type == HttpRequest::HTTP_REQUEST_DELETE) {
        handleCommandApplierDeleteState();
      }
      else {
        if (type != HttpRequest::HTTP_REQUEST_GET) {
          goto BAD_CALL;
        }
        handleCommandApplierGetState();
      }
    }
    else {
      generateError(HttpResponse::BAD,
                    TRI_ERROR_HTTP_BAD_PARAMETER,
                    "invalid command");
    }
      
    return Handler::HANDLER_DONE;
  }

BAD_CALL:
  if (len != 1) {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_SUPERFLUOUS_SUFFICES,
                  "expecting URL /_api/replication/<command>");
  }
  else {
    generateError(HttpResponse::METHOD_NOT_ALLOWED, TRI_ERROR_HTTP_METHOD_NOT_ALLOWED);
  }
  
  return Handler::HANDLER_DONE;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                             public static methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief filter a collection based on collection attributes
////////////////////////////////////////////////////////////////////////////////

bool RestReplicationHandler::filterCollection (TRI_vocbase_col_t* collection, 
                                               void* data) {
  if (collection->_type != (TRI_col_type_t) TRI_COL_TYPE_DOCUMENT && 
      collection->_type != (TRI_col_type_t) TRI_COL_TYPE_EDGE) {
    // invalid type
    return false;
  }

  if (TRI_ExcludeCollectionReplication(collection->_name)) {
    // collection is excluded
    return false;
  }

  // all other cases should be included
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief insert the applier action into an action list
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::insertClient () {
  bool found;
  char const* value;

  value = _request->value("serverId", found);

  if (found) {
    TRI_server_id_t serverId = (TRI_server_id_t) StringUtils::uint64(value);

    if (serverId > 0) {
      TRI_UpdateClientReplicationLogger(_vocbase->_replicationLogger, serverId, _request->fullUrl().c_str());
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determine the minimum chunk size
////////////////////////////////////////////////////////////////////////////////
  
uint64_t RestReplicationHandler::determineChunkSize () const {
  // determine chunk size
  uint64_t chunkSize = minChunkSize;

  bool found;
  const char* value = _request->value("chunkSize", found);

  if (found) {
    // url parameter "chunkSize" specified
    chunkSize = (uint64_t) StringUtils::uint64(value);
  }
  else {
    // not specified, use default
    chunkSize = minChunkSize;
  }

  return chunkSize;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief starts the replication logger
///
/// @RESTHEADER{PUT /_api/replication/logger-start,starts the replication logger}
///
/// @RESTDESCRIPTION
/// Starts the server's replication logger. Will do nothing if the replication
/// logger is already running.
///
/// The body of the response contains a JSON object with the following
/// attributes:
///
/// - `running`: will contain `true`
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{200}
/// is returned if the logger was started successfully, or was already running.
///
/// @RESTRETURNCODE{500}
/// is returned if the logger could not be started.
///
/// @EXAMPLES
///
/// Starts the replication logger.
///
/// @EXAMPLE_ARANGOSH_RUN{RestReplicationLoggerStart}
///     var url = "/_api/replication/logger-start";
///
///     var response = logCurlRequest('PUT', url, "");
///
///     assert(response.code === 200);
///
///     logJsonResponse(response);
/// @END_EXAMPLE_ARANGOSH_RUN
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandLoggerStart () {
  assert(_vocbase->_replicationLogger != 0);

  int res = TRI_StartReplicationLogger(_vocbase->_replicationLogger);

  if (res != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }

  TRI_json_t result;
    
  TRI_InitArrayJson(TRI_CORE_MEM_ZONE, &result);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, &result, "running", TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, true));

  generateResult(&result);
  TRI_DestroyJson(TRI_CORE_MEM_ZONE, &result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stops the replication logger
///
/// @RESTHEADER{PUT /_api/replication/logger-stop,stops the replication logger}
///
/// @RESTDESCRIPTION
/// Stops the server's replication logger. Will do nothing if the replication
/// logger is not running.
///
/// The body of the response contains a JSON object with the following
/// attributes:
///
/// - `running`: will contain `false`
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{200}
/// is returned if the logger was stopped successfully, or was not running
/// before.
///
/// @RESTRETURNCODE{500}
/// is returned if the logger could not be stopped.
///
/// @EXAMPLES
///
/// Starts the replication logger.
///
/// @EXAMPLE_ARANGOSH_RUN{RestReplicationLoggerStop}
///     var url = "/_api/replication/logger-stop";
///
///     var response = logCurlRequest('PUT', url, "");
///
///     assert(response.code === 200);
///
///     logJsonResponse(response);
/// @END_EXAMPLE_ARANGOSH_RUN
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandLoggerStop () {
  assert(_vocbase->_replicationLogger != 0);

  int res = TRI_StopReplicationLogger(_vocbase->_replicationLogger);

  if (res != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }

  TRI_json_t result;
    
  TRI_InitArrayJson(TRI_CORE_MEM_ZONE, &result);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, &result, "running", TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, false));

  generateResult(&result);
  TRI_DestroyJson(TRI_CORE_MEM_ZONE, &result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the state of the replication logger
///
/// @RESTHEADER{GET /_api/replication/logger-state,returns the replication logger state}
///
/// @RESTDESCRIPTION
/// Returns the current state of the server's replication logger. The state will
/// include information about whether the logger is running and about the last
/// logged tick value. This tick value is important for incremental fetching of
/// data.
///
/// The state API can be called regardless of whether the logger is currently
/// running or not.
///
/// The body of the response contains a JSON object with the following
/// attributes:
///
/// - `running`: will contain `false`
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{200}
/// is returned if the logger state could be determined successfully.
///
/// @RESTRETURNCODE{500}
/// is returned if the logger state could not be determined.
///
/// @EXAMPLES
///
/// Starts the replication logger.
///
/// @EXAMPLE_ARANGOSH_RUN{RestReplicationLoggerState}
///     var url = "/_api/replication/logger-state";
///
///     var response = logCurlRequest('GET', url);
///
///     assert(response.code === 200);
///
///     logJsonResponse(response);
/// @END_EXAMPLE_ARANGOSH_RUN
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandLoggerState () {
  assert(_vocbase->_replicationLogger != 0);

  TRI_json_t* json = TRI_JsonReplicationLogger(_vocbase->_replicationLogger);

  if (json == 0) {
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_OUT_OF_MEMORY);
    return;
  }
  
  generateResult(json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handle a follow command for the replication log
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandLoggerFollow () {
  // determine start tick
  TRI_voc_tick_t tickStart = 0;
  TRI_voc_tick_t tickEnd   = (TRI_voc_tick_t) UINT64_MAX;
  bool found;
  char const* value;
  
  value = _request->value("from", found);
  if (found) {
    tickStart = (TRI_voc_tick_t) StringUtils::uint64(value);
  }

  // determine end tick for dump
  value = _request->value("to", found);
  if (found) {
    tickEnd = (TRI_voc_tick_t) StringUtils::uint64(value);
  }

  if (tickStart > tickEnd || tickEnd == 0) {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_BAD_PARAMETER,
                  "invalid from/to values");
    return;
  }
  
  const uint64_t chunkSize = determineChunkSize(); 
  
  // initialise the dump container
  TRI_replication_dump_t dump; 
  TRI_InitDumpReplication(&dump);
  dump._buffer = TRI_CreateSizedStringBuffer(TRI_CORE_MEM_ZONE, (size_t) minChunkSize);

  if (dump._buffer == 0) {
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_OUT_OF_MEMORY);
    return;
  }

  int res = TRI_DumpLogReplication(_vocbase, &dump, tickStart, tickEnd, chunkSize);
  
  TRI_replication_log_state_t state;

  if (res == TRI_ERROR_NO_ERROR) {
    res = TRI_StateReplicationLogger(_vocbase->_replicationLogger, &state);
  }
 
  if (res == TRI_ERROR_NO_ERROR) { 
    const bool checkMore = (dump._lastFoundTick > 0 && dump._lastFoundTick != state._lastLogTick);

    // generate the result
    _response = createResponse(HttpResponse::OK);

    _response->setContentType("application/x-arango-dump; charset=utf-8");

    // set headers
    _response->setHeader(TRI_REPLICATION_HEADER_CHECKMORE, 
                         strlen(TRI_REPLICATION_HEADER_CHECKMORE), 
                         checkMore ? "true" : "false");

    _response->setHeader(TRI_REPLICATION_HEADER_LASTINCLUDED, 
                         strlen(TRI_REPLICATION_HEADER_LASTINCLUDED), 
                         StringUtils::itoa(dump._lastFoundTick));
    
    _response->setHeader(TRI_REPLICATION_HEADER_LASTTICK, 
                         strlen(TRI_REPLICATION_HEADER_LASTTICK), 
                         StringUtils::itoa(state._lastLogTick));
    
    _response->setHeader(TRI_REPLICATION_HEADER_ACTIVE, 
                         strlen(TRI_REPLICATION_HEADER_ACTIVE), 
                         state._active ? "true" : "false");

    // transfer ownership of the buffer contents
    _response->body().appendText(TRI_BeginStringBuffer(dump._buffer), TRI_LengthStringBuffer(dump._buffer));
    // avoid double freeing
    TRI_StealStringBuffer(dump._buffer);
    
    insertClient();
  }
  else {
    generateError(HttpResponse::SERVER_ERROR, res);
  }

  TRI_FreeStringBuffer(TRI_CORE_MEM_ZONE, dump._buffer);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the inventory (current replication and collection state)
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandInventory () {
  assert(_vocbase->_replicationLogger != 0);

  TRI_voc_tick_t tick = TRI_CurrentTickVocBase();
  
  // collections
  TRI_json_t* collections = TRI_InventoryCollectionsVocBase(_vocbase, tick, &filterCollection, NULL);

  if (collections == 0) {
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_OUT_OF_MEMORY);
    return;
  }

  TRI_replication_log_state_t state;

  int res = TRI_StateReplicationLogger(_vocbase->_replicationLogger, &state);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(TRI_CORE_MEM_ZONE, collections);

    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }
    
  TRI_json_t json;
  
  TRI_InitArrayJson(TRI_CORE_MEM_ZONE, &json);

  // add collections data
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, &json, "collections", collections);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, &json, "state", TRI_JsonStateReplicationLogger(&state)); 
  
  generateResult(&json);
  TRI_DestroyJson(TRI_CORE_MEM_ZONE, &json);
    
  insertClient();
}
    
////////////////////////////////////////////////////////////////////////////////
/// @brief handle a dump command for a specific collection
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandDump () {
  char const* collection = _request->value("collection");
    
  if (collection == 0) {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_BAD_PARAMETER,
                  "invalid collection parameter");
    return;
  }
  
  // determine start tick for dump
  TRI_voc_tick_t tickStart = 0;
  TRI_voc_tick_t tickEnd   = (TRI_voc_tick_t) UINT64_MAX;
  bool found;
  char const* value;
  
  value = _request->value("from", found);
  if (found) {
    tickStart = (TRI_voc_tick_t) StringUtils::uint64(value);
  }

  // determine end tick for dump
  value = _request->value("to", found);
  if (found) {
    tickEnd = (TRI_voc_tick_t) StringUtils::uint64(value);
  }

  if (tickStart > tickEnd || tickEnd == 0) {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_BAD_PARAMETER,
                  "invalid from/to values");
    return;
  }
  
  const uint64_t chunkSize = determineChunkSize(); 

  TRI_vocbase_col_t* c = TRI_LookupCollectionByNameVocBase(_vocbase, collection);

  if (c == 0) {
    generateError(HttpResponse::NOT_FOUND, TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
    return;
  }

  const TRI_voc_cid_t cid = c->_cid;

  LOGGER_DEBUG("request collection dump for collection '" << collection << "', "
               "tickStart: " << tickStart << ", tickEnd: " << tickEnd);

  TRI_vocbase_col_t* col = TRI_UseCollectionByIdVocBase(_vocbase, cid);

  if (col == 0) {
    generateError(HttpResponse::NOT_FOUND, TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
    return;
  }
  

  // initialise the dump container
  TRI_replication_dump_t dump; 
  TRI_InitDumpReplication(&dump);
  dump._buffer = TRI_CreateSizedStringBuffer(TRI_CORE_MEM_ZONE, (size_t) minChunkSize);

  if (dump._buffer == 0) {
    TRI_ReleaseCollectionVocBase(_vocbase, col);
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_OUT_OF_MEMORY);

    return;
  }

  int res = TRI_DumpCollectionReplication(&dump, col, tickStart, tickEnd, chunkSize);
  
  TRI_ReleaseCollectionVocBase(_vocbase, col);

  if (res == TRI_ERROR_NO_ERROR) {
    // generate the result
    _response = createResponse(HttpResponse::OK);

    _response->setContentType("application/x-arango-dump; charset=utf-8");

    // set headers
    _response->setHeader(TRI_REPLICATION_HEADER_CHECKMORE, 
                         strlen(TRI_REPLICATION_HEADER_CHECKMORE), 
                         ((dump._hasMore || dump._bufferFull) ? "true" : "false"));

    _response->setHeader(TRI_REPLICATION_HEADER_LASTINCLUDED, 
                         strlen(TRI_REPLICATION_HEADER_LASTINCLUDED), 
                         StringUtils::itoa(dump._lastFoundTick));
    
    // transfer ownership of the buffer contents
    _response->body().appendText(TRI_BeginStringBuffer(dump._buffer), TRI_LengthStringBuffer(dump._buffer));
    // avoid double freeing
    TRI_StealStringBuffer(dump._buffer);
    
    insertClient();
  }
  else {
    generateError(HttpResponse::SERVER_ERROR, res);
  }

  TRI_FreeStringBuffer(TRI_CORE_MEM_ZONE, dump._buffer);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the configuration of the replication applier
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandApplierGetConfig () {
  assert(_vocbase->_replicationApplier != 0);
    
  TRI_replication_apply_configuration_t config;
  TRI_InitConfigurationReplicationApplier(&config);
    
  TRI_ReadLockReadWriteLock(&_vocbase->_replicationApplier->_statusLock);
  TRI_CopyConfigurationReplicationApplier(&_vocbase->_replicationApplier->_configuration, &config);
  TRI_ReadUnlockReadWriteLock(&_vocbase->_replicationApplier->_statusLock);
  
  TRI_json_t* json = TRI_JsonConfigurationReplicationApplier(&config);
  TRI_DestroyConfigurationReplicationApplier(&config);
    
  if (json == 0) {
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_OUT_OF_MEMORY);
    return;
  }
    
  generateResult(json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure the replication applier
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandApplierSetConfig () {
  assert(_vocbase->_replicationApplier != 0);
  
  TRI_replication_apply_configuration_t config;
  TRI_InitConfigurationReplicationApplier(&config);
  
  TRI_json_t* json = parseJsonBody();

  if (json == 0) {
    generateError(HttpResponse::BAD, TRI_ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  const string endpoint = JsonHelper::getStringValue(json, "endpoint", "");

  config._endpoint          = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, endpoint.c_str(), endpoint.size());
  config._requestTimeout    = JsonHelper::getDoubleValue(json, "requestTimeout", config._requestTimeout);
  config._connectTimeout    = JsonHelper::getDoubleValue(json, "connectTimeout", config._connectTimeout);
  config._ignoreErrors      = JsonHelper::getUInt64Value(json, "ignoreErrors", config._ignoreErrors);
  config._maxConnectRetries = JsonHelper::getIntValue(json, "maxConnectRetries", config._maxConnectRetries);
  config._autoStart         = JsonHelper::getBooleanValue(json, "autoStart", config._autoStart);
  config._adaptivePolling   = JsonHelper::getBooleanValue(json, "adaptivePolling", config._adaptivePolling);

  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  int res = TRI_ConfigureReplicationApplier(_vocbase->_replicationApplier, &config);
  
  TRI_DestroyConfigurationReplicationApplier(&config);
    
  if (res != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }
  
  handleCommandApplierGetState();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication applier
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandApplierStart () {
  assert(_vocbase->_replicationApplier != 0);
  
  bool found;
  const char* value = _request->value("fullSync", found);

  bool fullSync;
  if (found) {
    fullSync = StringUtils::boolean(value);
  }
  else {
    fullSync = false;
  }

  int res = TRI_StartReplicationApplier(_vocbase->_replicationApplier, fullSync);
    
  if (res != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }
  
  handleCommandApplierGetState();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication applier
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandApplierStop () {
  assert(_vocbase->_replicationApplier != 0);

  int res = TRI_StopReplicationApplier(_vocbase->_replicationApplier, true);
  
  if (res != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }
    
  handleCommandApplierGetState();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the state of the replication applier
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandApplierGetState () {
  assert(_vocbase->_replicationApplier != 0);

  TRI_json_t* json = TRI_JsonReplicationApplier(_vocbase->_replicationApplier); 
  
  if (json == 0) {  
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_OUT_OF_MEMORY);
    return;
  }
    
  generateResult(json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief delete the state of the replication applier
////////////////////////////////////////////////////////////////////////////////

void RestReplicationHandler::handleCommandApplierDeleteState () {
  assert(_vocbase->_replicationApplier != 0);

  int res = TRI_ForgetReplicationApplier(_vocbase->_replicationApplier);
  
  if (res != TRI_ERROR_NO_ERROR) {
    generateError(HttpResponse::SERVER_ERROR, res);
    return;
  }

  handleCommandApplierGetState();
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
