/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true */
/*global _*/
/*global EventDispatcher, ArangoAdapter, JSONAdapter */
/*global ForceLayouter, EdgeShaper, NodeShaper */
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
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////


function GraphViewer(svg, width, height,
  adapterConfig, nodeShaperConfig, edgeShaperConfig,
  layouterConfig, eventsConfig) {
  
  "use strict";
  // Check if all required inputs are given
  if (svg === undefined || svg.append === undefined) {
    throw "SVG has to be given and has to be selected using d3.select";
  }
  
  if (width === undefined || width <= 0) {
    throw "A width greater 0 has to be given";
  }
  
  if (height === undefined || height <= 0) {
    throw "A height greater 0 has to be given";
  }
  
  if (adapterConfig === undefined) {
    throw "Adapter config has to be given";
  }
  
  if (nodeShaperConfig === undefined) {
    throw "Node Shaper config has to be given";
  }
  
  if (edgeShaperConfig === undefined) {
    throw "Edge Shaper config has to be given";
  }
  
  if (layouterConfig === undefined) {
    throw "Layouter config has to be given";
  }
  
  if (eventsConfig === undefined) {
    throw "Events config has to be given";
  }
  
  
  
  var self = this,
    adapter,
    nodeShaper,
    edgeShaper,
    nodeContainer,
    edgeContainer,
    layouter,
    fixedSize,
    dispatcher,
    edges = [],
    nodes = [],
    eventsDispatcherConfig = {},
    // Function after handling events, will update the drawers and the layouter.
    start;
  
  switch (adapterConfig.type.toLowerCase()) {
    case "arango":
      adapter = new ArangoAdapter(
        adapterConfig.host,
        nodes,
        edges,
        adapterConfig.nodeCollection,
        adapterConfig.edgeCollection,
        width,
        height
      );
      break;
    case "json":
      adapter = new JSONAdapter(
        adapterConfig.path,
        nodes,
        edges,
        width,
        height
      );
      break;
    default:
      throw "Sorry unknown adapter type.";
  }    
  
  
  switch (layouterConfig.type.toLowerCase()) {
    case "force":
      layouterConfig.nodes = nodes;
      layouterConfig.links = edges;
      layouterConfig.width = width;
      layouterConfig.height = height;
      layouter = new ForceLayouter(layouterConfig);
      break;
    default:
      throw "Sorry unknown layout type.";
  } 
  
  edgeContainer = svg.append("svg:g");
  
  edgeShaper = new EdgeShaper(edgeContainer, edgeShaperConfig);

  nodeContainer = svg.append("svg:g");
  
  if (nodeShaperConfig.childrenCentrality !== undefined) {
    fixedSize = nodeShaperConfig.size || 12;
    nodeShaperConfig.size = function(node) {
      if (node._expanded) {
        return fixedSize;
      }
      if (node._centrality !== undefined) {
        return fixedSize + 3 * node._centrality;
      }
      adapter.requestCentralityChildren(node._id, function(c) {
        node._centrality = c;
        nodeShaper.reshapeNode(node);
      });
      return fixedSize;
    };
  }
  if (nodeShaperConfig.dragable !== undefined) {
    nodeShaperConfig.ondrag = layouter.drag;
  }
  
  if (nodeShaperConfig.idfunc !== undefined) {
    nodeShaper = new NodeShaper(nodeContainer, nodeShaperConfig, nodeShaperConfig.idfunc);
  } else {
    nodeShaper = new NodeShaper(nodeContainer, nodeShaperConfig);
  }
  
  layouter.setCombinedUpdateFunction(nodeShaper, edgeShaper);
  
  start = function() {
    layouter.stop();
    edgeShaper.drawEdges(edges);
    nodeShaper.drawNodes(nodes);
    layouter.start();
  };
  
  if (eventsConfig.expand !== undefined) {
    eventsDispatcherConfig.expand = {
      edges: edges,
      nodes: nodes,
      startCallback: start,
      loadNode: adapter.loadNodeFromTreeById,
      reshapeNode: nodeShaper.reshapeNode
    };
  }
  if (eventsConfig.createNode !== undefined
    || eventsConfig.patchNode !== undefined
    || eventsConfig.deleteNode !== undefined) {
    eventsDispatcherConfig.nodeEditor = {
      nodes: nodes,
      adapter: adapter
    };
  }
  if (eventsConfig.edgeEditor !== undefined) {
    eventsDispatcherConfig.edgeEditor = {
      edges: edges,
      adapter: adapter
    };
  }
  
  dispatcher = new EventDispatcher(nodeShaper, edgeShaper, eventsDispatcherConfig);
  
  if (eventsConfig.expand !== undefined
    && eventsConfig.expand.target !== undefined
    && eventsConfig.expand.type !== undefined) {
    dispatcher.bind(eventsConfig.expand.target, eventsConfig.expand.type, dispatcher.events.EXPAND);
    dispatcher.bind("nodes", "update", function(node) {
      node.selectAll("circle")
      .attr("class", function(d) {
        return d._expanded ? "expanded" : 
          d._centrality === 0 ? "single" : "collapsed";
      });
    });
  }
  
  if (eventsConfig.createNode !== undefined
    && eventsConfig.createNode.target !== undefined
    && eventsConfig.createNode.type !== undefined
    && eventsConfig.createNode.callback !== undefined
    && _.isFunction(eventsConfig.createNode.callback)) {
    dispatcher.bind(eventsConfig.createNode.target,
      eventsConfig.createNode.type,
      function() {
        dispatcher.events.CREATENODE(eventsConfig.createNode.callback);
      });
  }
  
  
  if (eventsConfig.patchNode !== undefined
    && eventsConfig.patchNode.target !== undefined
    && eventsConfig.patchNode.type !== undefined) {
    dispatcher.bind(eventsConfig.patchNode.target,
      eventsConfig.patchNode.type,
      dispatcher.events.PATCHNODE);
  }
  
  if (eventsConfig.deleteNode !== undefined
    && eventsConfig.deleteNode.target !== undefined
    && eventsConfig.deleteNode.type !== undefined) {
    dispatcher.bind(eventsConfig.deleteNode.target,
      eventsConfig.deleteNode.type,
      dispatcher.events.DELETENODE);
  }
  
  self.loadGraph = function(nodeId) {
    nodes.length = 0;
    edges.length = 0;
    adapter.loadNodeFromTreeById(nodeId, function (node) {
      node._expanded = true;
      node.x = width / 2;
      node.y = height / 2;
      node.fixed = true;
      start();
    });
  };
  
}