////////////////////////////////////////////////////////////////////////////////
/// @brief replication applier
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

#include "replication-applier.h"

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

// -----------------------------------------------------------------------------
// --SECTION--                                               REPLICATION APPLIER
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Replication
/// @{
////////////////////////////////////////////////////////////////////////////////
  
////////////////////////////////////////////////////////////////////////////////
/// @brief set flag to terminate the applier thread
////////////////////////////////////////////////////////////////////////////////

static void SetTerminateFlag (TRI_replication_applier_t* applier,
                              bool value) {

  TRI_LockCondition(&applier->_runStateChangeCondition);
  applier->_terminateThread = value;
  TRI_UnlockCondition(&applier->_runStateChangeCondition);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether the applier thread should terminate
////////////////////////////////////////////////////////////////////////////////
  
static bool CheckTerminateFlag (TRI_replication_applier_t* applier) {
  bool result;

  TRI_LockSpin(&applier->_threadLock);
  result = applier->_terminateThread;
  TRI_UnlockSpin(&applier->_threadLock);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief read a tick value from a JSON struct
////////////////////////////////////////////////////////////////////////////////
    
static int ReadTick (TRI_json_t const* json,
                     char const* attributeName,
                     TRI_voc_tick_t* dst) {
  TRI_json_t* tick;

  assert(json != NULL);
  assert(json->_type == TRI_JSON_ARRAY);
                                     
  tick = TRI_LookupArrayJson(json, attributeName);

  if (! TRI_IsStringJson(tick)) {
    return TRI_ERROR_REPLICATION_INVALID_APPLIER_STATE;
  }

  *dst = (TRI_voc_tick_t) TRI_UInt64String2(tick->_value._string.data, tick->_value._string.length -1);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the filename of the replication applier configuration file
////////////////////////////////////////////////////////////////////////////////

static char* GetConfigurationFilename (TRI_vocbase_t* vocbase) {
  return TRI_Concatenate2File(vocbase->_path, "REPLICATION-APPLIER-CONFIG");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of the replication applier configuration
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonConfiguration (TRI_replication_applier_configuration_t const* config,
                                      bool includePassword) {
  TRI_json_t* json;

  json = TRI_CreateArray2Json(TRI_CORE_MEM_ZONE, 9);

  if (json == NULL) {
    return NULL;
  }

  if (config->_endpoint != NULL) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                         json, 
                         "endpoint", 
                         TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, config->_endpoint));
  }
  
  if (config->_username != NULL) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                         json, 
                         "username", 
                         TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, config->_username));
  }
  
  if (config->_password != NULL && includePassword) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                         json, 
                         "password", 
                         TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, config->_password));
  }

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "requestTimeout", 
                       TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, config->_requestTimeout));
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "connectTimeout", 
                       TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, config->_connectTimeout));

/* TODO: decide about the fate of this...
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "ignoreErrors", 
                       TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) config->_ignoreErrors));
*/
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "maxConnectRetries", 
                       TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) config->_maxConnectRetries));
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "chunkSize", 
                       TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) config->_chunkSize));
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "autoStart",
                       TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, config->_autoStart));
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "adaptivePolling",
                       TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, config->_adaptivePolling));

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief load the replication application configuration from a file
/// this function must be called under the statusLock
////////////////////////////////////////////////////////////////////////////////

static int LoadConfiguration (TRI_vocbase_t* vocbase,
                              TRI_replication_applier_configuration_t* config) {
  TRI_json_t* json;
  TRI_json_t* value;
  char* filename;
  char* error;
  int res;
   
  TRI_DestroyConfigurationReplicationApplier(config);
  TRI_InitConfigurationReplicationApplier(config);
  filename = GetConfigurationFilename(vocbase);

  if (! TRI_ExistsFile(filename)) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

    return TRI_ERROR_FILE_NOT_FOUND;
  }
  
  error = NULL;
  json  = TRI_JsonFile(TRI_CORE_MEM_ZONE, filename, &error);
  
  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  if (json == NULL || json->_type != TRI_JSON_ARRAY) {
    if (error != NULL) {
      TRI_Free(TRI_CORE_MEM_ZONE, error);
    }
    if (json != NULL) {
      TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
    }

    return TRI_ERROR_REPLICATION_INVALID_APPLIER_CONFIGURATION;
  }

  res = TRI_ERROR_NO_ERROR;

  if (config->_endpoint != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, config->_endpoint);
    config->_endpoint = NULL;
  }
  if (config->_username != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, config->_username);
    config->_username = NULL;
  }
  if (config->_password != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, config->_password);
    config->_password = NULL;
  }

  // read the endpoint
  value = TRI_LookupArrayJson(json, "endpoint");

  if (! TRI_IsStringJson(value)) {
    res = TRI_ERROR_REPLICATION_INVALID_APPLIER_CONFIGURATION;
  }
  else {
    config->_endpoint = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, 
                                              value->_value._string.data, 
                                              value->_value._string.length - 1);
  }

  // read username / password
  value = TRI_LookupArrayJson(json, "username");

  if (TRI_IsStringJson(value)) {
    config->_username = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, 
                                              value->_value._string.data, 
                                              value->_value._string.length - 1);
  }
  
  value = TRI_LookupArrayJson(json, "password");

  if (TRI_IsStringJson(value)) {
    config->_password = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, 
                                              value->_value._string.data, 
                                              value->_value._string.length - 1);
  }

  value = TRI_LookupArrayJson(json, "requestTimeout");

  if (TRI_IsNumberJson(value)) {
    config->_requestTimeout = value->_value._number;
  }
  
  value = TRI_LookupArrayJson(json, "connectTimeout");

  if (TRI_IsNumberJson(value)) {
    config->_connectTimeout = value->_value._number;
  }
  
  value = TRI_LookupArrayJson(json, "maxConnectRetries");

  if (TRI_IsNumberJson(value)) {
    config->_maxConnectRetries = (uint64_t) value->_value._number;
  }
  
  value = TRI_LookupArrayJson(json, "chunkSize");

  if (TRI_IsNumberJson(value)) {
    config->_chunkSize = (uint64_t) value->_value._number;
  }

  value = TRI_LookupArrayJson(json, "autoStart");

  if (TRI_IsBooleanJson(value)) {
    config->_autoStart = value->_value._boolean;
  }
  
  value = TRI_LookupArrayJson(json, "adaptivePolling");

  if (TRI_IsBooleanJson(value)) {
    config->_adaptivePolling = value->_value._boolean;
  }

  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the filename of the replication applier state file
////////////////////////////////////////////////////////////////////////////////

static char* GetStateFilename (TRI_vocbase_t* vocbase) {
  return TRI_Concatenate2File(vocbase->_path, "REPLICATION-APPLIER-STATE");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of the replication applier state
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonApplyState (TRI_replication_applier_state_t const* state) {
  TRI_json_t* json;
  char* serverId;
  char* lastProcessedContinuousTick;
  char* lastAppliedContinuousTick;

  json = TRI_CreateArray2Json(TRI_CORE_MEM_ZONE, 4);

  if (json == NULL) {
    return NULL;
  }

  lastProcessedContinuousTick = TRI_StringUInt64(state->_lastProcessedContinuousTick);
  lastAppliedContinuousTick   = TRI_StringUInt64(state->_lastAppliedContinuousTick);
  serverId                    = TRI_StringUInt64(state->_serverId);
 
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "serverId", 
                       TRI_CreateStringJson(TRI_CORE_MEM_ZONE, serverId));

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "lastProcessedContinuousTick", 
                       TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastProcessedContinuousTick));

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, 
                       json, 
                       "lastAppliedContinuousTick", 
                       TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastAppliedContinuousTick));

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register an applier error, without locking
////////////////////////////////////////////////////////////////////////////////

static int SetError (TRI_replication_applier_t* applier,
                     int errorCode,
                     char const* msg) {
  TRI_replication_applier_state_t* state;
  char const* realMsg;

  if (msg == NULL || strlen(msg) == 0) {
    realMsg = TRI_errno_string(errorCode);
  }
  else {
    realMsg = msg;
  }

  // log error message
  if (errorCode != TRI_ERROR_REPLICATION_NO_RESPONSE &&
      errorCode != TRI_ERROR_REPLICATION_APPLIER_STOPPED) {
    LOG_WARNING("replication applier error for database '%s': %s", applier->_databaseName, realMsg);
  }

  state = &applier->_state;
  state->_lastError._code = errorCode;
  
  TRI_GetTimeStampReplication(state->_lastError._time, sizeof(state->_lastError._time) - 1);

  if (state->_lastError._msg != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, state->_lastError._msg);
  }

  state->_lastError._msg = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, realMsg);

  return errorCode;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief applier thread main function
////////////////////////////////////////////////////////////////////////////////

void ApplyThread (void* data) {
  TRI_RunContinuousSyncerReplication(data); 
  TRI_DeleteContinuousSyncerReplication(data);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication applier
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int StartApplier (TRI_replication_applier_t* applier,
                         TRI_voc_tick_t initialTick,
                         bool useTick) {
  TRI_replication_applier_state_t* state;
  void* fetcher;

  state = &applier->_state;

  if (state->_active) {
    return TRI_ERROR_INTERNAL;
  }

  if (applier->_configuration._endpoint == NULL) {
    return SetError(applier, TRI_ERROR_REPLICATION_INVALID_APPLIER_CONFIGURATION, "no endpoint configured");
  }
  
  fetcher = (void*) TRI_CreateContinuousSyncerReplication(applier->_vocbase, 
                                                          &applier->_configuration,
                                                          initialTick,
                                                          useTick);

  if (fetcher == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
 
  // reset error 
  if (state->_lastError._msg != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, state->_lastError._msg);
    state->_lastError._msg = NULL;
  }

  state->_lastError._code = TRI_ERROR_NO_ERROR;
  
  TRI_GetTimeStampReplication(state->_lastError._time, sizeof(state->_lastError._time) - 1);

  
  SetTerminateFlag(applier, false); 
  state->_active = true;
  
  TRI_InitThread(&applier->_thread);

  if (! TRI_StartThread(&applier->_thread, "[applier]", ApplyThread, fetcher)) {
    TRI_DeleteContinuousSyncerReplication(fetcher);

    return TRI_ERROR_INTERNAL;
  }

  LOG_INFO("started replication applier for database '%s'",
           applier->_databaseName);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication applier
/// note: must hold the lock when calling this
////////////////////////////////////////////////////////////////////////////////

static int StopApplier (TRI_replication_applier_t* applier, 
                        bool resetError) {
  TRI_replication_applier_state_t* state;

  state = &applier->_state;

  if (! state->_active) {
    return TRI_ERROR_INTERNAL;
  }
  
  state->_active = false;

  SetTerminateFlag(applier, true);

  TRI_SetProgressReplicationApplier(applier, "applier stopped", false);
 
  if (resetError) { 
    if (state->_lastError._msg != NULL) {
      TRI_FreeString(TRI_CORE_MEM_ZONE, state->_lastError._msg);
      state->_lastError._msg = NULL;
    }

    state->_lastError._code = TRI_ERROR_NO_ERROR;
  
    TRI_GetTimeStampReplication(state->_lastError._time, sizeof(state->_lastError._time) - 1);
  }
  
  TRI_LockCondition(&applier->_runStateChangeCondition);
  TRI_SignalCondition(&applier->_runStateChangeCondition);
  TRI_UnlockCondition(&applier->_runStateChangeCondition);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of an applier state
////////////////////////////////////////////////////////////////////////////////
  
static TRI_json_t* JsonState (TRI_replication_applier_state_t const* state) {
  TRI_json_t* json;
  TRI_json_t* last;
  TRI_json_t* progress;
  TRI_json_t* error;
  char* lastString;
  char timeString[24];
  
  json = TRI_CreateArray2Json(TRI_CORE_MEM_ZONE, 9);

  // add replication state
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "running", TRI_CreateBooleanJson(TRI_CORE_MEM_ZONE, state->_active));
 
  // lastAppliedContinuousTick 
  if (state->_lastAppliedContinuousTick > 0) {
    lastString = TRI_StringUInt64(state->_lastAppliedContinuousTick);
    last = TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastString);
  }
  else {
    last = TRI_CreateNullJson(TRI_CORE_MEM_ZONE);
  }
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "lastAppliedContinuousTick", last);
  
  // lastProcessedContinuousTick 
  if (state->_lastProcessedContinuousTick > 0) {
    lastString = TRI_StringUInt64(state->_lastProcessedContinuousTick);
    last = TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastString);
  }
  else {
    last = TRI_CreateNullJson(TRI_CORE_MEM_ZONE);
  }
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "lastProcessedContinuousTick", last);
  
  // lastAvailableContinuousTick 
  if (state->_lastAvailableContinuousTick > 0) {
    lastString = TRI_StringUInt64(state->_lastAvailableContinuousTick);
    last = TRI_CreateStringJson(TRI_CORE_MEM_ZONE, lastString);
  }
  else {
    last = TRI_CreateNullJson(TRI_CORE_MEM_ZONE);
  }
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "lastAvailableContinuousTick", last);
  
  // progress
  progress = TRI_CreateArray2Json(TRI_CORE_MEM_ZONE, 2);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, progress, "time", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, state->_progressTime));

  if (state->_progressMsg != NULL) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, progress, "message", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, state->_progressMsg));
  }
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, progress, "failedConnects", TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) state->_failedConnects));

  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "progress", progress); 
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "totalRequests", TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) state->_totalRequests));
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "totalFailedConnects", TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) state->_totalFailedConnects));
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "totalEvents", TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) state->_totalEvents));

  // lastError
  error = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (error != NULL) {
    if (state->_lastError._code > 0) {
      TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, error, "time", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, state->_lastError._time));
 
      if (state->_lastError._msg != NULL) {
        TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, error, "errorMessage", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, state->_lastError._msg));
      }
    }

    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, error, "errorNum", TRI_CreateNumberJson(TRI_CORE_MEM_ZONE, (double) state->_lastError._code));
  
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "lastError", error);
  }
  
  TRI_GetTimeStampReplication(timeString, sizeof(timeString) - 1);
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "time", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, timeString));
  
  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Replication
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create a replication applier
////////////////////////////////////////////////////////////////////////////////

TRI_replication_applier_t* TRI_CreateReplicationApplier (TRI_vocbase_t* vocbase) {
  TRI_replication_applier_t* applier;
  int res;

  applier = TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_replication_applier_t), false);

  if (applier == NULL) {
    return NULL;
  }
  
  TRI_InitConfigurationReplicationApplier(&applier->_configuration);
  TRI_InitStateReplicationApplier(&applier->_state);

  res = LoadConfiguration(vocbase, &applier->_configuration);
  
  if (res != TRI_ERROR_NO_ERROR && 
      res != TRI_ERROR_FILE_NOT_FOUND) {
    TRI_set_errno(res);
    TRI_DestroyStateReplicationApplier(&applier->_state);
    TRI_DestroyConfigurationReplicationApplier(&applier->_configuration);
    TRI_Free(TRI_CORE_MEM_ZONE, applier);

    return NULL;
  }

  res = TRI_LoadStateReplicationApplier(vocbase, &applier->_state);

  if (res != TRI_ERROR_NO_ERROR && 
      res != TRI_ERROR_FILE_NOT_FOUND) {
    TRI_set_errno(res);
    TRI_DestroyStateReplicationApplier(&applier->_state);
    TRI_DestroyConfigurationReplicationApplier(&applier->_configuration);
    TRI_Free(TRI_CORE_MEM_ZONE, applier);

    return NULL;
  }
  
  TRI_InitReadWriteLock(&applier->_statusLock);
  TRI_InitSpin(&applier->_threadLock);
  TRI_InitCondition(&applier->_runStateChangeCondition);

  applier->_vocbase      = vocbase;
  applier->_databaseName = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, vocbase->_name);
  
  SetTerminateFlag(applier, false); 

  assert(applier->_databaseName != NULL);

  TRI_SetProgressReplicationApplier(applier, "applier created", false);
  
  return applier;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a replication applier
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyReplicationApplier (TRI_replication_applier_t* applier) {
  TRI_StopReplicationApplier(applier, true);
  
  TRI_DestroyStateReplicationApplier(&applier->_state);
  TRI_DestroyConfigurationReplicationApplier(&applier->_configuration);
  TRI_FreeString(TRI_CORE_MEM_ZONE, applier->_databaseName);
  TRI_DestroyCondition(&applier->_runStateChangeCondition);
  TRI_DestroySpin(&applier->_threadLock);
  TRI_DestroyReadWriteLock(&applier->_statusLock);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a replication applier
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeReplicationApplier (TRI_replication_applier_t* applier) {
  TRI_DestroyReplicationApplier(applier);
  TRI_Free(TRI_CORE_MEM_ZONE, applier);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Replication
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief checks whether the applier thread should terminate
////////////////////////////////////////////////////////////////////////////////
   
bool TRI_WaitReplicationApplier (TRI_replication_applier_t* applier,
                                 uint64_t sleepTime) {
  if (CheckTerminateFlag(applier)) {
    return false;
  }

  if (sleepTime > 0) {
    LOG_TRACE("replication applier going to sleep for %llu ns", (unsigned long long) sleepTime);

    TRI_LockCondition(&applier->_runStateChangeCondition);
    TRI_TimedWaitCondition(&applier->_runStateChangeCondition, sleepTime);
    TRI_UnlockCondition(&applier->_runStateChangeCondition);

    if (CheckTerminateFlag(applier)) {
      return false;
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of the replication applier configuration
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_JsonConfigurationReplicationApplier (TRI_replication_applier_configuration_t const* config) {
  return JsonConfiguration(config, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication applier
////////////////////////////////////////////////////////////////////////////////

int TRI_StartReplicationApplier (TRI_replication_applier_t* applier,
                                 TRI_voc_tick_t initialTick,
                                 bool useTick) {
  int res;
  
  res = TRI_ERROR_NO_ERROR;

  LOG_TRACE("requesting replication applier start. initialTick: %llu, useTick: %d",
            (unsigned long long) initialTick,
            (int) useTick);

  // wait until previous applier thread is shut down
  while (! TRI_WaitReplicationApplier(applier, 10 * 1000));
  
  TRI_WriteLockReadWriteLock(&applier->_statusLock);

  if (! applier->_state._active) {
    res = StartApplier(applier, initialTick, useTick);
  }
  
  TRI_WriteUnlockReadWriteLock(&applier->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication applier
////////////////////////////////////////////////////////////////////////////////

int TRI_StopReplicationApplier (TRI_replication_applier_t* applier,
                                bool resetError) {
  int res;

  res = TRI_ERROR_NO_ERROR;
  
  LOG_TRACE("requesting replication applier stop");
 
  TRI_WriteLockReadWriteLock(&applier->_statusLock);

  if (! applier->_state._active) {
    TRI_WriteUnlockReadWriteLock(&applier->_statusLock);

    return res;
  }

  res = StopApplier(applier, resetError);
  TRI_WriteUnlockReadWriteLock(&applier->_statusLock);
 
  // join the thread without the status lock (otherwise it would propbably not join) 
  TRI_JoinThread(&applier->_thread);

  
  SetTerminateFlag(applier, false);

  LOG_INFO("stopped replication applier for database '%s'",
           applier->_databaseName);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure the replication applier
////////////////////////////////////////////////////////////////////////////////

int TRI_ConfigureReplicationApplier (TRI_replication_applier_t* applier,
                                     TRI_replication_applier_configuration_t const* config) {
  int res;

  res = TRI_ERROR_NO_ERROR;

  if (config->_endpoint == NULL || strlen(config->_endpoint) == 0) {
    // no endpoint
    return TRI_ERROR_REPLICATION_INVALID_APPLIER_CONFIGURATION;
  }

  TRI_WriteLockReadWriteLock(&applier->_statusLock);

  if (applier->_state._active) {
    // cannot change the configuration while the replication is still running
    TRI_WriteUnlockReadWriteLock(&applier->_statusLock);

    return TRI_ERROR_REPLICATION_RUNNING;
  }

  res = TRI_SaveConfigurationReplicationApplier(applier->_vocbase, config, true);

  if (res == TRI_ERROR_NO_ERROR) {
    res = LoadConfiguration(applier->_vocbase, &applier->_configuration);
  }
  
  TRI_WriteUnlockReadWriteLock(&applier->_statusLock);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the current replication applier state
////////////////////////////////////////////////////////////////////////////////

int TRI_StateReplicationApplier (TRI_replication_applier_t* applier,
                                 TRI_replication_applier_state_t* state) {
  TRI_InitStateReplicationApplier(state);

  TRI_ReadLockReadWriteLock(&applier->_statusLock);
  
  state->_active                      = applier->_state._active;
  state->_lastAppliedContinuousTick   = applier->_state._lastAppliedContinuousTick;
  state->_lastProcessedContinuousTick = applier->_state._lastProcessedContinuousTick;
  state->_lastAvailableContinuousTick = applier->_state._lastAvailableContinuousTick;
  state->_serverId                    = applier->_state._serverId;
  state->_lastError._code             = applier->_state._lastError._code;
  state->_failedConnects              = applier->_state._failedConnects;
  state->_totalRequests               = applier->_state._totalRequests;
  state->_totalFailedConnects         = applier->_state._totalFailedConnects;
  state->_totalEvents                 = applier->_state._totalEvents;
  memcpy(&state->_lastError._time, &applier->_state._lastError._time, sizeof(state->_lastError._time));

  if (applier->_state._progressMsg != NULL) {
    state->_progressMsg = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, applier->_state._progressMsg);
  }
  else {
    state->_progressMsg = NULL;
  }

  memcpy(&state->_progressTime, &applier->_state._progressTime, sizeof(state->_progressTime));
  
  if (applier->_state._lastError._msg != NULL) {
    state->_lastError._msg          = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, applier->_state._lastError._msg);
  }
  else {
    state->_lastError._msg          = NULL;
  }

  TRI_ReadUnlockReadWriteLock(&applier->_statusLock);

  return TRI_ERROR_NO_ERROR;
} 

////////////////////////////////////////////////////////////////////////////////
/// @brief get a JSON representation of an applier
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_JsonReplicationApplier (TRI_replication_applier_t* applier) {
  TRI_replication_applier_state_t state;
  TRI_replication_applier_configuration_t config;
  TRI_json_t* server;
  TRI_json_t* json;
  int res;

  res = TRI_StateReplicationApplier(applier, &state);

  if (res != TRI_ERROR_NO_ERROR) {
    return NULL;
  }
    
  json = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (json == NULL) {
    TRI_DestroyStateReplicationApplier(&state);

    return NULL;
  }
  
  TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "state", JsonState(&state));
    
  // add server info
  server = TRI_CreateArrayJson(TRI_CORE_MEM_ZONE);

  if (server != NULL) {
    TRI_server_id_t serverId;

    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, server, "version", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, TRIAGENS_VERSION));

    serverId = TRI_GetServerId();  
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, server, "serverId", TRI_CreateStringJson(TRI_CORE_MEM_ZONE, TRI_StringUInt64(serverId)));

    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "server", server);
  }
   
  TRI_InitConfigurationReplicationApplier(&config);

  TRI_ReadLockReadWriteLock(&applier->_statusLock);
  TRI_CopyConfigurationReplicationApplier(&applier->_configuration, &config);
  TRI_ReadUnlockReadWriteLock(&applier->_statusLock);

  if (config._endpoint != NULL) {
    TRI_Insert3ArrayJson(TRI_CORE_MEM_ZONE, json, "endpoint", TRI_CreateStringCopyJson(TRI_CORE_MEM_ZONE, config._endpoint));
  }

  TRI_DestroyConfigurationReplicationApplier(&config);
  TRI_DestroyStateReplicationApplier(&state);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register an applier error
////////////////////////////////////////////////////////////////////////////////

int TRI_SetErrorReplicationApplier (TRI_replication_applier_t* applier,
                                    int errorCode,
                                    char const* msg) {
  TRI_WriteLockReadWriteLock(&applier->_statusLock);

  SetError(applier, errorCode, msg);

  TRI_WriteUnlockReadWriteLock(&applier->_statusLock);

  return errorCode;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief set the progress with or without a lock
////////////////////////////////////////////////////////////////////////////////

void TRI_SetProgressReplicationApplier (TRI_replication_applier_t* applier,
                                        char const* msg,
                                        bool lock) {
  char* copy;

  copy = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, msg);

  if (copy == NULL) {
    return;
  }
  
  if (lock) {
    TRI_WriteLockReadWriteLock(&applier->_statusLock);
  }

  if (applier->_state._progressMsg != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, applier->_state._progressMsg);
  }

  applier->_state._progressMsg = copy;
  
  // write time in buffer
  TRI_GetTimeStampReplication(applier->_state._progressTime, sizeof(applier->_state._progressTime) - 1);

  if (lock) {
    TRI_WriteUnlockReadWriteLock(&applier->_statusLock);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise an applier state struct
////////////////////////////////////////////////////////////////////////////////

void TRI_InitStateReplicationApplier (TRI_replication_applier_state_t* state) {
  memset(state, 0, sizeof(TRI_replication_applier_state_t));

  state->_active             = false;

  state->_lastError._code    = TRI_ERROR_NO_ERROR;
  state->_lastError._msg     = NULL;
  state->_lastError._time[0] = '\0';
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy an applier state struct
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyStateReplicationApplier (TRI_replication_applier_state_t* state) {
  if (state->_progressMsg != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, state->_progressMsg);
  }

  if (state->_lastError._msg != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, state->_lastError._msg);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove the replication application state file
////////////////////////////////////////////////////////////////////////////////

int TRI_RemoveStateReplicationApplier (TRI_vocbase_t* vocbase) {
  char* filename;
  int res;

  filename = GetStateFilename(vocbase);

  if (filename == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  if (TRI_ExistsFile(filename)) {
    LOG_TRACE("removing replication state file '%s'", filename);
    res = TRI_UnlinkFile(filename);
  }
  else {
    res = TRI_ERROR_NO_ERROR;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief save the replication application state to a file
////////////////////////////////////////////////////////////////////////////////

int TRI_SaveStateReplicationApplier (TRI_vocbase_t* vocbase,
                                     TRI_replication_applier_state_t const* state,
                                     bool sync) {
  TRI_json_t* json;
  char* filename;
  int res;

  json = JsonApplyState(state);

  if (json == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  filename = GetStateFilename(vocbase);
  LOG_TRACE("saving replication applier state to file '%s'", filename);

  if (! TRI_SaveJson(filename, json, sync)) {
    res = TRI_errno();
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

int TRI_LoadStateReplicationApplier (TRI_vocbase_t* vocbase,
                                     TRI_replication_applier_state_t* state) {
  TRI_json_t* json;
  TRI_json_t* serverId;
  char* filename;
  char* error;
  int res;
  
  TRI_InitStateReplicationApplier(state);
  filename = GetStateFilename(vocbase);

  if (filename == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  LOG_TRACE("looking for replication state file '%s'", filename);

  if (! TRI_ExistsFile(filename)) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

    return TRI_ERROR_FILE_NOT_FOUND;
  }
  
  LOG_TRACE("replication state file '%s' found", filename);

  error = NULL;
  json  = TRI_JsonFile(TRI_CORE_MEM_ZONE, filename, &error);
  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  if (json == NULL || json->_type != TRI_JSON_ARRAY) {
    if (error != NULL) {
      TRI_Free(TRI_CORE_MEM_ZONE, error);
    }
    if (json != NULL) {
      TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
    }

    return TRI_ERROR_REPLICATION_INVALID_APPLIER_STATE;
  }

  res = TRI_ERROR_NO_ERROR;

  // read the server id
  serverId = TRI_LookupArrayJson(json, "serverId");

  if (! TRI_IsStringJson(serverId)) {
    res = TRI_ERROR_REPLICATION_INVALID_APPLIER_STATE;
  }
  else {
    state->_serverId = TRI_UInt64String2(serverId->_value._string.data, 
                                         serverId->_value._string.length - 1);
  }

  if (res == TRI_ERROR_NO_ERROR) {
    // read the ticks
    res |= ReadTick(json, "lastAppliedContinuousTick", &state->_lastAppliedContinuousTick); 

    // set processed = applied
    state->_lastProcessedContinuousTick = state->_lastAppliedContinuousTick;
  }

  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
  
  LOG_TRACE("replication state file read successfully");

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise an applier configuration
////////////////////////////////////////////////////////////////////////////////

void TRI_InitConfigurationReplicationApplier (TRI_replication_applier_configuration_t* config) {
  memset(config, 0, sizeof(TRI_replication_applier_configuration_t));

  config->_endpoint          = NULL;
  config->_username          = NULL;
  config->_password          = NULL;
  config->_requestTimeout    = 300.0;
  config->_connectTimeout    = 10.0;
  config->_maxConnectRetries = 100;
  config->_autoStart         = false;
  config->_chunkSize         = 0;
  config->_adaptivePolling   = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy an applier configuration
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyConfigurationReplicationApplier (TRI_replication_applier_configuration_t* config) {
  if (config->_endpoint != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, config->_endpoint);
    config->_endpoint = NULL;
  }
  
  if (config->_username != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, config->_username);
    config->_username = NULL;
  }
  
  if (config->_password != NULL) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, config->_password);
    config->_password = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief copy an applier configuration
////////////////////////////////////////////////////////////////////////////////

void TRI_CopyConfigurationReplicationApplier (TRI_replication_applier_configuration_t const* src,
                                              TRI_replication_applier_configuration_t* dst) {
  if (src->_endpoint != NULL) {
    dst->_endpoint = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, src->_endpoint);
  }
  else {
    dst->_endpoint = NULL;
  }

  if (src->_username != NULL) {
    dst->_username = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, src->_username);
  }
  else {
    dst->_username = NULL;
  }

  if (src->_password != NULL) {
    dst->_password = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, src->_password);
  }
  else {
    dst->_password = NULL;
  }

  dst->_requestTimeout    = src->_requestTimeout;
  dst->_connectTimeout    = src->_connectTimeout;
  dst->_ignoreErrors      = src->_ignoreErrors;
  dst->_maxConnectRetries = src->_maxConnectRetries;
  dst->_chunkSize         = src->_chunkSize;
  dst->_autoStart         = src->_autoStart;
  dst->_adaptivePolling   = src->_adaptivePolling;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove the replication application configuration file
////////////////////////////////////////////////////////////////////////////////

int TRI_RemoveConfigurationReplicationApplier (TRI_vocbase_t* vocbase) {
  char* filename;
  int res;

  filename = GetConfigurationFilename(vocbase);

  if (filename == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  if (TRI_ExistsFile(filename)) {
    res = TRI_UnlinkFile(filename);
  }
  else {
    res = TRI_ERROR_NO_ERROR;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief save the replication application configuration to a file
////////////////////////////////////////////////////////////////////////////////

int TRI_SaveConfigurationReplicationApplier (TRI_vocbase_t* vocbase,
                                             TRI_replication_applier_configuration_t const* config,
                                             bool sync) {
  TRI_json_t* json;
  char* filename;
  int res;

  json = JsonConfiguration(config, true);

  if (json == NULL) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  filename = GetConfigurationFilename(vocbase);

  if (! TRI_SaveJson(filename, json, sync)) {
    res = TRI_errno();
  }
  else {
    res = TRI_ERROR_NO_ERROR;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the applier and "forget" everything
////////////////////////////////////////////////////////////////////////////////
  
int TRI_ForgetReplicationApplier (TRI_replication_applier_t* applier) {
  int res;

  res = TRI_StopReplicationApplier(applier, true);

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  TRI_RemoveStateReplicationApplier(applier->_vocbase);
  TRI_DestroyStateReplicationApplier(&applier->_state);
  TRI_InitStateReplicationApplier(&applier->_state);
  
  TRI_RemoveConfigurationReplicationApplier(applier->_vocbase);
  TRI_DestroyConfigurationReplicationApplier(&applier->_configuration);
  TRI_InitConfigurationReplicationApplier(&applier->_configuration);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
