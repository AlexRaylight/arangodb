/*jslint indent: 2, nomen: true, maxlen: 100, white: true  plusplus: true, browser: true*/
/*global describe, beforeEach, afterEach, it, */
/*global spyOn, runs, expect, waitsFor*/
/*global GraphManagementView, _, jasmine, $*/

(function () {
    "use strict";

    describe("Graph Management View", function () {

        var view,
            div,
            modalDiv,
            graphs,
            collections,
            v1, v2, e1, e2, sys1, cols;

        beforeEach(function () {
            modalDiv = document.createElement("div");
            modalDiv.id = "modalPlaceholder";
            document.body.appendChild(modalDiv);
            window.modalView = new window.ModalView();
            collections = new window.arangoCollections(cols);
            graphs = new window.GraphCollection();
            div = document.createElement("div");
            div.id = "content";
            document.body.appendChild(div);
            view = new window.GraphManagementView({
                collection: graphs,
                collectionCollection: new window.arangoCollections()
            });
        });

        afterEach(function () {
            document.body.removeChild(div);
            document.body.removeChild(modalDiv);
        });

        it("should fetch the graphs on render", function () {
            spyOn(graphs, "fetch");
            view.render();
            expect(graphs.fetch).toHaveBeenCalledWith({async: false});
        });

        describe("after rendering", function () {

            var g1, g2, g3;

            beforeEach(function () {
                g1 = {
                    _id: "_graphs/x1",
                    _key: "x1",
                    _rev: "123",
                    vertices: "v1",
                    edges: "e2"
                };
                g2 = {
                    _id: "_graphs/c2",
                    _key: "c2",
                    _rev: "321",
                    vertices: "v2",
                    edges: "e1"
                };
                g3 = {
                    _id: "_graphs/g3",
                    _key: "g3",
                    _rev: "111",
                    vertices: "v3",
                    edges: "e3"
                };
                spyOn(graphs, "fetch");
                graphs.add(g1);
                graphs.add(g2);
                graphs.add(g3);
                view.render();
            });

            it("should create a sorted list of all graphs", function () {
                var list = $("div.tile h5.collectionName", "#graphManagementThumbnailsIn");
                expect(list.length).toEqual(3);
                // Order would be g2, g3, g1
                expect($(list[0]).html()).toEqual(g2._key);
                expect($(list[1]).html()).toEqual(g3._key);
                expect($(list[2]).html()).toEqual(g1._key);
            });


            it("should loadGraphViewer", function () {
              var e = {
                  currentTarget: {
                    id: "blabalblub"
                  }
                },
                a = {
                  attr: function (x) {
                    return "blabalblub";
                  },
                  width : function () {
                    return 100
                  },
                  html : function () {

                  }
                };
              spyOn(window, "GraphViewerUI");

              spyOn(window, "$").andReturn(a);


              view.loadGraphViewer(e);

              expect(window.GraphViewerUI).toHaveBeenCalledWith(
                undefined,
                {
                  type : 'gharial',
                  graphName : 'blaba',
                  baseUrl : '/_db/_system/'
                }, 25, 680,
                {
                  nodeShaper :
                  {
                    label : '_key',
                    color : { type : 'attribute', key : '_key' }
                  }
                }, true
              );
            });

          it("should delete graph", function () {
            var e = {
                currentTarget: {
                  id: "blabalblub"
                }
              },
              a = [{
                value: "blabalblub"
              }],collReturn = {
                destroy : function (a) {
                  a.success();
                }
              };

            spyOn(graphs, "get").andReturn(collReturn);

            spyOn(window, "$").andReturn(a);

            spyOn(window.modalView, "hide").andReturn(a);

            view.deleteGraph(e);

            expect(graphs.get).toHaveBeenCalledWith("blabalblub");
            expect(window.modalView.hide).toHaveBeenCalled();
          });

          it("should NOT delete graph", function () {
            var e = {
                currentTarget: {
                  id: "blabalblub"
                }
              },
              a = [{
                value: "blabalblub"
              }],collReturn = {
                destroy : function (a) {
                  a.error("", {responseText : '{"errorMessage" : "errorMessage"}'});
                }
              };

            spyOn(graphs, "get").andReturn(collReturn);

            spyOn(window, "$").andReturn(a);
            spyOn(arangoHelper, "arangoError");

            spyOn(window.modalView, "hide").andReturn(a);

            view.deleteGraph(e);

            expect(graphs.get).toHaveBeenCalledWith("blabalblub");
            expect(arangoHelper.arangoError).toHaveBeenCalledWith("errorMessage");
            expect(window.modalView.hide).toHaveBeenCalled();


          });


          describe("creating a new graph", function () {

                it("should create a new graph", function () {
                    runs(function () {
                        $("#createGraph").click();
                    });
                    waitsFor(function () {
                        return $("#modal-dialog").css("display") === "block";
                    });
                    runs(function () {
                        $("#createNewGraphName").val("newGraph");
                        $("#newGraphVertices").val("newVertices");
                        $("#newGraphEdges").val("newEdges");
                        spyOn($, "ajax").andCallFake(function (opts) {
                            expect(opts.type).toEqual("POST");
                            expect(opts.url).toEqual("/_api/gharial");
                            expect(opts.data).toEqual(JSON.stringify(
                              {
                                "name":"newGraph",
                                "edgeDefinitions":[],
                                "orphanCollections":[]
                              }));
                        });
                        $("#modalButton1").click();
                        expect($.ajax).toHaveBeenCalled();
                    });
                });

            });

        });

    });

}());

