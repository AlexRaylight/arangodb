////////////////////////////////////////////////////////////////////////////////
/// @brief test attribute naming
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var jsunity = require("jsunity");

var arangodb = require("org/arangodb");

var db = arangodb.db;
var wait = require("internal").wait;

// -----------------------------------------------------------------------------
// --SECTION--                                                        attributes
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test attributes
////////////////////////////////////////////////////////////////////////////////

function AttributesSuite () {
  var cn = "UnitTestsCollectionAttributes";
  var c = null;

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      db._drop(cn);
      c = db._create(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      c.unload();
      c.drop();
      c = null;
      wait(0.0);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief no attributes
////////////////////////////////////////////////////////////////////////////////

    testNoAttributes : function () {
      var doc = { };
      
      var d1 = c.save(doc);
      var d2 = c.document(d1._id);
      delete d1.error;

      assertEqual(d1, d2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief query empty attribute name
////////////////////////////////////////////////////////////////////////////////

    testQueryEmptyAttribute : function () {
      var doc = { "" : "foo" };
      c.save(doc);

      var docs = c.toArray();
      assertEqual(1, docs.length);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief attribute name with special chars
////////////////////////////////////////////////////////////////////////////////

    testSpecialAttributes : function () {
      var doc = { "-meow-" : 1, "mötör" : 2, " " : 3, "\t" : 4, "\r" : 5, "\n" : 6 };
      
      var d1 = c.save(doc);
      var d2 = c.document(d1._id);

      assertEqual(1, d2["-meow-"]);
      assertEqual(2, d2["mötör"]);
      assertEqual(3, d2[" "]);
      assertEqual(4, d2["\t"]);
      assertEqual(5, d2["\r"]);
      assertEqual(6, d2["\n"]);
    }

  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                              main
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(AttributesSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
