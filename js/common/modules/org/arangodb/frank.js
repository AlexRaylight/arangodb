var Frank,
  baseMiddleware,
  _ = require("underscore"),
  internal = {};

internal.createUrlObject = function (url, constraint, method) {
  var urlObject = {};

  if(!_.isString(url)) {
    throw "URL has to be a String";
  }

  urlObject.match = url;
  urlObject.methods = [method];

  if (!_.isNull(constraint)) {
    urlObject.constraint = constraint;
  }

  return urlObject;
};

Frank = function (options) {
  var urlPrefix, templateCollection;

  options = options || {};

  urlPrefix = options.urlPrefix;
  templateCollection = options.templateCollection;

  this.routingInfo = {
    routes: []
  };

  if (_.isString(urlPrefix)) {
    this.routingInfo.urlPrefix = urlPrefix;
  }

  if (_.isString(templateCollection)) {
    this.routingInfo.templateCollection = db._collection(templateCollection) || db._create(templateCollection);
  }
};

_.extend(Frank.prototype, {
  handleRequest: function (method, route, argument1, argument2) {
      var newRoute = {}, options, handler;

      if (_.isUndefined(argument2)) {
        handler = argument1;
        options = {};
      } else  {
        options = argument1;
        handler = argument2;
      }

      newRoute.url = internal.createUrlObject(route, options.constraint, method);
      newRoute.handler = handler;

      this.routingInfo.routes.push(newRoute);
  },

  get: function (route, argument1, argument2) {
    this.handleRequest("get", route, argument1, argument2);
  },

  post: function (route, argument1, argument2) {
    this.handleRequest("post", route, argument1, argument2);
  },

  put: function (route, argument1, argument2) {
    this.handleRequest("put", route, argument1, argument2);
  },

  delete: function (route, argument1, argument2) {
    this.handleRequest("delete", route, argument1, argument2);
  },
});


BaseMiddleware = function (templateCollection) {
  var middleware = function (request, response, options, next) {
    var responseFunctions;

    responseFunctions = {
      status: function (code) {
        this.responseCode = code;
      },

      set: function (key, value) {
        var attributes = {};
        if (_.isUndefined(value)) {
          attributes = key;
        } else {
          attributes[key] = value;
        }

        _.each(attributes, function (value, key) {
          key = key.toLowerCase();
          this.headers = this.headers || {};
          this.headers[key] = value;

          if (key === "content-type") {
            this.contentType = value;
          }
        }, this);
      },

      json: function (obj) {
        this.contentType = "application/json";
        this.body = JSON.stringify(obj);
      },

      render: function (templatePath, data) {
        var template;

        if (_.isUndefined(templateCollection)) {
          throw "No template collection has been provided when creating a new Frank";
        }

        template = templateCollection.firstExample({path: "simple/path"});

        if (template.templateLanguage !== "underscore") {
          throw "Unknown template language '" + template.templateLanguage + "'";
        }

        this.body = _.template(template.content, data);
        this.contentType = template.contentType;
      }
    };

    response = _.extend(response, responseFunctions);
  };

  return middleware;
};

exports.Frank = Frank;
exports.BaseMiddleware = BaseMiddleware;
