////////////////////////////////////////////////////////////////////////////////
/// @brief ArangoDB server
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
/// @author Copyright 2010-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Basics/Common.h"

#include "Basics/messages.h"
#include "Basics/logging.h"
#include "Basics/tri-strings.h"
#include "Rest/InitialiseRest.h"
#include "Basics/files.h"
#include "RestServer/ArangoServer.h"
#include <signal.h>


using namespace triagens;
using namespace triagens::rest;
using namespace triagens::arango;

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief ArangoDB server
////////////////////////////////////////////////////////////////////////////////

static ArangoServer* ArangoInstance = nullptr;

////////////////////////////////////////////////////////////////////////////////
/// @brief running flag
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
static bool IsRunning = false;
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief Windows service name
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
static std::string ServiceName = "ArangoDB";
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief service status handler
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
static SERVICE_STATUS_HANDLE ServiceStatus;
#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

static void TRI_GlobalEntryFunction ();
static void TRI_GlobalExitFunction (int, void*);


////////////////////////////////////////////////////////////////////////////////
/// @brief handle fatal SIGNALs; print backtrace,
///        and rethrow signal for coredumps.
////////////////////////////////////////////////////////////////////////////////
void abortHandler(int signum) {
       TRI_PrintBacktrace();
#ifdef _WIN32
       exit(255 + signum);
#else
       signal(signum, SIG_DFL);
       kill(getpid(), signum);
#endif
}

#ifdef _WIN32
#include <DbgHelp.h>
LONG CALLBACK unhandledExceptionHandler(EXCEPTION_POINTERS *e)
{
#if HAVE_BACKTRACE

  if (e != nullptr) {
    LOG_WARNING("Unhandled exeption: %d", e->ExceptionRecord->ExceptionCode);
  }
  else {
    LOG_WARNING("Unhandled exeption witout ExceptionCode!");
  }

  std::string bt;
  TRI_GetBacktrace(bt);
  LOG_WARNING(bt.c_str());

  std::string miniDumpFilename = TRI_GetTempPath();

  miniDumpFilename += "\\minidump_" + std::to_string(GetCurrentProcessId()) + ".dmp";
  LOG_WARNING("writing minidump: %s", miniDumpFilename.c_str());
  HANDLE hFile = CreateFile(miniDumpFilename.c_str(),
                            GENERIC_WRITE,
                            FILE_SHARE_READ,
                            0, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, 0);

  if(hFile == INVALID_HANDLE_VALUE) {
    LOG_WARNING("could not open minidump file : %ld", GetLastError());
    return EXCEPTION_CONTINUE_SEARCH;
  }

  MINIDUMP_EXCEPTION_INFORMATION exceptionInfo;
  exceptionInfo.ThreadId = GetCurrentThreadId();
  exceptionInfo.ExceptionPointers = e;
  exceptionInfo.ClientPointers = FALSE;

  MiniDumpWriteDump(GetCurrentProcess(),
                    GetCurrentProcessId(),
                    hFile,
                    MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory |
                                  MiniDumpScanMemory |
                                  MiniDumpWithFullMemory),
                    e ? &exceptionInfo : NULL,
                    NULL,
                    NULL);

  if(hFile) {
      CloseHandle(hFile);
      hFile = NULL;
  }
#endif
    return EXCEPTION_CONTINUE_SEARCH; 
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief global entry function
///
/// TODO can we share this between the binaries
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void TRI_GlobalEntryFunction () {
  int maxOpenFiles = 2048;  // upper hard limit for windows
  int res = 0;


  // Uncomment this to call this for extended debug information.
  // If you familiar with valgrind ... then this is not like that, however
  // you do get some similar functionality.

  // res = initialiseWindows(TRI_WIN_INITIAL_SET_DEBUG_FLAG, 0);

  res = initialiseWindows(TRI_WIN_INITIAL_SET_INVALID_HANLE_HANDLER, 0);

  if (res != 0) {
    _exit(EXIT_FAILURE);
  }

  res = initialiseWindows(TRI_WIN_INITIAL_SET_MAX_STD_IO,(const char*)(&maxOpenFiles));

  if (res != 0) {
    _exit(EXIT_FAILURE);
  }

  res = initialiseWindows(TRI_WIN_INITIAL_WSASTARTUP_FUNCTION_CALL, 0);

  if (res != 0) {
    _exit(EXIT_FAILURE);
  }

  TRI_Application_Exit_SetExit(TRI_GlobalExitFunction);
}

#else

static void TRI_GlobalEntryFunction() {
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief global exit function
///
/// TODO can we share this between the binaries
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void TRI_GlobalExitFunction (int exitCode, void* data) {
  int res = 0;

  // ...........................................................................
  // TODO: need a terminate function for windows to be called and cleanup
  // any windows specific stuff.
  // ...........................................................................

  res = finaliseWindows(TRI_WIN_FINAL_WSASTARTUP_FUNCTION_CALL, 0);

  if (res != 0) {
    exit(EXIT_FAILURE);
  }

  exit(exitCode);
}

#else

static void TRI_GlobalExitFunction(int exitCode, void* data) {
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief installs arangod as service with command-line
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void InstallServiceCommand (std::string command) {
  std::string friendlyServiceName = "ArangoDB - the multi-purpose database";

  std::cout << "INFO: adding service '" << friendlyServiceName
            << "' (internal '" << ServiceName << "')"
            << std::endl;

  SC_HANDLE schSCManager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);

  if (schSCManager == 0) {
    std::cerr << "FATAL: OpenSCManager failed with " << GetLastError() << std::endl;
    exit(EXIT_FAILURE);
  }

  SC_HANDLE schService = CreateServiceA(
    schSCManager,                // SCManager database
    ServiceName.c_str(),         // name of service
    friendlyServiceName.c_str(), // service name to display
    SERVICE_ALL_ACCESS,          // desired access
    SERVICE_WIN32_OWN_PROCESS,   // service type
    SERVICE_AUTO_START,          // start type
    SERVICE_ERROR_NORMAL,        // error control type
    command.c_str(),             // path to service's binary
    NULL,                        // no load ordering group
    NULL,                        // no tag identifier
    NULL,                        // no dependencies
    NULL,                        // account (LocalSystem)
    NULL);                       // password

  CloseServiceHandle(schSCManager);

  if (schService == 0) {
    std::cerr << "FATAL: CreateServiceA failed with " << GetLastError() << std::endl;
    exit(EXIT_FAILURE);
  }

  SERVICE_DESCRIPTION description = { "multi-purpose NoSQL database (version " TRI_VERSION ")" };
  ChangeServiceConfig2(schService, SERVICE_CONFIG_DESCRIPTION, &description);

  std::cout << "INFO: added service with command line '" << command << "'" << std::endl;

  CloseServiceHandle(schService);
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief installs a windows service
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void InstallService (int argc, char* argv[]) {
  CHAR path[MAX_PATH];

  if(! GetModuleFileNameA(NULL, path, MAX_PATH)) {
    std::cerr << "FATAL: GetModuleFileNameA failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  // build command
  std::string command;

  command += "\"";
  command += path;
  command += "\"";

  command += " --start-service";

  // register service
  InstallServiceCommand(command);
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a windows service
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void DeleteService (int argc, char* argv[], bool force) {
  CHAR path[MAX_PATH] = "";

  if(! GetModuleFileNameA(NULL, path, MAX_PATH)) {
    std::cerr << "FATAL: GetModuleFileNameA failed" << std::endl;
    exit(EXIT_FAILURE);
  }

  std::cout << "INFO: removing service '" << ServiceName << "'" << std::endl;

  SC_HANDLE schSCManager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);

  if (schSCManager == 0) {
    std::cerr << "FATAL: OpenSCManager failed with " << GetLastError() << std::endl;
    exit(EXIT_FAILURE);
  }

  SC_HANDLE schService = OpenServiceA(
                                      schSCManager,                      // SCManager database
                                      ServiceName.c_str(),               // name of service
                                      DELETE|SERVICE_QUERY_CONFIG);      // first validate whether its us, then delete.

  char serviceConfigMemory[8192]; // msdn says: 8k is enough.
  DWORD bytesNeeded = 0;
  if (QueryServiceConfig(schService, 
                         (LPQUERY_SERVICE_CONFIGA)&serviceConfigMemory,
                         sizeof(serviceConfigMemory),
                         &bytesNeeded)) {
    QUERY_SERVICE_CONFIG *cfg = (QUERY_SERVICE_CONFIG*) &serviceConfigMemory;
    if (strcmp(cfg->lpBinaryPathName, path)) {
      if (!force) {
        std::cerr << "NOT removing service of other installation: " <<
          cfg->lpBinaryPathName <<
          "Our path is: " << 
          path << std::endl;

        CloseServiceHandle(schSCManager);
        return;
      }
      else {
        std::cerr << "Removing service of other installation because of FORCE: " <<
          cfg->lpBinaryPathName <<
          "Our path is: " << 
          path << std::endl;
      }
    }
  }

  CloseServiceHandle(schSCManager);

  if (schService == 0) {
    std::cerr << "FATAL: OpenServiceA failed with " << GetLastError() << std::endl;
    exit(EXIT_FAILURE);
  }

  if (! DeleteService(schService)) {
    std::cerr << "FATAL: DeleteService failed with " << GetLastError() << std::endl;
    exit(EXIT_FAILURE);
  }

  CloseServiceHandle(schService);
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a windows service
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void SetServiceStatus (DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwCheckPoint, DWORD dwWaitHint) {

  // disable control requests until the service is started
  SERVICE_STATUS ss;

  if (dwCurrentState == SERVICE_START_PENDING || dwCurrentState == SERVICE_STOP_PENDING) {
    ss.dwControlsAccepted = 0;
  }
  else {
    ss.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
  }

  // initialize ss structure
  ss.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
  ss.dwServiceSpecificExitCode = 0;
  ss.dwCurrentState            = dwCurrentState;
  ss.dwWin32ExitCode           = dwWin32ExitCode;
  ss.dwCheckPoint              = dwCheckPoint;
  ss.dwWaitHint                = dwWaitHint;

  // Send status of the service to the Service Controller.
  if (! SetServiceStatus(ServiceStatus, &ss)) {
    ss.dwCurrentState = SERVICE_STOP_PENDING;
    ss.dwControlsAccepted = 0;
    SetServiceStatus(ServiceStatus, &ss);

    if (ArangoInstance != nullptr) {
      ArangoInstance->beginShutdown();
    }

    ss.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(ServiceStatus, &ss);
  }
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief service control handler
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static void WINAPI ServiceCtrl (DWORD dwCtrlCode) {
  DWORD dwState = SERVICE_RUNNING;

  switch (dwCtrlCode) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
      dwState = SERVICE_STOP_PENDING;
      break;

    case SERVICE_CONTROL_INTERROGATE:
      break;

    default:
      break;
  }

  // stop service
  if (dwCtrlCode == SERVICE_CONTROL_STOP || dwCtrlCode == SERVICE_CONTROL_SHUTDOWN) {
    SetServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 0, 0);

    if (ArangoInstance != nullptr) {
      ArangoInstance->beginShutdown();

      while (IsRunning) {
        Sleep(100);
      }
    }
  }
  else {
    SetServiceStatus(dwState, NO_ERROR, 0, 0);
  }
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief starts server as service
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32

static int ARGC;
static char** ARGV;

static void WINAPI ServiceMain (DWORD dwArgc, LPSTR *lpszArgv) {

  // register the service ctrl handler,  lpszArgv[0] contains service name
  ServiceStatus = RegisterServiceCtrlHandlerA(lpszArgv[0], (LPHANDLER_FUNCTION) ServiceCtrl);

  // set start pending
  SetServiceStatus(SERVICE_START_PENDING, 0, 0, 0);

  // start palo
  SetServiceStatus(SERVICE_RUNNING, 0, 0, 0);

  IsRunning = true;
  ArangoInstance = new ArangoServer(ARGC, ARGV);
  ArangoInstance->start();
  IsRunning = false;

  // service has stopped
  SetServiceStatus(SERVICE_STOPPED, NO_ERROR, 0, 0);
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an application server
////////////////////////////////////////////////////////////////////////////////

int main (int argc, char* argv[]) {
  int res = 0;
  bool startAsService = false;

  signal(SIGSEGV, abortHandler);

#ifdef _WIN32
  SetUnhandledExceptionFilter(unhandledExceptionHandler);

  if (1 < argc) {
    if (TRI_EqualString(argv[1], "--install-service")) {
      InstallService(argc, argv);
      exit(EXIT_SUCCESS);
    }
    else if (TRI_EqualString(argv[1], "--uninstall-service")) {
      bool force = ((argc > 2) && !strcmp(argv[2], "--force"));
      DeleteService(argc, argv, force);
      exit(EXIT_SUCCESS);
    }
    else if (TRI_EqualString(argv[1], "--start-service")) {
      startAsService = true;
    }
  }

#endif

  // initialise sub-systems
  TRI_GlobalEntryFunction();
  TRIAGENS_REST_INITIALISE(argc, argv);

  // create and start an ArangoDB server

#ifdef _WIN32

  if (startAsService) {
    SERVICE_TABLE_ENTRY ste[] = {
      { TEXT(""), (LPSERVICE_MAIN_FUNCTION) ServiceMain },
      { NULL, NULL }
    };

    ARGC = argc;
    ARGV = argv;

    if (! StartServiceCtrlDispatcher(ste)) {
      std::cerr << "FATAL: StartServiceCtrlDispatcher has failed with " << GetLastError() << std::endl;
      exit(EXIT_FAILURE);
    }
  }

#endif

  if (! startAsService) {
    ArangoInstance = new ArangoServer(argc, argv);
    res = ArangoInstance->start();
  }

  if (ArangoInstance != nullptr) {
    try {
      delete ArangoInstance;
    }
    catch (...) {
      // caught an error during shutdown
      res = EXIT_FAILURE;

#ifdef TRI_ENABLE_MAINTAINER_MODE
      std::cerr << "Caught an exception during shutdown";
#endif      
    }
    ArangoInstance = nullptr;
  }

  // shutdown sub-systems
  TRIAGENS_REST_SHUTDOWN;
  TRI_GlobalExitFunction(res, nullptr);

  return res;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
