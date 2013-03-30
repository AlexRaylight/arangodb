/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, nonpropdel: true, white: true, plusplus: true, evil: true */
/*global require, IS_EXECUTE_SCRIPT, IS_CHECK_SCRIPT, IS_UNIT_TESTS, IS_JS_LINT */

////////////////////////////////////////////////////////////////////////////////
/// @brief ArangoShell client API
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2012 triagens GmbH, Cologne, Germany
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
/// @author Achim Brandt
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoShell
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief start paging
////////////////////////////////////////////////////////////////////////////////

function start_pager () {
  var internal = require("internal");
  internal.startPager();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop paging
////////////////////////////////////////////////////////////////////////////////

function stop_pager () {
  var internal = require("internal");
  internal.stopPager();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief print the overall help
////////////////////////////////////////////////////////////////////////////////

function help () {
  var internal = require("internal");
  var arangodb = require("org/arangodb");
  var client = require("org/arangodb/arangosh");

  internal.print(client.HELP);
  arangodb.ArangoStatement.prototype._help();
  arangodb.ArangoDatabase.prototype._help();
  arangodb.ArangoCollection.prototype._help();
  arangodb.ArangoQueryCursor.prototype._help();
  internal.print(client.helpExtended);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief clear screen (poor man's way)
////////////////////////////////////////////////////////////////////////////////

function clear () {
  var internal = require("internal");
  var i;
  var result = '';

  for (i = 0; i < 100; ++i) {
    result += '\n';
  }
  internal.print(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief prints help
////////////////////////////////////////////////////////////////////////////////

(function() {
  var internal = require("internal");
  var arangosh = require("org/arangodb/arangosh");
  var special;

  if (internal.db !== undefined) {
    try {
      internal.db._collections();
    }
    catch (err) {
    }
  }

  try {
    // these variables don't exist in the browser context
    special = IS_EXECUTE_SCRIPT || IS_CHECK_SCRIPT || IS_UNIT_TESTS || IS_JS_LINT;
  }
  catch (err2) {
    special = false;
  }

  if (internal.quiet !== true && ! special) {
    if (typeof internal.arango !== "undefined") {
      if (typeof internal.arango.isConnected !== "undefined" && internal.arango.isConnected()) {
        internal.print(arangosh.HELP);
      }
    }
  }
}());

////////////////////////////////////////////////////////////////////////////////
/// @brief global 'db'
////////////////////////////////////////////////////////////////////////////////

var db = require("org/arangodb").db;

////////////////////////////////////////////////////////////////////////////////
/// @brief global 'arango'
////////////////////////////////////////////////////////////////////////////////

var arango = require("org/arangodb").arango;

////////////////////////////////////////////////////////////////////////////////
/// @brief read rc file
////////////////////////////////////////////////////////////////////////////////


(function () {
  var special;

  try {
    // these variables are not defined in the browser context
    special = IS_EXECUTE_SCRIPT || IS_CHECK_SCRIPT || IS_UNIT_TESTS || IS_JS_LINT;
  }
  catch (err) {
    special = true;
  }
  
  if (! special) {  
    try {
      // this will not work from within a browser
      var fs = require("fs");
      var rcf = fs.join(fs.home(), ".arangosh.rc");

      if (fs.exists(rcf)) {
        var content = fs.read(rcf);
        eval(content);
      }
    } 
    catch (err2) {
      require("console").warn("arangosh.rc: %s", String(err2));
    }
  }

  try {
    delete IS_EXECUTE_SCRIPT;
    delete IS_CHECK_SCRIPT;
    delete IS_UNIT_TESTS;
    delete IS_JS_LINT;
  }
  catch (err3) {
  }
}());

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @}\\|/\\*jslint"
// End:
