/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true */
/*global Backbone, $, window, arangoHelper, templateEngine, Joi, _*/

window.ApplicationsView = Backbone.View.extend({
  el: '#content',

  template: templateEngine.createTemplate("applicationsView.ejs"),

  events: {
    "click #checkDevel"         : "toggleDevel",
    "click #checkActive"        : "toggleActive",
    "click #checkInactive"      : "toggleInactive",
    "click #foxxToggle"         : "slideToggle",
    "click #importFoxxToggle"   : "slideToggleImport",
    "change #importFoxx"        : "uploadSetup",
    "click #confirmFoxxImport"  : "importFoxx",
    "click #installFoxxFromGithub" : "createGithubModal"
  },

  uploadSetup: function (e) {
    var files = e.target.files || e.dataTransfer.files;
    this.file = files[0];
    this.allowUpload = true;
  },

  createGithubModal: function() {
    var buttons = [], tableContent = [];
    tableContent.push(
      window.modalView.createTextEntry(
        'github-url',
        'Github information',
        '',
        'Your Github link comes here: username/application-name',
        "username/application-name",
        false,
        [
          {
            rule: Joi.string().regex(/^[a-zA-Z0-9]+[\/]/),
            msg: "No valid github link given."
          },
          {
            rule: Joi.string().required(),
            msg: "No github link given."
          }
        ]
    ));
    tableContent.push(
      window.modalView.createTextEntry(
        'github-version',
        'Version (optional)',
        '',
        'Example: v1.1.2 for Version 1.1.2 - if no version is commited, master is used',
        'master',
        false,
        /[<>&'"]/
    ));
    buttons.push(
      window.modalView.createSuccessButton('Install', this.submitGithubFoxx.bind(this))
    );
    window.modalView.show(
      'modalTable.ejs', 'Install Foxx from Github', buttons, tableContent, undefined, undefined
    );
  },

  closeGithubModal: function() {
    window.modalView.hide();
  },

  submitGithubFoxx: function() {
    var name, url, version, result;

    //fetch needed information, need client side verification
    //remove name handling on server side because not needed
    name = "";
    url = $('#github-url').val();
    version = $('#github-version').val();

    if (version === '') {
      version = "master";
    }

    //send server req through collection
    result = this.collection.installFoxxFromGithub(url, name, version);
    if (result === true) {
      this.closeGithubModal();
      this.reload();
    }
  },

  importFoxx: function() {
    var self = this;
    if (this.allowUpload) {
      this.showSpinner();
      $.ajax({
        type: "POST",
        async: false,
        url: '/_api/upload',
        data: self.file,
        processData: false,
        contentType: 'application/octet-stream',
        complete: function(res) {
          if (res.readyState === 4) {
            if (res.status === 201) {
              $.ajax({
                type: "POST",
                async: false,
                url: "/_admin/aardvark/foxxes/inspect",
                data: res.responseText,
                contentType: "application/json"
              }).done(function(res) {
                $.ajax({
                  type: "POST",
                  async: false,
                  url: '/_admin/foxx/fetch',
                  data: JSON.stringify({ 
                    name: res.name,
                    version: res.version,
                    filename: res.filename
                  }),
                  processData: false
                }).done(function () {
                  self.reload();
                }).fail(function (err) {
                  self.hideSpinner();
                  var error = JSON.parse(err.responseText);
                  arangoHelper.arangoError("Error: " + error.errorMessage);
                });
              }).fail(function(err) {
                self.hideSpinner();
                var error = JSON.parse(err.responseText);
                arangoHelper.arangoError("Error: " + error.error);
              });
              delete self.file;
              self.allowUpload = false;
              self.hideSpinner();
              self.hideImportModal();
              return;
            }
          }
          self.hideSpinner();
          arangoHelper.arangoError("Upload error");
        }
      });
    }
  },
  
  showSpinner: function() {
    $('#uploadIndicator').show();
  },

  hideSpinner: function() {
    $('#uploadIndicator').hide();
  },

  toggleDevel: function() {
    var self = this;
    this._showDevel = !this._showDevel;
    _.each(this._installedSubViews, function(v) {
      v.toggle("devel", self._showDevel);
    });
  },
  
  toggleActive: function() {
    var self = this;
    this._showActive = !this._showActive;
    _.each(this._installedSubViews, function(v) {
      v.toggle("active", self._showActive);
    });
  },
  
  toggleInactive: function() {
    var self = this;
    this._showInactive = !this._showInactive;
    _.each(this._installedSubViews, function(v) {
      v.toggle("inactive", self._showInactive);
    });
  },

  hideImportModal: function() {
    $('#foxxDropdownImport').hide();
  },

  hideSettingsModal: function() {
    $('#foxxDropdownOut').hide();
  },

  slideToggleImport: function() {
    $('#foxxToggle').removeClass('activated');
    $('#importFoxxToggle').toggleClass('activated');
    $('#foxxDropdownImport').slideToggle(200);
    this.hideSettingsModal();
  },

  slideToggle: function() {
    $('#importFoxxToggle').removeClass('activated');
    $('#foxxToggle').toggleClass('activated');
    $('#foxxDropdownOut').slideToggle(200);
    this.hideImportModal();
  },

  reload: function() {
    var self = this;

    // unbind and remove any unused views
    _.each(this._installedSubViews, function (v) {
      v.undelegateEvents();
    });
    _.each(this._availableSubViews, function (v) {
      v.undelegateEvents();
    });

    this._installedSubViews = { };
    this._availableSubViews = { };

    this.collection.fetch({
      success: function() {
        self.collection.each(function (foxx) {
          var subView;
          if (foxx.get("type") === "app") {
            subView = new window.FoxxInstalledView({
              model: foxx,
              appsView: self
            });
            self._availableSubViews[foxx.get('_id')] = subView;
          } else if (foxx.get("type") === "mount") {
            subView = new window.FoxxActiveView({
              model: foxx,
              appsView: self
            });
            self._installedSubViews[foxx.get('_id')] = subView;
          }
        });
        self.render();
      }
    });
  },

  initialize: function() {
    this._installedSubViews = {};
    this._availableSubViews = {};
    this._showDevel = true;
    this._showActive = true;
    this._showInactive = true;
    this.reload();
  },

  render: function() {
    $(this.el).html(this.template.render({}));
    var self = this, name;
    var versions = {};

    _.each(this._installedSubViews, function (v) {
      $("#installedList").append(v.render());
    });
    _.each(this._availableSubViews, function (v) {
      name = v.model.get("name");

      //look which installed apps have multiple versions
      if (versions[name]) {
        versions[name].counter++;
        versions[name].versions.push(v.model.get("version"));
      }
      else {
        versions[name] = {
          counter: 1,
          versions: [v.model.get("version")]
        };
      }
    });
    _.each(this._availableSubViews, function (v) {
      var name = v.model.get("name"),
      version = v.model.get("version");
      if (versions[name].counter > 1 ) {
        //here comes special render for multiple versions view

        var highestVersion = "0.0.0";
        var selectOptions = [];

        _.each(versions[name].versions, function(x) {
          selectOptions.push({
            value: x,
            label: x
          });
          if (x > highestVersion) {
            highestVersion = x;
          }
        });

        if (version === highestVersion) {
          v.model.set("highestVersion", highestVersion);
          v.model.set("versions", versions[name].versions);
          v.model.set("selectOptions", selectOptions);

          $("#availableList").append(v.render());
        }
      }
      else {
        $("#availableList").append(v.render());
      }
    });
    this.delegateEvents();
    $('#checkActive').attr('checked', this._showActive);
    $('#checkInactive').attr('checked', this._showInactive);
    $('#checkDevel').attr('checked', this._showDevel);
    _.each(this._installedSubViews, function(v) {
      v.toggle("devel", self._showDevel);
      v.toggle("active", self._showActive);
      v.toggle("inactive", self._showInactive);
    });
  
    arangoHelper.fixTooltips("icon_arangodb", "left");
    return this;
  }
});
