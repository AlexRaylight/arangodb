require("internal").flushModuleCache();

var jsunity = require("jsunity"),
  console = require("console"),
  arangodb = require("org/arangodb"),
  FoxxApplication = require("org/arangodb/foxx").FoxxApplication,
  db = arangodb.db;

function CreateFoxxApplicationSpec () {
  return {
    testCreationWithoutParameters: function () {
      var app = new FoxxApplication(),
        routingInfo = app.routingInfo;

      assertEqual(routingInfo.routes.length, 0);
      assertUndefined(routingInfo.urlPrefix);
    },

    testCreationWithURLPrefix: function () {
      var app = new FoxxApplication({urlPrefix: "/wiese"}),
        routingInfo = app.routingInfo;

      assertEqual(routingInfo.routes.length, 0);
      assertEqual(routingInfo.urlPrefix, "/wiese");
    },

    testCreationWithTemplateCollectionIfCollectionDoesntExist: function () {
      var app, routingInfo, templateCollection;

      db._drop("wiese");
      app = new FoxxApplication({templateCollection: "wiese"});
      routingInfo = app.routingInfo;
      templateCollection = db._collection("wiese");

      assertEqual(routingInfo.routes.length, 0);
      assertNotNull(templateCollection);
      assertEqual(routingInfo.templateCollection, templateCollection);
    },

    testCreationWithTemplateCollectionIfCollectionDoesExist: function () {
      var app, routingInfo, templateCollection;

      db._drop("wiese");
      db._create("wiese");
      app = new FoxxApplication({templateCollection: "wiese"});
      routingInfo = app.routingInfo;
      templateCollection = db._collection("wiese");

      assertEqual(routingInfo.routes.length, 0);
      assertNotNull(templateCollection);
      assertEqual(routingInfo.templateCollection, templateCollection);
    },

    testAdditionOfBaseMiddlewareInRoutingInfo: function () {
      var app = new FoxxApplication(),
        routingInfo = app.routingInfo,
        hopefully_base = routingInfo.middleware[0];

      assertEqual(routingInfo.middleware.length, 1);
      assertEqual(hopefully_base.url.match, "/*");
    }
  };
}

function SetRoutesFoxxApplicationSpec () {
  var app;

  return {
    setUp: function () {
      app = new FoxxApplication();
    },

    testSettingRoutesWithoutConstraint: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.get('/simple/route', myFunc);
      assertEqual(routes.length, 1);
      assertEqual(routes[0].url.match, '/simple/route');
      assertUndefined(routes[0].url.constraint);
    },

    testSettingRoutesWithConstraint: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes,
        constraint = { test: "/[a-z]+/" };

      app.get('/simple/route', { constraint: constraint }, myFunc);
      assertEqual(routes.length, 1);
      assertEqual(routes[0].url.constraint, constraint);
    },

    testSetMethodToHead: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.head('/simple/route', myFunc);
      assertEqual(routes[0].url.methods, ["head"]);
    },

    testSetMethodToGet: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.get('/simple/route', myFunc);
      assertEqual(routes[0].url.methods, ["get"]);
    },

    testSetMethodToPost: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.post('/simple/route', myFunc);
      assertEqual(routes[0].url.methods, ["post"]);
    },

    testSetMethodToPut: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.put('/simple/route', myFunc);
      assertEqual(routes[0].url.methods, ["put"]);
    },

    testSetMethodToPatch: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.patch('/simple/route', myFunc);
      assertEqual(routes[0].url.methods, ["patch"]);
    },

    testSetMethodToDelete: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes;

      app.delete('/simple/route', myFunc);
      assertEqual(routes[0].url.methods, ["delete"]);
    },

    testRefuseRoutesWithRoutesThatAreNumbers: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes,
        error;

      try {
        app.get(2, myFunc);
      } catch(e) {
        error = e;
      }
      assertEqual(error, "URL has to be a String");
      assertEqual(routes.length, 0);
    },

    testRefuseRoutesWithRoutesThatAreRegularExpressions: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes,
        error;

      try {
        app.get(/[a-z]*/, myFunc);
      } catch(e) {
        error = e;
      }
      assertEqual(error, "URL has to be a String");
      assertEqual(routes.length, 0);
    },

    testRefuseRoutesWithRoutesThatAreArrays: function () {
      var myFunc = function () {},
        routes = app.routingInfo.routes,
        error;

      try {
        app.get(["/a", "/b"], myFunc);
      } catch(e) {
        error = e;
      }
      assertEqual(error, "URL has to be a String");
      assertEqual(routes.length, 0);
    },

    testSettingHandlers: function () {
      var myFunc = function a (b, c) { return b + c;},
        myFuncString = "function a(b, c) { return b + c;}",
        routes = app.routingInfo.routes,
        action;

      app.get('/simple/route', myFunc);

      action = routes[0].action;
      assertEqual(routes.length, 1);
      assertEqual(action.callback, myFuncString);
    }
  };
}

function AddMidlewareFoxxApplicationSpec () {
  var app;

  return {
    setUp: function () {
      app = new FoxxApplication();
    },

    testAddABeforeMiddlewareForAllRoutes: function () {
      var a = false,
        b = false,
        myFunc = function (req, res) { a = (req > res); },
        myNext = function () { b = a; },
        middleware = app.routingInfo.middleware,
        resultingCallback;

      app.before(myFunc);
      assertEqual(middleware.length, 2);
      assertEqual(middleware[1].url.match, '/*');
      resultingCallback = middleware[1].action.callback;
      resultingCallback(2, 1, undefined, myNext);

      assertTrue(a);
      assertTrue(b);
    },

    testAddABeforeMiddlewareForCertainRoutes: function () {
      var a = false,
        b = false,
        myFunc = function (req, res) { a = (req > res); },
        myNext = function () { b = a; },
        middleware = app.routingInfo.middleware,
        resultingCallback;

      app.before('/fancy/path', myFunc);
      assertEqual(middleware.length, 2);
      assertEqual(middleware[1].url.match, '/fancy/path');
      resultingCallback = middleware[1].action.callback;
      resultingCallback(2, 1, undefined, myNext);

      assertTrue(a);
      assertTrue(b);
    },

    testAddAfterMiddlewareForAllRoutes: function () {
      var a = false,
        b = false,
        myNext = function () { a = true; },
        myFunc = function (req, res) { b = a && (req > res); },
        middleware = app.routingInfo.middleware,
        resultingCallback;

      app.after(myFunc);
      assertEqual(middleware.length, 2);
      assertEqual(middleware[1].url.match, '/*');
      resultingCallback = middleware[1].action.callback;
      resultingCallback(2, 1, undefined, myNext);

      assertTrue(a);
      assertTrue(b);
    },

    testAddAfterMiddlewareForCertainRoutes: function () {
      var a = false,
        b = false,
        myNext = function () { a = true; },
        myFunc = function (req, res) { b = a && (req > res); },
        middleware = app.routingInfo.middleware,
        resultingCallback;

      app.after('/my/way', myFunc);
      assertEqual(middleware.length, 2);
      assertEqual(middleware[1].url.match, '/my/way');
      resultingCallback = middleware[1].action.callback;
      resultingCallback(2, 1, undefined, myNext);

      assertTrue(a);
      assertTrue(b);
    },

    testAddTheFormatMiddlewareUsingTheShortform: function () {
      // I wish I could mock like in Ruby :( This test is not really a test.
      app.accepts(["json"], "json");
      assertEqual(app.routingInfo.middleware.length, 2);
      assertEqual(app.routingInfo.middleware[1].url.match, '/*');
    }
  };
}

function BaseMiddlewareWithoutTemplateSpec () {
  var baseMiddleware, request, response, options, next;

  return {
    setUp: function () {
      baseMiddleware = require("org/arangodb/foxx").BaseMiddleware();
      request = {};
      response = {};
      options = {};
      next = function () {};
    },

    testBodyFunctionAddedToRequest: function () {
      request.requestBody = "test";
      baseMiddleware(request, response, options, next);
      assertEqual(request.body(), "test");
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

      assertEqual(error, "No template collection has been provided when creating a new FoxxApplication");
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

function BaseMiddlewareWithTemplateSpec () {
  var BaseMiddleware, request, response, options, next;

  return {
    setUp: function () {
      request = {};
      response = {};
      options = {};
      next = function () {};
      BaseMiddleware = require("org/arangodb/foxx").BaseMiddleware;
    },

    testRenderingATemplate: function () {
      var myCollection, middleware;

      db._drop("templateTest");
      myCollection = db._create("templateTest");

      myCollection.save({
        path: "simple/path",
        content: "hallo <%= username %>",
        contentType: "text/plain",
        templateLanguage: "underscore"
      });

      middleware = new BaseMiddleware(myCollection);
      middleware(request, response, options, next);

      response.render("simple/path", { username: "moonglum" });
      assertEqual(response.body, "hallo moonglum");
      assertEqual(response.contentType, "text/plain");
    },

    testRenderingATemplateWithAnUnknownTemplateEngine: function () {
      var myCollection, error, middleware;

      db._drop("templateTest");
      myCollection = db._create("templateTest");

      myCollection.save({
        path: "simple/path",
        content: "hallo <%= username %>",
        contentType: "text/plain",
        templateLanguage: "pirateEngine"
      });

      middleware = new BaseMiddleware(myCollection);
      middleware(request, response, options, next);

      try {
        response.render("simple/path", { username: "moonglum" });
      } catch(e) {
        error = e;
      }

      assertEqual(error, "Unknown template language 'pirateEngine'");
    },

    testRenderingATemplateWithAnNotExistingTemplate: function () {
      var myCollection, error, middleware;

      db._drop("templateTest");
      myCollection = db._create("templateTest");

      middleware = new BaseMiddleware(myCollection);
      middleware(request, response, options, next);

      try {
        response.render("simple/path", { username: "moonglum" });
      } catch(e) {
        error = e;
      }

      assertEqual(error, "Template 'simple/path' does not exist");
    }
  };
}

function ViewHelperSpec () {
  var app, Middleware, request, response, options, next;

  return {
    setUp: function () {
      app = new FoxxApplication();
      Middleware = require('org/arangodb/foxx').BaseMiddleware;
      request = {};
      response = {};
      options = {};
      next = function () {};
    },

    testDefiningAViewHelper: function () {
      var my_func = function () {};

      app.helper("testHelper", my_func);

      assertEqual(app.helperCollection["testHelper"], my_func);
    },

    testUsingTheHelperInATemplate: function () {
      var a = false;

      db._drop("templateTest");
      myCollection = db._create("templateTest");

      myCollection.save({
        path: "simple/path",
        content: "hallo <%= testHelper() %>",
        contentType: "text/plain",
        templateLanguage: "underscore"
      });

      middleware = new Middleware(myCollection, {
        testHelper: function() { a = true }
      });
      middleware(request, response, options, next);

      assertFalse(a);
      response.render("simple/path", {});
      assertTrue(a);
    }
  };
}

function FormatMiddlewareSpec () {
  var Middleware, middleware, request, response, options, next;

  return {
    setUp: function () {
      Middleware = require('org/arangodb/foxx').FormatMiddleware;
      request = {};
      response = {};
      options = {};
      next = function () {};
      middleware = new Middleware(["json"]);
    },

    testChangesTheURLAccordingly: function () {
      request = { path: "test/1.json", headers: {} };
      middleware(request, response, options, next);
      assertEqual(request.path, "test/1");
    },

    testRefusesFormatsThatHaveNotBeenAllowed: function () {
      var nextCalled = false,
        next = function () { nextCalled = true };
      middleware = new Middleware(["json"]);
      request = { path: "test/1.html", headers: {} };
      middleware(request, response, options, next);
      assertEqual(response.responseCode, 406);
      assertEqual(response.body, "Format 'html' is not allowed.");
      assertFalse(nextCalled);
    },

    testRefuseContradictingURLAndResponseType: function () {
      var nextCalled = false,
        next = function () { nextCalled = true };
      request = { path: "test/1.json", headers: {"accept": "text/html"} };
      middleware = new Middleware(["json"]);
      middleware(request, response, options, next);
      assertEqual(response.responseCode, 406);
      assertEqual(response.body, "Contradiction between Accept Header and URL.");
      assertFalse(nextCalled);
    },

    testRefuseMissingBothURLAndResponseTypeWhenNoDefaultWasGiven: function () {
      var nextCalled = false,
        next = function () { nextCalled = true };
      request = { path: "test/1", headers: {} };
      middleware = new Middleware(["json"]);
      middleware(request, response, options, next);
      assertEqual(response.responseCode, 406);
      assertEqual(response.body, "Format 'undefined' is not allowed.");
      assertFalse(nextCalled);
    },

    testFallBackToDefaultWhenMissingBothURLAndResponseType: function () {
      request = { path: "test/1", headers: {} };
      middleware = new Middleware(["json"], "json");
      middleware(request, response, options, next);
      assertEqual(request.format, "json");
      assertEqual(response.contentType, "application/json");
    },

    // JSON
    testSettingTheFormatAttributeAndResponseTypeForJsonViaURL: function () {
      request = { path: "test/1.json", headers: {} };
      middleware = new Middleware(["json"]);
      middleware(request, response, options, next);
      assertEqual(request.format, "json");
      assertEqual(response.contentType, "application/json");
    },

    testSettingTheFormatAttributeAndResponseTypeForJsonViaAcceptHeader: function () {
      request = { path: "test/1", headers: {"accept": "application/json"} };
      middleware = new Middleware(["json"]);
      middleware(request, response, options, next);
      assertEqual(request.format, "json");
      assertEqual(response.contentType, "application/json");
    },

    // HTML
    testSettingTheFormatAttributeAndResponseTypeForHtmlViaURL: function () {
      request = { path: "test/1.html", headers: {} };
      middleware = new Middleware(["html"]);
      middleware(request, response, options, next);
      assertEqual(request.format, "html");
      assertEqual(response.contentType, "text/html");
    },

    testSettingTheFormatAttributeAndResponseTypeForHtmlViaAcceptHeader: function () {
      request = { path: "test/1", headers: {"accept": "text/html"} };
      middleware = new Middleware(["html"]);
      middleware(request, response, options, next);
      assertEqual(request.format, "html");
      assertEqual(response.contentType, "text/html");
    },

    // TXT
    testSettingTheFormatAttributeAndResponseTypeForTxtViaURL: function () {
      request = { path: "test/1.txt", headers: {} };
      middleware = new Middleware(["txt"]);
      middleware(request, response, options, next);
      assertEqual(request.format, "txt");
      assertEqual(response.contentType, "text/plain");
    },

    testSettingTheFormatAttributeAndResponseTypeForTxtViaAcceptHeader: function () {
      request = { path: "test/1", headers: {"accept": "text/plain"} };
      middleware = new Middleware(["txt"]);
      middleware(request, response, options, next);
      assertEqual(request.format, "txt");
      assertEqual(response.contentType, "text/plain");
    }
  };
}

jsunity.run(CreateFoxxApplicationSpec);
jsunity.run(SetRoutesFoxxApplicationSpec);
jsunity.run(AddMidlewareFoxxApplicationSpec);
jsunity.run(BaseMiddlewareWithoutTemplateSpec);
jsunity.run(BaseMiddlewareWithTemplateSpec);
jsunity.run(ViewHelperSpec);
jsunity.run(FormatMiddlewareSpec);

return jsunity.done();
