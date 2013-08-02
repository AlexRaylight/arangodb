////////////////////////////////////////////////////////////////////////////////
/// @brief test the collection interface
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
var internal = require("internal");

var ArangoCollection = arangodb.ArangoCollection;
var db = arangodb.db;
var ERRORS = arangodb.errors;
 
  
var compareStringIds = function (l, r) {
  if (l.length != r.length) {
    return l.length - r.length < 0 ? -1 : 1;
  }

  // length is equal
  for (i = 0; i < l.length; ++i) {
    if (l[i] != r[i]) {
      return l[i] < r[i] ? -1 : 1;
    }
  }

  return 0;
};

// -----------------------------------------------------------------------------
// --SECTION--                                                collection methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite: error handling
////////////////////////////////////////////////////////////////////////////////

function CollectionSuiteErrorHandling () {
  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (too long)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameTooLong : function () {
      try {
        // one char too long
        db._create("a1234567890123456789012345678901234567890123456789012345678901234");
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (system name)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameSystem : function () {
      try {
        // one char too long
        db._create("1234");
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (underscore)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameUnderscore : function () {
      try {
        db._create("_illegal");
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (empty)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameEmpty : function () {
      try {
        db._create("");
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (number)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameNumber : function () {
      try {
        db._create("12345");
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (underscore) (short-cut)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameUnderscoreShortCut : function () {
      try {
        db["_illegal"];
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (empty) (short-cut)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameEmptyShortCut : function () {
      try {
        db[""];
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief bad name (number) (short-cut)
////////////////////////////////////////////////////////////////////////////////

    testErrorHandlingBadNameNumberShortCut : function () {
      try {
        db["12345"];
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_ILLEGAL_NAME.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief creating with too small journal size
////////////////////////////////////////////////////////////////////////////////

    testErrorInvalidJournalSize : function () {
      var cn = "example";

      db._drop(cn);
      try {
        db._create(cn, { waitForSync : false, journalSize : 1024 });
        fail();
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_BAD_PARAMETER.code, err.errorNum);
      }
    },

  };
}

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite: collection
////////////////////////////////////////////////////////////////////////////////

function CollectionSuite () {
  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief long name
////////////////////////////////////////////////////////////////////////////////

    testCreateLongName : function () {
      var cn = "a123456789012345678901234567890123456789012345678901234567890123";

      db._drop(cn);
      var c1 = db._create(cn);
      assertEqual(cn, c1.name());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief read by name
////////////////////////////////////////////////////////////////////////////////

    testReadingByName : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());

      var c2 = db._collection(cn);

      assertEqual(c1._id, c2._id);
      assertEqual(c1.name(), c2.name());
      assertEqual(c1.status(), c2.status());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief read by identifier
////////////////////////////////////////////////////////////////////////////////

    testReadingByIdentifier : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var c2 = db._collection(c1._id);

      assertEqual(c1._id, c2._id);
      assertEqual(c1.name(), c2.name());
      assertEqual(c1.status(), c2.status());
      assertEqual(c1.type(), c2.type());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief read by name (short-cut)
////////////////////////////////////////////////////////////////////////////////

    testReadingByNameShortCut : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var c2 = db[cn];

      assertEqual(c1._id, c2._id);
      assertEqual(c1.name(), c2.name());
      assertEqual(c1.status(), c2.status());
      assertEqual(c1.type(), c2.type());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief read all
////////////////////////////////////////////////////////////////////////////////

    testReadingAll : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var l = db._collections();

      assertNotEqual(0, l.length);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief creating document collection
////////////////////////////////////////////////////////////////////////////////

    testCreatingDocumentCollection : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._createDocumentCollection(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var p = c1.properties();

      assertEqual(false, p.waitForSync);
      assertEqual(false, p.isVolatile);
      assertEqual(true, p.doCompact);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief creating edge collection
////////////////////////////////////////////////////////////////////////////////

    testCreatingEdgeCollection : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._createEdgeCollection(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_EDGE, c1.type());
      assertTypeOf("number", c1.type());

      var p = c1.properties();

      assertEqual(false, p.waitForSync);
      assertEqual(false, p.isVolatile);
      assertEqual(true, p.doCompact);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check type of document collection after unload
////////////////////////////////////////////////////////////////////////////////

    testTypeDocumentCollection : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._createDocumentCollection(cn);
      c1.unload();
      c1 = null;

      c2 = db[cn];
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c2.type());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief check type of edge collection after unload
////////////////////////////////////////////////////////////////////////////////

    testTypeEdgeCollection : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._createEdgeCollection(cn);
      c1.unload();
      c1 = null;

      c2 = db[cn];
      assertEqual(ArangoCollection.TYPE_EDGE, c2.type());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief creating with defaults
////////////////////////////////////////////////////////////////////////////////

    testCreatingDefaults : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var p = c1.properties();

      assertEqual(false, p.waitForSync);
      assertEqual(false, p.isVolatile);
      assertEqual(1048576, p.journalSize);
      assertEqual(true, p.doCompact);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief creating with properties
////////////////////////////////////////////////////////////////////////////////

    testCreatingProperties : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn, { waitForSync : true, journalSize : 1024 * 1024 });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var p = c1.properties();

      assertEqual(true, p.waitForSync);
      assertEqual(false, p.isVolatile);
      assertEqual(1048576, p.journalSize);
      assertEqual(true, p.doCompact);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief creating with properties
////////////////////////////////////////////////////////////////////////////////

    testCreatingProperties2 : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn, { isVolatile : true, doCompact: false });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var p = c1.properties();

      assertEqual(true, p.isVolatile);
      assertEqual(1048576, p.journalSize);
      assertEqual(false, p.doCompact);
      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief drop new-born
////////////////////////////////////////////////////////////////////////////////

    testDroppingNewBorn : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      c1.drop();

      assertEqual(ArangoCollection.STATUS_DELETED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief drop loaded
////////////////////////////////////////////////////////////////////////////////

    testDroppingLoaded : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      c1.drop();

      assertEqual(ArangoCollection.STATUS_DELETED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief drop unloaded
////////////////////////////////////////////////////////////////////////////////

    testDroppingUnloaded : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });
      c1.unload();

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      c1.drop();

      assertEqual(ArangoCollection.STATUS_DELETED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief truncate new-born
////////////////////////////////////////////////////////////////////////////////

    testTruncatingNewBorn : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      c1.truncate();

      assertEqual(ArangoCollection.STATUS_LOADED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertEqual(0, c1.count());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief truncate loaded
////////////////////////////////////////////////////////////////////////////////

    testTruncatingLoaded : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      c1.truncate();

      assertEqual(ArangoCollection.STATUS_LOADED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertEqual(0, c1.count());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief truncate unloaded
////////////////////////////////////////////////////////////////////////////////

    testTruncatingUnloaded : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });
      c1.unload();

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      c1.truncate();

      assertEqual(ArangoCollection.STATUS_LOADED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertEqual(0, c1.count());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief rotate
////////////////////////////////////////////////////////////////////////////////

    testRotate : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ _key: "test" });
      var f = c1.figures(); 
      assertEqual(0, f.datafiles.count);

      if (c1.rotate) {
        // rotate() is only present server-side...
        c1.rotate();

        // must wait so the synchroniser can catch up
        require("internal").wait(5);

        f = c1.figures();
        assertEqual(1, f.datafiles.count);
        
        c1.rotate();

        // must wait so the synchroniser can catch up
        require("internal").wait(5);

        f = c1.figures();
        assertEqual(2, f.datafiles.count);
      }

      c1.unload();

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief figures
////////////////////////////////////////////////////////////////////////////////

    testFigures : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.load();

      var f = c1.figures();

      assertEqual(0, f.datafiles.count);
      assertEqual(0, f.alive.count);
      assertEqual(0, f.alive.size);
      assertEqual(0, f.dead.count);
      assertEqual(0, f.dead.size);
      assertEqual(0, f.dead.deletion);

      var d1 = c1.save({ hello : 1 });

      f = c1.figures();

      assertEqual(0, f.datafiles.count);
      assertEqual(0, f.datafiles.fileSize);
      assertEqual(1, f.journals.count);
      assertTrue(f.journals.fileSize > 0);
      assertEqual(1, f.alive.count);
      assertNotEqual(0, f.alive.size);
      assertEqual(0, f.dead.count);
      assertEqual(0, f.dead.size);
      assertEqual(0, f.dead.deletion);

      var d2 = c1.save({ hello : 2 });

      f = c1.figures();

      assertEqual(0, f.datafiles.count);
      assertEqual(0, f.datafiles.fileSize);
      assertEqual(1, f.journals.count);
      assertTrue(f.journals.fileSize > 0);
      assertEqual(2, f.alive.count);
      assertNotEqual(0, f.alive.size);
      assertEqual(0, f.dead.count);
      assertEqual(0, f.dead.size);
      assertEqual(0, f.dead.deletion);

      c1.remove(d1);

      f = c1.figures();

      assertEqual(0, f.datafiles.count);
      assertEqual(0, f.datafiles.fileSize);
      assertEqual(1, f.journals.count);
      assertTrue(f.journals.fileSize > 0);
      assertEqual(1, f.alive.count);
      assertNotEqual(0, f.alive.size);
      assertEqual(1, f.dead.count);
      assertNotEqual(0, f.dead.size);
      assertEqual(1, f.dead.deletion);

      c1.remove(d2);

      f = c1.figures();

      assertEqual(0, f.datafiles.count);
      assertEqual(0, f.datafiles.fileSize);
      assertEqual(1, f.journals.count);
      assertTrue(f.journals.fileSize > 0);
      assertEqual(0, f.alive.count);
      assertEqual(0, f.alive.size);
      assertEqual(2, f.dead.count);
      assertNotEqual(0, f.dead.size);
      assertEqual(2, f.dead.deletion);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief rename loaded collection
////////////////////////////////////////////////////////////////////////////////

    testRenameLoaded : function () {
      var cn = "example";
      var nn = "example2";

      db._drop(cn);
      db._drop(nn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var id = c1._id;

      c1.rename(nn);

      assertEqual(id, c1._id);
      assertEqual(nn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);

      db._drop(nn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief rename unloaded collection
////////////////////////////////////////////////////////////////////////////////

    testRenameUnloaded : function () {
      var cn = "example";
      var nn = "example2";

      db._drop(cn);
      db._drop(nn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });
      c1.unload();

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var id = c1._id;

      c1.rename(nn);

      assertEqual(id, c1._id);
      assertEqual(nn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);

      db._drop(nn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief rename a collection to an already existing collection
////////////////////////////////////////////////////////////////////////////////

    testRenameExisting : function () {
      var cn1 = "example";
      var cn2 = "example2";

      db._drop(cn1);
      db._drop(cn2);
      var c1 = db._create(cn1);
      var c2 = db._create(cn2);

      try {
        c1.rename(cn2);
      }
      catch (err) {
        assertEqual(ERRORS.ERROR_ARANGO_DUPLICATE_NAME.code, err.errorNum);
      }
      db._drop(cn1);
      db._drop(cn2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test checksum
////////////////////////////////////////////////////////////////////////////////

    testChecksum : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      // empty collection, checksum should be 0
      var r1 = c1.checksum();
      assertTypeOf("string", r1.revision);
      assertTrue(r1.revision !== "");
      assertTrue(r1.revision.match(/^[0-9]+$/));
      assertTypeOf("number", r1.checksum);
      assertEqual(0, r1.checksum); 

      // inserting a doc, checksum should change
      c1.save({ a : 1 });
      var r2 = c1.checksum();
      assertNotEqual(r1.revision, r2.revision);
      assertTypeOf("string", r2.revision);
      assertTrue(r2.revision !== "");
      assertTrue(r2.revision.match(/^[0-9]+$/));
      assertTypeOf("number", r2.checksum);
      assertNotEqual(0, r2.checksum); 
      
      // inserting another doc, checksum should change
      c1.save({ a : 2 });
      var r3 = c1.checksum();
      assertNotEqual(r1.revision, r3.revision);
      assertNotEqual(r2.revision, r3.revision);
      assertTypeOf("string", r3.revision);
      assertTrue(r3.revision !== "");
      assertTrue(r3.revision.match(/^[0-9]+$/));
      assertTypeOf("number", r3.checksum);
      assertNotEqual(0, r3.checksum); 
      assertNotEqual(r2.checksum, r3.checksum); 

      // test after unloading
      c1.unload();
      var r4 = c1.checksum();
      assertTypeOf("string", r4.revision);
      assertEqual(r3.revision, r4.revision);
      assertTypeOf("number", r4.checksum);
      assertNotEqual(0, r4.checksum); 
      assertEqual(r3.checksum, r4.checksum);
      
      // test withData
      var r5 = c1.checksum(true);
      assertTypeOf("string", r5.revision);
      assertEqual(r4.revision, r5.revision);
      assertTypeOf("number", r5.checksum);
      assertNotEqual(0, r5.checksum); 
      assertNotEqual(r4.checksum, r5.checksum);

      // test after truncation
      c1.truncate();
      var r6 = c1.checksum();
      assertTypeOf("string", r6.revision);
      assertNotEqual(r4.revision, r6.revision);
      assertNotEqual(r5.revision, r6.revision);
      assertTypeOf("number", r6.checksum);
      assertEqual(0, r6.checksum);
      
      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test checksum
////////////////////////////////////////////////////////////////////////////////

    testChecksumEdge : function () {
      var cn = "example";
      var vn = "example2";

      db._drop(cn);
      db._drop(vn);
      db._create(vn);
      var c1 = db._createEdgeCollection(cn);

      var r1 = c1.checksum();
      assertTypeOf("string", r1.revision);
      assertTrue(r1.revision !== "");
      assertTrue(r1.revision.match(/^[0-9]+$/));
      assertTypeOf("number", r1.checksum);
      assertEqual(0, r1.checksum); 

      c1.save(vn + "/1", vn + "/2", { a : 1 });
      var r2 = c1.checksum();
      assertNotEqual(r1.revision, r2.revision);
      assertTypeOf("string", r2.revision);
      assertTrue(r2.revision !== "");
      assertTrue(r2.revision.match(/^[0-9]+$/));
      assertTypeOf("number", r2.checksum);
      assertNotEqual(0, r2.checksum); 
      
      c1.save(vn + "/1", vn + "/2", { a : 2 });
      var r3 = c1.checksum();
      assertNotEqual(r1.revision, r3.revision);
      assertNotEqual(r2.revision, r3.revision);
      assertTypeOf("string", r3.revision);
      assertTrue(r3.revision !== "");
      assertTrue(r3.revision.match(/^[0-9]+$/));
      assertTypeOf("number", r3.checksum);
      assertNotEqual(0, r3.checksum); 
      assertNotEqual(r2.checksum, r3.checksum); 

      c1.unload();
      var r4 = c1.checksum();
      assertTypeOf("string", r4.revision);
      assertEqual(r3.revision, r4.revision);
      assertTypeOf("number", r4.checksum);
      assertEqual(r3.checksum, r4.checksum);
      
      // test withData
      var r5 = c1.checksum(true);
      assertTypeOf("string", r5.revision);
      assertEqual(r4.revision, r5.revision);
      assertTypeOf("number", r5.checksum);
      assertNotEqual(0, r5.checksum); 
      assertNotEqual(r4.checksum, r5.checksum);

      // test after truncation
      c1.truncate();
      var r6 = c1.checksum();
      assertTypeOf("string", r6.revision);
      assertNotEqual(r4.revision, r6.revision);
      assertNotEqual(r5.revision, r6.revision);
      assertTypeOf("number", r6.checksum);
      assertEqual(0, r6.checksum);
      
      db._drop(cn);
      db._drop(vn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test revision id
////////////////////////////////////////////////////////////////////////////////

    testRevision1 : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      var r1 = c1.revision();
      assertTypeOf("string", r1);
      assertTrue(r1.match(/^[0-9]+$/));

      c1.save({ a : 1 });
      var r2 = c1.revision();
      assertTypeOf("string", r2);
      assertTrue(r2.match(/^[0-9]+$/));
      assertEqual(1, compareStringIds(r2, r1));
      
      c1.save({ a : 2 });
      var r3 = c1.revision();
      assertTypeOf("string", r3);
      assertTrue(r3.match(/^[0-9]+$/));
      assertEqual(1, compareStringIds(r3, r2));

      // unload
      c1.unload();
      c1 = null;
      internal.wait(5);
      c1 = db._collection(cn);

      var r4 = c1.revision();
      assertTypeOf("string", r4);
      assertEqual(0, compareStringIds(r3, r4));
      
      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test revision id
////////////////////////////////////////////////////////////////////////////////

    testRevision2 : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      var r1 = c1.revision();
      assertTrue(r1.match(/^[0-9]+$/));

      c1.save({ _key: "abc" });
      var r2 = c1.revision();
      assertTrue(r2.match(/^[0-9]+$/));
      assertEqual(1, compareStringIds(r2, r1));
      
      c1.save({ _key: "123" });
      c1.save({ _key: "456" });
      c1.save({ _key: "789" });
      
      var r3 = c1.revision();
      assertTrue(r3.match(/^[0-9]+$/));
      assertEqual(1, compareStringIds(r3, r2));

      c1.remove("123");
      var r4 = c1.revision();
      assertTrue(r4.match(/^[0-9]+$/));
      assertEqual(1, compareStringIds(r4, r3));

      c1.truncate();
      var r5 = c1.revision();
      assertTrue(r5.match(/^[0-9]+$/));
      assertEqual(1, compareStringIds(r5, r4));

      // unload
      c1.unload();
      c1 = null;
      internal.wait(5);

      // compare rev
      c1 = db._collection(cn);
      var r6 = c1.revision();
      assertTrue(r6.match(/^[0-9]+$/));
      assertEqual(0, compareStringIds(r6, r5));
     
      for (var i = 0; i < 10; ++i) {
        c1.save({ _key: "test" + i });
        assertTrue(c1.revision().match(/^[0-9]+$/));
        assertEqual(1, compareStringIds(c1.revision(), r6));
        r6 = c1.revision();
      }

      // unload
      c1.unload();
      c1 = null;
      internal.wait(5);
      
      // compare rev
      c1 = db._collection(cn);
      var r7 = c1.revision();
      assertTrue(r7.match(/^[0-9]+$/));
      assertEqual(0, compareStringIds(r7, r6));

      c1.truncate();
      var r8 = c1.revision();

      // unload
      c1.unload();
      c1 = null;
      internal.wait(5);
      
      // compare rev
      c1 = db._collection(cn);
      var r9 = c1.revision();
      assertTrue(r9.match(/^[0-9]+$/));
      assertEqual(0, compareStringIds(r9, r8));

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test first
////////////////////////////////////////////////////////////////////////////////

    testFirst : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertNull(c1.first());

      c1.save({ "a": 1, "_key" : "one" });
      assertEqual(1, c1.first().a);
      
      c1.save({ "a": 2, "_key" : "two" });
      assertEqual(1, c1.first().a);

      c1.remove("one");
      assertEqual(2, c1.first().a);

      c1.remove("two");
      assertNull(c1.first());

      c1.truncate();
      for (var i = 0; i < 1000; ++i) {
        c1.save({ "a" : i, "_key" : "test" + i });
      }

      var actual = c1.first(1);
      assertEqual(1, actual.length);
      assertEqual(0, actual[0].a);
      assertEqual("test0", actual[0]._key);
      
      actual = c1.first(2);
      assertEqual(2, actual.length);
      assertEqual(0, actual[0].a);
      assertEqual(1, actual[1].a);
      assertEqual("test0", actual[0]._key);
      assertEqual("test1", actual[1]._key);
      
      actual = c1.first(10);
      assertEqual(10, actual.length);
      assertEqual(0, actual[0].a);
      assertEqual(1, actual[1].a);
      assertEqual(9, actual[9].a);
      assertEqual("test0", actual[0]._key);
      assertEqual("test1", actual[1]._key);
      assertEqual("test9", actual[9]._key);

      c1.remove("test0");
      c1.remove("test3");
      
      actual = c1.first(10);
      assertEqual(10, actual.length);
      assertEqual(1, actual[0].a);
      assertEqual(2, actual[1].a);
      assertEqual(4, actual[2].a);
      assertEqual(11, actual[9].a);
      assertEqual("test1", actual[0]._key);
      assertEqual("test2", actual[1]._key);
      assertEqual("test4", actual[2]._key);
      assertEqual("test11", actual[9]._key);

      c1.truncate();
      
      actual = c1.first(10);
      assertEqual(0, actual.length);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test last
////////////////////////////////////////////////////////////////////////////////

    testLast : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertNull(c1.last());

      c1.save({ "a": 1, "_key" : "one" });
      assertEqual(1, c1.last().a);
      
      c1.save({ "a": 2, "_key" : "two" });
      assertEqual(2, c1.last().a);

      c1.remove("two");
      assertEqual(1, c1.last().a);

      c1.remove("one");
      assertNull(c1.last());

      c1.truncate();
      for (var i = 0; i < 1000; ++i) {
        c1.save({ "a" : i, "_key" : "test" + i });
      }

      var actual = c1.last(1);
      assertEqual(1, actual.length);
      assertEqual(999, actual[0].a);
      assertEqual("test999", actual[0]._key);
      
      actual = c1.last(2);
      assertEqual(2, actual.length);
      assertEqual(999, actual[0].a);
      assertEqual(998, actual[1].a);
      assertEqual("test999", actual[0]._key);
      assertEqual("test998", actual[1]._key);
      
      actual = c1.last(10);
      assertEqual(10, actual.length);
      assertEqual(999, actual[0].a);
      assertEqual(998, actual[1].a);
      assertEqual(990, actual[9].a);
      assertEqual("test999", actual[0]._key);
      assertEqual("test998", actual[1]._key);
      assertEqual("test990", actual[9]._key);

      c1.remove("test999");
      c1.remove("test996");
      
      actual = c1.last(10);
      assertEqual(10, actual.length);
      assertEqual(998, actual[0].a);
      assertEqual(997, actual[1].a);
      assertEqual(995, actual[2].a);
      assertEqual(988, actual[9].a);
      assertEqual("test998", actual[0]._key);
      assertEqual("test997", actual[1]._key);
      assertEqual("test995", actual[2]._key);
      assertEqual("test988", actual[9]._key);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test first / last after reload
////////////////////////////////////////////////////////////////////////////////

    testFirstLastAfterReload : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      for (var i = 0; i < 1000; ++i) {
        c1.save({ _key : "test" + i, "value" : i });
      }
      c1.unload();
      c1 = null;

      require("internal").wait(5);

      c1 = db._collection(cn);

      for (i = 1000; i < 2000; ++i) {
        c1.save({ _key : "test" + i, "value" : i });
      }

      var actual = c1.first(10);
      assertEqual(10, actual.length);
      assertEqual(0, actual[0].value);
      assertEqual(1, actual[1].value);
      assertEqual(9, actual[9].value);
      assertEqual("test0", actual[0]._key);
      assertEqual("test1", actual[1]._key);
      assertEqual("test9", actual[9]._key);
      
      actual = c1.last(10);
      assertEqual(10, actual.length);
      assertEqual(1999, actual[0].value);
      assertEqual(1998, actual[1].value);
      assertEqual(1990, actual[9].value);
      assertEqual("test1999", actual[0]._key);
      assertEqual("test1998", actual[1]._key);
      assertEqual("test1990", actual[9]._key);

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test system collection dropping / renaming / unloading
////////////////////////////////////////////////////////////////////////////////

    testSystemSpecial : function () {
      [ '_trx', '_users' ].forEach(function(cn) {
        var c = db._collection(cn);

        // drop
        try {
          c.drop();
          fail();
        }
        catch (err1) {
          assertEqual(ERRORS.ERROR_FORBIDDEN.code, err1.errorNum);
        }
        
        // rename
        var cn = "example";
        db._drop(cn);

        try {
          c.rename(cn);
          fail();
        }
        catch (err2) {
          assertEqual(ERRORS.ERROR_FORBIDDEN.code, err2.errorNum);
        }

        // unload is allowed
        c.unload();

      });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test special properties of replication collection
////////////////////////////////////////////////////////////////////////////////

    testSpecialReplication : function () {
      var repl = db._collection('_replication');

      // drop is not allowed      
      try {
        repl.drop();
        fail();
      }
      catch (err1) {
        assertEqual(ERRORS.ERROR_FORBIDDEN.code, err1.errorNum);
      }

      // rename is not allowed
      try {
        var cn = "example";
        db._drop(cn);
        repl.rename(cn);
      }
      catch (err2) {
        assertEqual(ERRORS.ERROR_FORBIDDEN.code, err2.errorNum);
      }
      
      // unload is not allowed
      try {
        repl.unload();
      }
      catch (err3) {
        assertEqual(ERRORS.ERROR_FORBIDDEN.code, err3.errorNum);
      }
    }

  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   vocbase methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite: collection
////////////////////////////////////////////////////////////////////////////////

function CollectionDbSuite () {
  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief drop new-born (DB)
////////////////////////////////////////////////////////////////////////////////

    testCollections : function () {
      var cn1 = "example1";
      var cn2 = "example2";

      db._drop(cn1);
      db._drop(cn2);
      var c1 = db._createDocumentCollection(cn1);
      var c2 = db._createEdgeCollection(cn2);

      var collections = db._collections();
      for (var i in collections) {
        var c = collections[i];

        assertTypeOf("string", c.name());
        assertTypeOf("number", c.status());
        assertTypeOf("number", c.type());
        
        if (c.name() == cn1) {
          assertEqual(ArangoCollection.TYPE_DOCUMENT, c.type());
        }
        else if (c.name() == cn2) {
          assertEqual(ArangoCollection.TYPE_EDGE, c.type());
        }
      }

      db._drop(cn1);
      db._drop(cn2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief drop new-born (DB)
////////////////////////////////////////////////////////////////////////////////

    testDroppingNewBornDB : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      db._drop(cn);

      assertEqual(ArangoCollection.STATUS_DELETED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief drop loaded (DB)
////////////////////////////////////////////////////////////////////////////////

    testDroppingLoadedDB : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      db._drop(cn);

      assertEqual(ArangoCollection.STATUS_DELETED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief drop unloaded (DB)
////////////////////////////////////////////////////////////////////////////////

    testDroppingUnloadedDB : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });
      c1.unload();

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      db._drop(cn);

      assertEqual(ArangoCollection.STATUS_DELETED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());

      var c2 = db._collection(cn);

      assertEqual(null, c2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief truncate new-born (DB)
////////////////////////////////////////////////////////////////////////////////

    testTruncatingNewBornDB : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      db._truncate(cn);

      assertEqual(ArangoCollection.STATUS_LOADED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertEqual(0, c1.count());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief truncate loaded (DB)
////////////////////////////////////////////////////////////////////////////////

    testTruncatingLoadedDB : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      db._truncate(cn);

      assertEqual(ArangoCollection.STATUS_LOADED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertEqual(0, c1.count());

      db._drop(cn);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief truncate unloaded (DB)
////////////////////////////////////////////////////////////////////////////////

    testTruncatingUnloadedDB : function () {
      var cn = "example";

      db._drop(cn);
      var c1 = db._create(cn);

      c1.save({ a : 1 });
      c1.unload();

      assertTypeOf("string", c1._id);
      assertEqual(cn, c1.name());
      assertTypeOf("number", c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertTypeOf("number", c1.type());

      db._truncate(cn);

      assertEqual(ArangoCollection.STATUS_LOADED, c1.status());
      assertEqual(ArangoCollection.TYPE_DOCUMENT, c1.type());
      assertEqual(0, c1.count());

      db._drop(cn);
    }
  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                              main
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suites
////////////////////////////////////////////////////////////////////////////////

jsunity.run(CollectionSuiteErrorHandling);
jsunity.run(CollectionSuite);
jsunity.run(CollectionDbSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:

