////////////////////////////////////////////////////////////////////////////////
/// @brief V8-vocbase bridge
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "v8-vocbaseprivate.h"

#include "V8/v8-conv.h"
#include "V8/v8-utils.h"
#include "Wal/LogfileManager.h"

#include "VocBase/replication-dump.h"
#include "Replication/InitialSyncer.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::arango;
using namespace triagens::rest;

// -----------------------------------------------------------------------------
// --SECTION--                                                       REPLICATION
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief get the state of the replication logger
////////////////////////////////////////////////////////////////////////////////

static void JS_StateLoggerReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  triagens::wal::LogfileManagerState s = triagens::wal::LogfileManager::instance()->state();

  v8::Handle<v8::Object> result = v8::Object::New(isolate);

  v8::Handle<v8::Object> state = v8::Object::New(isolate);
  state->Set(TRI_V8_STRING("running"),     v8::True(isolate));
  state->Set(TRI_V8_STRING("lastLogTick"), V8TickId(isolate, s.lastTick));
  state->Set(TRI_V8_STRING("totalEvents"), v8::Number::New(isolate, (double) s.numEvents));
  state->Set(TRI_V8_STRING("time"),        TRI_V8_SYMBOL_STD_STRING(s.timeString));
  result->Set(TRI_V8_STRING("state"),      state);

  v8::Handle<v8::Object> server = v8::Object::New(isolate);
  server->Set(TRI_V8_STRING("version"),  TRI_V8_SYMBOL(TRI_VERSION));
  server->Set(TRI_V8_STRING("serverId"), TRI_V8_SYMBOL_STD_STRING(StringUtils::itoa(TRI_GetIdServer()))); /// TODO
  result->Set(TRI_V8_STRING("server"), server);
  
  v8::Handle<v8::Object> clients = v8::Object::New(isolate);
  result->Set(TRI_V8_STRING("clients"), clients);

  TRI_V8_RETURN(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the configuration of the replication logger
////////////////////////////////////////////////////////////////////////////////

static void JS_ConfigureLoggerReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  // the replication logger is actually non-existing in ArangoDB 2.2 and higher
  // as there is the WAL. To be downwards-compatible, we'll return dummy values
  v8::Handle<v8::Object> result = v8::Object::New(isolate);
  result->Set(TRI_V8_STRING("autoStart"),        v8::True(isolate));
  result->Set(TRI_V8_STRING("logRemoteChanges"), v8::True(isolate));
  result->Set(TRI_V8_STRING("maxEvents"),        v8::Number::New(isolate, 0));
  result->Set(TRI_V8_STRING("maxEventsSize"),    v8::Number::New(isolate, 0));

  TRI_V8_RETURN(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the last WAL entries
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_MAINTAINER_MODE
static void JS_LastLoggerReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);
  
  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (args.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE("REPLICATION_LOGGER_LAST(<fromTick>, <toTick>)");
  }

  TRI_replication_dump_t dump(vocbase, 0); 
  TRI_voc_tick_t tickStart = TRI_ObjectToUInt64(args[0], true);
  TRI_voc_tick_t tickEnd = TRI_ObjectToUInt64(args[1], true);

  int res = TRI_DumpLogReplication(&dump, tickStart, tickEnd, true);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(res);
  }

  TRI_json_t* json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, dump._buffer->_buffer);

  if (json == nullptr) {
    TRI_V8_EXCEPTION_MEMORY();
  }

  v8::Handle<v8::Value> result = TRI_ObjectJson(isolate, json);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  
  TRI_V8_RETURN(result);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief sync data from a remote master
////////////////////////////////////////////////////////////////////////////////

static void JS_SynchroniseReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  if (args.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE("REPLICATION_SYNCHRONISE(<config>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // treat the argument as an object from now on
  v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(args[0]);

  string endpoint;
  if (object->Has(TRI_V8_SYMBOL("endpoint"))) {
    endpoint = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("endpoint")));
  }

  string database;
  if (object->Has(TRI_V8_SYMBOL("database"))) {
    database = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("database")));
  }
  else {
    database = string(vocbase->_name);
  }

  string username;
  if (object->Has(TRI_V8_SYMBOL("username"))) {
    username = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("username")));
  }

  string password;
  if (object->Has(TRI_V8_SYMBOL("password"))) {
    password = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("password")));
  }

  map<string, bool> restrictCollections;
  if (object->Has(TRI_V8_SYMBOL("restrictCollections")) && object->Get(TRI_V8_SYMBOL("restrictCollections"))->IsArray()) {
    v8::Handle<v8::Array> a = v8::Handle<v8::Array>::Cast(object->Get(TRI_V8_SYMBOL("restrictCollections")));

    const uint32_t n = a->Length();

    for (uint32_t i = 0; i < n; ++i) {
      v8::Handle<v8::Value> cname = a->Get(i);

      if (cname->IsString()) {
        restrictCollections.insert(pair<string, bool>(TRI_ObjectToString(cname), true));
      }
    }
  }

  string restrictType;
  if (object->Has(TRI_V8_SYMBOL("restrictType"))) {
    restrictType = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("restrictType")));
  }

  bool verbose = true;
  if (object->Has(TRI_V8_SYMBOL("verbose"))) {
    verbose = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("verbose")));
  }

  if (endpoint.empty()) {
    TRI_V8_EXCEPTION_PARAMETER("<endpoint> must be a valid endpoint");
  }

  if ((restrictType.empty() && ! restrictCollections.empty()) ||
      (! restrictType.empty() && restrictCollections.empty()) ||
      (! restrictType.empty() && restrictType != "include" && restrictType != "exclude")) {
    TRI_V8_EXCEPTION_PARAMETER("invalid value for <restrictCollections> or <restrictType>");
  }

  TRI_replication_applier_configuration_t config;
  TRI_InitConfigurationReplicationApplier(&config);
  config._endpoint = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, endpoint.c_str(), endpoint.size());
  config._database = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, database.c_str(), database.size());
  config._username = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, username.c_str(), username.size());
  config._password = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, password.c_str(), password.size());

  if (object->Has(TRI_V8_SYMBOL("chunkSize"))) {
    if (object->Get(TRI_V8_SYMBOL("chunkSize"))->IsNumber()) {
      config._chunkSize = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("chunkSize")), true);
    }
  }

  string errorMsg = "";
  InitialSyncer syncer(vocbase, &config, restrictCollections, restrictType, verbose);
  TRI_DestroyConfigurationReplicationApplier(&config);

  int res = TRI_ERROR_NO_ERROR;
  v8::Handle<v8::Object> result = v8::Object::New(isolate);

  try {
    res = syncer.run(errorMsg);

    result->Set(TRI_V8_SYMBOL("lastLogTick"), V8TickId(isolate, syncer.getLastLogTick()));

    map<TRI_voc_cid_t, string>::const_iterator it;
    map<TRI_voc_cid_t, string> const& c = syncer.getProcessedCollections();

    uint32_t j = 0;
    v8::Handle<v8::Array> collections = v8::Array::New(isolate);
    for (it = c.begin(); it != c.end(); ++it) {
      const string cidString = StringUtils::itoa((*it).first);

      v8::Handle<v8::Object> ci = v8::Object::New(isolate);
      ci->Set(TRI_V8_SYMBOL("id"),   TRI_V8_SYMBOL_STD_STRING(cidString));
      ci->Set(TRI_V8_SYMBOL("name"), TRI_V8_SYMBOL_STD_STRING((*it).second));

      collections->Set(j++, ci);
    }

    result->Set(TRI_V8_SYMBOL("collections"), collections);
  }
  catch (...) {
  }

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(res, "cannot sync from remote endpoint: " + errorMsg);
  }

  TRI_V8_RETURN(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the server's id
////////////////////////////////////////////////////////////////////////////////

static void JS_ServerIdReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  const string serverId = StringUtils::itoa(TRI_GetIdServer());
  TRI_V8_RETURN_STDSTR(serverId);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure the replication applier manually
////////////////////////////////////////////////////////////////////////////////

static void JS_ConfigureApplierReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_INTERNAL);
  }

  if (args.Length() == 0) {
    // no argument: return the current configuration

    TRI_replication_applier_configuration_t config;
    TRI_InitConfigurationReplicationApplier(&config);

    TRI_ReadLockReadWriteLock(&vocbase->_replicationApplier->_statusLock);
    TRI_CopyConfigurationReplicationApplier(&vocbase->_replicationApplier->_configuration, &config);
    TRI_ReadUnlockReadWriteLock(&vocbase->_replicationApplier->_statusLock);

    TRI_json_t* json = TRI_JsonConfigurationReplicationApplier(&config);
    TRI_DestroyConfigurationReplicationApplier(&config);

    if (json == 0) {
      TRI_V8_EXCEPTION_MEMORY();
    }

    v8::Handle<v8::Value> result = TRI_ObjectJson(isolate, json);
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

    TRI_V8_RETURN(result);
  }

  else {
    // set the configuration

    if (args.Length() != 1 || ! args[0]->IsObject()) {
      TRI_V8_EXCEPTION_USAGE("REPLICATION_APPLIER_CONFIGURE(<configuration>)");
    }

    TRI_replication_applier_configuration_t config;
    TRI_InitConfigurationReplicationApplier(&config);

    // fill with previous configuration
    TRI_ReadLockReadWriteLock(&vocbase->_replicationApplier->_statusLock);
    TRI_CopyConfigurationReplicationApplier(&vocbase->_replicationApplier->_configuration, &config);
    TRI_ReadUnlockReadWriteLock(&vocbase->_replicationApplier->_statusLock);


    // treat the argument as an object from now on
    v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(args[0]);

    if (object->Has(TRI_V8_SYMBOL("endpoint"))) {
      if (object->Get(TRI_V8_SYMBOL("endpoint"))->IsString()) {
        string endpoint = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("endpoint")));

        if (config._endpoint != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._endpoint);
        }
        config._endpoint = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, endpoint.c_str(), endpoint.size());
      }
    }

    if (object->Has(TRI_V8_SYMBOL("database"))) {
      if (object->Get(TRI_V8_SYMBOL("database"))->IsString()) {
        string database = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("database")));

        if (config._database != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._database);
        }
        config._database = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, database.c_str(), database.size());
      }
    }
    else {
      if (config._database == 0) {
        // no database set, use current
        config._database = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, vocbase->_name);
      }
    }

    TRI_ASSERT(config._database != 0);

    if (object->Has(TRI_V8_SYMBOL("username"))) {
      if (object->Get(TRI_V8_SYMBOL("username"))->IsString()) {
        string username = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("username")));

        if (config._username != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._username);
        }
        config._username = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, username.c_str(), username.size());
      }
    }

    if (object->Has(TRI_V8_SYMBOL("password"))) {
      if (object->Get(TRI_V8_SYMBOL("password"))->IsString()) {
        string password = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("password")));

        if (config._password != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._password);
        }
        config._password = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, password.c_str(), password.size());
      }
    }

    if (object->Has(TRI_V8_SYMBOL("requestTimeout"))) {
      if (object->Get(TRI_V8_SYMBOL("requestTimeout"))->IsNumber()) {
        config._requestTimeout = TRI_ObjectToDouble(object->Get(TRI_V8_SYMBOL("requestTimeout")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("connectTimeout"))) {
      if (object->Get(TRI_V8_SYMBOL("connectTimeout"))->IsNumber()) {
        config._connectTimeout = TRI_ObjectToDouble(object->Get(TRI_V8_SYMBOL("connectTimeout")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("ignoreErrors"))) {
      if (object->Get(TRI_V8_SYMBOL("ignoreErrors"))->IsNumber()) {
        config._ignoreErrors = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("ignoreErrors")), false);
      }
    }

    if (object->Has(TRI_V8_SYMBOL("maxConnectRetries"))) {
      if (object->Get(TRI_V8_SYMBOL("maxConnectRetries"))->IsNumber()) {
        config._maxConnectRetries = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("maxConnectRetries")), false);
      }
    }

    if (object->Has(TRI_V8_SYMBOL("sslProtocol"))) {
      if (object->Get(TRI_V8_SYMBOL("sslProtocol"))->IsNumber()) {
        config._sslProtocol = (uint32_t) TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("sslProtocol")), false);
      }
    }

    if (object->Has(TRI_V8_SYMBOL("chunkSize"))) {
      if (object->Get(TRI_V8_SYMBOL("chunkSize"))->IsNumber()) {
        config._chunkSize = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("chunkSize")), true);
      }
    }

    if (object->Has(TRI_V8_SYMBOL("autoStart"))) {
      if (object->Get(TRI_V8_SYMBOL("autoStart"))->IsBoolean()) {
        config._autoStart = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("autoStart")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("adaptivePolling"))) {
      if (object->Get(TRI_V8_SYMBOL("adaptivePolling"))->IsBoolean()) {
        config._adaptivePolling = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("adaptivePolling")));
      }
    }

    int res = TRI_ConfigureReplicationApplier(vocbase->_replicationApplier, &config);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_DestroyConfigurationReplicationApplier(&config);
      TRI_V8_EXCEPTION(res);
    }

    TRI_json_t* json = TRI_JsonConfigurationReplicationApplier(&config);
    TRI_DestroyConfigurationReplicationApplier(&config);

    if (json == 0) {
      TRI_V8_EXCEPTION_MEMORY();
    }

    v8::Handle<v8::Value> result = TRI_ObjectJson(isolate, json);
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

    TRI_V8_RETURN(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication applier manually
////////////////////////////////////////////////////////////////////////////////

static void JS_StartApplierReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_INTERNAL);
  }

  if (args.Length() > 1) {
    TRI_V8_EXCEPTION_USAGE("REPLICATION_APPLIER_START(<from>)");
  }

  TRI_voc_tick_t initialTick = 0;
  bool useTick = false;

  if (args.Length() == 1) {
    initialTick = TRI_ObjectToUInt64(args[0], true);
    useTick = true;
  }

  int res = TRI_StartReplicationApplier(vocbase->_replicationApplier,
                                        initialTick,
                                        useTick);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(res, "cannot start replication applier");
  }

  TRI_V8_RETURN_TRUE();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief shuts down the replication applier manually
////////////////////////////////////////////////////////////////////////////////

static void JS_ShutdownApplierReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("REPLICATION_APPLIER_SHUTDOWN()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_INTERNAL);
  }

  int res = TRI_ShutdownReplicationApplier(vocbase->_replicationApplier);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(res, "cannot shut down replication applier");
  }

  TRI_V8_RETURN_TRUE();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the state of the replication applier
////////////////////////////////////////////////////////////////////////////////

static void JS_StateApplierReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("REPLICATION_APPLIER_STATE()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_INTERNAL);
  }

  TRI_json_t* json = TRI_JsonReplicationApplier(vocbase->_replicationApplier);

  if (json == 0) {
    TRI_V8_EXCEPTION(TRI_ERROR_OUT_OF_MEMORY);
  }

  v8::Handle<v8::Value> result = TRI_ObjectJson(isolate, json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  TRI_V8_RETURN(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication applier and "forget" all state
////////////////////////////////////////////////////////////////////////////////

static void JS_ForgetApplierReplication (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("REPLICATION_APPLIER_FORGET()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_INTERNAL);
  }

  int res = TRI_ForgetReplicationApplier(vocbase->_replicationApplier);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(res);
  }

  TRI_V8_RETURN_TRUE();
}



void TRI_InitV8replication (v8::Isolate* isolate,
                            v8::Handle<v8::Context> context,
                            TRI_server_t* server,
                            TRI_vocbase_t* vocbase,
                            JSLoader* loader,
                            const size_t threadNumber,
                            TRI_v8_global_t* v8g){

  // replication functions. not intended to be used by end users
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_LOGGER_STATE", JS_StateLoggerReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_LOGGER_CONFIGURE", JS_ConfigureLoggerReplication, true);
#ifdef TRI_ENABLE_MAINTAINER_MODE
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_LOGGER_LAST", JS_LastLoggerReplication, true);
#endif
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_SYNCHRONISE", JS_SynchroniseReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_SERVER_ID", JS_ServerIdReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_APPLIER_CONFIGURE", JS_ConfigureApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_APPLIER_START", JS_StartApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_APPLIER_SHUTDOWN", JS_ShutdownApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_APPLIER_STATE", JS_StateApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "REPLICATION_APPLIER_FORGET", JS_ForgetApplierReplication, true);
}
