/*global _, require, assertTrue, assertEqual, AQL_EXECUTE, AQL_EXECUTEJSON, AQL_EXPLAIN */
////////////////////////////////////////////////////////////////////////////////
/// @brief tests for optimizer rules
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
/// @author Jan Steemann
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var jsunity = require("jsunity");
var errors = require("internal").errors;
var helper = require("org/arangodb/aql-helper");
var isEqual = helper.isEqual;
var db = require("org/arangodb").db;
var _ = require("underscore");

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function optimizerRuleTestSuite () {
  var ruleName = "use-index-range";
  var cn = "UnitTestsAhuacatlRange";
  var c;

  // various choices to control the optimizer: 
  var paramNone     = { optimizer: { rules: [ "-all" ] } };
  var paramEnabled  = { optimizer: { rules: [ "-all", "+" + ruleName ] } };
  var paramDisabled = { optimizer: { rules: [ "+all", "-" + ruleName ] } };

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      db._drop(cn);
      c = db._create(cn);

      var i;

      for (i = 0; i < 100; ++i) {
        c.save({ value1: "test" + i, value2: i });
      }

      c.ensureSkiplist("value1");
      c.ensureSkiplist("value2");
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
//      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test results
////////////////////////////////////////////////////////////////////////////////

    testRanges : function () {
      var queries = [ 
        [ "FOR i IN [ 2 ] FOR j IN " + cn + " FILTER j.value2 == i SORT j.value2 RETURN j.value2", [ 2 ] ],
        [ "FOR i IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 == i SORT j.value2 RETURN j.value2", [ 2, 3 ] ],
        [ "FOR i IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 >= i FILTER j.value2 <= i SORT j.value2 RETURN j.value2", [ 2, 3 ] ],
        [ "FOR i IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 >= i FILTER j.value2 < i + 1 SORT j.value2 RETURN j.value2", [ 2, 3 ] ],
        [ "FOR i IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 >= i FILTER j.value2 <= i + 1 SORT j.value2 RETURN j.value2", [ 2, 3, 3, 4 ] ],
        [ "FOR i IN " + cn + " FILTER i.value2 == 2 FOR j IN " + cn + " FILTER j.value2 == i.value2 RETURN j.value2", [ 2 ] ],
//        [ "FOR i IN " + cn + " FILTER i.value2 == 2 || i.value2 == 3 FOR j IN " + cn + " FILTER j.value2 == i.value2 SORT j.value2 RETURN j.value2", [ 2, 3 ] ],
        [ "FOR i IN " + cn + " FILTER i.value2 IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 == i.value2 SORT j.value2 RETURN j.value2", [ 2, 3 ] ],
        [ "FOR i IN " + cn + " FILTER i.value2 IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 >= i.value2 FILTER j.value2 <= i.value2 + 1 SORT j.value2 RETURN j.value2", [ 2, 3, 3, 4 ] ],
        [ "FOR i IN " + cn + " FILTER i.value2 IN [ 2, 3 ] FOR j IN " + cn + " FILTER j.value2 IN [ i.value2 ] SORT j.value2 RETURN j.value2", [ 2, 3 ] ],
        [ "FOR i IN " + cn + " FILTER i.value2 >= 97 FOR j IN " + cn + " FILTER j.value2 IN [ i.value2 ] SORT j.value2 RETURN j.value2", [ 97, 98, 99 ] ],
        [ "FOR i IN " + cn + " FILTER i.value2 >= 97 FOR j IN " + cn + " FILTER j.value2 >= i.value2 SORT j.value2 RETURN j.value2", [ 97, 98, 98, 99, 99, 99 ] ]
      ];

      var opts = _.clone(paramEnabled);
      opts.allPlans = true;
      opts.verbosePlans = true;

      queries.forEach(function(query) {
        var resultDisabled = AQL_EXECUTE(query[0], { }, paramDisabled).json;
        var resultEnabled  = AQL_EXECUTE(query[0], { }, paramEnabled).json;

        // TODO: activate the following line once we are sure an index can be
        // used for arbitrary expressions
        // assertNotEqual(-1, AQL_EXPLAIN(query[0], { }, paramEnabled).plan.rules.indexOf(ruleName), query[0]);

        assertTrue(isEqual(query[1], resultDisabled), query[0]);
        assertTrue(isEqual(query[1], resultEnabled), query[0]);

        var plans = AQL_EXPLAIN(query[0], { }, opts).plans;
        plans.forEach(function(plan) {
          var result = AQL_EXECUTEJSON(plan, { optimizer: { rules: [ "-all" ] } }).json;
          assertEqual(query[1], result, query[0]);
        });
      });
    }

  };
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(optimizerRuleTestSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
