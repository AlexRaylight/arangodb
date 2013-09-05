require("internal").flushModuleCache();

var jsunity = require("jsunity"),
  arangodb = require("org/arangodb"),
  db = arangodb.db;

function BaseMiddlewareSpec () {
  var BaseMiddleware, request, response, options, next;

  return {
    setUp: function () {
      baseMiddleware = require("org/arangodb/foxx/base_middleware").BaseMiddleware().functionRepresentation;
      request = {};
      response = {};
      options = {};
      next = function () {};
    },

    testBodyFunctionAddedToRequest: function () {
      request.requestBody = JSON.stringify({test: 123});
      baseMiddleware(request, response, options, next);
      assertEqual(request.body(), {test: 123});
    },

    testRawBodyFunctionAddedToRequest: function () {
      request.requestBody = JSON.stringify({test: 123});
      baseMiddleware(request, response, options, next);
      assertEqual(request.rawBody(), JSON.stringify({test: 123}));
    },

    testParamFunctionReturnsUrlParameters: function () {
      request.urlParameters = {a: 1};
      baseMiddleware(request, response, options, next);
      assertEqual(request.params("a"), 1);
    },

    testParamFunctionReturnsParameters: function () {
      request.parameters = {a: 1};
      baseMiddleware(request, response, options, next);
      assertEqual(request.params("a"), 1);
    },

    testParamFunctionReturnsAllParams: function () {
      request.urlParameters = {a: 1};
      request.parameters = {b: 2};
      baseMiddleware(request, response, options, next);
      assertEqual(request.params("a"), 1);
      assertEqual(request.params("b"), 2);
    },

    testStatusFunctionAddedToResponse: function () {
      baseMiddleware(request, response, options, next);

      response.status(200);
      assertEqual(response.responseCode, 200);
    },

    testSetFunctionAddedToResponse: function () {
      baseMiddleware(request, response, options, next);

      response.set("Content-Length", "123");
      assertEqual(response.headers["content-length"], "123");

      response.set("Content-Type", "text/plain");
      assertEqual(response.contentType, "text/plain");
    },

    testSetFunctionTakingAnObjectAddedToResponse: function () {
      baseMiddleware(request, response, options, next);

      response.set({
        "Content-Length": "123",
        "Content-Type": "text/plain"
      });

      assertEqual(response.headers["content-length"], "123");
      assertEqual(response.contentType, "text/plain");
    },

    testJsonFunctionAddedToResponse: function () {
      var rawObject = {test: "123"};

      baseMiddleware(request, response, options, next);

      response.json(rawObject);

      assertEqual(response.body, JSON.stringify(rawObject));
      assertEqual(response.contentType, "application/json");
    },

    testTemplateFunctionAddedToResponse: function () {
      var error;

      baseMiddleware(request, response, options, next);

      try {
        response.render("simple/path", { username: "moonglum" });
      } catch(e) {
        error = e;
      }

      assertEqual(error, new Error("No template collection has been provided when creating a new FoxxController"));
    },

    testMiddlewareCallsTheAction: function () {
      var actionWasCalled = false;

      next = function () {
        actionWasCalled = true;
      };

      baseMiddleware(request, response, options, next);

      assertTrue(actionWasCalled);
    }
  };
}

jsunity.run(BaseMiddlewareSpec);

return jsunity.done();
