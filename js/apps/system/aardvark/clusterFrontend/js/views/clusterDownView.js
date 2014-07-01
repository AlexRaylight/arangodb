/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true, newcap: true */
/*global window, $, Backbone, templateEngine, plannerTemplateEngine, alert */

(function() {
  "use strict";
  
  window.ClusterDownView = Backbone.View.extend({

    el: "#content",

    template: templateEngine.createTemplate("clusterDown.ejs"),
    modal: templateEngine.createTemplate("waitModal.ejs"),

    events: {
      "click #relaunchCluster"  : "relaunchCluster",
      "click #editPlan"         : "editPlan",
      "click #submitEditPlan"   : "submitEditPlan",
      "click #deletePlan"       : "deletePlan",
      "click #submitDeletePlan" : "submitDeletePlan"
    },

    render: function() {
      $(this.el).html(this.template.render({}));
      $(this.el).append(this.modal.render({}));
    },

    relaunchCluster: function() {
      $('#waitModalLayer').modal('show');
      $('.modal-backdrop.fade.in').addClass('waitModalBackdrop');
      $('#waitModalMessage').html('Please be patient while your cluster will be relaunched');
      $.ajax({
        cache: false,
        type: "GET",
        url: "cluster/relaunch",
        success: function() {
          $('.modal-backdrop.fade.in').removeClass('waitModalBackdrop');
          $('#waitModalLayer').modal('hide');
          window.App.navigate("showCluster", {trigger: true});
        }
      });
    },

    editPlan: function() {
      $('#deletePlanModal').modal('hide');
      $('#editPlanModal').modal('show');
    },

    submitEditPlan : function() {
      $('#editPlanModal').modal('hide');
      var plan = window.App.clusterPlan;
      if (plan.isTestSetup()) {
        window.App.navigate("planTest", {trigger : true});
        return;
      }
      window.App.navigate("planAsymmetrical", {trigger : true});
    },

    deletePlan: function() {
      $('#editPlanModal').modal('hide');
      $('#deletePlanModal').modal('show');
    },

    submitDeletePlan : function() {
      $('#deletePlanModal').modal('hide');
      window.App.clusterPlan.destroy();
      window.App.clusterPlan = new window.ClusterPlan();
      window.App.planScenario();
    }

  });

}());
