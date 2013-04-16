/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true */
/*global document, $, _ */
/*global EventDispatcherControls, NodeShaperControls, EdgeShaperControls */
/*global LayouterControls, ArangoAdapterControls*/
/*global GraphViewer, d3*/
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

function GraphViewerUI(container, adapterConfig, optWidth, optHeight) {
  "use strict";
  
  if (container === undefined) {
    throw "A parent element has to be given.";
  }
  if (!container.id) {
    throw "The parent element needs an unique id.";
  }
  if (adapterConfig === undefined) {
    throw "An adapter configuration has to be given";
  } 
  
  var graphViewer,
    width = (optWidth || container.offsetWidth) - 60,
    height = optHeight || container.offsetHeight,
    menubar = document.createElement("ul"),
    svg, 
    makeBootstrapDropdown = function (div, id, title) {
      var btn, caret, list;
      div.className = "btn-group pull-right";
      btn = document.createElement("button");
      btn.className = "btn btn-inverse btn-small dropdown-toggle";
      btn.id = id;
      btn.setAttribute("data-toggle", "dropdown");
      btn.appendChild(document.createTextNode(title + " "));
      caret = document.createElement("span");
      caret.className = "caret";
      btn.appendChild(caret);
      list = document.createElement("ul");
      list.className = "dropdown-menu";
      div.appendChild(btn);
      div.appendChild(list);
      return list;
    },
    createSVG = function () {
      return d3.select("#" + container.id)
        .append("svg")
        .attr("width",width)
        .attr("height",height)
        .attr("class", "pull-right")
        .attr("style", "width:" + width + "px");
    },
    createToolbox = function() {
      var toolbox = document.createElement("div"),
        dispatcherUI = new EventDispatcherControls(
          toolbox,
          graphViewer.nodeShaper,
          graphViewer.edgeShaper,
          graphViewer.dispatcherConfig
        );
      toolbox.id = "toolbox";
      toolbox.className = "btn-group btn-group-vertical pull-left toolbox";
      container.appendChild(toolbox);
      dispatcherUI.addAll();
    },
    createMenu = function() {
      var transparentHeader = document.createElement("div"),
        searchDiv = document.createElement("div"),
        searchField = document.createElement("input"),
        searchStart = document.createElement("img"),
        nodeShaperDropDown = document.createElement("div"),
        nodeShaperList = makeBootstrapDropdown(
          nodeShaperDropDown,
          "nodeshaperdropdown",
          "Nodes"
        ),
        edgeShaperDropDown = document.createElement("div"),
        edgeShaperList = makeBootstrapDropdown(
          edgeShaperDropDown,
          "edgeshaperdropdown",
          "Edges"
        ),
        adapterDropDown = document.createElement("div"),
        adapterList = makeBootstrapDropdown(
          adapterDropDown,
          "adapterdropdown",
          "Connection"
        ),
        layouterDropDown = document.createElement("div"),
        layouterList = makeBootstrapDropdown(
          layouterDropDown,
          "layouterdropdown",
          "Layout"
        ),
        nodeShaperUI = new NodeShaperControls(
          nodeShaperList,
          graphViewer.nodeShaper
        ),
        edgeShaperUI = new EdgeShaperControls(
          edgeShaperList,
          graphViewer.edgeShaper
        ),
        adapterUI = new ArangoAdapterControls(
          adapterList,
          graphViewer.adapter
        ),
        layouterUI = new LayouterControls(
          layouterList,
          graphViewer.layouter
        );
      
      menubar.id = "menubar";
      menubar.className = "thumbnails2";
      
      transparentHeader.id = "transparentHeader";
      
      searchDiv.id = "transparentPlaceholder";
      searchDiv.className = "pull-left";
      
      searchField.id = "nodeid";
      searchField.className = "searchInput";
      searchField.type = "text";
      searchStart.id = "loadnode";
      searchStart.className = "searchSubmit";
      searchStart.width = 16;
      searchStart.height = 16;
      searchStart.src = "img/enter_icon.png";
      
      nodeShaperDropDown.id = "nodeshapermenu";
      edgeShaperDropDown.id = "edgeshapermenu";
      adapterDropDown.id = "adaptermenu";
      layouterDropDown.id = "layoutermenu";
      
      searchStart.onclick = function() {
        var nodeId = searchField.value;
        graphViewer.loadGraph(nodeId);
      };
      
      
      menubar.appendChild(transparentHeader);
      transparentHeader.appendChild(searchDiv);
      searchDiv.appendChild(searchField);
      searchDiv.appendChild(searchStart);
      transparentHeader.appendChild(nodeShaperDropDown);
      transparentHeader.appendChild(edgeShaperDropDown);
      transparentHeader.appendChild(adapterDropDown);
      transparentHeader.appendChild(layouterDropDown);
      
      nodeShaperUI.addAll();
      edgeShaperUI.addAll();
      adapterUI.addAll();
      layouterUI.addAll();
    };
  container.appendChild(menubar);
  svg = createSVG();
  graphViewer = new GraphViewer(svg, width, height, adapterConfig);
  
  createToolbox();
  createMenu();
}