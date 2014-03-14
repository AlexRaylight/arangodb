////////////////////////////////////////////////////////////////////////////////
/// @brief heartbeat thread
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2013, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "HeartbeatThread.h"
#include "Basics/ConditionLocker.h"
#include "Basics/JsonHelper.h"
#include "BasicsC/logging.h"
#include "Cluster/ClusterInfo.h"
#include "Cluster/ClusterMethods.h"
#include "Cluster/ServerJob.h"
#include "Cluster/ServerState.h"
#include "Dispatcher/ApplicationDispatcher.h"
#include "Dispatcher/Dispatcher.h"
#include "Dispatcher/Job.h"
#include "V8Server/ApplicationV8.h"
#include "V8/v8-globals.h"
#include "V8Server/v8-vocbase.h"
#include "VocBase/auth.h"
#include "VocBase/server.h"
#include "VocBase/vocbase.h"

using namespace triagens::arango;

// -----------------------------------------------------------------------------
// --SECTION--                                                   HeartbeatThread
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a heartbeat thread
////////////////////////////////////////////////////////////////////////////////

HeartbeatThread::HeartbeatThread (TRI_server_t* server,
                                  triagens::rest::ApplicationDispatcher* dispatcher,
                                  ApplicationV8* applicationV8,
                                  uint64_t interval,
                                  uint64_t maxFailsBeforeWarning)
  : Thread("heartbeat"),
    _server(server),
    _dispatcher(dispatcher),
    _applicationV8(applicationV8),
    _agency(),
    _condition(),
    _myId(ServerState::instance()->getId()),
    _interval(interval),
    _maxFailsBeforeWarning(maxFailsBeforeWarning),
    _numFails(0),
    _stop(0),
    _ready(0) {

  assert(_dispatcher != 0);
  allowAsynchronousCancelation();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a heartbeat thread
////////////////////////////////////////////////////////////////////////////////

HeartbeatThread::~HeartbeatThread () {
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    Thread methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief heartbeat main loop
/// the heartbeat thread constantly reports the current server status to the
/// agency. it does so by sending the current state string to the key
/// "Sync/ServerStates/" + my-id.
/// after transferring the current state to the agency, the heartbeat thread
/// will wait for changes on the "Sync/Commands/" + my-id key. If no changes occur,
/// then the request it aborted and the heartbeat thread will go on with 
/// reporting its state to the agency again. If it notices a change when 
/// watching the command key, it will wake up and apply the change locally.
////////////////////////////////////////////////////////////////////////////////

void HeartbeatThread::run () {
  LOG_TRACE("starting heartbeat thread");
  
  uint64_t oldUserVersion = 0;

  // convert timeout to seconds  
  const double interval = (double) _interval / 1000.0 / 1000.0;
  
  // last value of plan that we fetched
  uint64_t lastPlanVersion = 0;

  // value of Sync/Commands/my-id at startup 
  uint64_t lastCommandIndex = getLastCommandIndex(); 
  const bool isCoordinator = ServerState::instance()->isCoordinator();

  if (isCoordinator) {
    ready(true);
  }


  while (! _stop) {
    LOG_TRACE("sending heartbeat to agency");

    const double start = TRI_microtime();

    // send our state to the agency. 
    // we don't care if this fails
    sendState();

    if (_stop) {
      break;
    }

    {
      // send an initial GET request to Sync/Commands/my-id
      AgencyCommResult result = _agency.getValues("Sync/Commands/" + _myId, false);
  
      if (result.successful()) {
        handleStateChange(result, lastCommandIndex);
      }
    }

    if (_stop) {
      break;
    }

    bool shouldSleep = true;
    
    if (isCoordinator) {
      // isCoordinator
      // --------------------
      
      // get the current version of the Plan
      AgencyCommResult result = _agency.getValues("Plan/Version", false);

      if (result.successful()) {
        result.parse("", false);

        std::map<std::string, AgencyCommResultEntry>::iterator it = result._values.begin();

        if (it != result._values.end()) {
          // there is a plan version
          uint64_t planVersion = triagens::basics::JsonHelper::stringUInt64((*it).second._json);
 
          if (planVersion > lastPlanVersion) {
            handlePlanChangeCoordinator(planVersion, lastPlanVersion);
          }
        }
      }

      result.clear();

      result = _agency.getValues("Sync/UserVersion", false);
      if (result.successful()) {
        result.parse("", false);
        std::map<std::string, AgencyCommResultEntry>::iterator it 
            = result._values.begin();
        if (it != result._values.end()) {
          // there is a UserVersion
          uint64_t userVersion = triagens::basics::JsonHelper::stringUInt64((*it).second._json);
          if (userVersion != oldUserVersion) {
            // reload user cache for all databases
            vector<DatabaseID> dbs 
                = ClusterInfo::instance()->listDatabases(true);
            vector<DatabaseID>::iterator i;
            bool allOK = true;
            for (i = dbs.begin(); i != dbs.end(); i++) {
              TRI_vocbase_t* vocbase = TRI_UseCoordinatorDatabaseServer(_server,
                                                    i->c_str());

              if (vocbase != NULL) {
                LOG_INFO("Reloading users for database %s.",vocbase->_name);
                TRI_json_t* json = 0;

                int res = usersOnCoordinator(string(vocbase->_name), 
                                             json);

                if (res == TRI_ERROR_NO_ERROR) {
                  // we were able to read from the _users collection
                  assert(TRI_IsListJson(json));

                  if (json->_value._objects._length == 0) {
                    // no users found, now insert initial default user
                    TRI_InsertInitialAuthInfo(vocbase);
                  }
                  else {
                    // users found in collection, insert them into cache
                    TRI_PopulateAuthInfo(vocbase, json);
                  }
                }
                else if (res == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
                  // could not access _users collection, probably the cluster
                  // was just created... insert initial default user
                  TRI_InsertInitialAuthInfo(vocbase);
                }
                else if (res == TRI_ERROR_INTERNAL) {
                  // something is wrong... probably the database server
                  // with the _users collection is not yet available
                  TRI_InsertInitialAuthInfo(vocbase);
                  allOK = false;
                  // we will not set oldUserVersion such that we will try this
                  // very same exercise again in the next heartbeat
                }
                  
                if (json != 0) {
                  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
                }
                TRI_ReleaseVocBase(vocbase);
              }
            }
            if (allOK) {
              oldUserVersion = userVersion;
            }
          }
        }
      }
    }
    else {
      // ! isCoordinator
      // --------------------

      // get the current version of the Plan
      AgencyCommResult result = _agency.getValues("Plan/Version", false);

      if (result.successful()) {
        const uint64_t agencyIndex = result.index();
        result.parse("", false);
        bool changed = false;

        std::map<std::string, AgencyCommResultEntry>::iterator it = result._values.begin();

        if (it != result._values.end()) {
          // there is a plan version
          uint64_t planVersion = triagens::basics::JsonHelper::stringUInt64((*it).second._json);

          if (planVersion > lastPlanVersion) {
            handlePlanChangeDBServer(planVersion, lastPlanVersion);
            changed = true;
          }
        }

        if (_stop) {
          break;
        }

        if (! changed) {
          const double remain = interval - (TRI_microtime() - start);

          if (remain > 0.0) {
            // watch Plan/Version for changes
            result.clear();

            result = _agency.watchValue("Plan/Version",
                                        agencyIndex + 1,   
                                        remain,
                                        false); 

            if (result.successful()) {
              result.parse("", false);
              it = result._values.begin();

              if (it != result._values.end()) {
                // there is a plan version
                uint64_t planVersion = triagens::basics::JsonHelper::stringUInt64((*it).second._json);

                if (planVersion > lastPlanVersion) {
                  handlePlanChangeDBServer(planVersion, lastPlanVersion);
                  shouldSleep = false;
                }
              }
            }
          }
        }
      }
    }

    if (shouldSleep) {
      double const remain = interval - (TRI_microtime() - start);

      // sleep for a while if apropriate
      if (remain > 0.0) {
        usleep((unsigned long) (remain * 1000.0 * 1000.0));
      }
    }
  }

  // another thread is waiting for this value to appear in order to shut down properly
  _stop = 2;
  
  LOG_TRACE("stopped heartbeat thread");
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises the heartbeat
////////////////////////////////////////////////////////////////////////////////

bool HeartbeatThread::init () {
  // send the server state a first time and use this as an indicator about
  // the agency's health
  if (! sendState()) {
    return false;
  }

  return true;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief fetch the index id of the value of Sync/Commands/my-id from the 
/// agency this index value is determined initially and it is passed to the 
/// watch command (we're waiting for an entry with a higher id)
////////////////////////////////////////////////////////////////////////////////

uint64_t HeartbeatThread::getLastCommandIndex () {
  // get the initial command state  
  AgencyCommResult result = _agency.getValues("Sync/Commands/" + _myId, false);

  if (result.successful()) {
    result.parse("Sync/Commands/", false);
   
    std::map<std::string, AgencyCommResultEntry>::iterator it = result._values.find(_myId);

    if (it != result._values.end()) {
      // found something
      LOG_TRACE("last command index was: '%llu'", (unsigned long long) (*it).second._index);
      return (*it).second._index;
    }
  }
  
  if (result._index > 0) {
    // use the value returned in header X-Etcd-Index
    return result._index;
  }

  // nothing found. this is not an error
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a plan version change, coordinator case
/// this is triggered if the heartbeat thread finds a new plan version number
////////////////////////////////////////////////////////////////////////////////

bool HeartbeatThread::handlePlanChangeCoordinator (uint64_t currentPlanVersion,
                                                   uint64_t& remotePlanVersion) {
  static const string prefix = "Plan/Databases";

  LOG_TRACE("found a plan update");

  // invalidate our local cache
  ClusterInfo::instance()->flush();

  AgencyCommResult result; 

  {   
    AgencyCommLocker locker("Plan", "READ");

    if (locker.successful()) {
      result = _agency.getValues(prefix, true);
    }
  }

  bool mustRetry = false;
  
  if (result.successful()) {
    result.parse(prefix + "/", false);

    vector<TRI_voc_tick_t> ids;

    // loop over all database names we got and create a local database instance if not yet present
    std::map<std::string, AgencyCommResultEntry>::iterator it = result._values.begin();
    while (it != result._values.end()) {
      string const& name = (*it).first;
      TRI_json_t const* options = (*it).second._json;
        
      TRI_voc_tick_t id = 0;
      TRI_json_t const* v = TRI_LookupArrayJson(options, "id");
      if (TRI_IsStringJson(v)) {
        id = triagens::basics::StringUtils::uint64(v->_value._string.data);
      }

      if (id > 0) {
        ids.push_back(id);
      }
      
      TRI_vocbase_t* vocbase = TRI_UseCoordinatorDatabaseServer(_server, name.c_str());

      if (vocbase == 0) {
        // database does not yet exist, create it now

        if (id == 0) {
          // verify that we have an id
          id = ClusterInfo::instance()->uniqid();
        }

        TRI_vocbase_defaults_t defaults;
        TRI_GetDatabaseDefaultsServer(_server, &defaults);

        // create a local database object...
        TRI_CreateCoordinatorDatabaseServer(_server, id, name.c_str(), &defaults, &vocbase);
  
        if (vocbase != 0) {
          // insert initial user(s) for system database

          TRI_json_t* json = 0;
          int res = usersOnCoordinator(string(vocbase->_name), json);

          if (res == TRI_ERROR_NO_ERROR) {
            // we were able to read from the _users collection
            assert(TRI_IsListJson(json));

            if (json->_value._objects._length == 0) {
              // no users found, now insert initial default user
              TRI_InsertInitialAuthInfo(vocbase);
            }
            else {
              // users found in collection, insert them into cache
              TRI_PopulateAuthInfo(vocbase, json);
            }
          }
          else if (res == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
            // could not access _users collection, probably the cluster
            // was just created... insert initial default user
            TRI_InsertInitialAuthInfo(vocbase);
          }
          else if (res == TRI_ERROR_INTERNAL) {
            // something is wrong... probably the database server with the 
            // _users collection is not yet available
            // delete the database again (and try again next time)
            TRI_ReleaseVocBase(vocbase);
            TRI_DropByIdCoordinatorDatabaseServer(_server, vocbase->_id, true);

            mustRetry = true;
          }

          if (json != 0) {
            TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
          }
        }
      }
      else {
        TRI_ReleaseVocBase(vocbase);
      }

      ++it;
    }

   
    TRI_voc_tick_t* localIds = TRI_GetIdsCoordinatorDatabaseServer(_server); 

    if (localIds != 0) {
      TRI_voc_tick_t* p = localIds;

      while (*p != 0) {
        vector<TRI_voc_tick_t>::const_iterator r = std::find(ids.begin(), ids.end(), *p);

        if (r == ids.end()) {
          TRI_DropByIdCoordinatorDatabaseServer(_server, *p, false);
        }

        ++p;
      }

      TRI_Free(TRI_UNKNOWN_MEM_ZONE, localIds);
    }
  }
  else {
    mustRetry = true;
  }  
 
  if (mustRetry) {
    return false;
  }

  remotePlanVersion = currentPlanVersion;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a plan version change, DBServer case
/// this is triggered if the heartbeat thread finds a new plan version number
////////////////////////////////////////////////////////////////////////////////

bool HeartbeatThread::handlePlanChangeDBServer (uint64_t currentPlanVersion,
                                                uint64_t& remotePlanVersion) {
  LOG_TRACE("found a plan update");
  
  // invalidate our local cache
  ClusterInfo::instance()->flush();

  // schedule a job for the change
  triagens::rest::Job* job = new ServerJob(this, _server, _applicationV8);

  assert(job != 0);

  if (_dispatcher->dispatcher()->addJob(job)) {
    remotePlanVersion = currentPlanVersion;

    LOG_TRACE("scheduled plan update handler");
  }
  else {
    LOG_ERROR("could not schedule plan update handler");
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a state change
/// this is triggered if the watch command reports a change
/// when this is called, it will update the index value of the last command
/// (we'll pass the updated index value to the next watches so we don't get
/// notified about this particular change again).
////////////////////////////////////////////////////////////////////////////////
      
bool HeartbeatThread::handleStateChange (AgencyCommResult& result,
                                         uint64_t& lastCommandIndex) {
  result.parse("Sync/Commands/", false);

  std::map<std::string, AgencyCommResultEntry>::const_iterator it = result._values.find(_myId);

  if (it != result._values.end()) {
    lastCommandIndex = (*it).second._index;

    const std::string command = triagens::basics::JsonHelper::getStringValue((*it).second._json, "");
    ServerState::StateEnum newState = ServerState::stringToState(command);
      
    if (newState != ServerState::STATE_UNDEFINED) {
      // state change.
      ServerState::instance()->setState(newState);
      return true;
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sends the current server's state to the agency
////////////////////////////////////////////////////////////////////////////////

bool HeartbeatThread::sendState () {
  const AgencyCommResult result = _agency.sendServerState(
                 8.0 * static_cast<double>(_interval) / 1000.0 / 1000.0);

  if (result.successful()) {
    _numFails = 0;
    return true;

  }
    
  if (++_numFails % _maxFailsBeforeWarning == 0) {
    const std::string endpoints = AgencyComm::getEndpointsString();

    LOG_WARNING("heartbeat could not be sent to agency endpoints (%s): http code: %d, body: %s", 
                endpoints.c_str(),
                result.httpCode(),
                result.body().c_str());
    _numFails = 0;
  }

  return false;
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

