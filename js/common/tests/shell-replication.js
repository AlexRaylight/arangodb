/*jslint indent: 2, nomen: true, maxlen: 80 */
/*global require, assertEqual, assertTrue */

////////////////////////////////////////////////////////////////////////////////
/// @brief test the replication functions
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
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var jsunity = require("jsunity");
var arangodb = require("org/arangodb");
var errors = arangodb.errors;
var db = arangodb.db;

var replication = require("org/arangodb/replication");

// -----------------------------------------------------------------------------
// --SECTION--                                           replication logger test
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function ReplicationLoggerSuite () {
  var cn  = "UnitTestsReplication";
  var cn2 = "UnitTestsReplication2";

  var getLogEntries = function () {
    return db._collection('_replication').count();
  };
  
  var getLastLogEntry = function (count) {
    if (count) {
      return db._collection('_replication').last(count);
    }

    return db._collection('_replication').last();
  };

  var compareTicks = function (l, r) {
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
      replication.logger.stop();
      db._drop(cn);
      db._drop(cn2);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief start logger
////////////////////////////////////////////////////////////////////////////////

    testStartLogger : function () {
      var actual, state;
      
      // start 
      actual = replication.logger.start();
      assertTrue(actual);

      state = replication.logger.state().state;
      assertTrue(state.running);
      assertTrue(typeof state.lastLogTick === 'string');
    
      // start again  
      actual = replication.logger.start();
      assertTrue(actual);
      
      var entry = getLastLogEntry();
      assertEqual(1001, entry.type);
      entry = JSON.parse(entry.data);
      assertMatch(/^\d+$/, entry.lastTick);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief stop logger
////////////////////////////////////////////////////////////////////////////////

    testStopLogger : function () {
      var actual, state;
      
      // start 
      actual = replication.logger.start();
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertTrue(state.running);
      assertTrue(typeof state.lastLogTick === 'string');
    
      // stop
      actual = replication.logger.stop();
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertFalse(state.running);
      assertTrue(typeof state.lastLogTick === 'string');
      
      var entry = getLastLogEntry();
      assertEqual(1000, entry.type);
      entry = JSON.parse(entry.data);
      assertMatch(/^\d+$/, entry.lastTick);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief get state
////////////////////////////////////////////////////////////////////////////////

    testGetLoggerState : function () {
      var state, tick, server, clients;
      
      state = replication.logger.state().state;
      assertFalse(state.running);
      tick = state.lastLogTick;
      assertTrue(typeof tick === 'string');
      assertNotEqual("", state.time);
      assertMatch(/^\d+-\d+-\d+T\d+:\d+:\d+Z$/, state.time);
      
      state = replication.logger.state().state;
      assertFalse(state.running);
      assertTrue(tick, state.lastLogTick);
      assertTrue(typeof state.lastLogTick === 'string');
      
      server = replication.logger.state().server;
      assertEqual(server.version, db._version()); 
      assertNotEqual("", server.serverId); 
      assertMatch(/^\d+$/, server.serverId);
    
      clients = replication.logger.state().clients;
      assertTrue(Array.isArray(clients));
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test logging disabled
////////////////////////////////////////////////////////////////////////////////

    testDisabledLogger : function () {
      var state, tick;
      
      state = replication.logger.state().state;
      assertFalse(state.running);
      tick = state.lastLogTick;
      assertTrue(typeof tick === 'string');

      // do something that will cause logging (if it was enabled...)
      var c = db._create(cn);
      c.save({ "test" : 1 });

      state = replication.logger.state().state;
      assertFalse(state.running);
      assertEqual(tick, state.lastLogTick);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test logging enabled
////////////////////////////////////////////////////////////////////////////////

    testEnabledLogger : function () {
      var state, tick;
      
      state = replication.logger.state().state;
      assertFalse(state.running);
      tick = state.lastLogTick;
      assertTrue(typeof tick === 'string');

      replication.logger.start();

      // do something that will cause logging
      var c = db._create(cn);
      c.save({ "test" : 1 });

      state = replication.logger.state().state;
      assertTrue(state.running);
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test logger properties
////////////////////////////////////////////////////////////////////////////////

    testPropertiesLogger : function () {
      var properties;
      
      properties = replication.logger.properties();
      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('autoStart'));
      assertTrue(properties.hasOwnProperty('logRemoteChanges'));
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      var autoStart = properties.autoStart;
      var maxEvents = properties.maxEvents;
      var maxEventsSize = properties.maxEventsSize;

      assertFalse(properties.logRemoteChanges);

      properties = replication.logger.properties({
        logRemoteChanges: true
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('autoStart'));
      assertTrue(properties.hasOwnProperty('logRemoteChanges'));
      assertTrue(properties.logRemoteChanges);
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(autoStart, properties.autoStart);
      assertEqual(maxEvents, properties.maxEvents);
      assertEqual(maxEventsSize, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        logRemoteChanges: false
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('autoStart'));
      assertTrue(properties.hasOwnProperty('logRemoteChanges'));
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(autoStart, properties.autoStart);
      assertFalse(properties.logRemoteChanges);
      assertEqual(maxEvents, properties.maxEvents);
      assertEqual(maxEventsSize, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        autoStart: true
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('autoStart'));
      assertTrue(properties.hasOwnProperty('logRemoteChanges'));
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(true, properties.autoStart);
      assertFalse(properties.logRemoteChanges);
      assertEqual(maxEvents, properties.maxEvents);
      assertEqual(maxEventsSize, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        autoStart: false
      });
      
      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('autoStart'));
      assertTrue(properties.hasOwnProperty('logRemoteChanges'));
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(false, properties.autoStart);
      assertFalse(properties.logRemoteChanges);
      assertEqual(maxEvents, properties.maxEvents);
      assertEqual(maxEventsSize, properties.maxEventsSize);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test event sizes
////////////////////////////////////////////////////////////////////////////////

    testEventSizesLogger : function () {
      var properties;
      
      properties = replication.logger.properties();
      assertTrue(typeof properties === 'object');
      var maxEvents = properties.maxEvents;
      var maxEventsSize = properties.maxEventsSize;

      properties = replication.logger.properties({
        maxEvents: 8192
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(8192, properties.maxEvents);
      assertEqual(maxEventsSize, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        maxEventsSize: 16777216
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(8192, properties.maxEvents);
      assertEqual(16777216, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        maxEvents: 16384,
        maxEventsSize: 1048576
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(16384, properties.maxEvents);
      assertEqual(1048576, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        maxEvents: 0,
        maxEventsSize: 1048580
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(0, properties.maxEvents);
      assertEqual(1048580, properties.maxEventsSize);
      
      properties = replication.logger.properties({
        maxEvents: 16384,
        maxEventsSize: 0
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(16384, properties.maxEvents);
      assertEqual(0, properties.maxEventsSize);
      
      try {
        properties = replication.logger.properties({
          maxEvents: 10,
          maxEventsSize: 0
        });
      }
      catch (err1) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_LOGGER_CONFIGURATION.code, err1.errorNum);
      }
      
      try {
        properties = replication.logger.properties({
          maxEvents: 0,
          maxEventsSize: 10
        });
      }
      catch (err2) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_LOGGER_CONFIGURATION.code, err2.errorNum);
      }
      
      try {
        properties = replication.logger.properties({
          maxEvents: 10,
          maxEventsSize: 10
        });
      }
      catch (err3) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_LOGGER_CONFIGURATION.code, err3.errorNum);
      }
      
      try {
        properties = replication.logger.properties({
          maxEvents: 16384,
          maxEventsSize: 10
        });
      }
      catch (err4) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_LOGGER_CONFIGURATION.code, err4.errorNum);
      }
      
      try {
        properties = replication.logger.properties({
          maxEvents: 10,
          maxEventsSize: 1048576
        });
      }
      catch (err5) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_LOGGER_CONFIGURATION.code, err5.errorNum);
      }
      
      properties = replication.logger.properties({
        maxEvents: 0,
        maxEventsSize: 0
      });

      assertTrue(typeof properties === 'object');
      assertTrue(properties.hasOwnProperty('maxEvents'));
      assertTrue(properties.hasOwnProperty('maxEventsSize'));
      assertEqual(0, properties.maxEvents);
      assertEqual(0, properties.maxEventsSize);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateCollection : function () {
      var state, tick;
      
      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var c = db._create(cn);

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2000, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(2, entry.collection.type);
      assertEqual(c._id, entry.cid);
      assertEqual(c._id, entry.collection.cid);
      assertFalse(entry.collection.deleted);
      assertEqual(cn, entry.collection.name);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerDropCollection : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      db._drop(cn);

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertEqual(2001, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerRenameCollection : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.rename(cn2);

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertEqual(2002, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(cn2, entry.collection.name);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerPropertiesCollection1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.properties();

      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(0, compareTicks(state.lastLogTick, tick));
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerPropertiesCollection2 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.properties({ waitForSync: true, journalSize: 2097152 });

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertEqual(2003, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(c._id, entry.collection.cid);
      assertEqual(cn, entry.collection.name);
      assertEqual(2, entry.collection.type);
      assertEqual(false, entry.collection.deleted);
      assertEqual(2097152, entry.collection.maximalSize);
      assertEqual(true, entry.collection.waitForSync);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerSystemCollection : function () {
      var state, tick, i;
      
      db._drop("_unitfoxx");
      db._drop("_unittests");
      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var c = db._create("_unittests", { isSystem : true });

      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      c.properties({ waitForSync : true });

      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());

      c.rename("_unitfoxx");
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      c.rename("_unittests");
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());

      for (i = 0; i < 100; ++i) {
        c.save({ "_key" : "test" + i });
      }
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      for (i = 0; i < 50; ++i) {
        c.remove("test" + i);
      }
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());

      c.truncate();
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      db._drop("_unittests");
      db._drop("_unitfoxx");
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTruncateCollection1 : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.save({ "test": 1, "_key": "abc" });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.truncate();
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 3, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      c.truncate();
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      c.save({ "test": 1, "_key": "abc" });
      var rev = c.document("abc")._rev;
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      count = getLogEntries();
      
      c.truncate();

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 3, getLogEntries());
      
      var entries = getLastLogEntry(3), entry;
      // trx start
      entry = entries.pop();
      assertEqual(2200, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);
      assertTrue(entry.tid != "");
      var tid = entry.tid;

      // remove
      entry = entries.pop();
      assertEqual(2302, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual("abc", entry.key);
      assertEqual(rev, entry.oldRev);
      assertEqual(tid, entry.tid);
     
      // commit 
      entry = entries.pop();
      assertEqual(2201, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);
      assertEqual(tid, entry.tid);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTruncateCollection2 : function () {
      var state, tick;
      
      var c = db._create(cn);
      for (var i = 0; i < 100; ++i) {
        c.save({ "test": 1, "_key": "test" + i });
      }

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.truncate();
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 102, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexHash1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureUniqueConstraint("a", "b");
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("hash", entry.index.type);
      assertEqual(true, entry.index.unique);
      assertEqual([ "a", "b" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexHash2 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureHashIndex("a");
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("hash", entry.index.type);
      assertEqual(false, entry.index.unique);
      assertEqual([ "a" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexSkiplist1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureSkiplist("a", "b", "c");
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("skiplist", entry.index.type);
      assertEqual(false, entry.index.unique);
      assertEqual([ "a", "b", "c" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexSkiplist2 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureUniqueSkiplist("a");
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("skiplist", entry.index.type);
      assertEqual(true, entry.index.unique);
      assertEqual([ "a" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexFulltext1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureFulltextIndex("a", 5);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("fulltext", entry.index.type);
      assertEqual(false, entry.index.unique);
      assertEqual(5, entry.index.minLength);
      assertEqual([ "a" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexGeo1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureGeoIndex("a", "b");
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("geo2", entry.index.type);
      assertEqual(false, entry.index.unique);
      assertEqual(false, entry.index.constraint);
      assertEqual([ "a", "b" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexGeo2 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureGeoIndex("a", true);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("geo1", entry.index.type);
      assertEqual(false, entry.index.unique);
      assertEqual(false, entry.index.constraint);
      assertEqual([ "a" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexGeo3 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureGeoConstraint("a", "b", true);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("geo2", entry.index.type);
      assertEqual(true, entry.index.unique);
      assertEqual(true, entry.index.constraint);
      assertEqual(true, entry.index.ignoreNull);
      assertEqual([ "a", "b" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexGeo4 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureGeoConstraint("a", "b", false);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("geo2", entry.index.type);
      assertEqual(true, entry.index.unique);
      assertEqual(true, entry.index.constraint);
      assertEqual(false, entry.index.ignoreNull);
      assertEqual([ "a", "b" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexGeo5 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureGeoConstraint("a", true);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("geo1", entry.index.type);
      assertEqual(true, entry.index.unique);
      assertEqual(true, entry.index.constraint);
      assertEqual([ "a" ], entry.index.fields);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexCap1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureCapConstraint(100);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("cap", entry.index.type);
      assertEqual(false, entry.index.unique);
      assertEqual(100, entry.index.size);
      assertEqual(0, entry.index.byteSize);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerCreateIndexCap2 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.ensureCapConstraint(null, 1048576);
      var idx = c.getIndexes()[1];
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertTrue(2100, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.index.id);
      assertEqual("cap", entry.index.type);
      assertEqual(false, entry.index.unique);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerDropIndex : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.ensureUniqueConstraint("a", "b");

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      // use index at #1 (#0 is primary index)
      var idx = c.getIndexes()[1];
      c.dropIndex(idx);
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertTrue(2101, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual(idx.id.replace(/^.*\//, ''), entry.id);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerSaveDocument : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      c.save({ "test": 1, "_key": "abc" });
      var rev = c.document("abc")._rev;

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(1, entry.data.test);

      tick = state.lastLogTick;
      count = getLogEntries();
      
      c.save({ "test": 2, "foo" : "bar", "_key": "12345" });
      rev = c.document("12345")._rev;
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      entry = getLastLogEntry();
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);
      assertEqual("bar", entry.data.foo);

      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        c.save({ "test": 1, "_key": "12345" });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerSaveDocuments : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      for (var i = 0; i < 100; ++i) {
        c.save({ "test": 1, "_key": "test" + i });
      }

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 100, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerDeleteDocument : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.save({ "test": 1, "_key": "abc" });
      c.save({ "test": 1, "_key": "12345" });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      var rev = c.document("abc")._rev;
      c.remove("abc");

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2302, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual(rev, entry.oldRev);

      tick = state.lastLogTick;
      count = getLogEntries();

      rev = c.document("12345")._rev;
      c.remove("12345");
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      entry = getLastLogEntry();
      assertEqual(2302, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual(rev, entry.oldRev);
      
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        c.remove("12345");
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerUpdateDocument : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.save({ "test": 1, "_key": "abc" });
      c.save({ "test": 1, "_key": "12345" });

      var oldRev = c.document("abc")._rev;

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      c.update("abc", { "test" : 2 });
      var rev = c.document("abc")._rev;

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual(oldRev, entry.oldRev);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);
      
      tick = state.lastLogTick;
      count = getLogEntries();

      oldRev = rev;
      c.update("abc", { "test" : 3 });
      rev = c.document("abc")._rev;
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      entry = getLastLogEntry();
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual(oldRev, entry.oldRev);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(3, entry.data.test);
      
      tick = state.lastLogTick;
      count = getLogEntries();

      c.update("abc", { "test" : 3 });

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      c.update("12345", { "test" : 2 });
      c.update("abc", { "test" : 4 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 2, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        c.update("thefoxx", { });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerReplaceDocument : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.save({ "test": 1, "_key": "abc" });
      c.save({ "test": 1, "_key": "12345" });
      var oldRev = c.document("abc")._rev;

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      c.replace("abc", { "test" : 2 });
      var rev = c.document("abc")._rev;

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual(oldRev, entry.oldRev);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);

      tick = state.lastLogTick;
      count = getLogEntries();

      c.replace("abc", { "test" : 3 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      c.replace("abc", { "test" : 3 });

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      c.replace("12345", { "test" : 2 });
      c.replace("abc", { "test" : 4 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 2, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        c.replace("thefoxx", { });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerSaveEdge : function () {
      var state, tick;
      
      var v = db._create(cn);
      var e = db._createEdgeCollection(cn2);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      e.save(cn + "/test1", cn + "/test2", { "test": 1, "_key": "abc" });
      var rev = e.document("abc")._rev;
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2301, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(e._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(1, entry.data.test);

      tick = state.lastLogTick;
      count = getLogEntries();
      
      e.save(cn + "/test3", cn + "/test4", { "test": [ 99, false ], "_key": "12345" });
      rev = e.document("12345")._rev;
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      entry = getLastLogEntry();
      assertEqual(2301, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(e._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual([ 99, false ], entry.data.test);
      
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        e.save();
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerDeleteEdge : function () {
      var state, tick;
      
      var v = db._create(cn);
      var e = db._createEdgeCollection(cn2);
      
      e.save(cn + "/test1", cn + "/test2", { "test": 1, "_key": "abc" });
      e.save(cn + "/test3", cn + "/test4", { "test": 1, "_key": "12345" });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      var rev = e.document("abc")._rev; 
      e.remove("abc");
       
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      
      var entry = getLastLogEntry();
      assertEqual(2302, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(e._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual(rev, entry.oldRev);
      
      tick = state.lastLogTick;
      count = getLogEntries();
      
      e.remove("12345");
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        e.remove("12345");
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerUpdateEdge : function () {
      var state, tick;
      
      var v = db._create(cn);
      var e = db._createEdgeCollection(cn2);
      e.save(cn + "/test1", cn + "/test2", { "test": 1, "_key": "abc" });
      e.save(cn + "/test3", cn + "/test4", { "test": 1, "_key": "12345" });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      e.update("abc", { "test" : 2 });
      var rev = e.document("abc")._rev;

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2301, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(e._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);

      tick = state.lastLogTick;
      count = getLogEntries();

      e.update("abc", { "test" : 3 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      e.update("abc", { "test" : 3 });

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      e.update("12345", { "test" : 2 });
      e.update("abc", { "test" : 4 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 2, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        e.update("thefoxx", { });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerReplaceEdge : function () {
      var state, tick;
      
      var v = db._create(cn);
      var e = db._createEdgeCollection(cn2);
      e.save(cn + "/test1", cn + "/test2", { "test": 1, "_key": "abc" });
      e.save(cn + "/test3", cn + "/test4", { "test": 1, "_key": "12345" });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();
      
      e.replace("abc", { "test" : 2 });
      var rev = e.document("abc")._rev;

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());

      var entry = getLastLogEntry();
      assertEqual(2301, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(e._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);

      tick = state.lastLogTick;
      count = getLogEntries();

      e.replace("abc", { "test" : 3 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      e.replace("abc", { "test" : 3 });

      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 1, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      e.replace("12345", { "test" : 2 });
      e.replace("abc", { "test" : 4 });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 2, getLogEntries());
      tick = state.lastLogTick;
      count = getLogEntries();

      try {
        e.replace("thefoxx", { });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionEmpty : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var actual = db._executeTransaction({
        collections: {
        },
        action: function () {
          return true;
        }
      });
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionRead1 : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.save({ "test" : 1 });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var actual = db._executeTransaction({
        collections: {
          read: cn
        },
        action: function () {
          return true;
        }
      });
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionRead2 : function () {
      var state, tick;
      
      var c = db._create(cn);
      c.save({ "test" : 1, "_key": "abc" });

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var actual = db._executeTransaction({
        collections: {
          read: cn
        },
        action: function (params) {
          var c = require("internal").db._collection(params.cn);

          c.document("abc");

          return true;
        },
        params: {
          cn: cn
        }
      });
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionRead3 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      try {
        actual = db._executeTransaction({
          collections: {
            read: cn
          },
          action: function (params) {
            var c = require("internal").db._collection(params.cn);

            c.save({ "foo" : "bar" });

            return true;
          },
          params: {
            cn: cn
          }
        });
        fail();
      }
      catch (err) {
      }
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionWrite1 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var actual = db._executeTransaction({
        collections: {
          write: cn
        },
        action: function () {
          return true;
        }
      });
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionWrite2 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      var actual = db._executeTransaction({
        collections: {
          write: cn
        },
        action: function (params) {
          var c = require("internal").db._collection(params.cn);

          c.save({ "test" : 2, "_key": "abc" });
          return true;
        },
        params: {
          cn: cn
        }
      });
      assertTrue(actual);
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 3, getLogEntries());
      
      var rev = c.document("abc")._rev;
      var entries = getLastLogEntry(3), entry;
      entry = entries.pop();
      assertEqual(2200, entry.type);
      assertTrue(entry.tid != "");
      var tid = entry.tid;
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);

      entry = entries.pop();
      assertEqual(2300, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);
      
      entry = entries.pop();
      assertEqual(2201, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionWrite3 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      try {
        db._executeTransaction({
          collections: {
            write: cn
          },
          action: function (params) {
            var c = require("internal").db._collection(params.cn);

            c.save({ "test" : 2, "_key": "abc" });
            throw "fail";
          },
          params: {
            cn: cn
          }
        });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionWrite4 : function () {
      var state, tick;
      
      var c1 = db._create(cn);
      var c2 = db._create(cn2);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      try {
        db._executeTransaction({
          collections: {
            write: cn
          },
          action: function (params) {
            var c2 = require("internal").db._collection(params.cn2);

            c2.save({ "test" : 2, "_key": "abc" });
          },
          params: {
            cn2: cn2
          }
        });
        fail();
      }
      catch (err) {
        state = replication.logger.state().state;
        assertEqual(tick, state.lastLogTick);
        assertEqual(count, getLogEntries());
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionWrite5 : function () {
      var state, tick;
      
      var c1 = db._create(cn);
      var c2 = db._create(cn2);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      db._executeTransaction({
        collections: {
          write: [ cn, cn2 ]
        },
        action: function (params) {
          var c1 = require("internal").db._collection(params.cn);

          c1.save({ "test" : 1, "_key": "12345" });
          c1.save({ "test" : 2, "_key": "abc" });
        },
        params: {
          cn: cn 
        }
      });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 4, getLogEntries());
      
      var entries = getLastLogEntry(4), entry;
      entry = entries.pop();
      assertEqual(2200, entry.type);
      assertTrue(entry.tid != "");
      var tid = entry.tid;
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c1._id, entry.collections[0].cid);
      assertEqual(2, entry.collections[0].operations);

      var rev = c1.document("12345")._rev;
      entry = entries.pop();
      assertEqual(2300, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(c1._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(1, entry.data.test);
      
      rev = c1.document("abc")._rev;
      entry = entries.pop();
      assertEqual(2300, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(c1._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);
      
      entry = entries.pop();
      assertEqual(2201, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c1._id, entry.collections[0].cid);
      assertEqual(2, entry.collections[0].operations);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test actions
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionWrite6 : function () {
      var state, tick;
      
      var c1 = db._create(cn);
      var c2 = db._create(cn2);

      replication.logger.start();
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      db._executeTransaction({
        collections: {
          write: [ cn, cn2 ]
        },
        action: function (params) {
          var c1 = require("internal").db._collection(params.cn);
          var c2 = require("internal").db._collection(params.cn2);

          c1.save({ "test" : 1, "_key": "12345" });
          c2.save({ "test" : 2, "_key": "abc" });
        },
        params: {
          cn: cn,
          cn2: cn2
        }
      });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 4, getLogEntries());
      
      var entries = getLastLogEntry(4), entry;
      entry = entries.pop();
      assertEqual(2200, entry.type);
      assertTrue(entry.tid != "");
      var tid = entry.tid;
      entry = JSON.parse(entry.data);
      assertEqual(2, entry.collections.length);
      assertEqual(c1._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);
      assertEqual(c2._id, entry.collections[1].cid);
      assertEqual(1, entry.collections[1].operations);

      var rev = c1.document("12345")._rev;
      entry = entries.pop();
      assertEqual(2300, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(c1._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(1, entry.data.test);
      
      rev = c2.document("abc")._rev;
      entry = entries.pop();
      assertEqual(2300, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(c2._id, entry.cid);
      assertEqual("abc", entry.key);
      assertEqual("abc", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);
      
      entry = entries.pop();
      assertEqual(2201, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(2, entry.collections.length);
      assertEqual(c1._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);
      assertEqual(c2._id, entry.collections[1].cid);
      assertEqual(1, entry.collections[1].operations);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test collection exclusion
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionExcluded1 : function () {
      var state, tick;
      var c = db._create(cn);
      db._drop("_unitfoxx");
      db._create("_unitfoxx", { isSystem: true });

      replication.logger.start();
       
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      db._executeTransaction({
        collections: {
          write: [ cn, "_unitfoxx" ]
        },
        action: function (params) {
          var c = require("internal").db._collection(params.cn);
          var foxx = require("internal").db._collection("_unitfoxx");

          c.save({ "test" : 2, "_key": "12345" });
          foxx.save({ "_key": "unittests1", "foo": false });
          foxx.remove("unittests1");
        },
        params: {
          cn: cn
        }
      });
      
      state = replication.logger.state().state;
      assertNotEqual(tick, state.lastLogTick);
      assertEqual(1, compareTicks(state.lastLogTick, tick));
      assertEqual(count + 3, getLogEntries());
      
      var entries = getLastLogEntry(3), entry;
      entry = entries.pop();
      assertEqual(2200, entry.type);
      assertTrue(entry.tid != "");
      var tid = entry.tid;
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);

      var rev = c.document("12345")._rev;
      entry = entries.pop();
      assertEqual(2300, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(2, entry.data.test);
      
      entry = entries.pop();
      assertEqual(2201, entry.type);
      assertEqual(tid, entry.tid);
      entry = JSON.parse(entry.data);
      assertEqual(1, entry.collections.length);
      assertEqual(c._id, entry.collections[0].cid);
      assertEqual(1, entry.collections[0].operations);
      
      db._drop("_unitfoxx");
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test collection exclusion
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionExcluded2 : function () {
      var state, tick;
      
      var c = db._create(cn);
      replication.logger.start();
      
      c.save({ "test" : 1, "_key": "12345" });
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      db._executeTransaction({
        collections: {
          write: [ cn ]
        },
        action: function (params) {
          var c = require("internal").db._collection(params.cn);

          c.save({ "test" : 1, "_key" : "abc" });
        },
        params: {
          cn: cn
        },
        replicate: false
      });
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      var entry = getLastLogEntry();
      var rev = c.document("12345")._rev;
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(1, entry.data.test);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test collection exclusion
////////////////////////////////////////////////////////////////////////////////

    testLoggerTransactionExcluded3 : function () {
      var state, tick;
      
      var c = db._create(cn);

      replication.logger.start();
      c.save({ "test" : 1, "_key": "12345" });
      
      state = replication.logger.state().state;
      tick = state.lastLogTick;
      var count = getLogEntries();

      db._executeTransaction({
        collections: {
          write: [ cn ]
        },
        action: function (params) {
          var c = require("internal").db._collection(params.cn), i;

          for (i = 0; i < 100; ++i) {
            c.save({ "test" : 1, "_key" : "abc" + i });
          }

          for (i = 0; i < 100; ++i) {
            c.remove("abc" + i);
          }
        },
        params: {
          cn: cn
        },
        replicate: false
      });
      
      state = replication.logger.state().state;
      assertEqual(tick, state.lastLogTick);
      assertEqual(count, getLogEntries());
      
      var entry = getLastLogEntry();
      var rev = c.document("12345")._rev;
      assertEqual(2300, entry.type);
      entry = JSON.parse(entry.data);
      assertEqual(c._id, entry.cid);
      assertEqual("12345", entry.key);
      assertEqual("12345", entry.data._key);
      assertEqual(rev, entry.data._rev);
      assertEqual(1, entry.data.test);
    }

  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                          replication applier test
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function ReplicationApplierSuite () {

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      replication.applier.stop();
      replication.applier.forget();
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      replication.applier.stop();
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief start applier w/o configuration 
////////////////////////////////////////////////////////////////////////////////

    testStartApplierNoConfig : function () {
      var state;

      state = replication.applier.state();

      assertFalse(state.state.running);

      try {
        // start 
        replication.applier.start();
        fail();
      }
      catch (err) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_APPLIER_CONFIGURATION.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief start applier with configuration 
////////////////////////////////////////////////////////////////////////////////

    testStartApplierInvalidEndpoint : function () {
      var state;

      state = replication.applier.state();

      assertFalse(state.state.running);

      // configure && start 
      replication.applier.properties({ endpoint: "tcp://9.9.9.9:9999" });
      replication.applier.start();

      state = replication.applier.state();
      assertTrue(state.state.running);

      // call start again
      replication.applier.start();
      
      state = replication.applier.state();
      assertTrue(state.state.running);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief stop applier
////////////////////////////////////////////////////////////////////////////////

    testStopApplier : function () {
      var state;

      state = replication.applier.state();

      assertFalse(state.state.running);

      // configure && start 
      replication.applier.properties({ endpoint: "tcp://9.9.9.9:9999" });
      replication.applier.start();

      state = replication.applier.state();
      assertTrue(state.state.running);

      // stop
      replication.applier.stop();
      
      state = replication.applier.state();
      assertFalse(state.state.running);
      
      // stop again
      replication.applier.stop();
            
      state = replication.applier.state();
      assertFalse(state.state.running);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief properties
////////////////////////////////////////////////////////////////////////////////

    testApplierProperties : function () {
      var properties;

      properties = replication.applier.properties();

      assertEqual(300, properties.requestTimeout);
      assertEqual(10, properties.connectTimeout);
      assertEqual(10, properties.maxConnectRetries);
      assertFalse(properties.autoStart);
      assertTrue(properties.adaptivePolling);
      assertUndefined(properties.endpoint); 

      try {
        replication.applier.properties({ });
      }
      catch (err) {
        assertEqual(errors.ERROR_REPLICATION_INVALID_APPLIER_CONFIGURATION.code, err.errorNum);
      }
      
      replication.applier.properties({
        endpoint: "tcp://9.9.9.9:9999"
      });
      
      properties = replication.applier.properties();
      assertEqual(properties.endpoint, "tcp://9.9.9.9:9999");
      assertEqual(300, properties.requestTimeout);
      assertEqual(10, properties.connectTimeout);
      assertEqual(10, properties.maxConnectRetries);
      assertFalse(properties.autoStart);
      assertTrue(properties.adaptivePolling);

      replication.applier.properties({
        endpoint: "tcp://9.9.9.9:9998",
        autoStart: true,
        adaptivePolling: false,
        requestTimeout: 5,
        connectTimeout: 9,
        maxConnectRetries: 4
      });
      
      properties = replication.applier.properties();
      assertEqual(properties.endpoint, "tcp://9.9.9.9:9998");
      assertEqual(5, properties.requestTimeout);
      assertEqual(9, properties.connectTimeout);
      assertEqual(4, properties.maxConnectRetries);
      assertTrue(properties.autoStart);
      assertFalse(properties.adaptivePolling);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief start property change while running
////////////////////////////////////////////////////////////////////////////////

    testApplierPropertiesChange : function () {
      var state;

      replication.applier.properties({ endpoint: "tcp://9.9.9.9:9999" });
      replication.applier.start();

      state = replication.applier.state();
      assertTrue(state.state.running);
      
      try {
        replication.applier.properties({ endpoint: "tcp://9.9.9.9:9998" });
      }
      catch (err) {
        assertEqual(errors.ERROR_REPLICATION_RUNNING.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief applier state
////////////////////////////////////////////////////////////////////////////////

    testStateApplier : function () {
      var state;

      state = replication.applier.state();

      assertFalse(state.state.running);
      assertMatch(/^\d+-\d+-\d+T\d+:\d+:\d+Z$/, state.state.time);

      assertEqual(state.server.version, db._version()); 
      assertNotEqual("", state.server.serverId); 
      assertMatch(/^\d+$/, state.server.serverId);
      
      assertUndefined(state.endpoint);
    }

  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                              main
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suites
////////////////////////////////////////////////////////////////////////////////

jsunity.run(ReplicationLoggerSuite);
jsunity.run(ReplicationApplierSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
