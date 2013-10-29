////////////////////////////////////////////////////////////////////////////////
/// @brief connection endpoint, Unix domain socket
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
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "EndpointUnixDomain.h"

#include "Basics/Common.h"
#include "Basics/FileUtils.h"
#include "Basics/StringUtils.h"
#include "BasicsC/logging.h"

#include "Rest/Endpoint.h"

using namespace triagens::basics;
using namespace triagens::rest;

#ifdef TRI_HAVE_LINUX_SOCKETS

// -----------------------------------------------------------------------------
// --SECTION--                                                EndpointUnixDomain
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a Unix socket endpoint
////////////////////////////////////////////////////////////////////////////////

EndpointUnixDomain::EndpointUnixDomain (const Endpoint::EndpointType type,
                                        const std::string& specification,
                                        int listenBacklog,
                                        const std::string& path) :
    Endpoint(type, DOMAIN_UNIX, ENCRYPTION_NONE, specification, listenBacklog),
    _path(path) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a Unix socket endpoint
////////////////////////////////////////////////////////////////////////////////

EndpointUnixDomain::~EndpointUnixDomain () {
  if (_connected) {
    disconnect();
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Rest
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief connect the endpoint
////////////////////////////////////////////////////////////////////////////////

TRI_socket_t EndpointUnixDomain::connect (double connectTimeout, double requestTimeout) {
  TRI_socket_t listenSocket;
  listenSocket.fileDescriptor = 0;
  listenSocket.fileHandle = 0;

  LOG_DEBUG("connecting to unix endpoint '%s'", _specification.c_str());

  assert(_socket.fileHandle == 0);
  assert(!_connected);

  if (_type == ENDPOINT_SERVER && FileUtils::exists(_path)) {
    // socket file already exists
    LOG_WARNING("socket file '%s' already exists.", _path.c_str());

    int error = 0;
    // delete previously existing socket file
    if (FileUtils::remove(_path, &error)) {
      LOG_WARNING("deleted previously existing socket file '%s'", _path.c_str());
    }
    else {
      LOG_ERROR("unable to delete previously existing socket file '%s'", _path.c_str());

      return listenSocket;
    }
  }

  listenSocket.fileHandle = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenSocket.fileHandle == -1) {
    LOG_ERROR("socket() failed with %d (%s)", errno, strerror(errno));
    listenSocket.fileDescriptor = 0;
    listenSocket.fileHandle = 0;
    return listenSocket;
  }

  // reuse address
  int opt = 1;

  if (setsockopt(listenSocket.fileHandle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*> (&opt), sizeof (opt)) == -1) {
    LOG_ERROR("setsockopt() failed with %d (%s)", errno, strerror(errno));

    TRI_CLOSE_SOCKET(listenSocket);
    listenSocket.fileDescriptor = 0;
    listenSocket.fileHandle = 0;
    return listenSocket;
  }
  LOG_TRACE("reuse address flag set");

  struct sockaddr_un address;

  memset(&address, 0, sizeof(address));
  address.sun_family = AF_UNIX;
  snprintf(address.sun_path, 100, "%s", _path.c_str());

  if (_type == ENDPOINT_SERVER) {
    int result = bind(listenSocket.fileHandle, (struct sockaddr*) &address, SUN_LEN(&address));
    if (result != 0) {
      // bind error
      LOG_ERROR("bind() failed with %d (%s)", errno, strerror(errno));
      TRI_CLOSE_SOCKET(listenSocket);
      listenSocket.fileDescriptor = 0;
      listenSocket.fileHandle = 0;
      return listenSocket;
    }

    // listen for new connection, executed for server endpoints only
    LOG_TRACE("using backlog size %d", (int) _listenBacklog);
    result = listen(listenSocket.fileHandle, _listenBacklog);

    if (result < 0) {
      LOG_ERROR("listen() failed with %d (%s)", errno, strerror(errno));
      TRI_CLOSE_SOCKET(listenSocket);
      listenSocket.fileDescriptor = 0;
      listenSocket.fileHandle = 0;
      return listenSocket;
    }
  }

  else if (_type == ENDPOINT_CLIENT) {
    // connect to endpoint, executed for client endpoints only

    // set timeout
    setTimeout(listenSocket, connectTimeout);

    if (::connect(listenSocket.fileHandle, (const struct sockaddr*) &address, SUN_LEN(&address)) != 0) {
      TRI_CLOSE_SOCKET(listenSocket);
      listenSocket.fileDescriptor = 0;
      listenSocket.fileHandle = 0;
      return listenSocket;
    }
  }

  if (!setSocketFlags(listenSocket)) {
    TRI_CLOSE_SOCKET(listenSocket);
    listenSocket.fileDescriptor = 0;
    listenSocket.fileHandle = 0;
    return listenSocket;
  }

  if (_type == ENDPOINT_CLIENT) {
    setTimeout(listenSocket, requestTimeout);
  }

  _connected = true;
  _socket = listenSocket;

  return _socket;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief disconnect the endpoint
////////////////////////////////////////////////////////////////////////////////

void EndpointUnixDomain::disconnect () {
  if (_connected) {
    assert(_socket.fileHandle);

    _connected = false;
    TRI_CLOSE_SOCKET(_socket);

    _socket.fileHandle = 0;
    _socket.fileDescriptor = 0;

    if (_type == ENDPOINT_SERVER) {
      int error = 0;
      if (! FileUtils::remove(_path, &error)) {
        LOG_TRACE("unable to remove socket file '%s'", _path.c_str());
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief init an incoming connection
////////////////////////////////////////////////////////////////////////////////

bool EndpointUnixDomain::initIncoming (TRI_socket_t incoming) {
  return setSocketFlags(incoming);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
