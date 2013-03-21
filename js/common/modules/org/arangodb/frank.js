/*jslint indent: 2, nomen: true, maxlen: 100 */
/*global _, require, db, exports */

// `Codename Frank` is a classy way to create APIs and simple web applications
// from within **ArangoDB**.
// It is inspired by Sinatra, the classy Ruby web framework. If Frank is Sinatra,
// [ArangoDB Actions](http://www.arangodb.org/manuals/current/UserManualActions.html)
// are the corresponding `Rack`. They provide all the HTTP goodness.
//
// Please be aware of the following:
//
// > From now on Frank is only a codename.  
// > But don't tell it to anyone.  
// > Because it is a codename, you know.
//
// So let's get started, shall we?

// Frank uses Underscore internally. This library is wonderful.
var Frank,
  BaseMiddleware,
  _ = require("underscore"),
  db = require("org/arangodb").db,
  internal = {};

// ArangoDB uses a certain structure we refer to as `UrlObject`.
// With the following function (which is only internal, and not
// exported) you can create an UrlObject with a given URL,
// a constraint and a method. For example:
//
//     internal.createUrlObject('/rat/pack', null, 'get')
internal.createUrlObject = function (url, constraint, method) {
  'use strict';
  var urlObject = {};

  if (!_.isString(url)) {
    throw "URL has to be a String";
  }

  urlObject.match = url;
  urlObject.methods = [method];

  if (!_.isNull(constraint)) {
    urlObject.constraint = constraint;
  }

  return urlObject;
};

// ## Creating a new application
// And that's Frank. He's a constructor, so call him like this:
//
//     app = new Frank({
//       urlPrefix: "/classyroutes",
//       templateCollection: "my_templates"
//     })
//
// It takes two optional arguments as displayed above:
//
// * **The URL Prefix:** All routes you define within will be prefixed with it
// * **The Template Collection:** More information in the template section
Frank = function (options) {
  'use strict';
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
    this.routingInfo.templateCollection = db._collection(templateCollection) ||
      db._create(templateCollection);
  }
};

// ## The functions on a created Frank
//
// When you have created your Frank you can now define routes
// on it. You provide each with a function that will handle
// the request. It gets two arguments (four, to be honest. But the
// other two are not relevant for now):
//
// * The request object
// * The response object.
//
// You can find more about those in their individual sections.
_.extend(Frank.prototype, {
  // The `handleRequest` method is the raw way to create a new
  // route. You probably wont call it directly, but it is used
  // in the other request methods:
  //
  //     app.handleRequest("get", "/come/fly/with/me", function (req, res) {
  //       //handle the request
  //     });
  handleRequest: function (method, route, argument1, argument2) {
    'use strict';
    var newRoute = {}, options, callback;

    if (_.isUndefined(argument2)) {
      callback = argument1;
      options = {};
    } else {
      options = argument1;
      callback = argument2;
    }

    newRoute.url = internal.createUrlObject(route, options.constraint, method);
    newRoute.action = {
      callback: String(callback)
    };

    this.routingInfo.routes.push(newRoute);
  },

  // ### Handle a `head` request
  // This handles requests from the HTTP verb `head`.
  // As with all other requests you can give an option as the third
  // argument, or leave it blank. You have to give a function as
  // the last argument however. It will get a request and response
  // object as its arguments
  head: function (route, argument1, argument2) {
    'use strict';
    this.handleRequest("head", route, argument1, argument2);
  },

  // ### Handle a `get` request
  // This handles requests from the HTTP verb `get`.
  // See above for the arguments you can give. An example:
  //
  //     app.get('/high/society', function (req, res) {
  //       // Take this request and deal with it!
  //     });
  get: function (route, argument1, argument2) {
    'use strict';
    this.handleRequest("get", route, argument1, argument2);
  },

  // ### Handle a `post` request
  // This handles requests from the HTTP verb `post`.
  // See above for the arguments you can give. An example:
  //
  //     app.post('/high/society', function (req, res) {
  //       // Take this request and deal with it!
  //     });
  post: function (route, argument1, argument2) {
    'use strict';
    this.handleRequest("post", route, argument1, argument2);
  },

  // ### Handle a `put` request
  // This handles requests from the HTTP verb `put`.
  // See above for the arguments you can give. An example:
  //
  //     app.put('/high/society', function (req, res) {
  //       // Take this request and deal with it!
  //     });
  put: function (route, argument1, argument2) {
    'use strict';
    this.handleRequest("put", route, argument1, argument2);
  },

  // ### Handle a `patch` request
  // This handles requests from the HTTP verb `patch`.
  // See above for the arguments you can give. An example:
  //
  //     app.patch('/high/society', function (req, res) {
  //       // Take this request and deal with it!
  //     });
  patch: function (route, argument1, argument2) {
    'use strict';
    this.handleRequest("patch", route, argument1, argument2);
  },

  // ### Handle a `delete` request
  // This handles requests from the HTTP verb `delete`.
  // See above for the arguments you can give.
  // **A word of warning:** Do not forget that `delete` is
  // a reserved word in JavaScript so call it as follows:
  //
  //     app['delete']('/high/society', function (req, res) {
  //       // Take this request and deal with it!
  //     });
  'delete': function (route, argument1, argument2) {
    'use strict';
    this.handleRequest("delete", route, argument1, argument2);
  }
});


// ## The Base Middleware
// The `BaseMiddleware` manipulates the request and response
// objects to give you a nicer API.
BaseMiddleware = function (templateCollection) {
  'use strict';
  var middleware = function (request, response, options, next) {
    var responseFunctions, requestFunctions;

    // ### The Request Object
    // Every request object has the following attributes from the underlying Actions:
    //
    // * path: The complete path as supplied by the user
    // * prefix
    // * suffix
    // * urlParameters
    //
    // Frank currently leaves those unmodified.
    responseFunctions = {};

    // ### The Response Object
    // Every response object has the following attributes from the underlying Actions:
    //
    // * contentType
    // * responseCode
    // * body
    // * headers (an object)
    //
    // Frank adds the following methods to this response object.
    responseFunctions = {
      // ### The straightforward `status` function
      // Set the status code of your response, for example:
      //
      //     response.status(404);
      status: function (code) {
        this.responseCode = code;
      },

      // ### The radical `set` function
      // Set a header attribute, for example:
      //
      //     response.set("Content-Length", 123);
      //     response.set("Content-Type", "text/plain");
      //
      // or alternatively:
      //
      //     response.set({
      //       "Content-Length": "123",
      //       "Content-Type": "text/plain"
      //     });
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

      // ### The magical `json` function
      // Set the content type to JSON and the body to the
      // JSON encoded object you provided.
      //
      //     response.json({'born': 'December 12, 1915'});
      json: function (obj) {
        this.contentType = "application/json";
        this.body = JSON.stringify(obj);
      },

      // ### The mysterious `render` function
      // If you initialize your Frank with a `templateCollection`,
      // you're in luck now.
      // It expects documents in the following form in this collection:
      //
      //     {
      //       path: "my/way",
      //       content: "hallo <%= username %>",
      //       contentType: "text/plain",
      //       templateLanguage: "underscore"
      //     }
      //
      // The `content` is the string that will be rendered by the template
      // processor. The `contentType` is the type of content that results
      // from this call. And with the `templateLanguage` you can choose
      // your template processor. There is only one choice now: `underscore`.
      //
      // If you call render, Frank will look
      // into the this collection and search by the path attribute.
      // It will then render the template with the given data:
      //
      //     response.render("my/way", {username: 'Frank'})
      //
      // Which would set the body of the response to `hello Frank` with the
      // template defined above. It will also set the `contentType` to
      // `text/plain` in this case.
      render: function (templatePath, data) {
        var template;

        if (_.isUndefined(templateCollection)) {
          throw "No template collection has been provided when creating a new Frank";
        }

        template = templateCollection.firstExample({path: templatePath });

        if (_.isNull(template)) {
          throw "Template '" + templatePath + "' does not exist";
        }

        if (template.templateLanguage !== "underscore") {
          throw "Unknown template language '" + template.templateLanguage + "'";
        }

        this.body = _.template(template.content, data);
        this.contentType = template.contentType;
      }
    };

    // Now enhance the request and response as described above and call next at the
    // end of this middleware (otherwise your application would never
    // be executed. Would be a shame, really).
    request = _.extend(request, requestFunctions);
    response = _.extend(response, responseFunctions);
    next();
  };

  return middleware;
};

// We finish off with exporting Frank and the BaseMiddleware.
// Everything else will remain our secret.
//
// Fin.
exports.Frank = Frank;
exports.BaseMiddleware = BaseMiddleware;
