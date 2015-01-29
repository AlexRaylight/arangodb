/*jshint browser: true */
/*global describe, beforeEach, afterEach, it, spyOn, expect*/
/*global runs, waitsFor, jasmine*/
/*global $, arangoHelper */

(function() {
  "use strict";

  describe("Applications View", function() {
    var div, view, listDummy, closeButton, generateButton, modalDiv;

    beforeEach(function() {
      modalDiv = document.createElement("div");
      modalDiv.id = "modalPlaceholder";
      document.body.appendChild(modalDiv);
      window.modalView = new window.ModalView();
      closeButton = "#modalButton0";
      generateButton = "#modalButton1";
      div = document.createElement("div");
      div.id = "content";
      document.body.appendChild(div);
      window.CreateDummyForObject(window, "FoxxCollection");
      listDummy = new window.FoxxCollection();
    });

    afterEach(function() {
      document.body.removeChild(div);
      delete window.modalView;
      document.body.removeChild(modalDiv);
    });

    describe("Adding a new App", function() {
      var uploadCallback, storeApp, storeAppVersion;

      beforeEach(function() {
        storeApp = "Online App";
        storeAppVersion = "2.1.1";
        spyOn(listDummy, "fetch").andCallFake(function(opts) {
          opts.success();
        });
        spyOn(listDummy, "each");
        spyOn(listDummy, "sort");
        spyOn($.fn, "uploadFile").andCallFake(function(opts) {
          expect(opts.url).toEqual("/_api/upload?multipart=true");
          expect(opts.allowedTypes).toEqual("zip");
          expect(opts.onSuccess).toEqual(jasmine.any(Function));
          uploadCallback = opts.onSuccess;
        });
        spyOn($, "get").andCallFake(function(url, callback) {
          expect(url).toEqual("foxxes/fishbowl");
          callback([
            {
              name: "MyApp",
              author: "ArangoDB",
              description: "Description of the app",
              latestVersion: "1.1.0"
            },{
              name: storeApp,
              author: "ArangoDB",
              description: "Description of the other app",
              latestVersion: storeAppVersion
            }
          ]);
          return {
            fail: function() {}
          };
        });
        view = new window.ApplicationsView({
          collection: listDummy
        });

        runs(function () {
          $("#addApp").click();
        });

        waitsFor(function () {
          return $("#modal-dialog").css("display") === "block";
        }, "The Add App dialog should be shown", 750);

      });

      it("should open and close the install dialog", function() {

        runs(function() {
          $(closeButton).click();
        });

        waitsFor(function () {
          return $("#modal-dialog").css("display") === "none";
        }, "The Add App dialog should be hidden.", 750);

      });

      it("should generate an empty app", function() {
        var mount, author, name, desc, license, expectedInfo;

        runs(function() {
          $("#tab-new").click();
          spyOn(listDummy, "generate").andCallFake(function(i, m, callback) {
            callback({
              error: false
            }); 
          });
          mount = "/my/app";
          author = "ArangoDB";
          name = "MyApp";
          desc = "My new app";
          license = "Apache 2";
          expectedInfo = {
            author: author,
            name: name,
            description: desc,
            license: license,
            collectionNames: []
          };

          $("#new-app-mount").val(mount);
          $("#new-app-author").val(author);
          $("#new-app-name").val(name);
          $("#new-app-description").val(desc);
          $("#new-app-license").val(license);
          expect($(generateButton).prop("disabled")).toBeFalsy();
          spyOn(view, "reload");
          $(generateButton).click();
        });

        waitsFor(function () {
          return $("#modal-dialog").css("display") === "none";
        }, "The Add App dialog should be hidden.", 750);

        runs(function() {
          expect(view.reload).toHaveBeenCalled();
          expect(listDummy.generate).toHaveBeenCalledWith(
            expectedInfo, mount, jasmine.any(Function)
          );
        });

      });

      it("should install an app from the store", function() {
        var mount;

        runs(function() {
          var button;
          mount = "/my/app";
          $("#new-app-mount").val(mount);
          $("#tab-store").click();
          spyOn(listDummy, "installFromStore").andCallFake(function(i, m, callback) {
            callback({
              error: false
            });
          });
          expect($(generateButton).prop("disabled")).toBeTruthy();
          expect($("#appstore-content").children().length).toEqual(2);
          button = $("#appstore-content .install-app[appId='" + storeApp + "']");
          expect(button.length).toEqual(1);
          spyOn(view, "reload");
          button.click();
        });

        waitsFor(function () {
          return $("#modal-dialog").css("display") === "none";
        }, "The Add App dialog should be hidden.", 750);

        runs(function() {
          expect(view.reload).toHaveBeenCalled();
          expect(listDummy.installFromStore).toHaveBeenCalledWith(
            {name: storeApp, version: storeAppVersion}, mount, jasmine.any(Function)
          );
        });

      });

      it("should install an app from github", function() {
        var repository, version, mount, expectedInfo;
        runs(function() {
          $("#tab-git").click();
          spyOn(listDummy, "installFromGithub").andCallFake(function(i, m, callback) {
            callback({
              error: false
            }); 
          });
          repository = "arangodb/itzpapalotl";
          version = "1.2.0";
          mount = "/my/app";
          expectedInfo = {
            url: repository,
            version: version
          };
          $("#new-app-mount").val(mount);
          $("#repository").val(repository);
          $("#tag").val(version);
          expect($(generateButton).prop("disabled")).toBeFalsy();
          spyOn(view, "reload");
          $(generateButton).click();
        });

        waitsFor(function () {
          return $("#modal-dialog").css("display") === "none";
        }, "The Add App dialog should be hidden.", 750);

        runs(function() {
          expect(view.reload).toHaveBeenCalled();
          expect(listDummy.installFromGithub).toHaveBeenCalledWith(
            expectedInfo, mount, jasmine.any(Function)
          );
        });

      });

      it("should install an app from zip file", function() {
        var fileName = "/tmp/upload-12345.zip";
        var mount = "/my/app";

        runs(function() {
          $("#tab-zip").click();
          spyOn(listDummy, "installFromZip").andCallFake(function(i, m, callback) {
            callback({
              error: false
            });
          });
          $("#new-app-mount").val(mount);
          spyOn(view, "reload");
          uploadCallback(["app.zip"], {filename: fileName});
        });

        waitsFor(function () {
          return $("#modal-dialog").css("display") === "none";
        }, "The Add App dialog should be hidden.", 750);

        runs(function() {
          expect(view.reload).toHaveBeenCalled();
          expect(listDummy.installFromZip).toHaveBeenCalledWith(
            fileName, mount, jasmine.any(Function)
          );


        });

      });

    });

    describe("showing installed Apps", function() {

    });
  });
}());
