/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true */
/*global beforeEach, afterEach, jasmine */
/*global describe, it, expect, spyOn */
/*global window, eb, loadFixtures, document */
/*global $, _, d3*/
/*global helper*/
/*global NodeReducer*/

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


(function () {
  "use strict";

  describe('Modularity Joiner', function () {
    
    describe('setup process', function() {
      
      it('should throw an error if no nodes are given', function() {
        expect(function() {
          var s = new ModularityJoiner();
        }).toThrow("Nodes have to be given.");
      });
      
      it('should throw an error if no edges are given', function() {
        expect(function() {
          var s = new ModularityJoiner([]);
        }).toThrow("Edges have to be given.");
      });
      
      it('should not throw an error if mandatory information is given', function() {
        expect(function() {
          var s = new ModularityJoiner([], []);
        }).not.toThrow();
      });
    });
    
    describe('setup correctly', function() {
      
      var joiner,
      nodes,
      edges,
      testNetFour;

      beforeEach(function () {
        nodes = [];
        edges = [];
        joiner = new ModularityJoiner(nodes, edges);
        testNetFour = function() {
          helper.insertSimpleNodes(nodes, ["0", "1", "2", "3"]);
          edges.push(helper.createSimpleEdge(nodes, 0, 1));
          edges.push(helper.createSimpleEdge(nodes, 0, 3));
          edges.push(helper.createSimpleEdge(nodes, 1, 2));
          edges.push(helper.createSimpleEdge(nodes, 2, 1));
          edges.push(helper.createSimpleEdge(nodes, 2, 3));
          edges.push(helper.createSimpleEdge(nodes, 3, 0));
          edges.push(helper.createSimpleEdge(nodes, 3, 1));
          edges.push(helper.createSimpleEdge(nodes, 3, 2));
        };
      });
      
      describe('getters', function() {
        
        beforeEach(function() {
          this.addMatchers({
            toBeGetter: function() {
              var func = joiner[this.actual];
              if (!func) {
                this.message = function() {
                  return "Expected " + this.actual + " to be defined.";
                };
                return false;
              }
              if ("function" !== typeof func) {
                this.message = function() {
                  return "Expected " + this.actual + " to be a function.";
                };
                return false;
              }
              if (func.length !== 0) {
                this.message = function() {
                  return "Expected " + this.actual + " to be a getter function.";
                };
                return false;
              }
              return true;
            }
          });
        })
        
        it('should offer the adjacency matrix', function() {
          expect("getAdjacencyMatrix").toBeGetter();
        });
        
        it('should offer the heap', function() {
          expect("getHeap").toBeGetter();
        });
        
        it('should offer the delta qs', function() {
          expect("getDQ").toBeGetter();
        });
        
        it('should offer the degrees', function() {
          expect("getDegrees").toBeGetter();
        });
        
        it('should offer the best join', function() {
          expect("getBest").toBeGetter();
        });
        
        it('should offer the community list', function() {
          expect("getCommunities").toBeGetter();
        });
        
      });
      
      describe('checking the interface', function() {
        
        it('should offer a setup function', function() {
          expect(joiner.setup).toBeDefined();
          expect(joiner.setup).toEqual(jasmine.any(Function));
          expect(joiner.setup.length).toEqual(0);
        });
        
        it('should offer a function to join two communities', function() {
          expect(joiner.joinCommunity).toBeDefined();
          expect(joiner.joinCommunity).toEqual(jasmine.any(Function));
          expect(joiner.joinCommunity.length).toEqual(1);
        });
        
      });

      describe('after setup', function() {
        
        beforeEach(function() {
          testNetFour();
          joiner.setup();
        });
        
        describe('the adjacency matrix', function() {
          /*
          it('should be created', function() {
            expect(joiner.getAdjacencyMatrix()).toEqual({
              "0": ["1", "3"],
              "1": ["2"],
              "2": ["1", "3"],
              "3": ["0", "1", "2"]
            });
        
          });
          */
          
          it('should be created', function() {
            expect(joiner.getAdjacencyMatrix()).toEqual({
              "0": {
                "1": 1,
                "3": 1
              },
              "1": {
                "2": 1
              },
              "2": {
                "1": 1,
                "3": 1
              },
              "3": {
                "0": 1,
                "1": 1,
                "2": 1
              }
            });
          });
        });
        
        describe('the degrees', function() {
        
          var m, one, two, three, initDeg;
            
        
          beforeEach(function() {
            m = edges.length;
            one = 1 / m;
            two = 2 / m;
            three = 3 / m;
            initDeg = {
              "0": {
                in: one,
                out: two
              },
              "1": {
                in: three,
                out: one
              },
              "2": {
                in: two,
                out: two
              },
              "3": {
                in: two,
                out: three
              }
            };
          });
        
          it('should initialy be populated', function() {
            expect(joiner.getDegrees()).toEqual(initDeg);
          });
          
          /*
          it('should be updated after a joining step', function() {
            var toJoin = joiner.getBest(),
              expected = {};
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("0");
            expect(toJoin.lID).toEqual("3");
            
            expected["0"] = {
              in: initDeg["0"].in + initDeg["3"].in,
              out: initDeg["0"].out + initDeg["3"].out,
            };
            expected["1"] = initDeg["1"];
            expected["2"] = initDeg["2"];
            
            joiner.joinCommunity(toJoin);
            
            expect(joiner.getDegrees()).toEqual(expected);
          });
          */
        });
        
        
        describe('the deltaQ', function() {
        
          var m, zero, one, two, three, initDQ;
        
          beforeEach(function() {          
            m = edges.length;
            zero = {
              in: 1/m,
              out: 2/m
            };
            one = {
              in: 3/m,
              out: 1/m
            };
            two = {
              in: 2/m,
              out: 2/m
            };
            three = {
              in: 2/m,
              out: 3/m
            };
            initDQ = {
              "0": {
                "1": 1/m - zero.in * one.out - zero.out * one.in,
                "2": - zero.in * two.out - zero.out * two.in,
                "3": 2/m - zero.in * three.out - zero.out * three.in
              },
              "1": {
                "2": 2/m - one.in * two.out - one.out * two.in,
                "3": 1/m - one.in * three.out - one.out * three.in
              },
              "2": {
                "3": 2/m - two.in * three.out - two.out * three.in
              },
            };
          });
        
          it('should initialy be populated', function() {
            expect(joiner.getDQ()).toEqual(initDQ);
          });
        
          it('should be updated after a joining step', function() {
            var toJoin = joiner.getBest(),
              expected = {};
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("0");
            expect(toJoin.lID).toEqual("3");
            
            expected["0"] = {};
            expected["0"]["1"] = initDQ["0"]["1"] + initDQ["1"]["3"];
            expected["0"]["2"] = initDQ["0"]["2"] + initDQ["2"]["3"];
            expected["1"] = {};
            expected["1"]["2"] = initDQ["1"]["2"];
            
            joiner.joinCommunity(toJoin);
            
            expect(joiner.getDQ()).toEqual(expected);
          });
        
        });
        
        /*
        describe('the deltaQ', function() {
        
          var m, zero, one, two, three, initDQ;
        
          beforeEach(function() {          
            m = edges.length;
            zero = {
              in: 1/m,
              out: 2/m
            };
            one = {
              in: 3/m,
              out: 1/m
            };
            two = {
              in: 2/m,
              out: 2/m
            };
            three = {
              in: 2/m,
              out: 3/m
            };
            initDQ = {
              "0": {
                "1": 1/m - zero.in * one.out,
                "3": 2/m - zero.in * three.out - zero.out * three.in
              },
              "1": {
                "2": 2/m - one.in * two.out - one.out * two.in,
                "3": 1/m - one.out * three.in
              },
              "2": {
                "3": 2/m - two.in * three.out - two.out * three.in
              },
            };
          });
        
          it('should initialy be populated', function() {
            expect(joiner.getDQ()).toEqual(initDQ);
          });
        
          it('should be updated after a joining step', function() {
            var toJoin = joiner.getBest(),
              expected = {};
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("0");
            expect(toJoin.lID).toEqual("3");
            
            expected["0"] = {};
            expected["0"]["1"] = initDQ["0"]["1"] + initDQ["1"]["3"];
            expected["0"]["2"] = initDQ["2"]["3"] - 3;
            
            expected["1"] = {};
            expected["1"]["2"] = initDQ["1"]["2"];
            
            joiner.joinCommunity(toJoin);
            
            expect(joiner.getDQ()).toEqual(expected);
          });
        
        });
        
        */
        describe('the heap', function() {
        
          var m, zero, one, two, three
        
          beforeEach(function() {          
            m = edges.length;
            zero = {
              in: 1/m,
              out: 2/m
            };
            one = {
              in: 3/m,
              out: 1/m
            };
            two = {
              in: 2/m,
              out: 2/m
            };
            three = {
              in: 2/m,
              out: 3/m
            };
          });

          it('should initialy by populated', function() {
            expect(joiner.getHeap()).toEqual({
              "0": "3",
              "1": "2",
              "2": "3"
            });
          });
        
          it('should return the largest value', function() {
            expect(joiner.getBest()).toEqual({
              sID: "0",
              lID: "3",
              val: 2/m - zero.in * three.out - zero.out * three.in
            });
          });
        
          it('should be updated after a join step', function() {
            var toJoin = joiner.getBest(),
              expected = {};
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("0");
            expect(toJoin.lID).toEqual("3");

            joiner.joinCommunity(toJoin);
            
            expect(joiner.getHeap()).toEqual({
              "0": "2",
              "1": "2"
            });
          });
        
          it('should return the largest value after a join step', function() {
            var toJoin = joiner.getBest(),
              expected = {};
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("0");
            expect(toJoin.lID).toEqual("3");

            joiner.joinCommunity(toJoin);
            
            expect(joiner.getBest()).toEqual({
              sID: "1",
              lID: "2",
              val: 2/m - one.in * two.out - one.out * two.in
            });
          });
          
          it('should return null for best if the value is 0 or worse', function() {
            var toJoin = joiner.getBest();
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("0");
            expect(toJoin.lID).toEqual("3");
            joiner.joinCommunity(toJoin);
            
            toJoin = joiner.getBest();
            //Make sure we join the right ones:
            expect(toJoin.sID).toEqual("1");
            expect(toJoin.lID).toEqual("2");
            joiner.joinCommunity(toJoin);
            expect(joiner.getBest()).toBeNull();
          });
        
        });
        
        describe('communities', function() {
        
          it('should be able to get the communities', function() {
            // No communities yet. Should not return single nodes.
            expect(joiner.getCommunities()).toEqual({});
          });
        
          it('should be able to join two communities', function() {
            var toJoin = joiner.getBest(),
              joinVal = toJoin.val,
              lowId = toJoin.sID,
              highId = toJoin.lID,
              expected = {},
              m = edges.length,
              zero = {
                in: 1/m,
                out: 2/m
              },
              one = {
                in: 3/m,
                out: 1/m
              },
              two = {
                in: 2/m,
                out: 2/m
              },
              three = {
                in: 2/m,
                out: 3/m
              };
            expected[lowId] = {
              nodes: [
                lowId,
                highId
              ],
              q: joinVal
            };
            expect(toJoin).toEqual({
              sID: "0",
              lID: "3",
              val: 2/m - zero.in * three.out - zero.out * three.in
            });
            joiner.joinCommunity(toJoin);
            expect(joiner.getCommunities()).toEqual(expected);
          });
        
        });
        
      });
      
      describe('checking the zachary karate club', function() {
        
        beforeEach(function() {
          // This is the Zachary Karate Club Network
          helper.insertSimpleNodes(nodes, [
            "0", // Just Temporary node, as the orig is counting from 1 instead of 0
            "1","2","3","4","5","6","7","8","9","10",
            "11","12","13","14","15","16","17","18","19","20",
            "21","22","23","24","25","26","27","28","29","30",
            "31","32","33","34"
          ]);
          edges.push(helper.createSimpleEdge(nodes, 2, 1));
          edges.push(helper.createSimpleEdge(nodes, 3, 1));
          edges.push(helper.createSimpleEdge(nodes, 3, 2));
          edges.push(helper.createSimpleEdge(nodes, 4, 1));
          edges.push(helper.createSimpleEdge(nodes, 4, 2));
          edges.push(helper.createSimpleEdge(nodes, 4, 3));
          edges.push(helper.createSimpleEdge(nodes, 5, 1));
          edges.push(helper.createSimpleEdge(nodes, 6, 1));
          edges.push(helper.createSimpleEdge(nodes, 7, 1));
          edges.push(helper.createSimpleEdge(nodes, 7, 5));
          edges.push(helper.createSimpleEdge(nodes, 7, 6));
          edges.push(helper.createSimpleEdge(nodes, 8, 1));
          edges.push(helper.createSimpleEdge(nodes, 8, 2));
          edges.push(helper.createSimpleEdge(nodes, 8, 3));
          edges.push(helper.createSimpleEdge(nodes, 8, 4));
          edges.push(helper.createSimpleEdge(nodes, 9, 1));
          edges.push(helper.createSimpleEdge(nodes, 9, 3));
          edges.push(helper.createSimpleEdge(nodes, 10, 3));
          edges.push(helper.createSimpleEdge(nodes, 11, 1));
          edges.push(helper.createSimpleEdge(nodes, 11, 5));
          edges.push(helper.createSimpleEdge(nodes, 11, 6));
          edges.push(helper.createSimpleEdge(nodes, 12, 1));
          edges.push(helper.createSimpleEdge(nodes, 13, 1));
          edges.push(helper.createSimpleEdge(nodes, 13, 4));
          edges.push(helper.createSimpleEdge(nodes, 14, 1));
          edges.push(helper.createSimpleEdge(nodes, 14, 2));
          edges.push(helper.createSimpleEdge(nodes, 14, 3));
          edges.push(helper.createSimpleEdge(nodes, 14, 4));
          edges.push(helper.createSimpleEdge(nodes, 17, 6));
          edges.push(helper.createSimpleEdge(nodes, 17, 7));
          edges.push(helper.createSimpleEdge(nodes, 18, 1));
          edges.push(helper.createSimpleEdge(nodes, 18, 2));
          edges.push(helper.createSimpleEdge(nodes, 20, 1));
          edges.push(helper.createSimpleEdge(nodes, 20, 2));
          edges.push(helper.createSimpleEdge(nodes, 22, 1));
          edges.push(helper.createSimpleEdge(nodes, 22, 2));
          edges.push(helper.createSimpleEdge(nodes, 26, 24));
          edges.push(helper.createSimpleEdge(nodes, 26, 25));
          edges.push(helper.createSimpleEdge(nodes, 28, 3));
          edges.push(helper.createSimpleEdge(nodes, 28, 24));
          edges.push(helper.createSimpleEdge(nodes, 28, 25));
          edges.push(helper.createSimpleEdge(nodes, 29, 3));
          edges.push(helper.createSimpleEdge(nodes, 30, 24));
          edges.push(helper.createSimpleEdge(nodes, 30, 27));
          edges.push(helper.createSimpleEdge(nodes, 31, 2));
          edges.push(helper.createSimpleEdge(nodes, 31, 9));
          edges.push(helper.createSimpleEdge(nodes, 32, 1));
          edges.push(helper.createSimpleEdge(nodes, 32, 25));
          edges.push(helper.createSimpleEdge(nodes, 32, 26));
          edges.push(helper.createSimpleEdge(nodes, 32, 29));
          edges.push(helper.createSimpleEdge(nodes, 33, 3));
          edges.push(helper.createSimpleEdge(nodes, 33, 9));
          edges.push(helper.createSimpleEdge(nodes, 33, 15));
          edges.push(helper.createSimpleEdge(nodes, 33, 16));
          edges.push(helper.createSimpleEdge(nodes, 33, 19));
          edges.push(helper.createSimpleEdge(nodes, 33, 21));
          edges.push(helper.createSimpleEdge(nodes, 33, 23));
          edges.push(helper.createSimpleEdge(nodes, 33, 24));
          edges.push(helper.createSimpleEdge(nodes, 33, 30));
          edges.push(helper.createSimpleEdge(nodes, 33, 31));
          edges.push(helper.createSimpleEdge(nodes, 33, 32));
          edges.push(helper.createSimpleEdge(nodes, 34, 9));
          edges.push(helper.createSimpleEdge(nodes, 34, 10));
          edges.push(helper.createSimpleEdge(nodes, 34, 14));
          edges.push(helper.createSimpleEdge(nodes, 34, 15));
          edges.push(helper.createSimpleEdge(nodes, 34, 16));
          edges.push(helper.createSimpleEdge(nodes, 34, 19));
          edges.push(helper.createSimpleEdge(nodes, 34, 20));
          edges.push(helper.createSimpleEdge(nodes, 34, 21));
          edges.push(helper.createSimpleEdge(nodes, 34, 23));
          edges.push(helper.createSimpleEdge(nodes, 34, 24));
          edges.push(helper.createSimpleEdge(nodes, 34, 27));
          edges.push(helper.createSimpleEdge(nodes, 34, 28));
          edges.push(helper.createSimpleEdge(nodes, 34, 29));
          edges.push(helper.createSimpleEdge(nodes, 34, 30));
          edges.push(helper.createSimpleEdge(nodes, 34, 31));
          edges.push(helper.createSimpleEdge(nodes, 34, 32));
          edges.push(helper.createSimpleEdge(nodes, 34, 33));
          nodes.shift(1); //Remove the temporary node;
          joiner.setup();
          
        });
        
        it('should never have duplicates in communities', function() {
          this.addMatchers({
            toNotContainDuplicates: function() {
              var comms = this.actual,
                duplicate = [],
                found = {},
                failed = false;
              _.each(comms, function (v) {
                _.each(v.nodes, function (i) {
                  if (found[i]) {
                    failed = true;
                    duplicate.push(i);
                  } else {
                    found[i] = true;
                  }
                });
              });
              this.message = function() {
                var outComms = comms;
                _.each(outComms, function(o) {
                  o.nodes = _.filter(o.nodes, function(n) {
                    return _.contains(duplicate, n);
                  });
                })
                
                return "Found duplicate nodes [" + duplicate
                  + "] in communities: " + JSON.stringify(outComms);
              }
              return !failed;
            }
          });
          var best = joiner.getBest();
          while (best !== null) {
            joiner.joinCommunity(best);
            best = joiner.getBest();
            expect(joiner.getCommunities()).toNotContainDuplicates();
          }
        });
        
        
        it('should be able to find communities', function() {
          this.addMatchers({
            toContainKarateClubCommunities: function() {
              var c1 = ["15", "16", "19", "21", "23", "24", "27", "28", "30", "31", "33", "34", "9"].sort().join(),
                c2 = ["10", "13", "14", "18", "2", "22", "3", "4", "8"].sort().join(),
                c3 = ["1", "11", "12", "17", "20" , "5", "6", "7"].sort().join(),
                c4 = ["25", "26", "29", "32"].sort().join(),
                comms = this.actual,
                failed = false,
                msg = "Found incorrect: ";
              _.each(comms, function(o) {
                var check = o.nodes.sort().join();
                switch (check) {
                case c1:
                  c1 = "";
                  break;
                case c2:
                  c2 = "";
                  break;
                case c3:
                  c3 = "";
                  break;
                case c4:
                  c4 = "";
                  break;
                default:
                  msg += "[" + check + "] ";  
                  failed = true;
                }
                return;
              });
              this.message = function() {
                var notFound = "";
                if (c1 !== "") {
                  notFound += "[" + c1 + "] ";
                }
                if (c2 !== "") {
                  notFound += "[" + c2 + "] ";
                }
                if (c3 !== "") {
                  notFound += "[" + c3 + "] ";
                }
                if (c4 !== "") {
                  notFound += "[" + c4 + "] ";
                }
                return msg + " and did not find: " + notFound;
              };
              return !failed;
            }
          });
          
          
          var best = joiner.getBest();
          var steps = 0;
          while (best !== null) {
            joiner.joinCommunity(best);
            best = joiner.getBest();
            steps++;
          }
          expect(joiner.getCommunities()).toContainKarateClubCommunities();
        });
        
      });
      
      
      
    });
  });
}());