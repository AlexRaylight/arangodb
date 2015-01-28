/*jshint browser: true */
/*global require, Backbone, $, window, arangoHelper, templateEngine, Joi, alert, _*/
(function() {
  "use strict";

  var splitSnakeCase = function(snakeCase) {
    var str = snakeCase.replace(/([a-z])([A-Z])/g, "$1 $2");
    str = str.replace(/([a-z])([0-9])/gi, "$1 $2");
    str = str.replace(/_+/, " ");
    return _.map(str.split(/\s+/), function(s) {
      return s.charAt(0).toUpperCase() + s.slice(1).toLowerCase();
    }).join(" ");
  };
  var errors = require("internal").errors;

  window.ApplicationsView = Backbone.View.extend({
    el: '#content',

    template: templateEngine.createTemplate("applicationsView.ejs"),
    appStoreTemplate: templateEngine.createTemplate("applicationListView.ejs"),

    events: {
      "click #addApp"                : "createInstallModal",
      "click #foxxToggle"            : "slideToggle",
      "click #checkDevel"            : "toggleDevel",
      "click #checkProduction"       : "toggleProduction",
      "click #checkSystem"           : "toggleSystem",
      "click .css-label"             : "checkBoxes"
    },


    checkBoxes: function (e) {
      //chrome bugfix
      var clicked = e.currentTarget.id;
      $('#'+clicked).click();
    },

    showSpinner: function() {
      $('#uploadIndicator').show();
    },

    hideSpinner: function() {
      $('#uploadIndicator').hide();
    },

    ///// NEW CODE 

    toggleDevel: function() {
      var self = this;
      this._showDevel = !this._showDevel;
      _.each(this._installedSubViews, function(v) {
        v.toggle("devel", self._showDevel);
      });
    },

    toggleProduction: function() {
      var self = this;
      this._showProd = !this._showProd;
      _.each(this._installedSubViews, function(v) {
        v.toggle("production", self._showProd);
      });
    },

    toggleSystem: function() {
      this._showSystem = !this._showSystem;
      var self = this;
      _.each(this._installedSubViews, function(v) {
        v.toggle("system", self._showSystem);
      });
    },

    reload: function() {
      var self = this;

      // unbind and remove any unused views
      _.each(this._installedSubViews, function (v) {
        v.undelegateEvents();
      });

      this.collection.fetch({
        success: function() {
          self.createSubViews();
          self.render();
        }
      });
    },

    createSubViews: function() {
      var self = this;
      this._installedSubViews = { };

      self.collection.each(function (foxx) {
        var subView = new window.FoxxActiveView({
          model: foxx,
          appsView: self
        });
        self._installedSubViews[foxx.get('mount')] = subView;
      });
    },

    initialize: function() {
      this._installedSubViews = {};
      this._showDevel = true;
      this._showProd = true;
      this._showSystem = false;
      this.reload();
    },

    slideToggle: function() {
      $('#foxxToggle').toggleClass('activated');
      $('#foxxDropdownOut').slideToggle(200);
    },

    installFoxxFromStore: function(e) {
      var toInstall = $(e.currentTarget).attr("appId");
      var version = $(e.currentTarget).attr("appVersion");
      var self = this;
      $.post("foxxes/fishbowlinstall", JSON.stringify({
        name: toInstall,
        version: version
      }), function(result) {
        if (result.error === false) {
          window.modalView.hide();
          self.showConfigureDialog(result.configuration, result.name, result.version);
        } else {
          alert("Error: " + result.errorNum + ". " + result.errorMessage);
        }
      });
    },

    installFoxxFromZip: function(files, data) {
      this.collection.installFromZip(files, data, this.installCallback.bind(this));
    },

    installCallback: function(result) {
      if (result.error === false) {
        window.modalView.hide();
        // TODO has to be moved!
        this.showConfigureDialog(result.configuration, result.name, result.version);
      } else {
        switch(result.errorNum) {
          case errors.ERROR_APPLICATION_DOWNLOAD_FAILED.code:
            alert("Unable to download application from the given repository.");
            break;
          default:
            alert("Error: " + result.errorNum + ". " + result.errorMessage);
        }
      }
    },

    installFoxxFromGithub: function() {
      var mount = "TODO";

      var url, version;

      //fetch needed information, need client side verification
      //remove name handling on server side because not needed
      url = window.arangoHelper.escapeHtml($('#repository').val());
      version = window.arangoHelper.escapeHtml($('#tag').val());

      if (version === '') {
        version = "master";
      }
      var info = {
        url: window.arangoHelper.escapeHtml($('#repository').val()),
        version: window.arangoHelper.escapeHtml($('#tag').val())
      };

      try {
        Joi.assert(url, Joi.string().regex(/^[a-zA-Z0-9_\-]+\/[a-zA-Z0-9_\-]+$/));
      } catch (e) {
        return;
      }
      //send server req through collection
      this.collection.installFromGithub(info, mount, this.installCallback.bind(this));
    },

    showConfigureDialog: function(config, name, version) {
      var buttons = [],
          tableContent = [],
          entry;
      tableContent.push(
        window.modalView.createTextEntry(
          "mount-point",
          "Mount",
          "",
          "The path the app will be mounted. Has to start with /. Is not allowed to start with /_",
          "/my/app",
          true,
          [
            {
              rule: Joi.string().required(),
              msg: "No mountpoint given."
            },
            {
              rule: Joi.string().regex(/^\/[^_]/),
              msg: "Mountpoints with _ are reserved for internal use."
            },
            {
              rule: Joi.string().regex(/^(\/[a-zA-Z0-9_\-%]+)+$/),
              msg: "Mountpoints have to start with / and can only contain [a-zA-Z0-9_-%]"
            }
          ]
        )
      );
      _.each(config, function(obj, name) {
        var def;
        var check;
        switch (obj.type) {
          case "boolean":
          case "bool":
            def = obj.default || false;
            entry = window.modalView.createCheckboxEntry(
              "app_config_" + name,
              name,
              def,
              obj.description,
              def
            );
            break;
          case "integer":
            check = [{
              rule: Joi.number().integer(),
              msg: "Has to be an integer."
            }];
            /* falls through */
          default:
            if (check === undefined) {
              check = [{
                rule: Joi.string(),
                msg: "Has to be non-empty."
              }];
            }
            if (obj.default === undefined) {
              def = "";
            } else {
              def = String(obj.default);
            }
            entry = window.modalView.createTextEntry(
              "app_config_" + name,
              name,
              def,
              obj.description,
              def,
              true,
              check
            );
        }
        tableContent.push(entry);
      });
      buttons.push(
        window.modalView.createSuccessButton("Configure", this.mountFoxx.bind(this, config, name, version))
      );
      window.modalView.show(
        "modalTable.ejs", "Configure application", buttons, tableContent
      );

    },

    mountFoxx: function(config, name, version) {
      var cfg = {};
      try {
        _.each(config, function(opt, key) {
          var $el = $("#app_config_" + key);
          var val = window.arangoHelper.escapeHtml($el.val());
          if (opt.type === "boolean") {
            cfg[key] = $el.is(":checked");
            return;
          }
          if (val === "" && !opt.hasOwnProperty("default")) {
            throw new SyntaxError(
              "Must specify value for field \"" +
                (opt.label || splitSnakeCase(key)) +
                "\"!"
            );
          }
          if (opt.type === "number") {
            cfg[key] = parseFloat(val);
          } else if (opt.type === "integer") {
            cfg[key] = parseInt(val, 10);
          } else {
            cfg[key] = val;
            return;
          }
          if (_.isNaN(cfg[key])) {
            throw new SyntaxError(
              "Invalid value for field \"" +
                (opt.label || splitSnakeCase(key)) +
                "\"!"
            );
          }
          if (opt.type === "integer" && cfg[key] !== Math.floor(parseFloat(val))) {
            throw new SyntaxError(
              "Expected non-decimal value in field \"" +
                (opt.label || splitSnakeCase(key)) +
                "\"!"
            );
          }
        });
      } catch (err) {
        if (err instanceof SyntaxError) {
          alert(err.message);
          return false;
        }
        throw err;
      }
      var self = this;
      this.collection.create({
        mount: window.arangoHelper.escapeHtml($("#mount-point").val()),
        name: name,
        version: version,
        options: {
          configuration: cfg
        }
      },
      {
        success: function() {
          window.modalView.hide();
          self.reload();
        },
        error: function(e, info) {
          if (info.responseText.indexOf("already used by") > -1) {
            alert("Mount path already in use.");
          } else if (info.responseText.indexOf("app is not defined") > -1) {
            //temp ignore this message, fix needs to be server-side
            window.modalView.hide();
            self.reload();
          } else {
            alert(info.statusText);
          }
        }
      });
    },


    /// NEW CODE

    showFoxxDownload: function(file) {
      var self = this;
      var buttons = [
        window.modalView.createSuccessButton("Download", function() {
          window.open("templates/download/" + file);
          window.modalView.hide();
          self.reload();
        })
      ];
      window.modalView.show(
        "modalDownloadFoxx.ejs",
        "Download application",
        buttons
      );
    },

    showFoxxDevappPath: function(path) {
      var self = this;
      var buttons = [
        window.modalView.createSuccessButton("OK", function() {
          window.modalView.hide();
          self.reload();
        })
      ];
      window.modalView.show(
        "modalFoxxDevPath.ejs",
        "Application created",
        buttons,
        {
          path: path
        }
      );
    },

    generateNewFoxxApp: function() {
      var info = {
        name: window.arangoHelper.escapeHtml($("#new-app-name").val()),
        collectionNames: _.map($('#new-app-collections').select2("data"), function(d) {
          return window.arangoHelper.escapeHtml(d.text);
        }),
        authenticated: window.arangoHelper.escapeHtml($("#new-app-name").val()),
        author: window.arangoHelper.escapeHtml($("#new-app-author").val()),
        license: window.arangoHelper.escapeHtml($("#new-app-license").val()),
        description: window.arangoHelper.escapeHtml($("#new-app-description").val())
      },
        self = this;
      $.post("templates/generate", JSON.stringify(info), function(a) {
        window.modalView.hide();
        if (a.hasOwnProperty("file")) {
          self.showFoxxDownload(a.file);
          return;
        }
        if (a.hasOwnProperty("appPath")) {
          self.showFoxxDevappPath(a.appPath);
          return;
        }
        //TODO Error Handling
      });
    },

    addAppAction: function() {
      var openTab = $(".modal-body .tab-pane.active").attr("id");
      switch (openTab) {
        case "newApp":
          this.generateNewFoxxApp();
          break;
        case "github":
          this.installFoxxFromGithub();
          break;
        case "zip":
          // this.installFoxxFromZip();
          break;
        default:
      }
    },

    setGithubValidators: function() {
      window.modalView.modalBindValidation({
        id: "repository",
        validateInput: function() {
          return [
            {
              rule: Joi.string().required().regex(/^[a-zA-Z0-9_\-]+\/[a-zA-Z0-9_\-]+$/),
              msg: "No valid github account and repository."
            }
          ];
        }
      });
    },

    setNewAppValidators: function() {
      window.modalView.modalBindValidation({
        id: "new-app-author",
        validateInput: function() {
          return [
            {
              rule: Joi.string().required().min(1),
              msg: "Has to be non empty."
            }
          ];
        }
      });

      window.modalView.modalBindValidation({
        id: "new-app-name",
        validateInput: function() {
          return [
            {
              rule: Joi.string().required().regex(/^[a-zA-Z-_][a-zA-Z0-9-_]*$/),
              msg: "Can only contain a to z, A to Z, 0-9, '-' and '_'."
            }
          ];
        }
      });

      window.modalView.modalBindValidation({
        id: "new-app-description",
        validateInput: function() {
          return [
            {
              rule: Joi.string().required().min(1),
              msg: "Has to be non empty."
            }
          ];
        }
      });

      window.modalView.modalBindValidation({
        id: "new-app-license",
        validateInput: function() {
          return [
            {
              rule: Joi.string().required().regex(/^[a-zA-Z0-9 .,;\-]+$/),
              msg: "Has to be non empty."
            }
          ];
        }
      });
    },

    switchModalButton: function(event) {
      window.modalView.clearValidators();
      var openTab = $(event.currentTarget).attr("href").substr(1);
      var button = $("#modalButton1");
      switch (openTab) {
        case "newApp":
          button.html("Generate");
          button.prop("disabled", false);
          this.setNewAppValidators();
          break;
        case "appstore":
          button.html("Install");
          button.prop("disabled", true);
          break;
        case "github":
          this.setGithubValidators();
          button.html("Install");
          button.prop("disabled", false);
          break;
        case "zip":
          button.html("Install");
          button.prop("disabled", false);
          break;
        default:
      }
    },

    createInstallModal: function(event) {
      event.preventDefault();
      var buttons = [];
      var modalEvents = {
        "click #infoTab a"   : this.switchModalButton.bind(this),
        "click .install-app" : this.installFoxxFromStore.bind(this)
      };
      buttons.push(
        window.modalView.createSuccessButton("Generate", this.addAppAction.bind(this))
      );
      window.modalView.show(
        "modalApplicationMount.ejs",
        "Install application",
        buttons,
        undefined,
        undefined,
        modalEvents
      );
      $("#new-app-collections").select2({
        tags: [],
        showSearchBox: false,
        minimumResultsForSearch: -1,
        width: "336px"
      });
      $("#upload-foxx-zip").uploadFile({
        url: "/_api/upload?multipart=true",
        allowedTypes: "zip",
        onSuccess: this.installFoxxFromZip.bind(this)
      });
      var listTempl = this.appStoreTemplate;
      $.get("foxxes/fishbowl", function(list) {
        var table = $("#appstore-content");
        _.each(_.sortBy(list, "name"), function(app) {
          table.append(listTempl.render(app));
        });
      }).fail(function() {
        var table = $("#appstore-content");
        table.append("<tr><td>Store is not available. ArangoDB is not able to connect to github.com</td></tr>");
      });
      this.setNewAppValidators();
    },

    render: function() {
      this.collection.sort();

      $(this.el).html(this.template.render({}));
      _.each(this._installedSubViews, function (v) {
        $("#installedList").append(v.render());
      });
      this.delegateEvents();
      $('#checkDevel').attr('checked', this._showDevel);
      $('#checkProduction').attr('checked', this._showProd);
      $('#checkSystem').attr('checked', this._showSystem);
      
      var self = this;
      _.each(this._installedSubViews, function(v) {
        v.toggle("devel", self._showDevel);
        v.toggle("system", self._showSystem);
      });

      arangoHelper.fixTooltips("icon_arangodb", "left");
      return this;
    }

  });

}());
