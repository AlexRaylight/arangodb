////////////////////////////////////////////////////////////////////////////////
/// @brief line request
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2011 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2008-2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_FYN_REST_LINE_REQUEST_H
#define TRIAGENS_FYN_REST_LINE_REQUEST_H 1

#include <Basics/Common.h>

#include <Rest/ConnectionInfo.h>
#include <Basics/StringBuffer.h>
#include <Basics/StringUtils.h>

namespace triagens {
  namespace rest {

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief line request
    ///
    /// The line server reads the request string from the client and converts it
    /// into an instance of this class. An line request object provides methods to
    /// inspect the header and parameter fields.
    ////////////////////////////////////////////////////////////////////////////////

    class  LineRequest {
      LineRequest (LineRequest const&);
      LineRequest& operator= (LineRequest const&);

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief constructor
        ////////////////////////////////////////////////////////////////////////////////

        LineRequest () 
          : _connectionInfo() {
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief destructor
        ////////////////////////////////////////////////////////////////////////////////

        virtual ~LineRequest () {
        }

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief adds a body line to the response
        ////////////////////////////////////////////////////////////////////////////////

        virtual void addBodyLine (char const* ptr, size_t length) = 0;

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief adds a body blob to the response
        ////////////////////////////////////////////////////////////////////////////////

        virtual void addBody (char const* ptr, size_t length) = 0;

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief marks request as malformed
        ////////////////////////////////////////////////////////////////////////////////

        virtual void setLineRequestInvalid () = 0;

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief
        ////////////////////////////////////////////////////////////////////////////////

        virtual void write (StringBuffer* buffer) {
        };

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief returns the server IP
        ////////////////////////////////////////////////////////////////////////////////

        ConnectionInfo const& connectionInfo () const {
          return _connectionInfo;
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief sets the server IP
        ////////////////////////////////////////////////////////////////////////////////

        void setConnectionInfo (ConnectionInfo const& info) {
          _connectionInfo = info;
        }

      protected:
        ConnectionInfo _connectionInfo;
    };
  }
}

#endif
