/*jslint indent: 2, nomen: true, maxlen: 120, sloppy: true, vars: true, white: true, regexp: true, plusplus: true */
/*global module, require, exports */

////////////////////////////////////////////////////////////////////////////////
/// @brief Foxx application
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2013 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var internal = require("internal");

var console = require("console");
var fs = require("fs");

var arangodb = require("org/arangodb");

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the aal collection
////////////////////////////////////////////////////////////////////////////////

function getStorage () {
  return arangodb.db._collection('_aal');
}

////////////////////////////////////////////////////////////////////////////////
/// @brief builds one assets of an app
////////////////////////////////////////////////////////////////////////////////

function buildAssetContent (app, assets) {
  var files;
  var i;
  var j;
  var m;
  var match;
  var content;

  var reSub = /(.*)\/\*\*$/;
  var reAll = /(.*)\/\*$/;
  var rootDir = fs.join(app._root, app._path);

  files = [];

  for (j = 0; j < assets.length; ++j) {
    var asset = assets[j];

    match = reSub.exec(asset);

    if (match !== null) {
      m = fs.listTree(fs.join(rootDir, match[1]));

      for (i = 0; i < m.length; ++i) {
        var filename = fs.join(rootDir, match[1], m[i]);

        if (fs.isFile(filename)) {
          files.push(filename);
        }
      }
    }
    else {
      match = reAll.exec(asset);

      if (match !== null) {
        throw "not implemented";
      }
      else {
        files.push(fs.join(rootDir, asset));
      }
    }
  }

  content = "";

  for (i = 0; i < files.length; ++i) {
    var c = fs.read(files[i]);

    content += c;
  }

  return content;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief installs the assets of an app
////////////////////////////////////////////////////////////////////////////////

function installAssets (app, routes) {
  var desc;
  var path;
  var normalized;
  var type;
  var route;

  desc = app._manifest;

  if (desc.hasOwnProperty('assets')) {
    for (path in desc.assets) {
      if (desc.assets.hasOwnProperty(path)) {
        var asset = desc.assets[path];

        if (asset.hasOwnProperty('files')) {
          var content = buildAssetContent(app, asset.files);

          normalized = arangodb.normalizeURL("/" + path);
          type = arangodb.guessContentType(normalized);

          route = {
            url: { match: normalized },
            content: { contentType: type, body: content }
          };

          routes.routes.push(route);
        }
      }
    }
  }

  if (desc.hasOwnProperty('files')) {
    for (path in desc.files) {
      if (desc.files.hasOwnProperty(path)) {
        var directory = desc.files[path];

        normalized = arangodb.normalizeURL("/" + path);

        route = {
          url: { match: normalized + "/*" },
          action: {
            "do": "org/arangodb/actions/pathHandler",
            "options": {
              path: fs.join(app._path, directory)
            }
          }
        };

        routes.routes.push(route);
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up an app
////////////////////////////////////////////////////////////////////////////////

function executeAppScript (app, name, mount, prefix) {
  var desc;
  var appContext;

  appContext = {
    name: app._name,
    version: app._version,
    appId: app._id,
    mount: mount,
    collectionPrefix: prefix
  };

  desc = app._manifest;

  if (desc.hasOwnProperty(name)) {
    var cp = appContext.collectionPrefix;
    var context = {};

    if (cp !== "") {
      context.appCollectionName = function (name) {
        return cp + "_" + name;
      };

      context.appCollection = function (name) {
        return internal.db._collection(cp + "_" + name);
      };
    }
    else {
      context.appCollectionName = function (name) {
        return name;
      };

      context.appCollection = function (name) {
        return internal.db._collection(name);
      };
    }

    app.loadAppScript(app.createAppModule(), desc[name], appContext, context);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up an app
////////////////////////////////////////////////////////////////////////////////

function setupApp (app, mount, prefix) {
  return executeAppScript(app, "setup", mount, prefix);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief tears down an app
////////////////////////////////////////////////////////////////////////////////

function teardownApp (app, mount, prefix) {
  return executeAppScript(app, "teardown", mount, prefix);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an app entry for aal
////////////////////////////////////////////////////////////////////////////////

function upsertAalAppEntry (manifest, path) {
  var aal = getStorage();
  var doc = aal.firstExample({ name: manifest.name, version: manifest.version });

  if (doc === null) {
    aal.save({
      type: "app",
      app: "app:" + manifest.name + ":" + manifest.version,
      name: manifest.name,
      version: manifest.version,
      path: path
    });
  }
  else {
    if (doc.path !== path) {
      doc.path = path;
      aal.replace(doc, doc);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief installs an app for aal
////////////////////////////////////////////////////////////////////////////////

function installAalApp (app, mount, prefix, development) {
  'use strict';

  var aal;
  var desc;
  var find;

  aal = getStorage();

  // .............................................................................
  // check that the mount path is free
  // .............................................................................

  find = aal.firstExample({ type: "mount", mount: mount, active: true });

  if (find !== null) {
    throw "cannot use mount path '" + mount + "', already used by '" 
      + find.app + "' (" + find._key + ")";
  }

  // .............................................................................
  // check the prefix
  // .............................................................................

  if (prefix === undefined) {
    prefix = mount.substr(1).replace(/\//g, "_");
  }

  setupApp(app, mount, prefix);

  // .............................................................................
  // create a new (unique) entry in aal
  // .............................................................................

  desc = {
    type: "mount",
    app: app._id,
    mount: mount,
    active: false,
    collectionPrefix: prefix,
    development: development
  };

  return aal.save(desc);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief installs an app for aal
////////////////////////////////////////////////////////////////////////////////

function routingAalApp (app, mount, prefix) {
  'use strict';

  try {
    var apps;
    var i;
    var context;
    var routes;

    if (mount === "") {
      mount = "/";
    }
    else {
      mount = arangodb.normalizeURL(mount);
    }

    if (mount[0] !== "/") {
      throw "mount point must be absolute";
    }

    // setup the routes
    routes = {
      urlPrefix: mount,
      routes: [],
      middleware: []
    };

    routes.routes.push({
      "url" : { match: "/" },
      "action" : {
        "do" : "org/arangodb/actions/redirectRequest",
        "options" : {
          "permanently" : true,
          "destination" : mount + "/" + "index.html"
        }
      }
    });

    // compute the collection prefix
    if (prefix === undefined) {
      prefix = mount.substr(1).replace(/\//g, "_");
    }

    // mount all applications
    apps = app._manifest.apps;

    for (i in apps) {
      if (apps.hasOwnProperty(i)) {
        var file = apps[i];

        // set up a context for the applications
        context = {
          prefix: arangodb.normalizeURL("/" + i),   // app mount

          context: {
            name: app._name,                        // app name
            version: app._version,                  // app version
            appId: app._id,                         // app identifier
            mount: mount,                           // global mount
            collectionPrefix: prefix                // collection prefix
          }
        };

        app.loadAppScript(app.createAppModule(), file, context);

        if (context.routingInfo !== undefined) {
          var ri = context.routingInfo;
          var p = ri.urlPrefix;
          var route;
          var j;

          if (ri.hasOwnProperty("routes")) {
            for (j = 0;  j < ri.routes.length;  ++j) {
              route = ri.routes[j];

              if (route.hasOwnProperty("url")) {
                route.url.match = arangodb.normalizeURL(p + "/" + route.url.match);
              }

              routes.routes.push(route);
            }
          }

          if (ri.hasOwnProperty("middleware")) {
            for (j = 0;  j < ri.middleware.length;  ++j) {
              route = ri.middleware[j];

              if (route.hasOwnProperty("url")) {
                route.url.match = arangodb.normalizeURL(p + "/" + route.url.match);
              }

              routes.middleware.push(route);
            }
          }
        }
      }
    }

    // install all files and assets
    installAssets(app, routes);

    // and return all routes
    return routes;
  }
  catch (err) {
    console.error("cannot compute foxx application routes: %s - %s", String(err), String(err.stack));
  }

  return null;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief scans available FOXX applications
////////////////////////////////////////////////////////////////////////////////

exports.scanAppDirectory = function () {
  'use strict';

  var i;
  var j;

  var path = module.appPath();
  var aal = getStorage();

  aal.removeByExample({ type: "app" });

  if (typeof path === "undefined") {
    return;
  }

  var files = fs.list(path);

  for (j = 0;  j < files.length;  ++j) {
    var m = fs.join(path, files[j], "manifest.json");

    if (fs.exists(m)) {
      try {
        var mf = JSON.parse(fs.read(m));

        upsertAalAppEntry(mf, files[j]);
      }
      catch (err) {
        console.error("cannot read app manifest '%s': %s", m, String(err));
      }
    }
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief installs a FOXX application
////////////////////////////////////////////////////////////////////////////////

exports.installApp = function (appId, mount, options) {
  'use strict';

  var aal;
  var app;
  var desc;
  var doc;
  var prefix;
  var routes;
  var version;

  aal = getStorage();

  // .............................................................................
  // locate the application
  // .............................................................................

  if (appId.substr(0,4) !== "app:") {
    appId = "app:" + appId + ":latest";
  }

  app = module.createApp(appId);

  if (app === null) {
    throw "cannot find application '" + appId + "'";
  }

  // .............................................................................
  // compute the routing information
  // .............................................................................

  prefix = options && options.collectionPrefix;
  routes = routingAalApp(app, mount, prefix);

  if (routes === null) {
    throw "cannot compute the routing table for fox application '" 
      + app._id + "', check the log file for errors!";
  }

  // .............................................................................
  // install the application
  // .............................................................................

  try {
    doc = installAalApp(app, mount, prefix, false);

    // and save the routings
    routes.foxxMount = doc._key;
    arangodb.db._collection("_routing").save(routes);
  }
  catch (err) {
    if (doc !== undefined) {
      desc = aal.document(doc._key).shallowCopy;

      desc.error = String(err);
      desc.active = false;

      aal.replace(doc, desc);
    }

    throw err;
  }

  desc = aal.document(doc).shallowCopy;
  desc.active = true;
  doc = aal.replace(doc, desc);

  internal.executeGlobalContextFunction("require(\"org/arangodb/actions\").reloadRouting()");

  return { appId: app._id, mountId: doc._key };
};

////////////////////////////////////////////////////////////////////////////////
/// @brief installs a FOXX application in development
////////////////////////////////////////////////////////////////////////////////

exports.installDevApp = function (name, mount, options) {
  'use strict';

  var aal;
  var app;
  var desc;
  var doc;
  var prefix;
  var i;

  aal = getStorage();

  // .............................................................................
  // locate the application
  // .............................................................................

  var path = module.devAppPath();

  if (typeof path === "undefined") {
    throw "dev-app-path is not set, cannot instal development app '" + name + "'";
  }

  var appId = null;
  var filename = fs.join(path, name, "manifest.json");

  if (fs.exists(filename)) {
    appId = "dev:" + name + ":" + name;
  }

  app = appId !== null && module.createApp(appId);

  if (app === null) {
    throw "cannot find development application '" + name + "'";
  }

  // .............................................................................
  // install the application
  // .............................................................................

  try {
    prefix = options && options.collectionPrefix;
    doc = installAalApp(app, mount, prefix, true);
  }
  catch (err) {
    if (doc !== undefined) {
      aal.remove(doc._key);
    }

    throw err;
  }

  desc = aal.document(doc).shallowCopy;
  desc.active = true;
  doc = aal.replace(doc, desc);

  return { appId: app._id, mountId: doc._key };
};

////////////////////////////////////////////////////////////////////////////////
/// @brief uninstalls a FOXX application
////////////////////////////////////////////////////////////////////////////////

exports.uninstallApp = function (key) {
  'use strict';

  var aal;
  var app;
  var appDoc;
  var context;
  var doc;
  var routing;

  aal = getStorage();
  doc = aal.firstExample({ type: "mount", _key: key });

  if (doc === null) {
    doc = aal.firstExample({ type: "mount", mount: key });
  }

  if (doc === null) {
    throw "key '" + key + "' is neither a mount id nor a mount point";
  }

  var appId = doc.app;

  try {
    if (appId.substr(0,4) === "app:") {
      appDoc = aal.firstExample({ app: appId, type: "app" });

      if (appDoc === null) {
        throw "cannot find app '" + appId + "' in _aal collection";
      }
    }

    app = module.createApp(appId);
    teardownApp(app, doc.mount, doc.collectionPrefix);
  }
  catch (err) {
    console.error("teardown not possible for application '%s': %s", appId, String(err));
  }

  routing = arangodb.db._collection("_routing");

  routing.removeByExample({ foxxMount: doc._key });
  aal.remove(doc);

  internal.executeGlobalContextFunction("require(\"org/arangodb/actions\").reloadRouting()");
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the development routes
////////////////////////////////////////////////////////////////////////////////

exports.developmentRoutes = function () {
  var routes = [];
  var aal = getStorage();
  var cursor = aal.byExample({ type: "mount", active: true, development: true });

  while (cursor.hasNext()) {
    var doc = cursor.next();
    var appId = doc.app;
    var mount = doc.mount;
    var prefix = doc.collectionPrefix;

    var app = module.createApp(appId);
    var r = routingAalApp(app, mount, prefix);

    routes.push(r);
  }

  return routes;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

/// -----------------------------------------------------------------------------
/// --SECTION--                                                       END-OF-FILE
/// -----------------------------------------------------------------------------

/// Local Variables:
/// mode: outline-minor
/// outline-regexp: "/// @brief\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\|/\\*jslint"
/// End:
