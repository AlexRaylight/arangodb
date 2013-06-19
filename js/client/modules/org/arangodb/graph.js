/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true */
/*global require, exports */

////////////////////////////////////////////////////////////////////////////////
/// @brief Graph functionality
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
/// @author Dr. Frank Celler, Lucas Dohmen
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var arangodb = require("org/arangodb"),
  arangosh = require("org/arangodb/arangosh"),
  is = require("org/arangodb/is"),
  ArangoQueryCursor = require("org/arangodb/arango-query-cursor").ArangoQueryCursor,
  common = require("org/arangodb/graph-common"),
  Edge = common.Edge,
  Graph = common.Graph,
  Vertex = common.Vertex,
  GraphArray = common.GraphArray,
  Iterator,
  request;

Iterator = function (graph, cursor, MyPrototype, stringRepresentation) {
  this.next = function next() {
    if (cursor.hasNext()) {
      return new MyPrototype(graph, cursor.next());
    }

    return undefined;
  };

  this.hasNext = function hasNext() {
    return cursor.hasNext();
  };

  this._PRINT = function (context) {
    context.output += stringRepresentation;
  };
};


request = {
  send: function (method, graph, path, data) {
    var results = graph._connection[method]("/_api/graph/" +
      encodeURIComponent(graph._properties._key) +
      path,
      JSON.stringify(data));

    arangosh.checkRequestResult(results);
    return results;
  },

  postEdges: function (graph, edge, data) {
    var results = this.send("POST",
      graph,
      "/edges/" + encodeURIComponent(edge._properties._key),
      data);

    return new ArangoQueryCursor(graph._vertices._database, results);
  },

  putEdge: function (graph, edge, data) {
    var results = this.send("PUT",
      graph,
      "/edge/" + encodeURIComponent(edge._properties._key),
      data);

    return results;
  },

  putVertex: function (graph, vertex, data) {
    var results = this.send("PUT",
      graph,
      "/vertex/" + encodeURIComponent(vertex._properties._key),
      data);

    return results;
  },

  deleteGraph: function (graph) {
    var requestResult = graph._connection.DELETE("/_api/graph/" +
      encodeURIComponent(graph._properties._key));

    arangosh.checkRequestResult(requestResult);
  },

  postEdge: function(graph, params) {
    var requestResult = graph._connection.POST("/_api/graph/" +
      encodeURIComponent(graph._properties._key) + "/edge",
      JSON.stringify(params));

    arangosh.checkRequestResult(requestResult);
    return requestResult;
  },

  postVertex: function(graph, params) {
    var requestResult = graph._connection.POST("/_api/graph/" +
      encodeURIComponent(graph._properties._key) + "/vertex",
      JSON.stringify(params));

    arangosh.checkRequestResult(requestResult);
    return requestResult;
  },

  getVertex: function (graph, id) {
    var requestResult = graph._connection.GET("/_api/graph/" +
      encodeURIComponent(graph._properties._key) +
      "/vertex/" +
      encodeURIComponent(id));

    if (requestResult.error === true && requestResult.code === 404) {
      return null;
    }

    if (requestResult.vertex === null) {
      return null;
    }

    arangosh.checkRequestResult(requestResult);
    return requestResult;
  },

  getVertices: function (graph, params) {
    var requestResult = graph._connection.POST("/_api/graph/" +
      encodeURIComponent(graph._properties._key) + "/vertices",
      JSON.stringify(params));

    arangosh.checkRequestResult(requestResult);
    return new ArangoQueryCursor(graph._vertices._database, requestResult);
  },

  getEdge: function (graph, id) {
    var requestResult = graph._connection.GET("/_api/graph/" +
      encodeURIComponent(graph._properties._key) +
      "/edge/" +
      encodeURIComponent(id));

    if (requestResult.error === true && requestResult.code === 404) {
      return null;
    }

    if (requestResult.edge === null) {
      return null;
    }

    arangosh.checkRequestResult(requestResult);
    return requestResult;
  },

  getEdges: function (graph, params) {
    var requestResult = graph._connection.POST("/_api/graph/" +
      encodeURIComponent(graph._properties._key) + "/edges",
      JSON.stringify(params));

    arangosh.checkRequestResult(requestResult);
    return new ArangoQueryCursor(graph._vertices._database, requestResult);
  },

  deleteVertex: function (graph, vertex) {
    var requestResult = graph._connection.DELETE("/_api/graph/" +
      encodeURIComponent(graph._properties._key) +
      "/vertex/" +
      encodeURIComponent(vertex._properties._key));

    arangosh.checkRequestResult(requestResult);
  },

  deleteEdge: function (graph, edge) {
    var requestResult = graph._connection.DELETE("/_api/graph/" +
      encodeURIComponent(graph._properties._key) +
      "/edge/" +
      encodeURIComponent(edge._properties._key));

    arangosh.checkRequestResult(requestResult);
  }
};

// -----------------------------------------------------------------------------
// --SECTION--                                       module "org/arangodb/graph"
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                              Edge
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new edge object
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the to vertex
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getInVertex = function () {
  return this._graph.getVertex(this._properties._to);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the from vertex
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getOutVertex = function () {
  return this._graph.getVertex(this._properties._from);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the other vertex
////////////////////////////////////////////////////////////////////////////////

Edge.prototype.getPeerVertex = function (vertex) {
  if (vertex._properties._id === this._properties._to) {
    return this._graph.getVertex(this._properties._from);
  }

  if (vertex._properties._id === this._properties._from) {
    return this._graph.getVertex(this._properties._to);
  }

  return null;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief changes a property of an edge
////////////////////////////////////////////////////////////////////////////////


Edge.prototype.setProperty = function (name, value) {
  var requestResult;
  var update = this._properties;

  update[name] = value;

  requestResult = request.putEdge(this._graph, this, update);

  this._properties = requestResult.edge;

  return name;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                            Vertex
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new vertex object
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief inbound and outbound edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.edges = function (direction, labels) {
  var edge,
    edges = new GraphArray(),
    cursor;

  cursor = request.postEdges(this._graph, this, {
    filter : { direction : direction, labels: labels }
  });

  while (cursor.hasNext()) {
    edge = new Edge(this._graph, cursor.next());
    edges.push(edge);
  }

  return edges;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief inbound edges with given label
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.getInEdges = function () {
  var labels = Array.prototype.slice.call(arguments);
  return this.edges("in", labels);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief outbound edges with given label
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.getOutEdges = function () {
  var labels = Array.prototype.slice.call(arguments);
  return this.edges("out", labels);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief in- or outbound edges with given label
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.getEdges = function () {
  var labels = Array.prototype.slice.call(arguments);
  return this.edges("any", labels);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief inbound edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.inbound = function () {
  return this.getInEdges();
};

////////////////////////////////////////////////////////////////////////////////
/// @brief outbound edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.outbound = function () {
  return this.getOutEdges();
};

////////////////////////////////////////////////////////////////////////////////
/// @brief changes a property of a vertex
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.setProperty = function (name, value) {
  var requestResult,
    update = this._properties;

  update[name] = value;

  requestResult = request.putVertex(this._graph, this, update);

  this._properties = requestResult.vertex;

  return name;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.degree = function () {
  return this.getEdges().length;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of in-edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.inDegree = function () {
  return this.getInEdges().length;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of out-edges
////////////////////////////////////////////////////////////////////////////////

Vertex.prototype.outDegree = function () {
  return this.getOutEdges().length;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                             Graph
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new graph object
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.initialize = function (name, vertices, edges) {
  var requestResult;

  if (vertices === undefined && edges === undefined) {
    requestResult = arangodb.arango.GET("/_api/graph/" + encodeURIComponent(name));
  }
  else {
    requestResult = arangodb.arango.POST("/_api/graph", JSON.stringify({
      _key: name,
      vertices: vertices,
      edges: edges
    }));
  }

  arangosh.checkRequestResult(requestResult);

  this._properties = requestResult.graph;
  this._vertices = arangodb.db._collection(this._properties.vertices);
  this._edges = arangodb.db._collection(this._properties.edges);
  this._connection = arangodb.arango;

  return this;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief drops the graph, the vertices, and the edges
////////////////////////////////////////////////////////////////////////////////
Graph.prototype.drop = function () {
  request.deleteGraph(this);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief saves an edge to the graph
////////////////////////////////////////////////////////////////////////////////

Graph.prototype._saveEdge = function(id, out_vertex, in_vertex, params) {
  var requestResult;

  params._key = id;
  params._from = out_vertex._properties._key;
  params._to = in_vertex._properties._key;

  requestResult = request.postEdge(this, params);
  return new Edge(this, requestResult.edge);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief saves a vertex to the graph
////////////////////////////////////////////////////////////////////////////////

Graph.prototype._saveVertex = function (id, params) {
  var requestResult;

  if (is.existy(id)) {
    params._key = id;
  }

  requestResult = request.postVertex(this, params);
  return new Vertex(this, requestResult.vertex);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a vertex given its id
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.getVertex = function (id) {
  var requestResult = request.getVertex(this, id);

  if (requestResult === null) {
    return null;
  }

  return new Vertex(this, requestResult.vertex);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns an iterator for all vertices
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.getVertices = function () {
  var cursor = request.getVertices(this, {});
  return new Iterator(this, cursor, Vertex, "[vertex iterator]");
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns an edge given its id
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.getEdge = function (id) {
  var requestResult = request.getVertex(this, id);

  if (requestResult === null) {
    return null;
  }

  return new Edge(this, requestResult.edge);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns an iterator for all edges
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.getEdges = function () {
  var cursor = request.getEdges(this, {});
  return new Iterator(this, cursor, Edge, "[edge iterator]");
};

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a vertex and all in- or out-bound edges
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.removeVertex = function (vertex) {
  request.deleteVertex(this, vertex);
  vertex._properties = undefined;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief removes an edge
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.removeEdge = function (edge) {
  request.deleteEdge(this, edge);
  edge._properties = undefined;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of vertices
///
/// @FUN{@FA{graph}.order()}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.order = function () {
  return this._vertices.count();
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of edges
///
/// @FUN{@FA{graph}.size()}
////////////////////////////////////////////////////////////////////////////////

Graph.prototype.size = function () {
  return this._edges.count();
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    MODULE EXPORTS
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoGraph
/// @{
////////////////////////////////////////////////////////////////////////////////

exports.Edge = Edge;
exports.Graph = Graph;
exports.Vertex = Vertex;
exports.GraphArray = GraphArray;

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
