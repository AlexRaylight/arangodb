/*jslint indent: 2, nomen: true, maxlen: 100, vars: true, white: true, plusplus: true */
/*global Backbone, window, $, _ */
(function () {

  "use strict";

  window.NewArangoLogs = Backbone.Collection.extend({
    page: 0,
    pagesize: 10,
    upto: false,
    loglevel: 0,
    totalAmount: 0,

    parse: function(response) {
      var myResponse = [];
      _.each(response.lid, function(val, i) {
        myResponse.push({
          "level": response.level[i],
          "lid": val,
          "text": response.text[i],
          "timestamp": response.timestamp[i],
          "totalAmount": response.totalAmount
        });
      });
      this.totalAmount = response.totalAmount;
      return myResponse;
    },

    initialize: function(options) {
      if (options.upto === true) {
        this.upto = true;
      }
      this.loglevel = options.loglevel;
    },

    getPage: function() {
      return this.page+1;
    },

    setPage: function(counter) {
      this.page = counter-1;
    },

    getLastPageNumber: function() {
      return Math.ceil(this.totalAmount / this.pagesize);
      
    },

    model: window.newArangoLog,

    url: function() {
      var type, rtnStr, offset;
      offset = this.page * this.pagesize;

      if (this.upto) {
        type = 'upto';
      }
      else  {
        type = 'level';
      }
      rtnStr = '/_admin/log?'+type+'='+this.loglevel+'&size='+this.pagesize+'&offset='+offset;
      return rtnStr;
    }

  });
}());
