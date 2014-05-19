////////////////////////////////////////////////////////////////////////////////
/// @brief mruby client connection
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
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_MRCLIENT_MRUBY_CLIENT_CONNECTION_H
#define TRIAGENS_MRCLIENT_MRUBY_CLIENT_CONNECTION_H 1

#include "Basics/Common.h"

#include "MRuby/mr-utils.h"
#include "Rest/HttpRequest.h"

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

namespace triagens {
  namespace httpclient {
    class GeneralClientConnection;
    class SimpleHttpClient;
    class SimpleHttpResult;
  }

  namespace rest {
    class Endpoint;
  }
}

// -----------------------------------------------------------------------------
// --SECTION--                                       class MRubyClientConnection
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief class for http requests
////////////////////////////////////////////////////////////////////////////////

namespace triagens {
  namespace mrclient {

    class MRubyClientConnection {
      private:
        MRubyClientConnection (MRubyClientConnection const&);
        MRubyClientConnection& operator= (MRubyClientConnection const&);

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

        MRubyClientConnection (mrb_state*,
                               triagens::rest::Endpoint*,
                               const string& username,
                               const string& passwort,
                               double requestTimeout,
                               double connectionTimeout,
                               size_t numRetries,
                               bool warn);

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

        ~MRubyClientConnection ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief returns true if it is connected
////////////////////////////////////////////////////////////////////////////////

        bool isConnected ();

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the version and build number of the arango server
////////////////////////////////////////////////////////////////////////////////

        const string& getVersion ();

////////////////////////////////////////////////////////////////////////////////
/// @brief get the last http return code
///
/// @return int          the code
////////////////////////////////////////////////////////////////////////////////

        int getLastHttpReturnCode ();

////////////////////////////////////////////////////////////////////////////////
/// @brief get the last error message
///
/// @return string          the error message
////////////////////////////////////////////////////////////////////////////////

        const std::string& getErrorMessage ();

////////////////////////////////////////////////////////////////////////////////
/// @brief get the endpoint string
///
/// @return string
////////////////////////////////////////////////////////////////////////////////

        const std::string getEndpointSpecification ();

////////////////////////////////////////////////////////////////////////////////
/// @brief get the simple http client
///
/// @return triagens::httpclient::SimpleHttpClient*    then client connection
////////////////////////////////////////////////////////////////////////////////

        triagens::httpclient::SimpleHttpClient* getHttpClient();

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "GET" request
///
/// @param string location                     the request location
/// @param map<string, string> headerFields    additional header fields
///
/// @return v8::Value                          a V8 JavaScript object
////////////////////////////////////////////////////////////////////////////////

        mrb_value getData (std::string const& location,
                           map<string, string> const& headerFields);

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "DELETE" request
///
/// @param string location                     the request location
/// @param map<string, string> headerFields    additional header fields
///
/// @return v8::Value                          a V8 JavaScript object
////////////////////////////////////////////////////////////////////////////////

        mrb_value deleteData (std::string const& location,
                                          map<string, string> const& headerFields);

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "HEAD" request
///
/// @param string location                     the request location
/// @param map<string, string> headerFields    additional header fields
///
/// @return v8::Value                          a V8 JavaScript object
////////////////////////////////////////////////////////////////////////////////

        mrb_value headData (std::string const& location,
                            map<string, string> const& headerFields);

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "POST" request
///
/// @param string location                     the request location
/// @param string body                         the request body
/// @param map<string, string> headerFields    additional header fields
///
/// @return v8::Value                          a V8 JavaScript object
////////////////////////////////////////////////////////////////////////////////

        mrb_value postData (std::string const& location,
                            std::string const& body,
                            map<string, string> const& headerFields);

////////////////////////////////////////////////////////////////////////////////
/// @brief do a "PUT" request
///
/// @param string location                     the request location
/// @param string body                         the request body
/// @param map<string, string> headerFields    additional header fields
///
/// @return v8::Value                          a V8 JavaScript object
////////////////////////////////////////////////////////////////////////////////

        mrb_value putData (std::string const& location,
                           std::string const& body,
                           map<string, string> const& headerFields);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

    private:

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a request
////////////////////////////////////////////////////////////////////////////////

      mrb_value requestData (rest::HttpRequest::HttpRequestType method,
                             std::string const& location,
                             std::string const& body,
                             map<string, string> const& headerFields);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

    private:

////////////////////////////////////////////////////////////////////////////////
/// @brief ruby state
////////////////////////////////////////////////////////////////////////////////

      mrb_state* _mrb;

////////////////////////////////////////////////////////////////////////////////
/// @brief connection
////////////////////////////////////////////////////////////////////////////////

      triagens::httpclient::GeneralClientConnection* _connection;


////////////////////////////////////////////////////////////////////////////////
/// @brief server version
////////////////////////////////////////////////////////////////////////////////

      std::string _version;

////////////////////////////////////////////////////////////////////////////////
/// @brief connection status
////////////////////////////////////////////////////////////////////////////////

      bool _connected;

////////////////////////////////////////////////////////////////////////////////
/// @brief last http return code
////////////////////////////////////////////////////////////////////////////////

      int _lastHttpReturnCode;

////////////////////////////////////////////////////////////////////////////////
/// @brief last error message
////////////////////////////////////////////////////////////////////////////////

      std::string _lastErrorMessage;

////////////////////////////////////////////////////////////////////////////////
/// @brief underlying client
////////////////////////////////////////////////////////////////////////////////

      triagens::httpclient::SimpleHttpClient* _client;

////////////////////////////////////////////////////////////////////////////////
/// @brief last result
////////////////////////////////////////////////////////////////////////////////

      triagens::httpclient::SimpleHttpResult* _httpResult;
    };
  }
}

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
