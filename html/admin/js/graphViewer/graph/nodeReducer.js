/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true */
/*global _*/
// Will be injected by WebWorkers
/*global self*/
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

function NodeReducer(nodes, edges, prioList) {
  "use strict";
  
  if (nodes === undefined) {
    throw "Nodes have to be given.";
  }
  if (edges === undefined) {
    throw "Edges have to be given.";
  }
  
  prioList = prioList || [];
  
  var 
  
    ////////////////////////////////////
    // Private functions              //
    //////////////////////////////////// 
    
    /////////////////////////////
    // Functions for Buckets   //
    /////////////////////////////
    
   addNode = function(bucket, node) {
     bucket.push(node);
   },
   
   getSimilarityValue = function(bucket, node) {
     if (bucket.length === 0) {
       return 1;
     }
     var data = node._data || {},
       comp = bucket[0]._data || {},
       props = _.union(_.keys(comp), _.keys(data)),
       countMatch = 0,
       propCount = 0;
     _.each(props, function(key) {
       if (comp[key] !== undefined && data[key]!== undefined) {
         countMatch++;
         if (comp[key] === data[key]) {
           countMatch += 4;
         }
       }
     });
     propCount = props.length * 5;
     propCount++;
     countMatch++;
     return countMatch / propCount;
   },
   
   
   bucketByPrioList = function (toSort, numBuckets) {
     var res = {},
       resArray = [];
     _.each(toSort, function(n) {
       var d = n._data,
         sortTo = {},
         key,
         resKey,
         i = 0;
       for (i = 0; i < prioList.length; i++) {
         key = prioList[i];
         if (d[key] !== undefined) {
           resKey = d[key];
           res[key] = res[key] || {};
           res[key][resKey] = res[key][resKey] || [];
           res[key][resKey].push(n);
           return;
         }
       }
       resKey = "default";
       res[resKey] = res[resKey] || [];
       res[resKey].push(n);
     });
     _.each(res, function(first) {
       _.each(first, function(list) {
         resArray.push(list);
       });
     });
     return resArray;
   },

  bucketNodes = function(toSort, numBuckets) {
    
    var res = [],
    threshold = 0.5;
    if (toSort.length <= numBuckets) {
      res = _.map(toSort, function(n) {
        return [n];
      });
      return res;
    }
    if (!_.isEmpty(prioList)) {
      return bucketByPrioList(toSort, numBuckets);
    }
    _.each(toSort, function(n) {
      var i, shortest, sLength;
      shortest = 0;
      sLength = Number.POSITIVE_INFINITY;
      for (i = 0; i < numBuckets; i++) {
        res[i] = res[i] || [];
        if (getSimilarityValue(res[i], n) > threshold) {
          addNode(res[i], n);
          return;
        }
        if (sLength > res[i].length) {
          shortest = i;
          sLength = res[i].length;
        }
      }
      addNode(res[shortest], n);
    });
    return res;
  };
  
  ////////////////////////////////////
  // Public functions               //
  ////////////////////////////////////
   
  this.bucketNodes = bucketNodes;
  
}
