/*global window, btoa, $, Backbone, templateEngine, alert, _ */

(function() {
  "use strict";

  window.PlanSymmetricView = Backbone.View.extend({
    el: "#content",
    template: templateEngine.createTemplate("symmetricPlan.ejs"),
    entryTemplate: templateEngine.createTemplate("serverEntry.ejs"),
    modal: templateEngine.createTemplate("waitModal.ejs"),
    connectionValidationKey: null,

    events: {
      "click #startSymmetricPlan"   : "startPlan",
      "click .add"                  : "addEntry",
      "click .delete"               : "removeEntry",
      "click #cancel"               : "cancel",
      "click #test-all-connections" : "checkAllConnections",
      "focusout .host"              : "autoCheckConnections",
      "focusout .port"              : "autoCheckConnections",
      "focusout .user"              : "autoCheckConnections",
      "focusout .passwd"            : "autoCheckConnections"
    },

    cancel: function() {
      if(window.App.clusterPlan.get("plan")) {
        window.App.navigate("handleClusterDown", {trigger: true});
      } else {
        window.App.navigate("planScenario", {trigger: true});
      }
    },

    startPlan: function() {
      var self = this;
      var data = {dispatchers: []};
      var foundCoordinator = false;
      var foundDBServer = false;
            data.useSSLonDBservers = !!$(".useSSLonDBservers").prop('checked');
      data.useSSLonCoordinators = !!$(".useSSLonCoordinators").prop('checked');
      $(".dispatcher").each(function(i, dispatcher) {
        var host = $(".host", dispatcher).val();
        var port = $(".port", dispatcher).val();
        var user = $(".user", dispatcher).val();
        var passwd = $(".passwd", dispatcher).val();
        if (!host || 0 === host.length || !port || 0 === port.length) {
          return true;
        }
        var hostObject = {host :  host + ":" + port};
        if (!self.isSymmetric) {
          hostObject.isDBServer = !!$(".isDBServer", dispatcher).prop('checked');
          hostObject.isCoordinator = !!$(".isCoordinator", dispatcher).prop('checked');
        } else {
          hostObject.isDBServer = true;
          hostObject.isCoordinator = true;
        }

        hostObject.username = user;
        hostObject.passwd = passwd;

        foundCoordinator = foundCoordinator || hostObject.isCoordinator;
        foundDBServer = foundDBServer || hostObject.isDBServer;

        data.dispatchers.push(hostObject);
      });
            if (!self.isSymmetric) {
        if (!foundDBServer) {
            alert("Please provide at least one database server");
            return;
        }
        if (!foundCoordinator) {
            alert("Please provide at least one coordinator");
            return;
        }
      } else {
        if ( data.dispatchers.length === 0) {
            alert("Please provide at least one host");
            return;
        }

      }

      data.type = this.isSymmetric ? "symmetricalSetup" : "asymmetricalSetup";
      $('#waitModalLayer').modal('show');
      $('.modal-backdrop.fade.in').addClass('waitModalBackdrop');
      $('#waitModalMessage').html('Please be patient while your cluster is being launched');
      delete window.App.clusterPlan._coord;
            window.App.clusterPlan.save(
        data,
        {
          success : function() {
            $('.modal-backdrop.fade.in').removeClass('waitModalBackdrop');
            $('#waitModalLayer').modal('hide');
            window.App.updateAllUrls();
            window.App.navigate("showCluster", {trigger: true});
          },
          error: function(obj, err) {
            $('.modal-backdrop.fade.in').removeClass('waitModalBackdrop');
            $('#waitModalLayer').modal('hide');
            alert("Error while starting the cluster: " + err.statusText);
          }
        }
      );
          },

    addEntry: function() {
      //disable launch button
      this.disableLaunchButton();

      var lastUser = $("#server_list div.control-group.dispatcher:last .user").val();
      var lastPasswd = $("#server_list div.control-group.dispatcher:last .passwd").val();

      $("#server_list").append(this.entryTemplate.render({
        isSymmetric: this.isSymmetric,
        isFirst: false,
        isCoordinator: true,
        isDBServer: true,
        host: '',
        port: '',
        user: lastUser,
        passwd: lastPasswd
      }));
    },

    removeEntry: function(e) {
      $(e.currentTarget).closest(".control-group").remove();
      this.checkAllConnections();
    },

    render: function(isSymmetric) {
      var params = {},
        isFirst = true,
        config = window.App.clusterPlan.get("config");
      this.isSymmetric = isSymmetric;
      $(this.el).html(this.template.render({
        isSymmetric : isSymmetric,
        params      : params,
        useSSLonDBservers: config && config.useSSLonDBservers ?
            config.useSSLonDBservers : false,
        useSSLonCoordinators: config && config.useSSLonCoordinators ?
            config.useSSLonCoordinators : false
      }));
      if (config) {
        var self = this,
        isCoordinator = false,
        isDBServer = false;
        _.each(config.dispatchers, function(dispatcher) {
          if (dispatcher.allowDBservers === undefined) {
            isDBServer = true;
          } else {
            isDBServer = dispatcher.allowDBservers;
          }
          if (dispatcher.allowCoordinators === undefined) {
            isCoordinator = true;
          } else {
            isCoordinator = dispatcher.allowCoordinators;
          }
          var host = dispatcher.endpoint;
          host = host.split("//")[1];
          host = host.split(":");
          var user = dispatcher.username;
          var passwd = dispatcher.passwd;
          var template = self.entryTemplate.render({
            isSymmetric: isSymmetric,
            isFirst: isFirst,
            host: host[0],
            port: host[1],
            isCoordinator: isCoordinator,
            isDBServer: isDBServer,
            user: user,
            passwd: passwd
          });
          $("#server_list").append(template);
          isFirst = false;
        });
      } else {
        $("#server_list").append(this.entryTemplate.render({
          isSymmetric: isSymmetric,
          isFirst: true,
          isCoordinator: true,
          isDBServer: true,
          host: '',
          port: '',
          user: '',
          passwd: ''
        }));
      }
      //initially disable lunch button
      this.disableLaunchButton();

      $(this.el).append(this.modal.render({}));

    },

    autoCheckConnections: function (e) {
      var host,
        port,
        user,
        passwd,
        parentElement = $(e.currentTarget).parent();
      host = $(parentElement).children('.host').val();
      port = $(parentElement).children('.port').val();
      user = $(parentElement).children('.user').val();
      passwd = $(parentElement).children('.passwd').val();

      if (host !== '' && port !== '') {
        this.checkAllConnections();
      }
    },

    checkConnection: function(
      host,
      port,
      user,
      passwd,
      target,
      i,
      dispatcherArray,
      connectionValidationKey
    ) {
      var self = this;
      $(target).find('.cluster-connection-check-success').remove();
      $(target).find('.cluster-connection-check-fail').remove();
      try {
        $.ajax({
          async: true,
          cache: false,
          type: "GET",
          xhrFields: {
            withCredentials: true
          },
          url: "http://" + host + ":" + port + "/_api/version",
          success: function() {
            if (connectionValidationKey === self.connectionValidationKey) {
              $(target).append(
                '<span class="cluster-connection-check-success">Connection: ok</span>'
              );
              dispatcherArray[i] = true;
              self.checkDispatcherArray(dispatcherArray, connectionValidationKey);
            }
          },
          error: function(p) {
            if (connectionValidationKey === self.connectionValidationKey) {
              $(target).append(
                '<span class="cluster-connection-check-fail">Connection: fail</span>'
              );
              dispatcherArray[i] = false;
            }
          },
          beforeSend: function(xhr) {
            xhr.setRequestHeader("Authorization", "Basic " + btoa(user + ":" + passwd));
            //send this header to prevent the login box
            xhr.setRequestHeader("X-Omit-Www-Authenticate", "content");
          }
        });
      } catch (e) {
        this.disableLaunchButton();
      }
    },

    checkDispatcherArray: function(dispatcherArray, connectionValidationKey) {
      if(
        (_.every(dispatcherArray, function (e) {return e;}))
          && connectionValidationKey === this.connectionValidationKey
        ) {
        this.enableLaunchButton();
      }
    },

    checkAllConnections: function() {
      this.connectionValidationKey = Math.random();
      this.disableLaunchButton();
      var numOfDispatcher = $('.dispatcher').length,
        dispatcherArray = [],
        idx;
      for (idx = 0; idx < numOfDispatcher; idx++) {
        dispatcherArray.push(false);
      }

      //Object mit #dispatcher + random key
      var self = this;
      $('.dispatcher').each(
        function(i, dispatcher) {
          var target = $('.controls', dispatcher)[0];
          var host = $('.host', dispatcher).val();
          var port = $('.port', dispatcher).val();
          var user = $('.user', dispatcher).val();
          var passwd = $('.passwd', dispatcher).val();
          self.checkConnection(
            host,
            port,
            user,
            passwd,
            target,
            i,
            dispatcherArray,
            self.connectionValidationKey
          );
        }
      );
    },

    disableLaunchButton: function() {
      $('#startSymmetricPlan').attr('disabled', 'disabled');
      $('#startSymmetricPlan').removeClass('button-success');
      $('#startSymmetricPlan').addClass('button-neutral');
    },

    enableLaunchButton: function() {
      $('#startSymmetricPlan').attr('disabled', false);
      $('#startSymmetricPlan').removeClass('button-neutral');
      $('#startSymmetricPlan').addClass('button-success');
    }

  });

}());


