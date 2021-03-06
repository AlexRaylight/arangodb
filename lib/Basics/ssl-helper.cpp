////////////////////////////////////////////////////////////////////////////////
/// @brief ssl helper functions
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
/// @author Jan Steemann
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "ssl-helper.h"

#include <openssl/err.h>

#include "Basics/logging.h"

using namespace triagens::basics;
using namespace std;

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an SSL context
////////////////////////////////////////////////////////////////////////////////

SSL_CTX* triagens::basics::sslContext (protocol_e protocol, string const& keyfile) {

  // create our context
  SSL_METHOD SSL_CONST* meth = nullptr;

  switch (protocol) {
#ifndef OPENSSL_NO_SSL2
    case SSL_V2:
      meth = SSLv2_method();
      break;
#endif
    case SSL_V3:
      meth = SSLv3_method();
      break;

    case SSL_V23:
      meth = SSLv23_method();
      break;

    case TLS_V1:
      meth = TLSv1_method();
      break;

    default:
      LOG_ERROR("unknown SSL protocol method");
      return nullptr;
  }

  SSL_CTX* sslctx = SSL_CTX_new(meth);
            
  // load our keys and certificates
  if (! SSL_CTX_use_certificate_chain_file(sslctx, keyfile.c_str())) {
    LOG_ERROR("cannot read certificate from '%s': %s", keyfile.c_str(), triagens::basics::lastSSLError().c_str());
    return nullptr;
  }

  if (! SSL_CTX_use_PrivateKey_file(sslctx, keyfile.c_str(), SSL_FILETYPE_PEM)) {
    LOG_ERROR("cannot read key from '%s': %s", keyfile.c_str(), triagens::basics::lastSSLError().c_str());
    return nullptr;
  }

#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
  SSL_CTX_set_verify_depth(sslctx, 1);
#endif

  return sslctx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the name of an SSL protocol version
////////////////////////////////////////////////////////////////////////////////

string triagens::basics::protocolName (const protocol_e protocol) {
  switch (protocol) {
    case SSL_V2:
      return "SSLv2";

    case SSL_V23:
      return "SSLv23";

    case SSL_V3:
      return "SSLv3";

    case TLS_V1:
      return "TLSv1";

    default:
      return "unknown";
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get last SSL error
////////////////////////////////////////////////////////////////////////////////

string triagens::basics::lastSSLError () {
  char buf[122];
  memset(buf, 0, sizeof(buf));

  unsigned long err = ERR_get_error();
  ERR_error_string_n(err, buf, sizeof(buf) - 1);

  return string(buf);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
