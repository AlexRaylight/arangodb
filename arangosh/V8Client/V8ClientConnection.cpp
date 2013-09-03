////////////////////////////////////////////////////////////////////////////////
/// @brief v8 client connection
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
/// @author Achim Brandt
/// @author Copyright 2008-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "V8ClientConnection.h"

#include <sstream>

#include "Basics/JsonHelper.h"
#include "Basics/StringUtils.h"
#include "BasicsC/json.h"
#include "BasicsC/tri-strings.h"
#include "Rest/HttpRequest.h"
#include "Rest/HttpResponse.h"
#include "SimpleHttpClient/GeneralClientConnection.h"
#include "SimpleHttpClient/SimpleHttpClient.h"
#include "SimpleHttpClient/SimpleHttpResult.h"
#include "V8/v8-conv.h"

using namespace triagens::basics;
using namespace triagens::httpclient;
using namespace triagens::rest;
using namespace triagens::v8client;
using namespace std;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8ClientConnection
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

V8ClientConnection::V8ClientConnection (Endpoint* endpoint,
                                        const string& username,
                                        const string& password,
                                        double requestTimeout,
                                        double connectTimeout,
                                        size_t numRetries,
                                        bool warn)
  : _connection(0),
    _lastHttpReturnCode(0),
    _lastErrorMessage(""),
    _client(0),
    _httpResult(0) {


  _connection = GeneralClientConnection::factory(endpoint, requestTimeout, connectTimeout, numRetries);

  if (_connection == 0) {
    throw "out of memory";
  }

  _client = new SimpleHttpClient(_connection, requestTimeout, warn);

  if (_client == 0) {
    throw "out of memory";
  }

  _client->setUserNamePassword("/", username, password);

  // connect to server and get version number
  map<string, string> headerFields;
  SimpleHttpResult* result = _client->request(HttpRequest::HTTP_REQUEST_GET, "/_api/version", 0, 0, headerFields);

  if (! result || ! result->isComplete()) {
    // save error message
    _lastErrorMessage = _client->getErrorMessage();
    _lastHttpReturnCode = 500;
  }
  else {
    _lastHttpReturnCode = result->getHttpReturnCode();

    if (result->getHttpReturnCode() == HttpResponse::OK) {
      // default value
      _version = "arango";

      // convert response body to json
      TRI_json_t* json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, result->getBody().str().c_str());

      if (json) {
        // look up "server" value
        const string server = JsonHelper::getStringValue(json, "server", "");

        // "server" value is a string and content is "arango"
        if (server == "arango") {
          // look up "version" value
          _version = JsonHelper::getStringValue(json, "version", "");
        }

        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }
    }
    else {
      // initial request for /_api/version return some non-HTTP 200 response.
      // now set up an error message
      _lastErrorMessage = _client->getErrorMessage();

      if (result) {
        _lastErrorMessage = StringUtils::itoa(result->getHttpReturnCode()) + ": " + result->getHttpReturnMessage();
      }
    }
  }

  if (result) {
    delete result;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

V8ClientConnection::~V8ClientConnection () {
  if (_httpResult) {
    delete _httpResult;
  }

  if (_client) {
    delete _client;
  }

  if (_connection) {
    delete _connection;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8ClientConnection
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief returns true if it is connected
////////////////////////////////////////////////////////////////////////////////

bool V8ClientConnection::isConnected () {
  return _connection->isConnected();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the version and build number of the arango server
////////////////////////////////////////////////////////////////////////////////

const string& V8ClientConnection::getVersion () {
  return _version;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the last http return code
////////////////////////////////////////////////////////////////////////////////

int V8ClientConnection::getLastHttpReturnCode () {
  return _lastHttpReturnCode;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the last error message
////////////////////////////////////////////////////////////////////////////////

const std::string& V8ClientConnection::getErrorMessage () {
  return _lastErrorMessage;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the simple http client
////////////////////////////////////////////////////////////////////////////////

triagens::httpclient::SimpleHttpClient* V8ClientConnection::getHttpClient() {
  return _client;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "GET" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::getData (std::string const& location,
                                                   map<string, string> const& headerFields,
                                                   bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_GET, location, "", headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_GET, location, "", headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "DELETE" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::deleteData (std::string const& location,
                                                      map<string, string> const& headerFields,
                                                      bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_DELETE, location, "", headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_DELETE, location, "", headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "HEAD" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::headData (std::string const& location,
                                                    map<string, string> const& headerFields,
                                                    bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_HEAD, location, "", headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_HEAD, location, "", headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do an "OPTIONS" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::optionsData (std::string const& location,
                                                       std::string const& body,
                                                       map<string, string> const& headerFields,
                                                       bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_OPTIONS, location, body, headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_OPTIONS, location, body, headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "POST" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::postData (std::string const& location,
                                                    std::string const& body,
                                                    map<string, string> const& headerFields,
                                                    bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_POST, location, body, headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_POST, location, body, headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "POST" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::postData (std::string const& location,
                                                    const char* body,
                                                    const size_t bodySize,
                                                    map<string, string> const& headerFields) {
  return requestData(HttpRequest::HTTP_REQUEST_POST, location, body, bodySize, headerFields);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "PUT" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::putData (std::string const& location,
                                                   std::string const& body,
                                                   map<string, string> const& headerFields,
                                                   bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_PUT, location, body, headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_PUT, location, body, headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "PATCH" request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::patchData (std::string const& location,
                                                     std::string const& body,
                                                     map<string, string> const& headerFields,
                                                     bool raw) {
  if (raw) {
    return requestDataRaw(HttpRequest::HTTP_REQUEST_PATCH, location, body, headerFields);
  }
  else {
    return requestData(HttpRequest::HTTP_REQUEST_PATCH, location, body, headerFields);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8ClientConnection
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::requestData (HttpRequest::HttpRequestType method,
                                                       string const& location,
                                                       const char* body,
                                                       const size_t bodySize,
                                                       map<string, string> const& headerFields) {
  
  _lastErrorMessage = "";
  _lastHttpReturnCode = 0;

  if (_httpResult) {
    delete _httpResult;
  }

  _httpResult = _client->request(method, location, body, bodySize, headerFields);

  return handleResult();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a request
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::requestData (HttpRequest::HttpRequestType method,
                                                       string const& location,
                                                       string const& body,
                                                       map<string, string> const& headerFields) {
  _lastErrorMessage = "";
  _lastHttpReturnCode = 0;

  if (_httpResult) {
    delete _httpResult;
  }

  if (body.empty()) {
    _httpResult = _client->request(method, location, 0, 0, headerFields);
  }
  else {
    _httpResult = _client->request(method, location, body.c_str(), body.length(), headerFields);
  }

  return handleResult();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a result
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::handleResult () {
  if (!_httpResult->isComplete()) {
    // not complete
    _lastErrorMessage = _client->getErrorMessage();

    if (_lastErrorMessage.empty()) {
      _lastErrorMessage = "Unknown error";
    }

    _lastHttpReturnCode = HttpResponse::SERVER_ERROR;

    v8::Handle<v8::Object> result = v8::Object::New();
    result->Set(v8::String::New("error"), v8::Boolean::New(true));
    result->Set(v8::String::New("code"), v8::Integer::New(HttpResponse::SERVER_ERROR));

    int errorNumber = 0;

    switch (_httpResult->getResultType()) {
      case (SimpleHttpResult::COULD_NOT_CONNECT) :
        errorNumber = TRI_SIMPLE_CLIENT_COULD_NOT_CONNECT;
        break;

      case (SimpleHttpResult::READ_ERROR) :
        errorNumber = TRI_SIMPLE_CLIENT_COULD_NOT_READ;
        break;

      case (SimpleHttpResult::WRITE_ERROR) :
        errorNumber = TRI_SIMPLE_CLIENT_COULD_NOT_WRITE;
        break;

      default:
        errorNumber = TRI_SIMPLE_CLIENT_UNKNOWN_ERROR;
        break;
    }

    result->Set(v8::String::New("errorNum"), v8::Integer::New(errorNumber));
    result->Set(v8::String::New("errorMessage"), v8::String::New(_lastErrorMessage.c_str(), _lastErrorMessage.length()));

    return result;
  }
  else {
    // complete
    _lastHttpReturnCode = _httpResult->getHttpReturnCode();

    // got a body
    if (_httpResult->getBody().str().length() > 0) {
      string contentType = _httpResult->getContentType(true);

      if (contentType == "application/json") {
        TRI_json_t* js = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, _httpResult->getBody().str().c_str());

        if (js != NULL) {
          // return v8 object
          v8::Handle<v8::Value> result = TRI_ObjectJson(js);
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, js);

          return result;
        }
      }

      // return body as string
      v8::Handle<v8::String> result = v8::String::New(_httpResult->getBody().str().c_str(), _httpResult->getBody().str().length());

      return result;
    }
    else {
      // no body
      v8::HandleScope scope;

      v8::Handle<v8::Object> result = v8::Object::New();

      result->Set(v8::String::New("code"), v8::Integer::New(_lastHttpReturnCode));

      if (_lastHttpReturnCode >= 400) {
        string returnMessage(_httpResult->getHttpReturnMessage());

        result->Set(v8::String::New("error"), v8::Boolean::New(true));
        result->Set(v8::String::New("errorNum"), v8::Integer::New(_lastHttpReturnCode));
        result->Set(v8::String::New("errorMessage"), v8::String::New(returnMessage.c_str(), returnMessage.size()));
      }
      else {
        result->Set(v8::String::New("error"), v8::Boolean::New(false));
      }

      return scope.Close(result);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a request and returns raw response
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> V8ClientConnection::requestDataRaw (HttpRequest::HttpRequestType method,
                                                          string const& location,
                                                          string const& body,
                                                          map<string, string> const& headerFields) {
  v8::HandleScope scope;

  _lastErrorMessage = "";
  _lastHttpReturnCode = 0;

  if (_httpResult) {
    delete _httpResult;
  }

  if (body.empty()) {
    _httpResult = _client->request(method, location, 0, 0, headerFields);
  }
  else {
    _httpResult = _client->request(method, location, body.c_str(), body.length(), headerFields);
  }

  if (!_httpResult->isComplete()) {
    // not complete
    _lastErrorMessage = _client->getErrorMessage();

    if (_lastErrorMessage.empty()) {
      _lastErrorMessage = "Unknown error";
    }

    _lastHttpReturnCode = HttpResponse::SERVER_ERROR;

    v8::Handle<v8::Object> result = v8::Object::New();
    result->Set(v8::String::New("code"), v8::Integer::New(HttpResponse::SERVER_ERROR));

    int errorNumber = 0;

    switch (_httpResult->getResultType()) {
      case (SimpleHttpResult::COULD_NOT_CONNECT) :
        errorNumber = TRI_SIMPLE_CLIENT_COULD_NOT_CONNECT;
        break;

      case (SimpleHttpResult::READ_ERROR) :
        errorNumber = TRI_SIMPLE_CLIENT_COULD_NOT_READ;
        break;

      case (SimpleHttpResult::WRITE_ERROR) :
        errorNumber = TRI_SIMPLE_CLIENT_COULD_NOT_WRITE;
        break;

      default:
        errorNumber = TRI_SIMPLE_CLIENT_UNKNOWN_ERROR;
        break;
    }

    result->Set(v8::String::New("errorNum"), v8::Integer::New(errorNumber));
    result->Set(v8::String::New("errorMessage"), v8::String::New(_lastErrorMessage.c_str(), _lastErrorMessage.length()));

    return scope.Close(result);
  }
  else {
    // complete
    _lastHttpReturnCode = _httpResult->getHttpReturnCode();

    // create raw response
    v8::Handle<v8::Object> result = v8::Object::New();

    result->Set(v8::String::New("code"), v8::Integer::New(_lastHttpReturnCode));

    if (_lastHttpReturnCode >= 400) {
      string returnMessage(_httpResult->getHttpReturnMessage());

      result->Set(v8::String::New("error"), v8::Boolean::New(true));
      result->Set(v8::String::New("errorNum"), v8::Integer::New(_lastHttpReturnCode));
      result->Set(v8::String::New("errorMessage"), v8::String::New(returnMessage.c_str(), returnMessage.size()));
    }
    else {
      result->Set(v8::String::New("error"), v8::Boolean::New(false));
    }

    // got a body, copy it into the result
    if (_httpResult->getBody().str().length() > 0) {
      v8::Handle<v8::String> body = v8::String::New(_httpResult->getBody().str().c_str(), _httpResult->getBody().str().length());

      result->Set(v8::String::New("body"), body);
    }

    // copy all headers
    v8::Handle<v8::Object> headers = v8::Object::New();
    const map<string, string>& headerFields = _httpResult->getHeaderFields();

    for (map<string, string>::const_iterator i = headerFields.begin();  i != headerFields.end();  ++i) {
      v8::Handle<v8::String> key = v8::String::New(i->first.c_str());
      v8::Handle<v8::String> val = v8::String::New(i->second.c_str());

      headers->Set(key, val);
    }

    result->Set(v8::String::New("headers"), headers);

    // and returns
    return scope.Close(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
