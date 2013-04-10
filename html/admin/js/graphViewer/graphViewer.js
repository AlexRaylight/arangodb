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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////


function GraphViewer(svg, width, height,
  adapterConfig, config) {
  
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
  
  if (adapterConfig === undefined || adapterConfig.type === undefined) {
    throw "An adapter configuration has to be given";
  }
  
  var self = this,
    nodeContainer,
    edgeContainer,
    layouter,
    fixedSize,
    dispatcher,
    edges = [],
    nodes = [],

  
    /*
  bindEventsFromConfig = function(conf) {
    var checkDefs = function(el) {
      return el !== undefined && el.target !== undefined && el.type !== undefined;
    },
    checkFunction = function(el) {
      return el !== undefined && _.isFunction(el);
    };
    
    if (checkDefs(conf.expand)) {
      dispatcher.bind(conf.expand.target, conf.expand.type, dispatcher.events.EXPAND);
    }
  
    if (checkDefs(conf.createNode)
    && checkFunction(conf.createNode.callback)) {
      dispatcher.bind(conf.createNode.target,
        conf.createNode.type,
        function() {
          dispatcher.events.CREATENODE(conf.createNode.callback);
          start();
        });
    }
  
    if (checkDefs(conf.patchNode)) {
      dispatcher.bind(conf.patchNode.target,
        conf.patchNode.type,
        dispatcher.events.PATCHNODE);
    }
  
    if (checkDefs(conf.deleteNode)
    && checkFunction(conf.deleteNode.callback)) {
      dispatcher.bind(conf.deleteNode.target,
        conf.deleteNode.type,
        function(n) {
          dispatcher.events.DELETENODE(n, conf.deleteNode.callback);
        }
      );
    }
    if (conf.custom !== undefined) {
      _.each(conf.custom, function(toBind) {
        if (checkDefs(toBind)
        && checkFunction(toBind.func)) {
          dispatcher.bind(toBind.target, toBind.type, toBind.func);
        }
      });
    }
  },
  */
  parseLayouterConfig = function (config) {
    if (!config) {
      // Default
      config = {};
      config.nodes = nodes;
      config.links = edges;
      config.width = width;
      config.height = height;
      layouter = new ForceLayouter(config);
      return;
    }
    switch (config.type.toLowerCase()) {
      case "force":
        config.nodes = nodes;
        config.links = edges;
        config.width = width;
        config.height = height;
        layouter = new ForceLayouter(config);
        break;
      default:
        throw "Sorry unknown layout type.";
    }
  },
  parseConfig = function(config) {
    var esConf = config.edgeShaper || {},
      nsConf = config.nodeShaper || {},
      idFunc = nsConf.idfunc || undefined;
    
    parseLayouterConfig(config.layouter);
    edgeContainer = svg.append("svg:g");
    self.edgeShaper = new EdgeShaper(edgeContainer, esConf);
    nodeContainer = svg.append("svg:g");
    self.nodeShaper = new NodeShaper(nodeContainer, nsConf, idFunc);
    layouter.setCombinedUpdateFunction(self.nodeShaper, self.edgeShaper);
    
    /*
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
    */
    /*
    if (nodeShaperConfig.dragable !== undefined) {
      nodeShaperConfig.ondrag = layouter.drag;
    }
    */
  
    
    
    
  };
  
  switch (adapterConfig.type.toLowerCase()) {
    case "arango":
      self.adapter = new ArangoAdapter(
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
      self.adapter = new JSONAdapter(
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
  
  parseConfig(config || {});
    
  self.start = function() {
    layouter.stop();
    self.edgeShaper.drawEdges(edges);
    self.nodeShaper.drawNodes(nodes);
    layouter.start();
  };
  /*
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
  */
  
  //dispatcher = new EventDispatcher(nodeShaper, edgeShaper, eventsDispatcherConfig);
  
  //bindEventsFromConfig(eventsConfig);
  
  self.loadGraph = function(nodeId) {
    nodes.length = 0;
    edges.length = 0;
    self.adapter.loadNodeFromTreeById(nodeId, function (node) {
      node._expanded = true;
      node.x = width / 2;
      node.y = height / 2;
      node.fixed = true;
      self.start();
    });
  };
  
  self.loadGraphWithAttributeValue = function(attribute, value) {
    nodes.length = 0;
    edges.length = 0;
    self.adapter.loadNodeFromTreeByAttributeValue(attribute, value, function (node) {
      node._expanded = true;
      node.x = width / 2;
      node.y = height / 2;
      node.fixed = true;
      self.start();
    });
  };
  
  
  //TODO REMOVE
  //HACK to view the Controls in the Demo
  /*
  var edgeShaperControls = new EdgeShaperControls($("#controls")[0], edgeShaper);
  edgeShaperControls.addAll();
  var nodeShaperControls = new NodeShaperControls($("#controls")[0], nodeShaper);
  nodeShaperControls.addAll();
  */
  
}