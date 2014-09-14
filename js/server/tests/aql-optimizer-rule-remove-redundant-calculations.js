/*global _, require, assertTrue, assertEqual, AQL_EXECUTE, AQL_EXPLAIN */
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

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function optimizerRuleTestSuite () {
  var ruleName = "remove-redundant-calculations";

  // various choices to control the optimizer: 
  var paramNone     = { optimizer: { rules: [ "-all" ] } };
  var paramEnabled  = { optimizer: { rules: [ "-all", "+" + ruleName ] } };
  var paramDisabled = { optimizer: { rules: [ "+all", "-" + ruleName ] } };

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test that rule has no effect when explicitly disabled
////////////////////////////////////////////////////////////////////////////////

    testRuleDisabled : function () {
      var queries = [
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = i.a LET b = i.a RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = LENGTH(i), b = LENGTH(i) RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = MAX(i) RETURN MAX(i)"
      ];

      queries.forEach(function(query) {
        var result = AQL_EXPLAIN(query, { }, paramNone);
        assertEqual([ ], result.plan.rules);
      });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test that rule has no effect
////////////////////////////////////////////////////////////////////////////////

    testRuleNoEffect : function () {
      var queries = [ 
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = i.a LET b = i.b RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = LENGTH(i), b = LENGTH(i) + 1 RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = MAX(i) RETURN MIN(i)",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = RAND(), b = RAND() RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = MAX(i) COLLECT x = a LET i = RAND() LET a = i.a RETURN a"
      ];

      queries.forEach(function(query) {
        var result = AQL_EXPLAIN(query, { }, paramEnabled);
        assertEqual([ ], result.plan.rules, query);
      });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test that rule has an effect
////////////////////////////////////////////////////////////////////////////////

    testRuleHasEffect : function () {
      var queries = [ 
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = i.a LET b = i.a RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = LENGTH(i), b = LENGTH(i) RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = MAX(i) RETURN MAX(i)",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = IS_NUMBER(i), b = IS_NUMBER(i) RETURN [ a, b ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = MAX(i) COLLECT x = a LET i = x.a, j = x.a RETURN [ i, j ]",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] SORT i.a, i.a RETURN i",
        "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET x = i.a SORT i.a RETURN x"
      ]

      queries.forEach(function(query) {
        var result = AQL_EXPLAIN(query, { }, paramEnabled);
        assertEqual([ ruleName ], result.plan.rules, query);
      });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test generated plans
////////////////////////////////////////////////////////////////////////////////

    testPlans : function () {
      var query = "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET x = i.a RETURN i.a";
      var actual = AQL_EXPLAIN(query, null, paramEnabled);
      var nodes = helper.getLinearizedPlan(actual).reverse(), node;

      node = nodes[0];
      assertEqual("ReturnNode", node.type);
      var variable = node.inVariable.id;
      
      // this node becomes superfluous
      node = nodes[1];
      assertEqual("CalculationNode", node.type);
      assertNotEqual(variable, node.outVariable.id);

      node = nodes[2];
      assertEqual("CalculationNode", node.type);
      assertEqual(variable, node.outVariable.id);
      
      node = nodes[3];
      assertEqual("EnumerateListNode", node.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test results
////////////////////////////////////////////////////////////////////////////////

    testResults : function () {
      var queries = [ 
        [ "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] SORT i.a + 2 RETURN  /* */ i.a + 2", [ 3, 4, 5 ] ],
        [ "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] SORT       i.a RETURN  /* */ i.a", [ 1, 2, 3 ] ],
        [ "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] SORT i.a SORT i.a RETURN i.a", [ 1, 2, 3 ] ],
        [ "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET x = [ i.a ] RETURN [ i.a ]", [ [ 1 ], [ 2 ], [ 3 ] ] ],
        [ "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET a = IS_NUMBER(i.a) RETURN IS_NUMBER(i.a)", [ true, true, true ] ],
        [ "FOR i IN [ { a: 1 }, { a: 2 }, { a: 3 } ] LET r = MAX([ i.a ]) == 3 RETURN MAX([ i.a ]) == 3", [ false, false, true ] ],
        [ "LET v = [ { a: 1 }, { a: 2 }, { a: 3 } ] FOR i IN v LET r = MAX(v[*].a) == 3 RETURN MAX(v[*].a) == 3", [ true, true, true ] ],
        [ "FOR i IN [ { a: 'foo' }, { a: 'food' }, { a: 'foobar' } ] LET a = LENGTH(i.a), b = LENGTH(i.a) RETURN [ a, b ]", [ [ 3, 3 ], [ 4, 4 ], [ 6, 6 ] ] ],
        [ "FOR i IN [ { a: 'foo' }, { a: 'food' }, { a: 'foobar' } ] LET a = LENGTH(i.a) SORT a LET b = LENGTH(i.a) SORT b RETURN [ a, b ]", [ [ 3, 3 ], [ 4, 4 ], [ 6, 6 ] ] ]
      ];

      queries.forEach(function(query) {
        var planDisabled   = AQL_EXPLAIN(query[0], { }, paramDisabled);
        var planEnabled    = AQL_EXPLAIN(query[0], { }, paramEnabled);
        var resultDisabled = AQL_EXECUTE(query[0], { }, paramDisabled).json;
        var resultEnabled  = AQL_EXECUTE(query[0], { }, paramEnabled).json;

        assertTrue(isEqual(resultDisabled, resultEnabled), query[0]);

        assertTrue(planDisabled.plan.rules.indexOf(ruleName) === -1, query[0]);
        assertTrue(planEnabled.plan.rules.indexOf(ruleName) !== -1, query[0]);

        assertEqual(resultDisabled, query[1], query[0]);
        assertEqual(resultEnabled, query[1], query[0]);
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
