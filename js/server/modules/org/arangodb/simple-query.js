/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true, continue: true */
/*global require, exports, ArangoClusterComm, ArangoClusterInfo */

////////////////////////////////////////////////////////////////////////////////
/// @brief Arango Simple Query Language
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
/// @author Dr. Frank Celler
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var internal = require("internal");
var console = require("console");

var ArangoError = require("org/arangodb").ArangoError;

var sq = require("org/arangodb/simple-query-common");

var GeneralArrayCursor = sq.GeneralArrayCursor;
var SimpleQueryAll = sq.SimpleQueryAll;
var SimpleQueryArray = sq.SimpleQueryArray;
var SimpleQueryByExample = sq.SimpleQueryByExample;
var SimpleQueryFulltext = sq.SimpleQueryFulltext;
var SimpleQueryGeo = sq.SimpleQueryGeo;
var SimpleQueryNear = sq.SimpleQueryNear;
var SimpleQueryRange = sq.SimpleQueryRange;
var SimpleQueryWithin = sq.SimpleQueryWithin;

////////////////////////////////////////////////////////////////////////////////
/// @brief rewrites an index id by stripping the collection name from it
////////////////////////////////////////////////////////////////////////////////

var rewriteIndex = function (id) {
  if (id === null || id === undefined) {
    return;
  }

  if (typeof id === "string") {
    return id.replace(/^[a-zA-Z0-9_\-]+\//, '');
  }
  if (typeof id === "object" && id.hasOwnProperty("id")) {
    return id.id.replace(/^[a-zA-Z0-9_\-]+\//, '');
  }
  return id;
};

// -----------------------------------------------------------------------------
// --SECTION--                                                  SIMPLE QUERY ALL
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief executes an all query
////////////////////////////////////////////////////////////////////////////////

SimpleQueryAll.prototype.execute = function () {
  if (this._execution === null) {
    if (this._skip === null) {
      this._skip = 0;
    }
  
    var documents;
    var cluster = require("org/arangodb/cluster");
    
    if (cluster.isCoordinator()) {
      var dbName = require("internal").db._name();
      var shards = cluster.shardList(dbName, this._collection.name());
      var coord = { coordTransactionID: ArangoClusterInfo.uniqid() };
      var options = { coordTransactionID: coord.coordTransactionID, timeout: 360 };
      var limit = 0;
      if (this._limit > 0) {
        if (this._skip >= 0) {
          limit = this._skip + this._limit;
        }
      }

      shards.forEach(function (shard) {
        ArangoClusterComm.asyncRequest("put", 
                                       "shard:" + shard, 
                                       dbName, 
                                       "/_api/simple/all", 
                                       JSON.stringify({ 
                                         collection: shard, 
                                         skip: 0, 
                                         limit: limit || undefined,
                                         batchSize: 100000000
                                       }), 
                                       { }, 
                                       options);
      });

      var _documents = [ ], total = 0;
      var result = cluster.wait(coord, shards);
      var toSkip = this._skip, toLimit = this._limit;

      if (toSkip < 0) {
        // negative skip is special
        toLimit = null;
      }

      result.forEach(function(part) {
        var body = JSON.parse(part.body);
        total += body.total;

        if (toSkip > 0) {
          if (toSkip >= body.result.length) {
            toSkip -= body.result.length;
            return;
          }
          
          body.result = body.result.slice(toSkip);
          toSkip = 0;
        }

        if (toLimit !== null && toLimit !== undefined) {
          if (body.result.length >= toLimit) {
            body.result = body.result.slice(0, toLimit);
            toLimit = 0;
          }
          else {
            toLimit -= body.result.length;
          }
        }

        _documents = _documents.concat(body.result);
      });
     
      if (this._skip < 0) {
        // apply negative skip
        _documents = _documents.slice(_documents.length + this._skip, this._limit || 100000000);
      }

      documents = { 
        documents: _documents, 
        count: _documents.length, 
        total: total
      };
    }
    else {
      documents = this._collection.ALL(this._skip, this._limit);
    }

    this._execution = new GeneralArrayCursor(documents.documents);
    this._countQuery = documents.count;
    this._countTotal = documents.total;
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  QUERY BY EXAMPLE
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief normalise attribute names for searching in indexes
/// this will turn 
/// { a: { b: { c: 1, d: true }, e: "bar" } } 
/// into
/// { "a.b.c" : 1, "a.b.d" : true, "a.e" : "bar" }
////////////////////////////////////////////////////////////////////////////////

function normalizeAttributes (obj, prefix) {
  var prep = ((prefix === "" || prefix === undefined) ? "" : prefix + "."), o, normalized = { };

  for (o in obj) {
    if (obj.hasOwnProperty(o)) {
      if (typeof obj[o] === 'object' && ! Array.isArray(obj[o]) && obj[o] !== null) {
        var sub = normalizeAttributes(obj[o], prep + o), i;
        for (i in sub) {
          if (sub.hasOwnProperty(i)) {
            normalized[i] = sub[i];
          }
        }
      }
      else {
        normalized[prep + o] = obj[o];
      }
    }
  }

  return normalized;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief query-by scan or hash index
////////////////////////////////////////////////////////////////////////////////

function byExample (data) {
  var unique = true;
  var documentId = null;
  var k;
 
  var collection = data._collection;
  var example    = data._example;
  var skip       = data._skip;
  var limit      = data._limit;
     
  if (typeof example !== "object" || Array.isArray(example)) {
    // invalid datatype for example
    var err1 = new ArangoError();
    err1.errorNum = internal.errors.ERROR_ARANGO_DOCUMENT_TYPE_INVALID;
    err1.errorMessage = "invalid document type";
    throw err1;
  }

  for (k in example) {
    if (example.hasOwnProperty(k)) {
      if (example[k] === null) {
        unique = false;
      }
      else if (k === '_id' || k === '_key') {
        // example contains the document id in attribute "_id" or "_key"
        documentId = example[k];
        break;
      }
    }
  }

  if (documentId !== null) {
    // we can use the collection's primary index
    var doc;
    try {
      // look up document by id
      doc = collection.document(documentId);
    
      // we have used the primary index to look up the document
      // now we need to post-filter because non-indexed values might not have matched  
      for (k in example) {
        if (example.hasOwnProperty(k)) {
          if (doc[k] !== example[k]) {
            doc = null;
            break;
          }
        }
      }
    }
    catch (e) {
    }

    return { 
      total: doc ? 1 : 0, 
      count: doc ? 1 : 0, 
      documents: doc ? [ doc ] : [ ] 
    };
  }

  var idx = null;
  var normalized = normalizeAttributes(example, "");
  var keys = Object.keys(normalized);
  
  // try these index types
  var checks = [
    { type: "hash", fields: keys, unique: false },
    { type: "skiplist", fields: keys, unique: false }
  ];

  if (unique) {
    checks.push({ type: "hash", fields: keys, unique: true });
    checks.push({ type: "skiplist", fields: keys, unique: true });
  }
    
  var fields = [ ];
  for (k in normalized) {
    if (normalized.hasOwnProperty(k)) {
      fields.push([ k, [ normalized[k] ] ]);
    } 
  }
  checks.push({ type: "bitarray", fields: fields });

  for (k = 0; k < checks.length; ++k) {
    if (data._type !== undefined && data._type !== checks[k].type) {
      continue;
    }

    idx = collection.lookupIndex(checks[k]);
    if (idx !== null) {
      // found an index
      break;
    }
  }

  if (data._index !== undefined) {
    var invalid = false;

    // an index was specified
    if (idx === null || 
        idx.type !== data._type) {
      invalid = true;
    }
    else if (typeof data._index === 'object' && data._index.hasOwnProperty("id")) {
      if (rewriteIndex(idx.id) !== rewriteIndex(data._index.id)) {
        invalid = true;
      }
    }
    else if (typeof data._index === 'string') {
      if (rewriteIndex(idx.id) !== rewriteIndex(data._index)) {
        invalid = true;
      }
    }

    if (invalid) {
      // but none was found or the found one has a different type
      var err2 = new ArangoError();
      err2.errorNum = internal.errors.ERROR_ARANGO_NO_INDEX.code;
      err2.errorMessage = internal.errors.ERROR_ARANGO_NO_INDEX.message;
      throw err2;
    }
  }

  if (idx !== null) {
    // use an index
    switch (idx.type) {
      case "hash":
        return collection.BY_EXAMPLE_HASH(idx.id, normalized, skip, limit);
      case "skiplist":
        return collection.BY_EXAMPLE_SKIPLIST(idx.id, normalized, skip, limit);
      case "bitarray":
        return collection.BY_EXAMPLE_BITARRAY(idx.id, normalized, skip, limit);
    }
  }

  // use full collection scan
  return collection.BY_EXAMPLE(example, skip, limit);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a query-by-example
////////////////////////////////////////////////////////////////////////////////

SimpleQueryByExample.prototype.execute = function () {
  var documents;
  
  if (this._execution === null) {
    if (this._skip === null || this._skip <= 0) {
      this._skip = 0;
    }
    
    var cluster = require("org/arangodb/cluster");
    
    if (cluster.isCoordinator()) {
      var dbName = require("internal").db._name();
      var shards = cluster.shardList(dbName, this._collection.name());
      var coord = { coordTransactionID: ArangoClusterInfo.uniqid() };
      var options = { coordTransactionID: coord.coordTransactionID, timeout: 360 };
      var limit = 0;
      if (this._limit > 0) {
        if (this._skip >= 0) {
          limit = this._skip + this._limit;
        }
      }

      var method = "by-example";
      if (this._type !== undefined) {
        switch (this._type) {
          case "hash":
            method = "by-example-hash";
            break;
          case "skiplist": 
            method = "by-example-skiplist";
            break;
          case "bitarray":
            method = "by-example-bitarray";
            break;
        }
      }
      
      shards.forEach(function (shard) {
        ArangoClusterComm.asyncRequest("put", 
                                       "shard:" + shard, 
                                       dbName, 
                                       "/_api/simple/" + method,
                                       JSON.stringify({ 
                                         example: this._example,
                                         collection: shard, 
                                         skip: 0, 
                                         limit: limit || undefined, 
                                         batchSize: 100000000,
                                         index: rewriteIndex(this._index)
                                       }), 
                                       { }, 
                                       options);
      });

      var _documents = [ ];
      var result = cluster.wait(coord, shards);
      var toSkip = this._skip, toLimit = this._limit;
      
      if (toSkip < 0) {
        // negative skip is special
        toLimit = null;
      }

      result.forEach(function(part) {
        var body = JSON.parse(part.body);

        if (toSkip > 0) {
          if (toSkip >= body.result.length) {
            toSkip -= body.result.length;
            return;
          }
          
          body.result = body.result.slice(toSkip);
          toSkip = 0;
        }

        if (toLimit !== null && toLimit !== undefined) {
          if (body.result.length >= toLimit) {
            body.result = body.result.slice(0, toLimit);
            toLimit = 0;
          }
          else {
            toLimit -= body.result.length;
          }
        }

        _documents = _documents.concat(body.result);
      });
      
      if (this._skip < 0) {
        // apply negative skip
        _documents = _documents.slice(_documents.length + this._skip, this._limit || 100000000);
      }

      documents = { 
        documents: _documents, 
        count: _documents.length
      };
    }
    else {
      documents = byExample(this);
    }

    this._execution = new GeneralArrayCursor(documents.documents);
    this._countQuery = documents.count;
    this._countTotal = documents.total;
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                      RANGED QUERY
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief ranged query
////////////////////////////////////////////////////////////////////////////////

function rangedQuery (collection, attribute, left, right, type, skip, limit) {
  var documents;
  var cluster = require("org/arangodb/cluster");
    
  if (cluster.isCoordinator()) {
    var dbName = require("internal").db._name();
    var shards = cluster.shardList(dbName, collection.name());
    var coord = { coordTransactionID: ArangoClusterInfo.uniqid() };
    var options = { coordTransactionID: coord.coordTransactionID, timeout: 360 };
    var _limit = 0;
    if (limit > 0) {
      if (skip >= 0) {
        _limit = skip + limit;
      }
    }
                                     
    shards.forEach(function (shard) {
      ArangoClusterComm.asyncRequest("put", 
                                     "shard:" + shard, 
                                     dbName, 
                                     "/_api/simple/range", 
                                     JSON.stringify({ 
                                       collection: shard,
                                       attribute: attribute,
                                       left: left,
                                       right: right,
                                       closed: type,
                                       skip: 0, 
                                       limit: _limit || undefined,
                                       batchSize: 100000000
                                     }), 
                                     { }, 
                                     options);
    });

    var _documents = [ ], total = 0;
    var result = cluster.wait(coord, shards);

    result.forEach(function(part) {
      var body = JSON.parse(part.body);
      total += body.total;

      _documents = _documents.concat(body.result);
    });

    if (shards.length > 1) {
      var cmp = require("org/arangodb/ahuacatl").RELATIONAL_CMP;
      _documents.sort(function (l, r) {
        return cmp(l[attribute], r[attribute]); 
      });
    }
    
    if (limit > 0 && skip >= 0) {
      _documents = _documents.slice(skip, skip + limit);
    }
    else if (skip > 0) {
      _documents = _documents.slice(skip, _documents.length);
    }
    else if (skip < 0) {
      // apply negative skip
      _documents = _documents.slice(_documents.length + skip, limit || 100000000);
    }

    documents = { 
      documents: _documents, 
      count: _documents.length, 
      total: total
    };
  }
  else {
    var idx = collection.lookupSkiplist(attribute);

    if (idx === null) {
      idx = collection.lookupUniqueSkiplist(attribute);

      if (idx !== null) {
        console.debug("found unique skip-list index %s", idx.id);
      }
    }
    else {
      console.debug("found skip-list index %s", idx.id);
    }

    if (idx !== null) {
      var cond = {};

      if (type === 0) {
        cond[attribute] = [ [ ">=", left ], [ "<", right ] ];
      }
      else if (type === 1) {
        cond[attribute] = [ [ ">=", left ], [ "<=", right ] ];
      }
      else {
        throw "unknown type";
      }

      documents = collection.BY_CONDITION_SKIPLIST(idx.id, cond, skip, limit);
    }
    else {
      throw "not implemented";
    }
  }

  return documents;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a range query
////////////////////////////////////////////////////////////////////////////////

SimpleQueryRange.prototype.execute = function () {
  var documents;

  if (this._execution === null) {
    if (this._skip === null) {
      this._skip = 0;
    }

    documents = rangedQuery(this._collection,
                            this._attribute,
                            this._left,
                            this._right,
                            this._type,
                            this._skip, 
                            this._limit);

    this._execution = new GeneralArrayCursor(documents.documents);
    this._countQuery = documents.count;
    this._countTotal = documents.total;
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 SIMPLE QUERY NEAR
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a near query
////////////////////////////////////////////////////////////////////////////////

SimpleQueryNear.prototype.execute = function () {
  var documents;
  var result;
  var limit;
  var i, n;
  
  if (this._execution !== null) {
    return;
  }
    
  if (this._skip === null) {
    this._skip = 0;
  }

  if (this._skip < 0) {
    var err = new ArangoError();
    err.errorNum = internal.errors.ERROR_BAD_PARAMETER;
    err.errorMessage = "skip must be non-negative";
    throw err;
  }
    
  if (this._limit === null) {
    limit = this._skip + 100;
  }
  else {
    limit = this._skip + this._limit;
  }
  
  var cluster = require("org/arangodb/cluster");
    
  if (cluster.isCoordinator()) {
    var dbName = require("internal").db._name();
    var shards = cluster.shardList(dbName, this._collection.name());
    var coord = { coordTransactionID: ArangoClusterInfo.uniqid() };
    var options = { coordTransactionID: coord.coordTransactionID, timeout: 360 };
    var _limit = 0;
    if (this._limit > 0) {
      if (this._skip >= 0) {
        _limit = this._skip + this._limit;
      }
    }

    var attribute;
    if (this._distance !== null) {
      attribute = this._distance;
    }
    else {
      // use a pseudo-attribute for distance (we need this for sorting)
      attribute = "$distance";
    }
                                     
    var self = this;
    shards.forEach(function (shard) {
      ArangoClusterComm.asyncRequest("put", 
                                     "shard:" + shard, 
                                     dbName, 
                                     "/_api/simple/near", 
                                     JSON.stringify({ 
                                       collection: shard,
                                       latitude: self._latitude,
                                       longitude: self._longitude,
                                       distance: attribute,
                                       geo: rewriteIndex(self._index),
                                       skip: 0, 
                                       limit: _limit || undefined,
                                       batchSize: 100000000
                                     }), 
                                     { }, 
                                     options);
    });

    var _documents = [ ], total = 0;
    result = cluster.wait(coord, shards);

    result.forEach(function(part) {
      var body = JSON.parse(part.body);
      total += body.total;

      _documents = _documents.concat(body.result);
    });

    if (shards.length > 1) {
      _documents.sort(function (l, r) {
        if (l[attribute] === r[attribute]) {
          return 0;
        }
        return (l[attribute] < r[attribute] ? -1 : 1);
      });
    }
    
    if (this._limit > 0) {
      _documents = _documents.slice(0, this._skip + this._limit);
    }

    if (this._distance === null) {
      n = _documents.length;
      for (i = 0; i < n; ++i) {
        delete _documents[i][attribute];
      }
    }
     
    documents = { 
      documents: _documents, 
      count: _documents.length, 
      total: total
    };
  }
  else {
    result = this._collection.NEAR(this._index, this._latitude, this._longitude, limit);

    documents = {
      documents: result.documents,
      count: result.documents.length,
      total: result.documents.length
    };

    if (this._distance !== null) {
      var distances = result.distances;
      n = documents.documents.length;
      for (i = this._skip;  i < n;  ++i) {
        documents.documents[i][this._distance] = distances[i];
      }
    }
  }

  this._execution = new GeneralArrayCursor(documents.documents, this._skip, null);
  this._countQuery = documents.total - this._skip;
  this._countTotal = documents.total;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                               SIMPLE QUERY WITHIN
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a within query
////////////////////////////////////////////////////////////////////////////////

SimpleQueryWithin.prototype.execute = function () {
  var result;
  var documents;
  var i, n;

  if (this._execution !== null) {
    return;
  }
  
  if (this._skip === null) {
    this._skip = 0;
  }

  if (this._skip < 0) {
    var err = new ArangoError();
    err.errorNum = internal.errors.ERROR_BAD_PARAMETER;
    err.errorMessage = "skip must be non-negative";
    throw err;
  }
  
  var cluster = require("org/arangodb/cluster");
    
  if (cluster.isCoordinator()) {
    var dbName = require("internal").db._name();
    var shards = cluster.shardList(dbName, this._collection.name());
    var coord = { coordTransactionID: ArangoClusterInfo.uniqid() };
    var options = { coordTransactionID: coord.coordTransactionID, timeout: 360 };
    var _limit = 0;
    if (this._limit > 0) {
      if (this._skip >= 0) {
        _limit = this._skip + this._limit;
      }
    }

    var attribute;
    if (this._distance !== null) {
      attribute = this._distance;
    }
    else {
      // use a pseudo-attribute for distance (we need this for sorting)
      attribute = "$distance";
    }
                                     
    var self = this;
    shards.forEach(function (shard) {
      ArangoClusterComm.asyncRequest("put", 
                                     "shard:" + shard, 
                                     dbName, 
                                     "/_api/simple/within", 
                                     JSON.stringify({ 
                                       collection: shard,
                                       latitude: self._latitude,
                                       longitude: self._longitude,
                                       distance: attribute,
                                       radius: self._radius,
                                       geo: rewriteIndex(self._index),
                                       skip: 0, 
                                       limit: _limit || undefined,
                                       batchSize: 100000000
                                     }), 
                                     { }, 
                                     options);
    });

    var _documents = [ ], total = 0;
    result = cluster.wait(coord, shards);

    result.forEach(function(part) {
      var body = JSON.parse(part.body);
      total += body.total;

      _documents = _documents.concat(body.result);
    });

    if (shards.length > 1) {
      _documents.sort(function (l, r) {
        if (l[attribute] === r[attribute]) {
          return 0;
        }
        return (l[attribute] < r[attribute] ? -1 : 1);
      });
    }

    if (this._limit > 0) {
      _documents = _documents.slice(0, this._skip + this._limit);
    }
      
    if (this._distance === null) {
      n = _documents.length;
      for (i = 0; i < n; ++i) {
        delete _documents[i][attribute];
      }
    }
     
    documents = { 
      documents: _documents, 
      count: _documents.length, 
      total: total
    };
  }
  else {
    result = this._collection.WITHIN(this._index, this._latitude, this._longitude, this._radius);

    documents = {
      documents: result.documents,
      count: result.documents.length,
      total: result.documents.length
    };

    if (this._limit > 0) {
      documents.documents = documents.documents.slice(0, this._skip + this._limit);
    }
      
    if (this._distance !== null) {
      var distances = result.distances;
      n = documents.documents.length;
      for (i = this._skip;  i < n;  ++i) {
        documents.documents[i][this._distance] = distances[i];
      }
    }
  }

  this._execution = new GeneralArrayCursor(documents.documents, this._skip, null);
  this._countQuery = documents.total - this._skip;
  this._countTotal = documents.total;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                             SIMPLE QUERY FULLTEXT
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a fulltext query
////////////////////////////////////////////////////////////////////////////////

SimpleQueryFulltext.prototype.execute = function () {
  var result;
  var documents;

  if (this._execution !== null) {
    return;
  }

  var cluster = require("org/arangodb/cluster");
    
  if (cluster.isCoordinator()) {
    var dbName = require("internal").db._name();
    var shards = cluster.shardList(dbName, this._collection.name());
    var coord = { coordTransactionID: ArangoClusterInfo.uniqid() };
    var options = { coordTransactionID: coord.coordTransactionID, timeout: 360 };
    var _limit = 0;
    if (this._limit > 0) {
      if (this._skip >= 0) {
        _limit = this._skip + this._limit;
      }
    }

    var self = this;
    shards.forEach(function (shard) {
      ArangoClusterComm.asyncRequest("put", 
                                     "shard:" + shard, 
                                     dbName, 
                                     "/_api/simple/fulltext", 
                                     JSON.stringify({ 
                                       collection: shard,
                                       attribute: self._attribute,
                                       query: self._query,
                                       index: rewriteIndex(self._index),
                                       skip: 0, 
                                       limit: _limit || undefined,
                                       batchSize: 100000000
                                     }), 
                                     { }, 
                                     options);
    });

    var _documents = [ ], total = 0;
    result = cluster.wait(coord, shards);

    result.forEach(function(part) {
      var body = JSON.parse(part.body);
      total += body.total;

      _documents = _documents.concat(body.result);
    });

    if (this._limit > 0) {
      _documents = _documents.slice(0, this._skip + this._limit);
    }
      
    documents = { 
      documents: _documents, 
      count: _documents.length, 
      total: total
    };
  }
  else {
    result = this._collection.FULLTEXT(this._index, this._query);

    documents = {
      documents: result.documents,
      count: result.documents.length - this._skip,
      total: result.documents.length
    };
    
    if (this._limit > 0) {
      documents.documents = documents.documents.slice(0, this._skip + this._limit);
    }
  }

  this._execution = new GeneralArrayCursor(documents.documents, this._skip, null);
  this._countQuery = documents.total - this._skip;
  this._countTotal = documents.total;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    MODULE EXPORTS
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup SimpleQuery
/// @{
////////////////////////////////////////////////////////////////////////////////

exports.GeneralArrayCursor = GeneralArrayCursor;
exports.SimpleQueryAll = SimpleQueryAll;
exports.SimpleQueryArray = SimpleQueryArray;
exports.SimpleQueryByExample = SimpleQueryByExample;
exports.SimpleQueryFulltext = SimpleQueryFulltext;
exports.SimpleQueryGeo = SimpleQueryGeo;
exports.SimpleQueryNear = SimpleQueryNear;
exports.SimpleQueryRange = SimpleQueryRange;
exports.SimpleQueryWithin = SimpleQueryWithin;
exports.byExample = byExample;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
