/*jslint indent: 2, nomen: true, maxlen: 150, sloppy: true, vars: true, white: true, plusplus: true, stupid: true */
/*global require */

////////////////////////////////////////////////////////////////////////////////
/// @brief database management
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2013 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var arangodb = require("org/arangodb");
var actions = require("org/arangodb/actions");

var API = "_api/database";

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoAPI
/// @{
////////////////////////////////////////////////////////////////////////////////

function get_api_database (req, res) {
  if (req.suffix.length !== 0) { 
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var result = arangodb.db._listDatabases();

  actions.resultOk(req, res, actions.HTTP_OK, { result : result });
}

function post_api_database (req, res) {
  if (req.suffix.length !== 0) { 
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }
  
  var json = actions.getJsonBody(req, res);
  
  if (json === undefined) {
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var options = json.options;
  
  if (options === undefined) {
    options = { };
  }

  if (typeof options !== 'object') {
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var result = arangodb.db._createDatabase(json.name || "", json.path || "", options);

  actions.resultOk(req, res, actions.HTTP_OK, { result : result });
}

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a database request
////////////////////////////////////////////////////////////////////////////////

actions.defineHttp({
  url : API,
  context : "api",

  callback : function (req, res) {
    try {
      if (req.requestType === actions.GET) {
        get_api_database(req, res);
      }
      else if (req.requestType === actions.POST) {
        post_api_database(req, res);
      }
      else {
        actions.resultUnsupported(req, res);
      }
    }
    catch (err) {
      actions.resultException(req, res, err, undefined, false);
    }
  }
});

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @\\}"
// End:
