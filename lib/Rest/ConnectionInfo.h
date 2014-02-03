////////////////////////////////////////////////////////////////////////////////
/// @brief connection info
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
/// @author Copyright 2009-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_REST_CONNECTION_INFO_H
#define TRIAGENS_REST_CONNECTION_INFO_H 1

#include "Basics/Common.h"

#include "Basics/StringUtils.h"
#include "Rest/Endpoint.h"

namespace triagens {
  namespace rest {

////////////////////////////////////////////////////////////////////////////////
/// @brief connection info
////////////////////////////////////////////////////////////////////////////////

    struct ConnectionInfo {
      public:
        ConnectionInfo ()
          : serverPort(0),
            clientPort(0),
            serverAddress(),
            clientAddress(),
            endpoint(),
            endpointType(Endpoint::DOMAIN_UNKNOWN),
            sslContext(0) {
        }

        ConnectionInfo (ConnectionInfo const& that)
          : serverPort(that.serverPort),
            clientPort(that.clientPort),
            serverAddress(that.serverAddress),
            clientAddress(that.clientAddress),
            endpoint(that.endpoint),
            endpointType(that.endpointType),
            sslContext(that.sslContext) {
        }

        ConnectionInfo& operator= (ConnectionInfo const& that) {
          if (this != &that) {
            serverPort = that.serverPort;
            clientPort = that.clientPort;
            serverAddress = that.serverAddress;
            clientAddress = that.clientAddress;
            endpoint = that.endpoint;
            endpointType = that.endpointType;
            sslContext = that.sslContext;
          }

          return *this;
        }

      public:

        int serverPort;
        int clientPort;

        string serverAddress;
        string clientAddress;
        string endpoint;
        Endpoint::DomainType endpointType;

        void* sslContext;
    };
  }
}

#endif
