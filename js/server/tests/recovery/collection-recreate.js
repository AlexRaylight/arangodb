
var db = require("org/arangodb").db;
var internal = require("internal");
var jsunity = require("jsunity");


function runSetup () {
  internal.debugClearFailAt();
  
  db._drop("UnitTestsRecovery");
  var c = db._create("UnitTestsRecovery"), i;

  for (i = 0; i < 1000; ++i) {
    c.save({ _key: "test" + i, value1: "test" + i });
  }

  // make sure the next operations go into a separate log
  internal.wal.flush(true, true);

  db._drop("UnitTestsRecovery");
  c = db._create("UnitTestsRecovery");
  for (i = 0; i < 100; ++i) {
    c.save({ _key: "test" + i, value1: i });
  }

  c.save({ _key: "foo" }, true);

  internal.debugSegfault("crashing server");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function recoverySuite () {
  jsunity.jsUnity.attachAssertions();

  return {
    setUp: function () {
    },
    tearDown: function () {
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test whether we can restore the trx data
////////////////////////////////////////////////////////////////////////////////
    
    testCollectionRecreate : function () {
      var c = db._collection("UnitTestsRecovery"), i;
      assertEqual(101, c.count());

      for (i = 0; i < 100; ++i) {
        assertEqual(i, c.document("test" + i).value1);
      }
    }
        
  };
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

function main (argv) {
  if (argv[1] === "setup") {
    runSetup();
    return 0;
  }
  else {
    jsunity.run(recoverySuite);
    return jsunity.done() ? 0 : 1;
  }
}

