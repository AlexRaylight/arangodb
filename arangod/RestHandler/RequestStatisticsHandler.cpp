////////////////////////////////////////////////////////////////////////////////
/// @brief statistics handler
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2010-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "RequestStatisticsHandler.h"

#include "Basics/StringUtils.h"
#include "Rest/HttpRequest.h"
#include "Variant/VariantArray.h"

using namespace triagens::arango;
using namespace triagens::basics;
using namespace triagens::rest;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new handler
////////////////////////////////////////////////////////////////////////////////

RequestStatisticsHandler::RequestStatisticsHandler (HttpRequest* request) 
  : RestBaseHandler(request) {
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                   Handler methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool RequestStatisticsHandler::isDirect () {
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

Handler::status_e RequestStatisticsHandler::execute () {
  bool showTotalTime = false;
  bool showQueueTime = false;
  bool showRequestTime = false;
  bool showBytesSent = false;
  bool showBytesReceived = false;

  TRI_request_statistics_granularity_e granularity = TRI_REQUEST_STATISTICS_MINUTES;

  int length = -1;

  // .............................................................................
  // extract the figures to show
  // .............................................................................

  bool found;
  string figures = StringUtils::tolower(_request->value("figures", found));

  if (found) {
    if (figures == "*" || figures == "all") {
      showTotalTime = true;
      showQueueTime = true;
      showRequestTime = true;
      showBytesSent = true;
      showBytesReceived = true;
    }
    else {
      vector<string> f = StringUtils::split(figures);

      for (vector<string>::iterator i = f.begin();  i != f.end();  ++i) {
        string const& fn = *i;
        
        if (fn == "totaltime") {
          showTotalTime = true;
        }
        else if (fn == "queuetime") {
          showQueueTime = true;
        }
        else if (fn == "requesttime") {
          showRequestTime = true;
        }
        else if (fn == "bytessent") {
          showBytesSent = true;
        }
        else if (fn == "bytesreceived") {
          showBytesReceived = true;
        }
        else {
          generateError(HttpResponse::BAD, TRI_ERROR_HTTP_BAD_PARAMETER, "unknown figure '" + fn + "'");
          return HANDLER_DONE;
        }
      }
    }

  }
  else {
    showTotalTime = true;
    showBytesSent = true;
    showBytesReceived = true;
  }

  // .............................................................................
  // extract the granularity to show
  // .............................................................................

  string gran = StringUtils::tolower(_request->value("granularity", found));

  if (found) {
    if (gran == "minute" || gran == "minutes") {
      granularity = TRI_REQUEST_STATISTICS_MINUTES;
    }
    else if (gran == "hour" || gran == "hours") {
      granularity = TRI_REQUEST_STATISTICS_HOURS;
    }
    else if (gran == "day" || gran == "days") {
      granularity = TRI_REQUEST_STATISTICS_DAYS;
    }
  }
  else {
    granularity = TRI_REQUEST_STATISTICS_MINUTES;
  }

  // .............................................................................
  // extract the length
  // .............................................................................

  string l = StringUtils::tolower(_request->value("length", found));

  if (found) {
    if (l == "current") {
      length = 0;
    }
    else {
      length = StringUtils::uint32(l);
    }
  }
  else {
    length = -1;
  }

  // .............................................................................
  // extract the statistics blocks
  // .............................................................................

  VariantArray* result = TRI_RequestStatistics(granularity,
                                               length,
                                               showTotalTime,
                                               showQueueTime,
                                               showRequestTime,
                                               showBytesSent,
                                               showBytesReceived);

  generateResult(result);
  return HANDLER_DONE;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\)"
// End:


