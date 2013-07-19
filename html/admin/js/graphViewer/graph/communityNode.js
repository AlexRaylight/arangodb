/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true */
/*global _, document*/
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
/// @author Michael Hackstein
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////



function CommunityNode(parent, initial) {
  "use strict";
  
  if (_.isUndefined(parent) || !_.isFunction(parent.dissolveCommunity)) {
    throw "A parent element has to be given.";
  }
  
  initial = initial || [];
  
  var
    myInternalNodes = [
      {
        _id: "inter1",
        x: Math.floor(Math.random()*400 - 200),
        y: Math.floor(Math.random()*400 - 200),
        z: 1
      },
      {
        _id: "inter2",
        x: Math.floor(Math.random()*400 - 200),
        y: Math.floor(Math.random()*400 - 200),
        z: 1
      },
      {
        _id: "inter3",
        x: Math.floor(Math.random()*400 - 200),
        y: Math.floor(Math.random()*400 - 200),
        z: 1
      }
    ],

  ////////////////////////////////////
  // Private variables              //
  ////////////////////////////////////   
    self = this,
    boundingBox,
    nodes = {},
    observer,
    nodeArray = [],
    internal = {},
    inbound = {},
    outbound = {},
    outReferences = {},
  ////////////////////////////////////
  // Private functions              //
  //////////////////////////////////// 
  
    getObserver = function() {
      if (!observer) {
        observer = new WebKitMutationObserver(function(e){
          if (_.any(e, function(obj) {
            return obj.attributeName === "transform";
          })) {
            updateBoundingBox();
            observer.disconnect();
          }
        });
      }
      return observer;
    },
  
    toArray = function(obj) {
      var res = [];
      _.each(obj, function(v) {
        res.push(v);
      });
      return res;
    },
  
    hasNode = function(id) {
      return !!nodes[id];
    },
  
    getNodes = function() {
      return toArray(nodes);
    },
    
    getNode = function(id) {
      return nodes[id];
    },
  
    insertNode = function(n) {
      nodes[n._id] = n;
      nodeArray = toArray(nodes);
      self._size++;
    },
    
    removeNode = function(n) {
      var id = n._id || n;
      delete nodes[id];
      nodeArray = toArray(nodes);
      self._size--;
    },
    
    removeInboundEdge = function(e) {
      var id;
      if (!_.has(e, "_id")) {
        id = e;
        e = internal[id] || inbound[id];
      } else {
        id = e._id;
      }
      e.target = e._target;
      delete e._target;
      if (internal[id]) {
        delete internal[id];
        self._outboundCounter++;
        outbound[id] = e;
        return;
      }
      delete inbound[id];
      self._inboundCounter--;
      return;
    },
    
    removeOutboundEdge = function(e) {
      var id;
      if (!_.has(e, "_id")) {
        id = e;
        e = internal[id] || outbound[id];
      } else {
        id = e._id;
      }
      e.source = e._source;
      delete e._source;
      delete outReferences[e.source._id][id];
      if (internal[id]) {
        delete internal[id];
        self._inboundCounter++;
        inbound[id] = e;
        return;
      }
      delete outbound[id];
      self._outboundCounter--;
      return;
    },
    
    removeOutboundEdgesFromNode = function(n) {
      var id = n._id || n,
        res = [];
      _.each(outReferences[id], function(e) {
        removeOutboundEdge(e);
        res.push(e);
      });
      delete outReferences[id];
      return res;
    },
    
    insertInboundEdge = function(e) {
      e._target = e.target;
      e.target = self;
      if (outbound[e._id]) {
        delete outbound[e._id];
        self._outboundCounter--;
        internal[e._id] = e;
        return true;
      }
      inbound[e._id] = e;
      self._inboundCounter++;
      return false;
    },
    
    insertOutboundEdge = function(e) {
      var sId = e.source._id;
      e._source = e.source;
      e.source = self;
      outReferences[sId] = outReferences[sId] || {};
      outReferences[sId][e._id] = e;
      if (inbound[e._id]) {
        delete inbound[e._id];
        self._inboundCounter--;
        internal[e._id] = e;
        return true;
      }
      self._outboundCounter++;
      outbound[e._id] = e;
      return false;
    },
  
    getDissolveInfo = function() {
      return {
        nodes: nodeArray,
        edges: {
          both: toArray(internal),
          inbound: toArray(inbound),
          outbound: toArray(outbound)
        }
      };
    },
    
    expand = function() {
      this._expanded = true;
      // TODO: Just piped through for old Adapter Interface
      //dissolve();
    },
    
    dissolve = function() {
      parent.dissolveCommunity(self);
    },
    
    collapse = function() {
      this._expanded = false;
      // TODO
    },
    
    addDistortion = function() {
      // Fake Layouting TODO
      _.each(nodeArray, function(n) {
        n.position = {
          x: n.x,
          y: n.y,
          z: 1
        };
      });
    },
    
    addShape = function (g, shapeFunc, colourMapper) {
      g.attr("stroke", colourMapper.getForegroundCommunityColour());
      shapeFunc(g);
    },
    
    addCollapsedShape = function(g, shapeFunc, colourMapper) {
      g.attr("stroke", colourMapper.getForegroundCommunityColour());
      shapeFunc(g, 9);
      shapeFunc(g, 6);
      shapeFunc(g, 3);
      shapeFunc(g);
    },
    
    addCollapsedLabel = function(g, colourMapper) {
      var textN = g.append("text") // Append a label for the node
        .attr("text-anchor", "middle") // Define text-anchor
        .attr("fill", colourMapper.getForegroundCommunityColour())
        .attr("stroke", "none"); // Make it readable
      if (self._reason && self._reason.key) {
        textN.append("tspan")
          .attr("x", "0")
          .attr("dy", "-4")
          .text(self._reason.key + ":");
        textN.append("tspan")
          .attr("x", "0")
          .attr("dy", "16")
          .text(self._reason.value);
      } else {
        textN.text(self._size);
      }
    },
    
    addNodeShapes = function(g, shapeFunc, colourMapper) {
      var interior = g.selectAll(".node")
      .data(nodeArray, function(d) {
        return d._id;
      });
      interior.enter()
        .append("g")
        .attr("class", "node")
        .attr("id", function(d) {
          return d._id;
        });
      // Remove all old
      interior.exit().remove();
      interior.selectAll("* > *").remove();
      addShape(interior, shapeFunc, colourMapper);
    },
    
    addBoundingBox = function(g) {
      boundingBox = g.append("rect")
        .attr("rx", "8")
        .attr("ry", "8")
        .attr("fill", "none")
        .attr("stroke", "black");
      getObserver().observe(document.getElementById(self._id), {
        subtree:true,
        attributes:true
      });
    },

    updateBoundingBox = function() {
      var bbox = document.getElementById(self._id).getBBox();
      boundingBox.attr("width", bbox.width + 10)
       .attr("height", bbox.height + 10)
       .attr("x", bbox.x - 5)
       .attr("y", bbox.y - 5);
    },
    
    shapeAll = function(g, shapeFunc, colourMapper) {
      if (self._expanded) {
        addBoundingBox(g);
        addDistortion();
        addNodeShapes(g, shapeFunc, colourMapper);
        return;
      }
      addCollapsedShape(g, shapeFunc, colourMapper);
      addCollapsedLabel(g, colourMapper);
    };
  
  ////////////////////////////////////
  // Setup                          //
  ////////////////////////////////////
  
  ////////////////////////////////////
  // Values required for shaping    //
  ////////////////////////////////////
  this._id = "*community_" + Math.floor(Math.random()* 1000000);
  if (initial.length > 0) {
    this.x = initial[0].x;
    this.y = initial[0].y;
  } else {
    this.x = 0;
    this.y = 0;
  }
  this._size = 0;
  this._inboundCounter = 0;
  this._outboundCounter = 0;
  this._expanded = false;
  // Easy check for the other classes,
  // no need for a regex on the _id any more.
  this._isCommunity = true;
  
  _.each(initial, function(n) {
    insertNode(n);
  });
  
  ////////////////////////////////////
  // Public functions               //
  ////////////////////////////////////
  
  this.hasNode = hasNode;
  this.getNodes = getNodes;
  this.getNode = getNode;
  
  this.insertNode = insertNode;
  this.insertInboundEdge = insertInboundEdge;
  this.insertOutboundEdge = insertOutboundEdge;
  
  this.removeNode = removeNode;
  this.removeInboundEdge = removeInboundEdge;
  this.removeOutboundEdge = removeOutboundEdge;
  this.removeOutboundEdgesFromNode = removeOutboundEdgesFromNode;
  
  this.dissolve = dissolve;
  this.getDissolveInfo = getDissolveInfo;
  
  this.collapse = collapse;
  this.expand = expand;
  
  this.shape = shapeAll;
}
