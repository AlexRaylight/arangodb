////////////////////////////////////////////////////////////////////////////////
/// @brief V8 shell
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

#include "BasicsC/common.h"

#include <v8.h>

#include <stdio.h>
#include <fstream>

#include "ArangoShell/ArangoClient.h"
#include "BasicsC/messages.h"
#include "Basics/FileUtils.h"
#include "Basics/ProgramOptions.h"
#include "Basics/ProgramOptionsDescription.h"
#include "Basics/StringUtils.h"
#include "Basics/Utf8Helper.h"
#include "BasicsC/csv.h"
#include "BasicsC/files.h"
#include "BasicsC/init.h"
#include "BasicsC/shell-colors.h"
#include "BasicsC/terminal-utils.h"
#include "BasicsC/tri-strings.h"
#include "Rest/Endpoint.h"
#include "Rest/InitialiseRest.h"
#include "Rest/HttpResponse.h"
#include "SimpleHttpClient/SimpleHttpClient.h"
#include "SimpleHttpClient/SimpleHttpResult.h"
#include "V8/JSLoader.h"
#include "V8/V8LineEditor.h"
#include "V8/v8-buffer.h"
#include "V8/v8-conv.h"
#include "V8/v8-shell.h"
#include "V8/v8-utils.h"
#include "V8Client/ImportHelper.h"
#include "V8Client/V8ClientConnection.h"

#include "3rdParty/valgrind/valgrind.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::rest;
using namespace triagens::httpclient;
using namespace triagens::v8client;
using namespace triagens::arango;

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief command prompt
////////////////////////////////////////////////////////////////////////////////

static string Prompt = "arangosh [%d]> ";

////////////////////////////////////////////////////////////////////////////////
/// @brief base class for clients
////////////////////////////////////////////////////////////////////////////////

ArangoClient BaseClient;

////////////////////////////////////////////////////////////////////////////////
/// @brief the initial default connection
////////////////////////////////////////////////////////////////////////////////

V8ClientConnection* ClientConnection = 0;

////////////////////////////////////////////////////////////////////////////////
/// @brief Windows console codepage
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
static int CodePage = -1;
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief object template for the initial connection
////////////////////////////////////////////////////////////////////////////////

v8::Persistent<v8::ObjectTemplate> ConnectionTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief max size body size (used for imports)
////////////////////////////////////////////////////////////////////////////////

static uint64_t ChunkSize = 1024 * 1024 * 4;

////////////////////////////////////////////////////////////////////////////////
/// @brief startup JavaScript files
////////////////////////////////////////////////////////////////////////////////

static JSLoader StartupLoader;

////////////////////////////////////////////////////////////////////////////////
/// @brief path for JavaScript modules files
////////////////////////////////////////////////////////////////////////////////

static string StartupModules = "";

////////////////////////////////////////////////////////////////////////////////
/// @brief path for JavaScript files
////////////////////////////////////////////////////////////////////////////////

static string StartupPath = "";

////////////////////////////////////////////////////////////////////////////////
/// @brief put current directory into module path
////////////////////////////////////////////////////////////////////////////////

static bool UseCurrentModulePath = true;

////////////////////////////////////////////////////////////////////////////////
/// @brief javascript files to execute
////////////////////////////////////////////////////////////////////////////////

static vector<string> ExecuteScripts;

////////////////////////////////////////////////////////////////////////////////
/// @brief javascript string to execute
////////////////////////////////////////////////////////////////////////////////

static string ExecuteString;

////////////////////////////////////////////////////////////////////////////////
/// @brief javascript files to syntax check
////////////////////////////////////////////////////////////////////////////////

static vector<string> CheckScripts;

////////////////////////////////////////////////////////////////////////////////
/// @brief unit file test cases
////////////////////////////////////////////////////////////////////////////////

static vector<string> UnitTests;

////////////////////////////////////////////////////////////////////////////////
/// @brief files to jslint
////////////////////////////////////////////////////////////////////////////////

static vector<string> JsLint;

////////////////////////////////////////////////////////////////////////////////
/// @brief garbage collection interval
////////////////////////////////////////////////////////////////////////////////

static uint64_t GcInterval = 10;

////////////////////////////////////////////////////////////////////////////////
/// @brief console object
////////////////////////////////////////////////////////////////////////////////
      
static V8LineEditor* _console = 0;

// -----------------------------------------------------------------------------
// --SECTION--                                              JavaScript functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief outputs the arguments
///
/// @FUN{internal.output(@FA{string1}, @FA{string2}, @FA{string3}, ...)}
///
/// Outputs the arguments to standard output.
///
/// @verbinclude fluent39
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_PagerOutput (v8::Arguments const& argv) {
  for (int i = 0; i < argv.Length(); i++) {
    v8::HandleScope scope;

    // extract the next argument
    v8::Handle<v8::Value> val = argv[i];

    string str = TRI_ObjectToString(val);

    BaseClient.internalPrint(str);
  }

  return v8::Undefined();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief starts the output pager
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StartOutputPager (v8::Arguments const& ) {
  if (BaseClient.usePager()) {
    BaseClient.internalPrint("Using pager already.\n");
  }
  else {
    BaseClient.setUsePager(true);
    BaseClient.internalPrint(string(string("Using pager ") + BaseClient.outputPager() + " for output buffering.\n"));
  }

  return v8::Undefined();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stops the output pager
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StopOutputPager (v8::Arguments const& ) {
  if (BaseClient.usePager()) {
    BaseClient.internalPrint("Stopping pager.\n");
  }
  else {
    BaseClient.internalPrint("Pager not running.\n");
  }

  BaseClient.setUsePager(false);

  return v8::Undefined();
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   import function
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief imports a CSV file
///
/// @FUN{importCsvFile(@FA{filename}, @FA{collection})}
///
/// Imports data of a CSV file. The data is imported to @FA{collection}.
////The seperator is @CODE{\,} and the quote is @CODE{"}.
///
/// @FUN{importCsvFile(@FA{filename}, @FA{collection}, @FA{options})}
///
/// Imports data of a CSV file. The data is imported to @FA{collection}.
////The seperator is @CODE{\,} and the quote is @CODE{"}.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ImportCsvFile (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "importCsvFile(<filename>, <collection>[, <options>])");
  }

  // extract the filename
  v8::String::Utf8Value filename(argv[0]);

  if (*filename == 0) {
    TRI_V8_TYPE_ERROR(scope, "<filename> must be an UTF-8 filename");
  }

  v8::String::Utf8Value collection(argv[1]);

  if (*collection == 0) {
    TRI_V8_TYPE_ERROR(scope, "<collection> must be an UTF-8 filename");
  }

  // extract the options
  v8::Handle<v8::String> separatorKey = v8::String::New("separator");
  v8::Handle<v8::String> quoteKey = v8::String::New("quote");

  string separator = ",";
  string quote = "\"";

  if (3 <= argv.Length()) {
    v8::Handle<v8::Object> options = argv[2]->ToObject();
    // separator
    if (options->Has(separatorKey)) {
      separator = TRI_ObjectToString(options->Get(separatorKey));

      if (separator.length() < 1) {
        TRI_V8_EXCEPTION_PARAMETER(scope, "<options>.separator must be at least one character");
      }
    }

    // quote
    if (options->Has(quoteKey)) {
      quote = TRI_ObjectToString(options->Get(quoteKey));

      if (quote.length() > 1) {
        TRI_V8_EXCEPTION_PARAMETER(scope, "<options>.quote must be at most one character");
      }
    }
  }

  ImportHelper ih(ClientConnection->getHttpClient(), ChunkSize);

  ih.setQuote(quote);
  ih.setSeparator(separator.c_str());

  string fileName = TRI_ObjectToString(argv[0]);
  string collectionName = TRI_ObjectToString(argv[1]);

  if (ih.importDelimited(collectionName, fileName, ImportHelper::CSV)) {
    v8::Handle<v8::Object> result = v8::Object::New();
    result->Set(v8::String::New("lines"), v8::Integer::New((int32_t) ih.getReadLines()));
    result->Set(v8::String::New("created"), v8::Integer::New((int32_t) ih.getImportedLines()));
    result->Set(v8::String::New("errors"), v8::Integer::New((int32_t) ih.getErrorLines()));
    return scope.Close(result);
  }

  TRI_V8_EXCEPTION_MESSAGE(scope, TRI_ERROR_FAILED, ih.getErrorMessage().c_str());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief imports a JSON file
///
/// @FUN{importJsonFile(@FA{filename}, @FA{collection})}
///
/// Imports data of a CSV file. The data is imported to @FA{collection}.
///
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ImportJsonFile (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "importJsonFile(<filename>, <collection>)");
  }

  // extract the filename
  v8::String::Utf8Value filename(argv[0]);

  if (*filename == 0) {
    TRI_V8_TYPE_ERROR(scope, "<filename> must be an UTF-8 filename");
  }

  v8::String::Utf8Value collection(argv[1]);

  if (*collection == 0) {
    TRI_V8_TYPE_ERROR(scope, "<collection> must be an UTF8 filename");
  }


  ImportHelper ih(ClientConnection->getHttpClient(), ChunkSize);

  string fileName = TRI_ObjectToString(argv[0]);
  string collectionName = TRI_ObjectToString(argv[1]);

  if (ih.importJson(collectionName, fileName)) {
    v8::Handle<v8::Object> result = v8::Object::New();
    result->Set(v8::String::New("lines"), v8::Integer::New((int32_t) ih.getReadLines()));
    result->Set(v8::String::New("created"), v8::Integer::New((int32_t) ih.getImportedLines()));
    result->Set(v8::String::New("errors"), v8::Integer::New((int32_t) ih.getErrorLines()));
    return scope.Close(result);
  }

  TRI_V8_EXCEPTION_MESSAGE(scope, TRI_ERROR_FAILED, ih.getErrorMessage().c_str());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief normalizes UTF 16 strings
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_normalize_string (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "NORMALIZE_STRING(<string>)");
  }

  return scope.Close(TRI_normalize_V8_Obj(argv[0]));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compare two UTF 16 strings
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_compare_string (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "COMPARE_STRING(<left string>, <right string>)");
  }

  v8::String::Value left(argv[0]);
  v8::String::Value right(argv[1]);

  int result = Utf8Helper::DefaultUtf8Helper.compareUtf16(*left, left.length(), *right, right.length());

  return scope.Close(v8::Integer::New(result));
}

// -----------------------------------------------------------------------------
// --SECTION--                                                     private enums
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief enum for wrapped V8 objects
////////////////////////////////////////////////////////////////////////////////

enum WRAP_CLASS_TYPES {WRAP_TYPE_CONNECTION = 1};

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief parses the program options
////////////////////////////////////////////////////////////////////////////////

static vector<string> ParseProgramOptions (int argc, char* argv[]) {
  ProgramOptionsDescription description("STANDARD options");
  ProgramOptionsDescription javascript("JAVASCRIPT options");

  javascript
    ("javascript.execute", &ExecuteScripts, "execute Javascript code from file")
    ("javascript.execute-string", &ExecuteString, "execute Javascript code from string")
    ("javascript.check", &CheckScripts, "syntax check code Javascript code from file")
    ("javascript.gc-interval", &GcInterval, "JavaScript request-based garbage collection interval (each x commands)")
    ("javascript.startup-directory", &StartupPath, "startup paths containing the JavaScript files")
    ("javascript.unit-tests", &UnitTests, "do not start as shell, run unit tests instead")
    ("javascript.current-module-directory", &UseCurrentModulePath, "add current directory to module path")
    ("jslint", &JsLint, "do not start as shell, run jslint instead")
  ;

#ifdef _WIN32
  description
    ("code-page", &CodePage, "windows codepage")
  ;
#endif
  
  description
    ("chunk-size", &ChunkSize, "maximum size for individual data batches (in bytes)")
    ("prompt", &Prompt, "command prompt")
    (javascript, false)
  ;

  vector<string> arguments;

  description.arguments(&arguments);

  // fill in used options
  BaseClient.setupGeneral(description);
  BaseClient.setupColors(description);
  BaseClient.setupAutoComplete(description);
  BaseClient.setupPrettyPrint(description);
  BaseClient.setupPager(description);
  BaseClient.setupLog(description);
  BaseClient.setupServer(description);

  // and parse the command line and config file
  ProgramOptions options;

  char* p = TRI_BinaryName(argv[0]);
  string conf = p;
  TRI_FreeString(TRI_CORE_MEM_ZONE, p);
  conf += ".conf";

  BaseClient.parse(options, description, "<options>", argc, argv, conf);

  // set V8 options
  v8::V8::SetFlagsFromCommandLine(&argc, argv, true);

  // derive other paths from `--javascript.directory`
  StartupModules = StartupPath + TRI_DIR_SEPARATOR_STR + "client" + TRI_DIR_SEPARATOR_STR + "modules;" + 
                   StartupPath + TRI_DIR_SEPARATOR_STR + "common" + TRI_DIR_SEPARATOR_STR + "modules;" + 
                   StartupPath + TRI_DIR_SEPARATOR_STR + "node"; 

  if (UseCurrentModulePath) {
    StartupModules += ";" + FileUtils::currentDirectory();
  }

  // turn on paging automatically if "pager" option is set
  if (options.has("pager") && ! options.has("use-pager")) {
    BaseClient.setUsePager(true);
  }

  // disable excessive output in non-interactive mode
  if (! ExecuteScripts.empty() || ! ExecuteString.empty() || ! CheckScripts.empty() || ! UnitTests.empty() || ! JsLint.empty()) {
    BaseClient.shutup();
  }

  // return the positional arguments
  return arguments;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief copies v8::Object to std::map<string, string>
////////////////////////////////////////////////////////////////////////////////

static void objectToMap (map<string, string>& myMap, v8::Handle<v8::Value> val) {
  v8::Handle<v8::Object> v8Headers = val.As<v8::Object> ();

  if (v8Headers->IsObject()) {
    v8::Handle<v8::Array> props = v8Headers->GetPropertyNames();

    for (uint32_t i = 0; i < props->Length(); i++) {
      v8::Handle<v8::Value> key = props->Get(v8::Integer::New(i));
      myMap[TRI_ObjectToString(key)] = TRI_ObjectToString(v8Headers->Get(key));
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a new client connection instance
////////////////////////////////////////////////////////////////////////////////

static V8ClientConnection* CreateConnection () {
  return new V8ClientConnection(BaseClient.endpointServer(),
                                BaseClient.databaseName(),
                                BaseClient.username(),
                                BaseClient.password(),
                                BaseClient.requestTimeout(),
                                BaseClient.connectTimeout(),
                                ArangoClient::DEFAULT_RETRIES,
                                BaseClient.sslProtocol(),
                                false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief weak reference callback for queries (call the destructor here)
////////////////////////////////////////////////////////////////////////////////

static void ClientConnection_DestructorCallback (v8::Isolate*,
                                                 v8::Persistent<v8::Value>,
                                                 void* parameter) {
  V8ClientConnection* client = (V8ClientConnection*) parameter;
  delete client;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief wrap V8ClientConnection in a v8::Object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Object> wrapV8ClientConnection (V8ClientConnection* connection) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Handle<v8::Object> result = ConnectionTempl->NewInstance();
  v8::Persistent<v8::Value> persistent = v8::Persistent<v8::Value>::New(isolate, v8::External::New(connection));
  result->SetInternalField(SLOT_CLASS_TYPE, v8::Integer::New(WRAP_TYPE_CONNECTION));
  result->SetInternalField(SLOT_CLASS, persistent);
  persistent.MakeWeak(isolate, connection, ClientConnection_DestructorCallback);

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection constructor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_ConstructorCallback (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() > 0 && argv[0]->IsString()) {
    string definition = TRI_ObjectToString(argv[0]);

    BaseClient.createEndpoint(definition);

    if (BaseClient.endpointServer() == 0) {
      string errorMessage = "error in '" + definition + "'";
      TRI_V8_EXCEPTION_PARAMETER(scope, errorMessage.c_str());
    }
  }

  if (BaseClient.endpointServer() == 0) {
    return v8::Undefined();
  }

  V8ClientConnection* connection = CreateConnection();

  if (connection->isConnected() && connection->getLastHttpReturnCode() == HttpResponse::OK) {
    ostringstream s;
    s << "Connected to ArangoDB '" << BaseClient.endpointServer()->getSpecification()
      << "', version " << connection->getVersion() << ", database '" << BaseClient.databaseName() 
      << "', username: '" << BaseClient.username() << "'";
    BaseClient.printLine(s.str());
  }
  else {
    string errorMessage = "Could not connect. Error message: " + connection->getErrorMessage();
    delete connection;
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_SIMPLE_CLIENT_COULD_NOT_CONNECT, errorMessage.c_str());
  }

  return scope.Close(wrapV8ClientConnection(connection));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "reconnect"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_reconnect (v8::Arguments const& argv) {
  v8::HandleScope scope;

  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "reconnect(<endpoint>, <database>, [, <username>, <password>])");
  }

  string const definition = TRI_ObjectToString(argv[0]);
  string databaseName = TRI_ObjectToString(argv[1]);

  string username;
  if (argv.Length() < 3) {
    username = BaseClient.username();
  }
  else {
    username = TRI_ObjectToString(argv[2]);
  }

  string password;
  if (argv.Length() < 4) {
    BaseClient.printContinuous("Please specify a password: ");

    // now prompt for it
#ifdef TRI_HAVE_TERMIOS_H
    TRI_SetStdinVisibility(false);
    getline(cin, password);

    TRI_SetStdinVisibility(true);
#else
    getline(cin, password);
#endif
    BaseClient.printLine("");
  }
  else {
    password = TRI_ObjectToString(argv[3]);
  }

  string const oldDefinition   = BaseClient.endpointString();
  string const oldDatabaseName = BaseClient.databaseName();
  string const oldUsername     = BaseClient.username();
  string const oldPassword     = BaseClient.password();

  delete connection;

  BaseClient.setEndpointString(definition);
  BaseClient.setDatabaseName(databaseName);
  BaseClient.setUsername(username);
  BaseClient.setPassword(password);

  // re-connect using new options
  BaseClient.createEndpoint();
  if (BaseClient.endpointServer() == 0) {
    BaseClient.setEndpointString(oldDefinition);
    BaseClient.setDatabaseName(oldDatabaseName);
    BaseClient.setUsername(oldUsername);
    BaseClient.setPassword(oldPassword);
    BaseClient.createEndpoint();

    string errorMessage = "error in '" + definition + "'";
    TRI_V8_EXCEPTION_PARAMETER(scope, errorMessage.c_str());
  }

  V8ClientConnection* newConnection = CreateConnection();

  if (newConnection->isConnected() && newConnection->getLastHttpReturnCode() == HttpResponse::OK) {
    ostringstream s;
    s << "Connected to ArangoDB '" << BaseClient.endpointServer()->getSpecification()
      << "' version: " << newConnection->getVersion() << ", database: '" << BaseClient.databaseName() 
      << "', username: '" << BaseClient.username() << "'";

    BaseClient.printLine(s.str());

    argv.Holder()->SetInternalField(SLOT_CLASS, v8::External::New(newConnection));

    v8::Handle<v8::Value> db = v8::Context::GetCurrent()->Global()->Get(TRI_V8_SYMBOL("db"));
    if (db->IsObject()) {
      v8::Handle<v8::Object> dbObj = v8::Handle<v8::Object>::Cast(db);

      if (dbObj->Has(TRI_V8_STRING("_flushCache")) && dbObj->Get(TRI_V8_STRING("_flushCache"))->IsFunction()) {
        v8::Handle<v8::Function> func = v8::Handle<v8::Function>::Cast(dbObj->Get(TRI_V8_STRING("_flushCache")));

        v8::Handle<v8::Value>* args = 0;
        func->Call(dbObj, 0, args);
      }
    }

    // ok
    return scope.Close(v8::True());
  }
  else {
    ostringstream s;
    s << "Could not connect to endpoint '" << BaseClient.endpointString() << 
         "', username: '" << BaseClient.username() << "'";
    BaseClient.printErrLine(s.str());
    
    string errorMsg = "could not connect";
    if (newConnection->getErrorMessage() != "") {
      errorMsg = newConnection->getErrorMessage();
    }

    delete newConnection;

    // rollback
    BaseClient.setEndpointString(oldDefinition);
    BaseClient.setDatabaseName(oldDatabaseName);
    BaseClient.setUsername(oldUsername);
    BaseClient.setPassword(oldPassword);
    BaseClient.createEndpoint();

    ClientConnection = CreateConnection();
    argv.Holder()->SetInternalField(SLOT_CLASS, v8::External::New(ClientConnection));

    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_SIMPLE_CLIENT_COULD_NOT_CONNECT, errorMsg.c_str());
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "GET" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpGetAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 1 || argv.Length() > 2 || ! argv[0]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "get(<url>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);

  // check header fields
  map<string, string> headerFields;

  if (argv.Length() > 1) {
    objectToMap(headerFields, argv[1]);
  }

  return scope.Close(connection->getData(*url, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "GET"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpGet (v8::Arguments const& argv) {
  return ClientConnection_httpGetAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "GET_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpGetRaw (v8::Arguments const& argv) {
  return ClientConnection_httpGetAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "HEAD" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpHeadAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 1 || argv.Length() > 2 || ! argv[0]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "head(<url>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);

  // check header fields
  map<string, string> headerFields;

  if (argv.Length() > 1) {
    objectToMap(headerFields, argv[1]);
  }

  return scope.Close(connection->headData(*url, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "HEAD"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpHead (v8::Arguments const& argv) {
  return ClientConnection_httpHeadAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "HEAD_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpHeadRaw (v8::Arguments const& argv) {
  return ClientConnection_httpHeadAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "DELETE" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpDeleteAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 1 || argv.Length() > 2 || ! argv[0]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "delete(<url>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);

  // check header fields
  map<string, string> headerFields;
  if (argv.Length() > 1) {
    objectToMap(headerFields, argv[1]);
  }

  return scope.Close(connection->deleteData(*url, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "DELETE"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpDelete (v8::Arguments const& argv) {
  return ClientConnection_httpDeleteAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "DELETE_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpDeleteRaw (v8::Arguments const& argv) {
  return ClientConnection_httpDeleteAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "OPTIONS" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpOptionsAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 2 || argv.Length() > 3 || ! argv[0]->IsString() || ! argv[1]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "options(<url>, <body>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);
  v8::String::Utf8Value body(argv[1]);

  // check header fields
  map<string, string> headerFields;
  if (argv.Length() > 2) {
    objectToMap(headerFields, argv[2]);
  }

  return scope.Close(connection->optionsData(*url, *body, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "OPTIONS"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpOptions (v8::Arguments const& argv) {
  return ClientConnection_httpOptionsAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "OPTIONS_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpOptionsRaw (v8::Arguments const& argv) {
  return ClientConnection_httpOptionsAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "POST" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPostAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 2 || argv.Length() > 3 || ! argv[0]->IsString() || ! argv[1]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "post(<url>, <body>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);
  v8::String::Utf8Value body(argv[1]);

  // check header fields
  map<string, string> headerFields;
  if (argv.Length() > 2) {
    objectToMap(headerFields, argv[2]);
  }

  return scope.Close(connection->postData(*url, *body, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "POST"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPost (v8::Arguments const& argv) {
  return ClientConnection_httpPostAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "POST_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPostRaw (v8::Arguments const& argv) {
  return ClientConnection_httpPostAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "PUT" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPutAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 2 || argv.Length() > 3 || ! argv[0]->IsString() || ! argv[1]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "put(<url>, <body>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);
  v8::String::Utf8Value body(argv[1]);

  // check header fields
  map<string, string> headerFields;
  if (argv.Length() > 2) {
    objectToMap(headerFields, argv[2]);
  }

  return scope.Close(connection->putData(*url, *body, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "PUT"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPut (v8::Arguments const& argv) {
  return ClientConnection_httpPutAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "PUT_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPutRaw (v8::Arguments const& argv) {
  return ClientConnection_httpPutAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "PATCH" helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPatchAny (v8::Arguments const& argv, bool raw) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() < 2 || argv.Length() > 3 || ! argv[0]->IsString() || ! argv[1]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "patch(<url>, <body>[, <headers>])");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);
  v8::String::Utf8Value body(argv[1]);

  // check header fields
  map<string, string> headerFields;
  if (argv.Length() > 2) {
    objectToMap(headerFields, argv[2]);
  }

  return scope.Close(connection->patchData(*url, *body, headerFields, raw));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "PATCH"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPatch (v8::Arguments const& argv) {
  return ClientConnection_httpPatchAny(argv, false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "PATCH_RAW"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpPatchRaw (v8::Arguments const& argv) {
  return ClientConnection_httpPatchAny(argv, true);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection send file helper
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_httpSendFile (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() != 2 || ! argv[0]->IsString() || ! argv[1]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "sendFile(<url>, <file>)");
  }

  TRI_Utf8ValueNFC url(TRI_UNKNOWN_MEM_ZONE, argv[0]);

  const string infile = TRI_ObjectToString(argv[1]);

  if (! TRI_ExistsFile(infile.c_str())) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_FILE_NOT_FOUND);
  }

  size_t bodySize;
  char* body = TRI_SlurpFile(TRI_UNKNOWN_MEM_ZONE, infile.c_str(), &bodySize);

  if (body == 0) {
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_errno(), "could not read file");
  }

  v8::TryCatch tryCatch;

  // check header fields
  map<string, string> headerFields;

  v8::Handle<v8::Value> result = connection->postData(*url, body, bodySize, headerFields);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, body);

  if (tryCatch.HasCaught()) {
    return scope.Close(v8::ThrowException(tryCatch.Exception()));
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "getEndpoint"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_getEndpoint (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "getEndpoint()");
  }

  const string endpoint = BaseClient.endpointString();
  return scope.Close(v8::String::New(endpoint.c_str(), (int) endpoint.size()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "lastError"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_lastHttpReturnCode (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "lastHttpReturnCode()");
  }

  return scope.Close(v8::Integer::New(connection->getLastHttpReturnCode()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "lastErrorMessage"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_lastErrorMessage (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  // check params
  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "lastErrorMessage()");
  }

  return scope.Close(v8::String::New(connection->getErrorMessage().c_str()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "isConnected"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_isConnected (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "isConnected()");
  }

  return scope.Close(v8::Boolean::New(connection->isConnected()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "isConnected"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_toString (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "toString()");
  }

  string result = "[object ArangoConnection:" + BaseClient.endpointServer()->getSpecification();

  if (connection->isConnected()) {
    result += ","
            + connection->getVersion()
            + ",connected]";
  }
  else {
    result += ",unconnected]";
  }

  return scope.Close(v8::String::New(result.c_str()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "getVersion"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_getVersion (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "getVersion()");
  }

  return scope.Close(v8::String::New(connection->getVersion().c_str()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "getDatabaseName"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_getDatabaseName (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "getDatabaseName()");
  }

  return scope.Close(v8::String::New(connection->getDatabaseName().c_str()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ClientConnection method "setDatabaseName"
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ClientConnection_setDatabaseName (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // get the connection
  V8ClientConnection* connection = TRI_UnwrapClass<V8ClientConnection>(argv.Holder(), WRAP_TYPE_CONNECTION);

  if (connection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "connection class corrupted");
  }

  if (argv.Length() != 1 || ! argv[0]->IsString()) {
    TRI_V8_EXCEPTION_USAGE(scope, "setDatabaseName(<name>)");
  }

  string const dbName = TRI_ObjectToString(argv[0]);
  connection->setDatabaseName(dbName);
  BaseClient.setDatabaseName(dbName);

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dynamically replace %d, %e, %u in the prompt
////////////////////////////////////////////////////////////////////////////////

static std::string BuildPrompt () {
  string result;

  char const* p = Prompt.c_str();
  bool esc = false;

  while (true) {
    const char c = *p;

    if (c == '\0') {
      break;
    }

    if (esc) {
      if (c == '%') {
        result.push_back(c);
      }
      else if (c == 'd') {
        result.append(BaseClient.databaseName());
      }
      else if (c == 'e') {
        result.append(BaseClient.endpointString());
      }
      else if (c == 'u') {
        result.append(BaseClient.username());
      }
      
      esc = false;
    }
    else {
      if (c == '%') {
        esc = true;
      }
      else {
        result.push_back(c);
      }
    }

    ++p;
  }


  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief signal handler for CTRL-C
////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
  // TODO

#else

static void SignalHandler (int signal) {
  if (_console != 0) {
    _console->close();
    _console = 0;
  }
  printf("\n");

  TRI_EXIT_FUNCTION(EXIT_SUCCESS, NULL);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the shell
////////////////////////////////////////////////////////////////////////////////

static void RunShell (v8::Handle<v8::Context> context, bool promptError) {
  v8::Context::Scope contextScope(context);
  v8::Local<v8::String> name(v8::String::New("(shell)"));

  _console = new V8LineEditor(context, ".arangosh.history");
  _console->open(BaseClient.autoComplete());

  // install signal handler for CTRL-C
#ifdef _WIN32
  // TODO

#else 
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = &SignalHandler;
          
  int res = sigaction(SIGINT, &sa, 0);
        
  if (res != 0) {
    LOG_ERROR("unable to install signal handler");
  }
#endif 

  uint64_t nrCommands = 0;

  while (true) {
    // set up prompts
    string dynamicPrompt;
    if (ClientConnection != 0) {
      dynamicPrompt = BuildPrompt();
    }
    else {
      dynamicPrompt = "-";
    }
    
    string goodPrompt;
    string badPrompt;


#ifdef __APPLE__

    // ........................................................................................
    // MacOS uses libedit, which does not support ignoring of non-printable characters in the prompt
    // using non-printable characters in the prompt will lead to wrong prompt lengths being calculated
    // we will therefore disable colorful prompts for MacOS.
    // ........................................................................................

    goodPrompt = badPrompt = dynamicPrompt;

#elif _WIN32

    // ........................................................................................
    // Windows console is not coloured by escape sequences. So the method given below will not
    // work. For now we simply ignore the colours until we move the windows version into
    // a GUI Window.
    // ........................................................................................

    goodPrompt = badPrompt = dynamicPrompt;

#else

    if (BaseClient.colors()) {

#ifdef TRI_HAVE_LINENOISE
      // linenoise doesn't need escape sequences for escape sequences
      goodPrompt = string(TRI_SHELL_COLOR_BOLD_GREEN) + dynamicPrompt + string(TRI_SHELL_COLOR_RESET);
      badPrompt  = string(TRI_SHELL_COLOR_BOLD_RED)   + dynamicPrompt + string(TRI_SHELL_COLOR_RESET);

#else
      // readline does...
      goodPrompt = string(ArangoClient::PROMPT_IGNORE_START) + string(TRI_SHELL_COLOR_BOLD_GREEN) + string(ArangoClient::PROMPT_IGNORE_END) +
                   dynamicPrompt +
                   string(ArangoClient::PROMPT_IGNORE_START) + string(TRI_SHELL_COLOR_RESET) + string(ArangoClient::PROMPT_IGNORE_END);

      badPrompt  = string(ArangoClient::PROMPT_IGNORE_START) + string(TRI_SHELL_COLOR_BOLD_RED)   + string(ArangoClient::PROMPT_IGNORE_END) +
                   dynamicPrompt + 
                   string(ArangoClient::PROMPT_IGNORE_START) + string(TRI_SHELL_COLOR_RESET) + string(ArangoClient::PROMPT_IGNORE_END);
#endif
    }
    else {
      goodPrompt = badPrompt = dynamicPrompt;
    }

#endif

    // gc
    if (++nrCommands >= GcInterval) {
      nrCommands = 0;

      v8::V8::LowMemoryNotification();
      while (! v8::V8::IdleNotification()) {
      }
    }

    char* input = _console->prompt(promptError ? badPrompt.c_str() : goodPrompt.c_str());

    if (input == 0) {
      break;
    }

    if (*input == '\0') {
      // input string is empty, but we must still free it
      TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, input);
      continue;
    }

    BaseClient.log("%s%s\n", dynamicPrompt.c_str(), input);

    string i = triagens::basics::StringUtils::trim(input);

    if (i == "exit" || i == "quit" || i == "exit;" || i == "quit;") {
      TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, input);
      break;
    }

    if (i == "help" || i == "help;") {
      TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, input);
      input = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, "help()");
      if (input == 0) {
        LOG_FATAL_AND_EXIT("out of memory");
      }
    }

    _console->addHistory(input);

    v8::HandleScope scope;
    v8::TryCatch tryCatch;

    BaseClient.startPager();

    // assume the command succeeds
    promptError = false;

    // execute command and register its result in __LAST__
    v8::Handle<v8::Value> v = TRI_ExecuteJavaScriptString(context, v8::String::New(input), name, true);

    if (v.IsEmpty()) {
      context->Global()->Set(TRI_V8_SYMBOL("_last"), v8::Undefined());
    }
    else {
      context->Global()->Set(TRI_V8_SYMBOL("_last"), v);
    }

    TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, input);

    if (tryCatch.HasCaught()) {
      // command failed
      string exception(TRI_StringifyV8Exception(&tryCatch));

      BaseClient.printErrLine(exception);
      BaseClient.log("%s", exception.c_str());

      // this will change the prompt for the next round
      promptError = true;
    }

    BaseClient.stopPager();
    BaseClient.printLine("");

    BaseClient.log("%s\n", "");
    // make sure the last command result makes it into the log file
    BaseClient.flushLog();
  }

  _console->close();
  delete _console;
  _console = 0;

  BaseClient.printLine("");

  BaseClient.printByeBye();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the unit tests
////////////////////////////////////////////////////////////////////////////////

static bool RunUnitTests (v8::Handle<v8::Context> context) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;
  bool ok;

  // set-up unit tests array
  v8::Handle<v8::Array> sysTestFiles = v8::Array::New();

  for (size_t i = 0;  i < UnitTests.size();  ++i) {
    sysTestFiles->Set((uint32_t) i, v8::String::New(UnitTests[i].c_str()));
  }

  TRI_AddGlobalVariableVocbase(context, "SYS_UNIT_TESTS", sysTestFiles);
  // do not use TRI_AddGlobalVariableVocBase because it creates read-only variables!!
  context->Global()->Set(v8::String::New("SYS_UNIT_TESTS_RESULT"), v8::True());

  // run tests
  char const* input = "require(\"jsunity\").runCommandLineTests();";
  v8::Local<v8::String> name(v8::String::New("(arangosh)"));
  TRI_ExecuteJavaScriptString(context, v8::String::New(input), name, true);

  if (tryCatch.HasCaught()) {
    BaseClient.printErrLine(TRI_StringifyV8Exception(&tryCatch));
    ok = false;
  }
  else {
    ok = TRI_ObjectToBoolean(context->Global()->Get(v8::String::New("SYS_UNIT_TESTS_RESULT")));
  }

  return ok;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the Javascript files
////////////////////////////////////////////////////////////////////////////////

static bool RunScripts (v8::Handle<v8::Context> context,
                        const vector<string>& scripts,
                        const bool execute) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;
  bool ok;

  ok = true;

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  v8::Handle<v8::Function> func = v8g->ExecuteFileCallback;

  if (func.IsEmpty()) {
    string msg = "no execute function has been registered";

    BaseClient.printErrLine(msg.c_str());
    BaseClient.log("%s", msg.c_str());

    BaseClient.flushLog();

    return false;
  }

  for (size_t i = 0;  i < scripts.size();  ++i) {
    if (! FileUtils::exists(scripts[i])) {
      string msg = "error: Javascript file not found: '" + scripts[i] + "'";

      BaseClient.printErrLine(msg.c_str());
      BaseClient.log("%s", msg.c_str());

      ok = false;
      break;
    }

    if (execute) {
      v8::Handle<v8::String> name = v8::String::New(scripts[i].c_str());
      v8::Handle<v8::Value> args[] = { name };

      func->Call(func, 1, args);
    }
    else {
      TRI_ParseJavaScriptFile(scripts[i].c_str());
    }

    if (tryCatch.HasCaught()) {
      string exception(TRI_StringifyV8Exception(&tryCatch));

      BaseClient.printErrLine(exception);
      BaseClient.log("%s\n", exception.c_str());

      ok = false;
      break;
    }
  }

  BaseClient.flushLog();

  return ok;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the Javascript string
////////////////////////////////////////////////////////////////////////////////

static bool RunString (v8::Handle<v8::Context> context,
                       const string& script) {
  v8::HandleScope scope;

  v8::TryCatch tryCatch;
  bool ok = true;

  v8::Handle<v8::Value> result = TRI_ExecuteJavaScriptString(context,
                                                             v8::String::New(script.c_str(), (int) script.size()),
                                                             v8::String::New("(command-line)"),
                                                             false);

  if (tryCatch.HasCaught()) {
    string exception(TRI_StringifyV8Exception(&tryCatch));

    BaseClient.printErrLine(exception);
    BaseClient.log("%s\n", exception.c_str());
    ok = false;
  }
  else {
    // check return value of script
    if (result->IsNumber()) {
      int64_t intResult = TRI_ObjectToInt64(result);

      if (intResult != 0) {
        ok = false;
      }
    }
  }

  BaseClient.flushLog();

  return ok;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the jslint tests
////////////////////////////////////////////////////////////////////////////////

static bool RunJsLint (v8::Handle<v8::Context> context) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;
  bool ok;

  // set-up jslint files array
  v8::Handle<v8::Array> sysTestFiles = v8::Array::New();

  for (size_t i = 0;  i < JsLint.size();  ++i) {
    sysTestFiles->Set((uint32_t) i, v8::String::New(JsLint[i].c_str()));
  }

  context->Global()->Set(v8::String::New("SYS_UNIT_TESTS"), sysTestFiles);
  context->Global()->Set(v8::String::New("SYS_UNIT_TESTS_RESULT"), v8::True());

  // run tests
  char const* input = "require(\"jslint\").runCommandLineTests({ });";
  v8::Local<v8::String> name(v8::String::New("(arangosh)"));
  TRI_ExecuteJavaScriptString(context, v8::String::New(input), name, true);

  if (tryCatch.HasCaught()) {
    BaseClient.printErrLine(TRI_StringifyV8Exception(&tryCatch));
    ok = false;
  }
  else {
    ok = TRI_ObjectToBoolean(context->Global()->Get(v8::String::New("SYS_UNIT_TESTS_RESULT")));
  }

  return ok;
}


// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief startup and exit functions
////////////////////////////////////////////////////////////////////////////////

void* arangoshResourcesAllocated = NULL;
static void arangoshEntryFunction ();
static void arangoshExitFunction (int, void*);

#ifdef _WIN32

// .............................................................................
// Call this function to do various initialistions for windows only
//
// TODO can we move this to a general function for all binaries?
// .............................................................................

void arangoshEntryFunction() {
  int maxOpenFiles = 1024;
  int res = 0;

  // ...........................................................................
  // Uncomment this to call this for extended debug information.
  // If you familiar with valgrind ... then this is not like that, however
  // you do get some similar functionality.
  // ...........................................................................
  //res = initialiseWindows(TRI_WIN_INITIAL_SET_DEBUG_FLAG, 0);

  res = initialiseWindows(TRI_WIN_INITIAL_SET_INVALID_HANLE_HANDLER, 0);

  if (res != 0) {
    _exit(1);
  }

  res = initialiseWindows(TRI_WIN_INITIAL_SET_MAX_STD_IO,(const char*)(&maxOpenFiles));

  if (res != 0) {
    _exit(1);
  }

  res = initialiseWindows(TRI_WIN_INITIAL_WSASTARTUP_FUNCTION_CALL, 0);

  if (res != 0) {
    _exit(1);
  }

  TRI_Application_Exit_SetExit(arangoshExitFunction);

}

static void arangoshExitFunction(int exitCode, void* data) {
  int res = 0;
  // ...........................................................................
  // TODO: need a terminate function for windows to be called and cleanup
  // any windows specific stuff.
  // ...........................................................................

  res = finaliseWindows(TRI_WIN_FINAL_WSASTARTUP_FUNCTION_CALL, 0);

  if (res != 0) {
    exit(1);
  }

  exit(exitCode);
}
#else

static void arangoshEntryFunction() {
}

static void arangoshExitFunction(int exitCode, void* data) {
}

#endif


////////////////////////////////////////////////////////////////////////////////
/// @brief main
////////////////////////////////////////////////////////////////////////////////

int main (int argc, char* argv[]) {
  int ret = EXIT_SUCCESS;

  arangoshEntryFunction();

  TRIAGENS_C_INITIALISE(argc, argv);
  TRIAGENS_REST_INITIALISE(argc, argv);

  TRI_InitialiseLogging(false);

  BaseClient.setEndpointString(Endpoint::getDefaultEndpoint());

  // .............................................................................
  // parse the program options
  // .............................................................................

  vector<string> positionals = ParseProgramOptions(argc, argv);

  // .............................................................................
  // set-up client connection
  // .............................................................................

  // check if we want to connect to a server
  bool useServer = (BaseClient.endpointString() != "none");

  // if we are in jslint mode, we will not need the server at all
  if (! JsLint.empty()) {
    useServer = false;
  }

  if (useServer) {
    BaseClient.createEndpoint();

    if (BaseClient.endpointServer() == 0) {
      ostringstream s;
      s << "invalid value for --server.endpoint ('" << BaseClient.endpointString() << "')";

      BaseClient.printErrLine(s.str());

      TRI_EXIT_FUNCTION(EXIT_FAILURE, NULL);
    }

    ClientConnection = CreateConnection();
  }

  // .............................................................................
  // set-up V8 objects
  // .............................................................................

  v8::HandleScope handle_scope;

  v8::Isolate* isolate = v8::Isolate::GetCurrent();

  // create the global template
  v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New();

  // create the context
  v8::Persistent<v8::Context> context = v8::Context::New(0, global);

  if (context.IsEmpty()) {
    BaseClient.printErrLine("cannot initialize V8 engine");
    TRI_EXIT_FUNCTION(EXIT_FAILURE, NULL);
  }

  context->Enter();

  // set pretty print default: (used in print.js)
  TRI_AddGlobalVariableVocbase(context, "PRETTY_PRINT", v8::Boolean::New(BaseClient.prettyPrint()));

  // add colors for print.js
  TRI_AddGlobalVariableVocbase(context, "COLOR_OUTPUT", v8::Boolean::New(BaseClient.colors()));

  // add function SYS_OUTPUT to use pager
  TRI_AddGlobalVariableVocbase(context, "SYS_OUTPUT", v8::FunctionTemplate::New(JS_PagerOutput)->GetFunction());

  TRI_InitV8Buffer(context);

  TRI_InitV8Utils(context, StartupPath, StartupModules);
  TRI_InitV8Shell(context);

  // reset the prompt error flag (will determine prompt colors)
  bool promptError = false;

  // .............................................................................
  // define ArangoConnection class
  // .............................................................................

  if (useServer) {
    v8::Handle<v8::FunctionTemplate> connection_templ = v8::FunctionTemplate::New();
    connection_templ->SetClassName(v8::String::New("ArangoConnection"));

    v8::Handle<v8::ObjectTemplate> connection_proto = connection_templ->PrototypeTemplate();

    connection_proto->Set("DELETE", v8::FunctionTemplate::New(ClientConnection_httpDelete));
    connection_proto->Set("DELETE_RAW", v8::FunctionTemplate::New(ClientConnection_httpDeleteRaw));
    connection_proto->Set("GET", v8::FunctionTemplate::New(ClientConnection_httpGet));
    connection_proto->Set("GET_RAW", v8::FunctionTemplate::New(ClientConnection_httpGetRaw));
    connection_proto->Set("HEAD", v8::FunctionTemplate::New(ClientConnection_httpHead));
    connection_proto->Set("HEAD_RAW", v8::FunctionTemplate::New(ClientConnection_httpHeadRaw));
    connection_proto->Set("OPTIONS", v8::FunctionTemplate::New(ClientConnection_httpOptions));
    connection_proto->Set("OPTIONS_RAW", v8::FunctionTemplate::New(ClientConnection_httpOptionsRaw));
    connection_proto->Set("PATCH", v8::FunctionTemplate::New(ClientConnection_httpPatch));
    connection_proto->Set("PATCH_RAW", v8::FunctionTemplate::New(ClientConnection_httpPatchRaw));
    connection_proto->Set("POST", v8::FunctionTemplate::New(ClientConnection_httpPost));
    connection_proto->Set("POST_RAW", v8::FunctionTemplate::New(ClientConnection_httpPostRaw));
    connection_proto->Set("PUT", v8::FunctionTemplate::New(ClientConnection_httpPut));
    connection_proto->Set("PUT_RAW", v8::FunctionTemplate::New(ClientConnection_httpPutRaw));
    connection_proto->Set("SEND_FILE", v8::FunctionTemplate::New(ClientConnection_httpSendFile));
    connection_proto->Set("getEndpoint", v8::FunctionTemplate::New(ClientConnection_getEndpoint));
    connection_proto->Set("lastHttpReturnCode", v8::FunctionTemplate::New(ClientConnection_lastHttpReturnCode));
    connection_proto->Set("lastErrorMessage", v8::FunctionTemplate::New(ClientConnection_lastErrorMessage));
    connection_proto->Set("isConnected", v8::FunctionTemplate::New(ClientConnection_isConnected));
    connection_proto->Set("reconnect", v8::FunctionTemplate::New(ClientConnection_reconnect));
    connection_proto->Set("toString", v8::FunctionTemplate::New(ClientConnection_toString));
    connection_proto->Set("getVersion", v8::FunctionTemplate::New(ClientConnection_getVersion));
    connection_proto->Set("getDatabaseName", v8::FunctionTemplate::New(ClientConnection_getDatabaseName));
    connection_proto->Set("setDatabaseName", v8::FunctionTemplate::New(ClientConnection_setDatabaseName));
    connection_proto->SetCallAsFunctionHandler(ClientConnection_ConstructorCallback);

    v8::Handle<v8::ObjectTemplate> connection_inst = connection_templ->InstanceTemplate();
    connection_inst->SetInternalFieldCount(2);

    TRI_AddGlobalVariableVocbase(context, "ArangoConnection", connection_proto->NewInstance());
    ConnectionTempl = v8::Persistent<v8::ObjectTemplate>::New(isolate, connection_inst);

    // add the client connection to the context:
    TRI_AddGlobalVariableVocbase(context, "SYS_ARANGO", wrapV8ClientConnection(ClientConnection));
  }

  TRI_AddGlobalVariableVocbase(context, "SYS_START_PAGER", v8::FunctionTemplate::New(JS_StartOutputPager)->GetFunction());
  TRI_AddGlobalVariableVocbase(context, "SYS_STOP_PAGER", v8::FunctionTemplate::New(JS_StopOutputPager)->GetFunction());
  TRI_AddGlobalVariableVocbase(context, "SYS_IMPORT_CSV_FILE", v8::FunctionTemplate::New(JS_ImportCsvFile)->GetFunction());
  TRI_AddGlobalVariableVocbase(context, "SYS_IMPORT_JSON_FILE", v8::FunctionTemplate::New(JS_ImportJsonFile)->GetFunction());
  TRI_AddGlobalVariableVocbase(context, "NORMALIZE_STRING", v8::FunctionTemplate::New(JS_normalize_string)->GetFunction());
  TRI_AddGlobalVariableVocbase(context, "COMPARE_STRING", v8::FunctionTemplate::New(JS_compare_string)->GetFunction());

  // .............................................................................
  // banner
  // .............................................................................

  // http://www.network-science.de/ascii/   Font: ogre

  if (! BaseClient.quiet()) {

#ifdef _WIN32

    // .............................................................................
    // Quick hack for windows
    // .............................................................................

    if (BaseClient.colors()) {
      int greenColour   = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
      int redColour     = FOREGROUND_RED | FOREGROUND_INTENSITY;
      int defaultColour = 0;
      CONSOLE_SCREEN_BUFFER_INFO csbiInfo;

      if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbiInfo) != 0) {
        defaultColour = csbiInfo.wAttributes;
      }
     
      // not sure about the code page. let user set code page by command-line argument if required
      if (CodePage > 0) {
        SetConsoleOutputCP((UINT) CodePage);
      }
      else {
        UINT cp = GetConsoleOutputCP();
        SetConsoleOutputCP(cp);
      }

      // TODO we should have a special "printf" which can handle the color escape sequences!
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), greenColour);
      printf("                                  ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), redColour);
      printf("     _     ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColour);
      printf("\n");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), greenColour);
      printf("  __ _ _ __ __ _ _ __   __ _  ___ ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), redColour);
      printf(" ___| |__  ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColour);
      printf("\n");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), greenColour);
      printf(" / _` | '__/ _` | '_ \\ / _` |/ _ \\");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), redColour);
      printf("/ __| '_ \\ ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColour);
      printf("\n");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), greenColour);
      printf("| (_| | | | (_| | | | | (_| | (_) ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), redColour);
      printf("\\__ \\ | | |");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColour);
      printf("\n");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), greenColour);
      printf(" \\__,_|_|  \\__,_|_| |_|\\__, |\\___/");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), redColour);
      printf("|___/_| |_|");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColour);
      printf("\n");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), greenColour);
      printf("                       |___/      ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), redColour);
      printf("           ");
      SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), defaultColour);
      printf("\n");
    }

#else

    char const* g = TRI_SHELL_COLOR_GREEN;
    char const* r = TRI_SHELL_COLOR_RED;
    char const* z = TRI_SHELL_COLOR_RESET;

    if (! BaseClient.colors()) {
      g = "";
      r = "";
      z = "";
    }

    BaseClient.printLine("");

    printf("%s                                  %s     _     %s\n", g, r, z);
    printf("%s  __ _ _ __ __ _ _ __   __ _  ___ %s ___| |__  %s\n", g, r, z);
    printf("%s / _` | '__/ _` | '_ \\ / _` |/ _ \\%s/ __| '_ \\ %s\n", g, r, z);
    printf("%s| (_| | | | (_| | | | | (_| | (_) %s\\__ \\ | | |%s\n", g, r, z);
    printf("%s \\__,_|_|  \\__,_|_| |_|\\__, |\\___/%s|___/_| |_|%s\n", g, r, z);
    printf("%s                       |___/      %s           %s\n", g, r, z);

#endif
    BaseClient.printLine("");

    ostringstream s;
    s << "Welcome to arangosh " << TRI_VERSION_FULL << ". Copyright (c) triAGENS GmbH";

    BaseClient.printLine(s.str(), true);

    ostringstream info;
    info << "Using ";

#ifdef TRI_V8_VERSION
    info << "Google V8 " << TRI_V8_VERSION << " JavaScript engine";
#else
    info << "Google V8 JavaScript engine";
#endif

#ifdef TRI_READLINE_VERSION
    info << ", READLINE " << TRI_READLINE_VERSION;
#endif

#ifdef TRI_ICU_VERSION
    info << ", ICU " << TRI_ICU_VERSION;
#endif

    BaseClient.printLine(info.str(), true); 
    BaseClient.printLine("", true);

    BaseClient.printWelcomeInfo();

    if (useServer) {
      if (ClientConnection->isConnected() && ClientConnection->getLastHttpReturnCode() == HttpResponse::OK) {
        ostringstream is;
        is << "Connected to ArangoDB '" << BaseClient.endpointString()
           << "' version: " << ClientConnection->getVersion() << ", database: '" << BaseClient.databaseName() 
           << "', username: '" << BaseClient.username() << "'";

        BaseClient.printLine(is.str(), true);
      }
      else {
        ostringstream is;
        is << "Could not connect to endpoint '" << BaseClient.endpointString() 
           << "', database: '" << BaseClient.databaseName() 
           << "', username: '" << BaseClient.username() << "'";
        BaseClient.printErrLine(is.str());

        if (ClientConnection->getErrorMessage() != "") {
          ostringstream is2;
          is2 << "Error message '" << ClientConnection->getErrorMessage() << "'";
          BaseClient.printErrLine(is2.str());
        }
        promptError = true;
      }

      BaseClient.printLine("", true);
    }
  }

  // .............................................................................
  // read files
  // .............................................................................

  // load java script from js/bootstrap/*.h files
  if (StartupPath.empty()) {
    LOG_FATAL_AND_EXIT("no 'javascript.startup-directory' has been supplied, giving up");
  }

  LOG_DEBUG("using JavaScript startup files at '%s'", StartupPath.c_str());
  StartupLoader.setDirectory(StartupPath);

  TRI_AddGlobalVariableVocbase(context, "ARANGO_QUIET", v8::Boolean::New(BaseClient.quiet()));
  TRI_AddGlobalVariableVocbase(context, "VALGRIND", v8::Boolean::New((RUNNING_ON_VALGRIND) > 0));

  bool isExecuteScript = false;
  bool isExecuteString = false;
  bool isCheckScripts  = false;
  bool isUnitTests     = false;
  bool isJsLint        = false;

  if (! ExecuteScripts.empty()) {
    isExecuteScript = true;
  }
  else if (! ExecuteString.empty()) {
    isExecuteString = true;
  }
  else if (! CheckScripts.empty()) {
    isCheckScripts  = true;
  }
  else if (! UnitTests.empty()) {
    isUnitTests     = true;
  }
  else if (! JsLint.empty()) {
    isJsLint        = true;
  }

  TRI_AddGlobalVariableVocbase(context, "IS_EXECUTE_SCRIPT", v8::Boolean::New(isExecuteScript));
  TRI_AddGlobalVariableVocbase(context, "IS_EXECUTE_STRING", v8::Boolean::New(isExecuteString));
  TRI_AddGlobalVariableVocbase(context, "IS_CHECK_SCRIPT", v8::Boolean::New(isCheckScripts));
  TRI_AddGlobalVariableVocbase(context, "IS_UNIT_TESTS", v8::Boolean::New(isUnitTests));
  TRI_AddGlobalVariableVocbase(context, "IS_JS_LINT", v8::Boolean::New(isJsLint));

  // load all init files
  vector<string> files;

  files.push_back("common/bootstrap/modules.js");
  files.push_back("common/bootstrap/module-internal.js");
  files.push_back("common/bootstrap/module-fs.js");
  files.push_back("common/bootstrap/module-console.js");  // needs internal
  files.push_back("common/bootstrap/errors.js");

  if (! isJsLint) {
    files.push_back("common/bootstrap/monkeypatches.js");
  }

  files.push_back("client/bootstrap/module-internal.js");
  files.push_back("client/client.js"); // needs internal

  for (size_t i = 0;  i < files.size();  ++i) {
    bool ok = StartupLoader.loadScript(context, files[i]);

    if (ok) {
      LOG_TRACE("loaded JavaScript file '%s'", files[i].c_str());
    }
    else {
      LOG_FATAL_AND_EXIT("cannot load JavaScript file '%s'", files[i].c_str());
    }
  }

  // .............................................................................
  // create arguments
  // .............................................................................

  v8::Handle<v8::Array> p = v8::Array::New((int) positionals.size());

  for (uint32_t i = 0;  i < positionals.size();  ++i) {
    p->Set(i, v8::String::New(positionals[i].c_str(), (int) positionals[i].size()));
  }
  
  TRI_AddGlobalVariableVocbase(context, "ARGUMENTS", p);

  // .............................................................................
  // start logging
  // .............................................................................

  BaseClient.openLog();

  // .............................................................................
  // run normal shell
  // .............................................................................

  if (! (isExecuteScript || isExecuteString || isCheckScripts || isUnitTests || isJsLint)) {
    RunShell(context, promptError);
  }

  // .............................................................................
  // run unit tests or jslint
  // .............................................................................

  else {
    bool ok = false;

    if (isExecuteScript) {
      // we have scripts to execute
      ok = RunScripts(context, ExecuteScripts, true);
    }
    else if (isExecuteString) {
      // we have string to execute
      ok = RunString(context, ExecuteString);
    }
    else if (isCheckScripts) {
      // we have scripts to syntax check
      ok = RunScripts(context, CheckScripts, false);
    }
    else if (isUnitTests) {
      // we have unit tests
      ok = RunUnitTests(context);
    }
    else if (isJsLint) {
      // we don't have unittests, but we have files to jslint
      ok = RunJsLint(context);
    }

    if (! ok) {
      ret = EXIT_FAILURE;
    }
  }

  // .............................................................................
  // cleanup
  // .............................................................................

  context->Exit();
  context.Dispose(isolate);

  BaseClient.closeLog();

  // calling dispose in V8 3.10.x causes a segfault. the v8 docs says its not necessary to call it upon program termination
  // v8::V8::Dispose();

  TRIAGENS_REST_SHUTDOWN;

  arangoshExitFunction(ret, NULL);

  return ret;
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
