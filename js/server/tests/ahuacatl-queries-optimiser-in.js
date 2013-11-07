////////////////////////////////////////////////////////////////////////////////
/// @brief tests for query language, in
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
var internal = require("internal");
var helper = require("org/arangodb/aql-helper");
var getQueryResults = helper.getQueryResults;
var getQueryExplanation = helper.getQueryExplanation;

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function ahuacatlQueryOptimiserInTestSuite () {
  var c = null;
  var cn = "UnitTestsAhuacatlOptimiserIn";

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      internal.db._drop(cn);
      c = internal.db._create(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      internal.db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInMergeOr : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }

      var expected = [ 'test1', 'test2', 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c._key IN parents || c._key IN [ 'test1' ] || c._key IN [ 'test2' ] || c._key IN parents SORT c._key RETURN c._key");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInMergeAnd : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c._key IN parents && c._key IN [ 'test5', 'test7' ] && c._key IN [ 'test7', 'test5' ] && c._key IN parents SORT c._key RETURN c._key");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInPrimaryConst : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c._key IN parents SORT c._key RETURN c._key");
      assertEqual(expected, actual);

      var explain = getQueryExplanation("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c._key IN parents SORT c._key RETURN c._key");
      assertEqual("index", explain[1].expression.extra.accessType);
      assertEqual("primary", explain[1].expression.extra.index.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInPrimaryDynamic : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = (FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] RETURN c._key) FOR c IN " + cn + " FILTER c._key IN parents SORT c._key RETURN c._key");
      assertEqual(expected, actual);

      var explain = getQueryExplanation("LET parents = (FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] RETURN c._key) FOR c IN " + cn + " FILTER c._key IN parents SORT c._key RETURN c._key");
      assertEqual("index", explain[5].expression.extra.accessType);
      assertEqual("primary", explain[5].expression.extra.index.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInPrimaryDynamicRef : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }

      var expected = [ { keys: [ 'test4' ] }, { keys: [ 'test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] SORT c._key RETURN { keys: (FOR c2 IN " + cn + " FILTER c2._key IN [ c.parent ] RETURN c2._key) }");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInPrimaryRef : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }

      var expected = [ { keys: [ 'test4' ] }, { keys: [ 'test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] SORT c._key RETURN { keys: (FOR c2 IN " + cn + " FILTER c2._key IN c.parents SORT c2._key RETURN c2._key) }");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInHashConst : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueConstraint("code");

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual(expected, actual);
      
      var explain = getQueryExplanation("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual("index", explain[1].expression.extra.accessType);
      assertEqual("hash", explain[1].expression.extra.index.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInHashDynamic : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueConstraint("code");

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = (FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] RETURN c.code) FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual(expected, actual);
      
      var explain = getQueryExplanation("LET parents = (FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] RETURN c.code) FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual("index", explain[5].expression.extra.accessType);
      assertEqual("hash", explain[5].expression.extra.index.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInHashDynamicRef : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueConstraint("code");

      var expected = [ { keys: [ 'test4' ] }, { keys: [ 'test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] SORT c.code RETURN { keys: (FOR c2 IN " + cn + " FILTER c2.code IN [ c.parent ] RETURN c2.code) }");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInHashRef : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueConstraint("code");

      var expected = [ { keys: [ 'test4' ] }, { keys: [ 'test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] SORT c.code RETURN { keys: (FOR c2 IN " + cn + " FILTER c2.code IN c.parents SORT c2.code RETURN c2.code) }");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInSkipConst : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueSkiplist("code");

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual(expected, actual);
      
      var explain = getQueryExplanation("LET parents = [ 'test5', 'test7' ] FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual("index", explain[1].expression.extra.accessType);
      assertEqual("skiplist", explain[1].expression.extra.index.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInSkipDynamic : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueSkiplist("code");

      var expected = [ 'test5', 'test7' ];
      var actual = getQueryResults("LET parents = (FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] RETURN c.code) FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual(expected, actual);
      
      var explain = getQueryExplanation("LET parents = (FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] RETURN c.code) FOR c IN " + cn + " FILTER c.code IN parents SORT c.code RETURN c.code");
      assertEqual("index", explain[5].expression.extra.accessType);
      assertEqual("skiplist", explain[5].expression.extra.index.type);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInSkipDynamicRef : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueSkiplist("code");

      var expected = [ { keys: [ 'test4' ] }, { keys: [ 'test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] SORT c.code RETURN { keys: (FOR c2 IN " + cn + " FILTER c2.code IN [ c.parent ] RETURN c2.code) }");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInSkipRef : function () {
      c.save({ code: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ code: "test" + i, parent: "test" + (i - 1), parents: [ "test" + (i - 1) ] });
      }
      c.ensureUniqueSkiplist("code");

      var expected = [ { keys: [ 'test4' ] }, { keys: [ 'test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c.code IN [ 'test5', 'test7' ] SORT c.code RETURN { keys: (FOR c2 IN " + cn + " FILTER c2.code IN c.parents SORT c2.code RETURN c2.code) }");
      assertEqual(expected, actual);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInEdgeConst : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i });
      }
      
      var en = cn + "Edge";
      internal.db._drop(en);
      var e = internal.db._createEdgeCollection(en);
      
      for (var i = 1; i < 100; ++i) {
        e.save(cn + "/test" + i, cn + "/test" + (i - 1), { });
      }

      var expected = [ cn + '/test4', cn + '/test6' ];
      var actual = getQueryResults("LET parents = [ '" + cn + "/test5', '" + cn + "/test7' ] FOR c IN " + en + " FILTER c._from IN parents SORT c._to RETURN c._to");
      assertEqual(expected, actual);
      
      var explain = getQueryExplanation("LET parents = [ '" + cn + "/test5', '" + cn + "/test7' ] FOR c IN " + en + " FILTER c._from IN parents SORT c._to RETURN c._to");
      assertEqual("index", explain[1].expression.extra.accessType);
      assertEqual("edge", explain[1].expression.extra.index.type);
      
      internal.db._drop(en);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInEdgeDynamic : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i });
      }
      
      var en = cn + "Edge";
      internal.db._drop(en);
      var e = internal.db._createEdgeCollection(en);
      
      for (var i = 1; i < 100; ++i) {
        e.save(cn + "/test" + i, cn + "/test" + (i - 1), { });
      }

      var expected = [ cn + '/test4', cn + '/test6' ];
      var actual = getQueryResults("LET parents = (FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] RETURN c._id) FOR c IN " + en + " FILTER c._from IN parents SORT c._to RETURN c._to");
      assertEqual(expected, actual);
      
      var explain = getQueryExplanation("LET parents = (FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] RETURN c._id) FOR c IN " + en + " FILTER c._from IN parents SORT c._to RETURN c._to");
      assertEqual("index", explain[5].expression.extra.accessType);
      assertEqual("edge", explain[5].expression.extra.index.type);
      
      internal.db._drop(en);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInEdgeDynamicRef : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i });
      }
      
      var en = cn + "Edge";
      internal.db._drop(en);
      var e = internal.db._createEdgeCollection(en);
      
      for (var i = 1; i < 100; ++i) {
        e.save(cn + "/test" + i, cn + "/test" + (i - 1), { });
      }

      var expected = [ { keys: [ cn + '/test4' ] }, { keys: [ cn + '/test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] SORT c._key RETURN { keys: (FOR c2 IN " + en + " FILTER c2._from IN [ c._id ] RETURN c2._to) }");
      assertEqual(expected, actual);
      
      internal.db._drop(en);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check a ref access without any indexes
////////////////////////////////////////////////////////////////////////////////

    testInEdgeRef : function () {
      c.save({ _key: "test0" });
      for (var i = 1; i < 100; ++i) {
        c.save({ _key: "test" + i, ids: [ cn + "/test" + i ] });
      }
      
      var en = cn + "Edge";
      internal.db._drop(en);
      var e = internal.db._createEdgeCollection(en);
      
      for (var i = 1; i < 100; ++i) {
        e.save(cn + "/test" + i, cn + "/test" + (i - 1), { });
      }

      var expected = [ { keys: [ cn + '/test4' ] }, { keys: [ cn + '/test6' ] } ];
      var actual = getQueryResults("FOR c IN " + cn + " FILTER c._key IN [ 'test5', 'test7' ] SORT c._key RETURN { keys: (FOR c2 IN " + en + " FILTER c2._from IN c.ids RETURN c2._to) }");
      assertEqual(expected, actual);
      
      internal.db._drop(en);
    }
        
  };

}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(ahuacatlQueryOptimiserInTestSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
