/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true */
/*global require */

////////////////////////////////////////////////////////////////////////////////
/// @brief module "js"
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2013 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2010-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       Module "fs"
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoShell
/// @{
////////////////////////////////////////////////////////////////////////////////

(function () {
  var internal = require("internal");
  var fs = require("fs");

////////////////////////////////////////////////////////////////////////////////
/// @brief tests if a file exists
////////////////////////////////////////////////////////////////////////////////

  fs.exists = internal.exists;

////////////////////////////////////////////////////////////////////////////////
/// @brief tests if path points to a directory
////////////////////////////////////////////////////////////////////////////////

  fs.isDirectory = internal.isDirectory;

////////////////////////////////////////////////////////////////////////////////
/// @brief lists all files and directory under a given path
////////////////////////////////////////////////////////////////////////////////

  fs.listTree = internal.listTree;

////////////////////////////////////////////////////////////////////////////////
/// @brief moves a file or directory
////////////////////////////////////////////////////////////////////////////////

  fs.move = internal.move;

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a file
////////////////////////////////////////////////////////////////////////////////

  fs.remove = internal.remove;
}());

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\|/\\*jslint"
// End:
