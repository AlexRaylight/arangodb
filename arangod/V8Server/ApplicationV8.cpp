////////////////////////////////////////////////////////////////////////////////
/// @brief V8 engine configuration
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

#include "ApplicationV8.h"

#include "Basics/ConditionLocker.h"
#include "Basics/FileUtils.h"
#include "Basics/MutexLocker.h"
#include "Basics/Mutex.h"
#include "Basics/ReadLocker.h"
#include "Basics/StringUtils.h"
#include "Basics/Thread.h"
#include "Basics/WriteLocker.h"
#include "BasicsC/logging.h"
#include "V8/v8-buffer.h"
#include "V8/v8-conv.h"
#include "V8/v8-shell.h"
#include "V8/v8-utils.h"
#include "V8Server/v8-actions.h"
#include "V8Server/v8-query.h"
#include "V8Server/v8-vocbase.h"
#include "VocBase/server.h"
#include "Actions/actions.h"

using namespace triagens;
using namespace triagens::basics;
using namespace triagens::arango;
using namespace std;

// -----------------------------------------------------------------------------
// --SECTION--                                                  class V8GcThread
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

namespace {

////////////////////////////////////////////////////////////////////////////////
/// @brief garbage collector
////////////////////////////////////////////////////////////////////////////////

  class V8GcThread : public Thread {
    public:
      V8GcThread (ApplicationV8* applicationV8)
        : Thread("v8-gc"),
          _applicationV8(applicationV8),
          _lock(),
          _lastGcStamp(TRI_microtime()) {
      }

    public:

////////////////////////////////////////////////////////////////////////////////
/// @brief collect garbage in an endless loop (main functon of GC thread)
////////////////////////////////////////////////////////////////////////////////

      void run () {
        _applicationV8->collectGarbage();
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the timestamp of the last GC
////////////////////////////////////////////////////////////////////////////////

      double getLastGcStamp () {
        READ_LOCKER(_lock);
        return _lastGcStamp;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief set the global GC timestamp
////////////////////////////////////////////////////////////////////////////////

      void updateGcStamp (double value) {
        WRITE_LOCKER(_lock);
        _lastGcStamp = value;
      }

    private:
      ApplicationV8* _applicationV8;
      ReadWriteLock _lock;
      double _lastGcStamp;
  };
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a global method
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::V8Context::addGlobalContextMethod (string const& method) {
  MUTEX_LOCKER(_globalMethodsLock);

  _globalMethods.push_back(method);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes all global methods
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::V8Context::handleGlobalContextMethods () {
  v8::HandleScope scope;
  
  MUTEX_LOCKER(_globalMethodsLock);

  for (vector<string>::iterator i = _globalMethods.begin();  i != _globalMethods.end();  ++i) {
    string const& func = *i;

    LOG_DEBUG("executing global context methods '%s' for context %d", func.c_str(), (int) _id);

    TRI_ExecuteJavaScriptString(_context,
                                v8::String::New(func.c_str()),
                                v8::String::New("global context method"),
                                false);
  }

  _globalMethods.clear();
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                               class ApplicationV8
// -----------------------------------------------------------------------------

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

ApplicationV8::ApplicationV8 (TRI_server_t* server) 
  : ApplicationFeature("V8"),
    _server(server),
    _startupPath(),
    _modulesPath(),
    _packagePath(),
    _actionPath(),
    _appPath(),
    _devAppPath(),
    _useActions(true),
    _developmentMode(false),
    _performUpgrade(false),
    _skipUpgrade(false),
    _gcInterval(1000),
    _gcFrequency(10.0),
    _v8Options(""),
    _startupLoader(),
    _actionLoader(),
    _vocbase(0),
    _nrInstances(0),
    _contexts(0),
    _contextCondition(),
    _freeContexts(),
    _dirtyContexts(),
    _busyContexts(),
    _stopping(0),
    _gcThread(0) {

  assert(_server != 0);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::~ApplicationV8 () {
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the concurrency
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setConcurrency (size_t n) {
  _nrInstances = n;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the database
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setVocbase (TRI_vocbase_t* vocbase) {
  _vocbase = vocbase;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief perform a database upgrade
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::performUpgrade () {
  _performUpgrade = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief skip a database upgrade
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::skipUpgrade () {
  _skipUpgrade = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enters a context
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::V8Context* ApplicationV8::enterContext (TRI_vocbase_s* vocbase, 
                                                       bool initialise,
                                                       bool allowUseDatabase) {
  CONDITION_LOCKER(guard, _contextCondition);

  while (_freeContexts.empty() && ! _stopping) {
    LOG_DEBUG("waiting for unused V8 context");
    guard.wait();
  }

  // in case we are in the shutdown phase, do not enter a context!
  // the context might have been deleted by the shutdown
  if (_stopping) {
    return 0;
  }

  LOG_TRACE("found unused V8 context");

  V8Context* context = _freeContexts.back();

  assert(context != 0);
  assert(context->_isolate != 0);

  _freeContexts.pop_back();
  _busyContexts.insert(context);

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();
  
  // set the current database
  v8::HandleScope scope;
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) context->_isolate->GetData();  
  v8g->_vocbase = vocbase;
  v8g->_allowUseDatabase = allowUseDatabase;
  

  LOG_TRACE("entering V8 context %d", (int) context->_id);
  context->handleGlobalContextMethods();

  if (_developmentMode && ! initialise) {
    v8::HandleScope scope;

    TRI_ExecuteJavaScriptString(context->_context,
                                v8::String::New("require(\"internal\").resetEngine()"),
                                v8::String::New("global context method"),
                                false);
  }

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief exists an context
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::exitContext (V8Context* context) {
  V8GcThread* gc = dynamic_cast<V8GcThread*>(_gcThread);
  assert(gc != 0);

  LOG_TRACE("leaving V8 context %d", (int) context->_id);
  double lastGc = gc->getLastGcStamp();

  CONDITION_LOCKER(guard, _contextCondition);

  context->handleGlobalContextMethods();

  ++context->_dirt;

  // exit the context
  context->_context->Exit();
  context->_isolate->Exit();
  delete context->_locker;


  bool performGarbageCollection;

  if (context->_lastGcStamp + _gcFrequency < lastGc) {
    LOG_TRACE("V8 context has reached GC timeout threshold and will be scheduled for GC");
    performGarbageCollection = true;
  }
  else if (context->_dirt >= _gcInterval) {
    LOG_TRACE("V8 context has reached maximum number of requests and will be scheduled for GC");
    performGarbageCollection = true;
  }
  else {
    performGarbageCollection = false;
  }

  if (performGarbageCollection) {
    _dirtyContexts.push_back(context);
    _busyContexts.erase(context);
  }
  else {
    _freeContexts.push_back(context);
    _busyContexts.erase(context);
  }

  guard.broadcast();

  LOG_TRACE("returned dirty V8 context");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief adds a global context functions to be executed asap
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::addGlobalContextMethod (string const& method) {
  for (size_t i = 0; i < _nrInstances; ++i) {
    _contexts[i]->addGlobalContextMethod(method);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief determine which of the free contexts should be picked for the GC
////////////////////////////////////////////////////////////////////////////////

ApplicationV8::V8Context* ApplicationV8::pickContextForGc () {
  size_t n = _freeContexts.size();

  if (n == 0) {
    // this is easy...
    return 0;
  }

  V8GcThread* gc = dynamic_cast<V8GcThread*>(_gcThread);
  assert(gc != 0);

  V8Context* context = 0;

  // we got more than 1 context to clean up, pick the one with the "oldest" GC stamp
  size_t pickedContextNr = 0; // index of context with lowest GC stamp

  for (size_t i = 0; i < n; ++i) {
    // compare last GC stamp
    if (_freeContexts[i]->_lastGcStamp <= _freeContexts[pickedContextNr]->_lastGcStamp) {
      pickedContextNr = i;
    }
  }
  // we now have the context to clean up in pickedContextNr

  // this is the context to clean up
  context = _freeContexts[pickedContextNr];
  assert(context != 0);

  // now compare its last GC timestamp with the last global GC stamp
  if (context->_lastGcStamp + _gcFrequency >= gc->getLastGcStamp()) {
    // no need yet to clean up the context
    return 0;
  }

  // we'll pop the context from the vector. the context might be at any position in the vector
  // so we need to move the other elements around
  if (n > 1) {
    for (size_t i = pickedContextNr; i < n - 1; ++i) {
      _freeContexts[i] = _freeContexts[i + 1];
    }
  }
  _freeContexts.pop_back();

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the garbage collection
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::collectGarbage () {
  V8GcThread* gc = dynamic_cast<V8GcThread*>(_gcThread);
  assert(gc != 0);

  // this flag will be set to true if we timed out waiting for a GC signal
  // if set to true, the next cycle will use a reduced wait time so the GC
  // can be performed more early for all dirty contexts. The flag is set
  // to false again once all contexts have been cleaned up and there is nothing
  // more to do
  bool useReducedWait = false;

  // the time we'll wait for a signal
  const uint64_t regularWaitTime = (uint64_t) (_gcFrequency * 1000.0 * 1000.0);

  // the time we'll wait for a signal when the previous wait timed out
  const uint64_t reducedWaitTime = (uint64_t) (_gcFrequency * 1000.0 * 100.0);

  while (_stopping == 0) {
    V8Context* context = 0;

    {
      bool gotSignal = false;
      CONDITION_LOCKER(guard, _contextCondition);

      if (_dirtyContexts.empty()) {
        uint64_t waitTime = useReducedWait ? reducedWaitTime : regularWaitTime;

        // we'll wait for a signal or a timeout
        gotSignal = guard.wait(waitTime);

        // use a reduced wait time in the next round because we seem to be idle
        // the reduced wait time will allow use to perfom GC for more contexts
        useReducedWait = ! gotSignal;
      }

      if (! _dirtyContexts.empty()) {
        context = _dirtyContexts.back();
        _dirtyContexts.pop_back();
        useReducedWait = false;
      }
      else if (! gotSignal && ! _freeContexts.empty()) {
        // we timed out waiting for a signal, so we have idle time that we can
        // spend on running the GC pro-actively
        // We'll pick one of the free contexts and clean it up
        context = pickContextForGc();

        // there is no context to clean up, probably they all have been cleaned up
        // already. increase the wait time so we don't cycle too much in the GC loop
        // and waste CPU unnecessary
        useReducedWait =  (context != 0);
      }
    }

    // update last gc time
    double lastGc = TRI_microtime();
    gc->updateGcStamp(lastGc);

    if (context != 0) {
      LOG_TRACE("collecting V8 garbage");

      context->_locker = new v8::Locker(context->_isolate);
      context->_isolate->Enter();
      context->_context->Enter();

      v8::V8::LowMemoryNotification();
      while (! v8::V8::IdleNotification()) {
      }

      context->_context->Exit();
      context->_isolate->Exit();
      delete context->_locker;

      context->_dirt = 0;
      context->_lastGcStamp = lastGc;

      {
        CONDITION_LOCKER(guard, _contextCondition);

        _freeContexts.push_back(context);
        guard.broadcast();
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief disables actions
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::disableActions () {
  _useActions = false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enables development mode
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::enableDevelopmentMode () {
  _developmentMode = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        ApplicationFeature methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ApplicationServer
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::setupOptions (map<string, basics::ProgramOptionsDescription>& options) {
  options["JAVASCRIPT Options:help-admin"]
    ("javascript.gc-interval", &_gcInterval, "JavaScript request-based garbage collection interval (each x requests)")
    ("javascript.gc-frequency", &_gcFrequency, "JavaScript time-based garbage collection frequency (each x seconds)")
    ("javascript.action-directory", &_actionPath, "path to the JavaScript action directory")
    ("javascript.app-path", &_appPath, "directory for Foxx applications (normal mode)")
    ("javascript.dev-app-path", &_devAppPath, "directory for Foxx applications (development mode)")
    ("javascript.modules-path", &_modulesPath, "one or more directories separated by semi-colons")
    ("javascript.package-path", &_packagePath, "one or more directories separated by semi-colons")
    ("javascript.startup-directory", &_startupPath, "path to the directory containing JavaScript startup scripts")
    ("javascript.v8-options", &_v8Options, "options to pass to v8")
  ;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::prepare () {
  // check the startup modules
  if (_modulesPath.empty()) {
    LOG_FATAL_AND_EXIT("no 'javascript.modules-path' has been supplied, giving up");
  }

  // set up the startup loader
  if (_startupPath.empty()) {
    LOG_FATAL_AND_EXIT("no 'javascript.startup-directory' has been supplied, giving up");
  }

  // set the actions path
  if (_useActions && _actionPath.empty()) {
    LOG_FATAL_AND_EXIT("no 'javascript.action-directory' has been supplied, giving up");
  }

  // dump paths
  {
    vector<string> paths;

    paths.push_back(string("startup '" + _startupPath + "'"));
    paths.push_back(string("modules '" + _modulesPath + "'"));

    if (! _packagePath.empty()) {
      paths.push_back(string("packages '" + _packagePath + "'"));
    }

    if (_useActions) {
      paths.push_back(string("actions '" + _actionPath + "'"));
    }

    if (! _appPath.empty()) {
      paths.push_back(string("application '" + _appPath + "'"));
    }

    if (! _devAppPath.empty()) {
      paths.push_back(string("dev application '" + _devAppPath + "'"));
    }

    LOG_INFO("JavaScript using %s", StringUtils::join(paths, ", ").c_str());
  }
  
  // check whether app-path was specified
  if (_appPath.empty()) {
    LOG_FATAL_AND_EXIT("no value has been specified for --javascript.app-path.");
  }

  if (_packagePath.empty()) {
    LOG_ERROR("--javascript.package-path option was not specified. this may cause follow-up errors.");
    // TODO: decide if we want to abort server start here
  }


  _startupLoader.setDirectory(_startupPath);
  

  // check for development mode
  if (! _devAppPath.empty()) {
    _developmentMode = true;
  }

  // set up action loader
  if (_useActions) {
    _actionLoader.setDirectory(_actionPath);
  }

  // add v8 options
  if (_v8Options.size() > 0) {
    LOG_INFO("using V8 options '%s'", _v8Options.c_str());
    v8::V8::SetFlagsFromString(_v8Options.c_str(), (int) _v8Options.size());
  }

  // use a minimum of 1 second for GC
  if (_gcFrequency < 1) {
    _gcFrequency = 1;
  }

  // setup instances
  _contexts = new V8Context*[_nrInstances];

  for (size_t i = 0;  i < _nrInstances;  ++i) {
    bool ok = prepareV8Instance(i);

    if (! ok) {
      return false;
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::start () {
  _gcThread = new V8GcThread(this);
  _gcThread->start();

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::close () {
  _stopping = 1;
  _contextCondition.broadcast();
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::stop () {
  // stop GC
  _gcThread->shutdown();

  // shutdown all action threads
  for (size_t i = 0;  i < _nrInstances;  ++i) {
    shutdownV8Instance(i);
  }

  delete[] _contexts;

  // delete GC thread after all action threads have been stopped
  delete _gcThread;
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
/// @brief prepares a V8 instance
////////////////////////////////////////////////////////////////////////////////

bool ApplicationV8::prepareV8Instance (const size_t i) {
  vector<string> files;

  files.push_back("common/bootstrap/modules.js");
  files.push_back("common/bootstrap/module-internal.js");
  files.push_back("common/bootstrap/module-fs.js");
  files.push_back("common/bootstrap/module-console.js"); // needs internal
  files.push_back("common/bootstrap/errors.js");
  files.push_back("common/bootstrap/monkeypatches.js");

  files.push_back("server/bootstrap/module-internal.js");
  files.push_back("server/server.js"); // needs internal

  LOG_TRACE("initialising V8 context #%d", (int) i);

  V8Context* context = _contexts[i] = new V8Context();

  if (context == 0) {
    LOG_FATAL_AND_EXIT("cannot initialize V8 engine");
  }

  // enter a new isolate
  context->_id = i;
  context->_isolate = v8::Isolate::New();
  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();

  // create the context
  context->_context = v8::Context::New();

  if (context->_context.IsEmpty()) {
    LOG_FATAL_AND_EXIT("cannot initialize V8 engine");
  }

  context->_context->Enter();

  TRI_InitV8VocBridge(context->_context, _server, _vocbase, &_startupLoader, i);
  TRI_InitV8Queries(context->_context);


  if (_useActions) {
    TRI_InitV8Actions(context->_context, this);
  }

  TRI_InitV8Buffer(context->_context);
  TRI_InitV8Conversions(context->_context);
  TRI_InitV8Utils(context->_context, _modulesPath, _packagePath, _startupPath);
  TRI_InitV8Shell(context->_context);

  {
    v8::HandleScope scope;

    TRI_AddGlobalVariableVocbase(context->_context, "APP_PATH", v8::String::New(_appPath.c_str()));
    TRI_AddGlobalVariableVocbase(context->_context, "DEV_APP_PATH", v8::String::New(_devAppPath.c_str()));
    TRI_AddGlobalVariableVocbase(context->_context, "DEVELOPMENT_MODE", v8::Boolean::New(_developmentMode));
  }

  // set global flag before loading system files
  if (i == 0 && ! _skipUpgrade) {
    v8::HandleScope scope;
    TRI_AddGlobalVariableVocbase(context->_context, "UPGRADE", v8::Boolean::New(_performUpgrade));
  }

  // load all init files
  for (size_t j = 0;  j < files.size();  ++j) {
    bool ok = _startupLoader.loadScript(context->_context, files[j]);
    
    if (! ok) {
      LOG_FATAL_AND_EXIT("cannot load JavaScript utilities from file '%s'", files[j].c_str());
    }
  }
  
  
  // run upgrade script
  if (i == 0 && ! _skipUpgrade) {
    LOG_DEBUG("running database version check");
    
    // can do this without a lock as this is the startup
    for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
      TRI_vocbase_t* vocbase = (TRI_vocbase_t*) _server->_databases._table[j];

      if (vocbase != 0) {
        // special check script to be run just once in first thread (not in all) 
        // but for all databases
        bool ok = TRI_V8RunVersionCheck(vocbase, &_startupLoader, context->_context);
      
        if (! ok) {
          if (_performUpgrade) {
            LOG_FATAL_AND_EXIT("Database upgrade failed for '%s'. Please inspect the logs from the upgrade procedure", vocbase->_name);
          }
          else {
            LOG_FATAL_AND_EXIT("Database version check failed for '%s'. Please start the server with the --upgrade option", vocbase->_name);
          }
        }
  
        LOG_DEBUG("database version check passed for '%s'", vocbase->_name);
      }
    }
  }

  if (_performUpgrade) {
    // issue #391: when invoked with --upgrade, the server will not always shut down
    LOG_INFO("database upgrade passed");
    context->_context->Exit();
    context->_isolate->Exit();
    delete context->_locker;

    // regular shutdown... wait for all threads to finish
    
    // again, can do this without the lock
    for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
      TRI_vocbase_t* vocbase = (TRI_vocbase_t*) _server->_databases._table[j];
   
      if (vocbase != 0) {
        vocbase->_state = 2;

        int res = TRI_ERROR_NO_ERROR;
        
        res |= TRI_JoinThread(&vocbase->_synchroniser);
        res |= TRI_JoinThread(&vocbase->_compactor);
        vocbase->_state = 3;
        res |= TRI_JoinThread(&vocbase->_cleanup);

        if (res != TRI_ERROR_NO_ERROR) {
          LOG_ERROR("unable to join database threads for database '%s'", vocbase->_name);
        }
      }
    }
    
    LOG_INFO("finished");
    TRI_EXIT_FUNCTION(EXIT_SUCCESS, NULL);
  }

  // scan for foxx applications
  if (i == 0) {
    // once again, we don't need the lock as this is the startup
    for (size_t j = 0; j < _server->_databases._nrAlloc; ++j) {
      TRI_vocbase_t* vocbase = (TRI_vocbase_t*) _server->_databases._table[j];
   
      if (vocbase != 0) {
        TRI_V8InitialiseFoxx(vocbase, context->_context);
      }
    }
  }

  // load all actions
  if (_useActions) {
    v8::HandleScope scope;

    bool ok = _actionLoader.executeAllScripts(context->_context);

    if (! ok) {
      LOG_FATAL_AND_EXIT("cannot load JavaScript actions from directory '%s'", _actionLoader.getDirectory().c_str());
    }

    {
      v8::HandleScope scope;
      TRI_ExecuteJavaScriptString(context->_context,
                                  v8::String::New("require(\"internal\").actionLoaded()"),
                                  v8::String::New("action loaded"),
                                  false);
    }
  
  }

  // and return from the context
  context->_context->Exit();
  context->_isolate->Exit();
  delete context->_locker;

  context->_lastGcStamp = TRI_microtime();

  LOG_TRACE("initialised V8 context #%d", (int) i);

  _freeContexts.push_back(context);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief shut downs a V8 instances
////////////////////////////////////////////////////////////////////////////////

void ApplicationV8::shutdownV8Instance (size_t i) {
  LOG_TRACE("shutting down V8 context #%d", (int) i);

  V8Context* context = _contexts[i];

  context->_locker = new v8::Locker(context->_isolate);
  context->_isolate->Enter();
  context->_context->Enter();

  v8::V8::LowMemoryNotification();
  while (! v8::V8::IdleNotification()) {
  }

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) context->_isolate->GetData();

  if (v8g) {
    delete v8g;
  }

  context->_context->Exit();
  context->_context.Dispose(context->_isolate);

  context->_isolate->Exit();
  delete context->_locker;

  context->_isolate->Dispose();

  delete context;

  LOG_TRACE("closed V8 context #%d", (int) i);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
