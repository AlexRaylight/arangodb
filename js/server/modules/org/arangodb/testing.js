/*jshint strict: false */
/*global require, exports */

////////////////////////////////////////////////////////////////////////////////
/// @brief General unittest framework
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 triagens GmbH, Cologne, Germany
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @fn JSF_UnitTest
/// @brief framework to perform unittests
///
/// This function gets one or two arguments, the first describes which tests
/// to perform and the second is an options object. For `which` the following
/// values are allowed:
///
///   - "all": do all tests
///   - "config"
///   - "boost"
///   - "shell_server"
///   - "shell_server_ahuacatl"
///   - "shell_server_aql"
///   - "http_server"
///   - "ssl_server"
///   - "shell_client"
///   - "dump"
///   - "arangob"
///   - "import"
///   - "upgrade"
///   - "dfdb"
///   - "foxx-manager"
///   - "authentication"
///   - "authentication-parameters
///   - "single": convenience to execute a single test file
///
/// The following properties of `options` are defined:
///
///   - `jsonReply`: if set a json is returned which the caller has to 
///        present the user
///   - `force`: if set to true the tests are continued even if one fails
///   - `skipBoost`: if set to true the boost unittests are skipped
///   - `skipGeo`: if set to true the geo index tests are skipped
///   - `skipAhuacatl`: if set to true the ahuacatl tests are skipped
///   - `skipAql`: if set to true the AQL tests are skipped
///   - `skipRanges`: if set to true the ranges tests are skipped
///   - `valgrind`: if set to true the arangods are run with the valgrind
///     memory checker
///   - `valgrindXmlFileBase`: string to prepend to the xml report name
///   - `valgrindargs`: commandline parameters to add to valgrind
///   - `extraargs`: extra commandline arguments to add to arangod
///   - `cluster`: if set to true the tests are run with the coordinator
///     of a small local cluster
///   - `test`: path to single test to execute for "single" test target
///   - `skipServer`: flag for "single" test target to skip the server test
///   - `skipClient`: flag for "single" test target to skip the client test
///   - `cleanup`: if set to true (the default), the cluster data files
///     and logs are removed after termination of the test.
///   - `jasmineReportFormat`: this option is passed on to the `format`
///     option of the Jasmin options object, only for Jasmin tests.
////////////////////////////////////////////////////////////////////////////////

var _ = require("underscore");

var testFuncs = {};
var print = require("internal").print;
var fs = require("fs");
var download = require("internal").download;
var wait = require("internal").wait;
var executeExternal = require("internal").executeExternal;
var killExternal = require("internal").killExternal;
var statusExternal = require("internal").statusExternal;
var base64Encode = require("internal").base64Encode;

var PortFinder = require("org/arangodb/cluster").PortFinder;
var Planner = require("org/arangodb/cluster").Planner;
var Kickstarter = require("org/arangodb/cluster").Kickstarter;

var endpointToURL = require("org/arangodb/cluster/planner").endpointToURL;

var optionsDefaults = { "cluster": false,
                        "valgrind": false,
                        "force": true,
                        "skipBoost": false,
                        "skipGeo": false,
                        "skipAhuacatl": false,
                        "skipAql": false,
                        "skipRanges": false,
                        "username": "root",
                        "password": "",
                        "test": undefined,
                        "skipServer": false,
                        "skipClient": false,
                        "cleanup": true,
                        "jsonReply": false};

function findTopDir () {
  var topDir = fs.normalize(fs.makeAbsolute("."));
  if (! fs.exists("3rdParty") && ! fs.exists("arangod") &&
      ! fs.exists("arangosh") && ! fs.exists("UnitTests")) {
    throw "Must be in ArangoDB topdir to execute unit tests.";
  }
  return topDir;
}

function makeTestingArgs () {
  var topDir = findTopDir();
  return [ "--configuration",                  "none",
           "--server.keyfile",       fs.join(topDir, "UnitTests", "server.pem"),
           "--database.maximal-journal-size",  "1048576",
           "--database.force-sync-properties", "false",
           "--javascript.app-path",            fs.join(topDir, "js", "apps"),
           "--javascript.startup-directory",   fs.join(topDir, "js"),
           "--ruby.modules-path", fs.join(topDir,"mr", "common", "modules"),
           "--server.threads",                 "4",
           "--server.disable-authentication",  "true",
           "--server.allow-use-database",      "true" ];
}

function makeTestingArgsClient (options) {
  var topDir = findTopDir();
  return [ "--configuration",                  "none",
           "--javascript.startup-directory",   fs.join(topDir, "js"),
           "--no-colors",
           "--quiet",
           "--server.username",                options.username,
           "--server.password",                options.password ];
}

function makeAuthorisationHeaders (options) {
  return {"headers":
            {"Authorization": "Basic " + base64Encode(options.username+":"+
                                                      options.password)}};
}

function startInstance (protocol, options, addArgs, testname) {
  // protocol must be one of ["tcp", "ssl", "unix"]
  var topDir = findTopDir();
  var instanceInfo = {};
  instanceInfo.topDir = topDir;
  var tmpDataDir = fs.getTempFile();
  fs.makeDirectoryRecursive(tmpDataDir);
  instanceInfo.tmpDataDir = tmpDataDir;

  var endpoint;
  var pos;
  var dispatcher;
  if (options.cluster) {
    // FIXME: valgrind currently ignored!
    dispatcher = {"endpoint":"tcp://localhost:",
                  "arangodExtraArgs":makeTestingArgs(),
                  "username": "root",
                  "password": ""};
    if (addArgs !== undefined) {
      dispatcher.arangodExtraArgs = addArgs;
    }
    print("Temporary cluster data and logs are in",tmpDataDir);
    var p = new Planner({"numberOfDBservers":2,
                         "numberOfCoordinators":1,
                         "dispatchers": {"me": dispatcher},
                         "dataPath": tmpDataDir,
                         "logPath": tmpDataDir,
                         "useSSLonCoordinators": protocol === "ssl"});
    instanceInfo.kickstarter = new Kickstarter(p.getPlan());
    instanceInfo.kickstarter.launch();
    var runInfo = instanceInfo.kickstarter.runInfo;
    var j = runInfo.length-1;
    while (j > 0 && runInfo[j].isStartServers === undefined) {
      j--;
    }
    var roles = runInfo[j].roles;
    var endpoints = runInfo[j].endpoints;
    pos = roles.indexOf("Coordinator");
    endpoint = endpoints[pos];
  }
  else {   // single instance mode
    // We use the PortFinder to find a free port for our subinstance,
    // to this end, we have to fake a dummy dispatcher:
    dispatcher = {endpoint: "tcp://localhost:", avoidPorts: {}, id: "me"};
    var pf = new PortFinder([8529],dispatcher);
    var port = pf.next();
    instanceInfo.port = port;
    var args = makeTestingArgs();
    args.push("--server.endpoint");
    endpoint = protocol+"://127.0.0.1:"+port;
    args.push(endpoint);
    args.push("--database.directory");
    args.push(fs.join(tmpDataDir,"data"));
    fs.makeDirectoryRecursive(fs.join(tmpDataDir,"data"));
    args.push("--log.file");
    args.push(fs.join(tmpDataDir,"log"));
    if (protocol === "ssl") {
      args.push("--server.keyfile");
      args.push(fs.join("UnitTests","server.pem"));
    }
    if (typeof(options.extraargs) === 'object') {
      args = args.concat(options.extraargs);
    }
    if (addArgs !== undefined) {
      args = args.concat(addArgs);
    }
    if (typeof(options.valgrind) === 'string') {
      var run = fs.join("bin","arangod");
      var valgrindopts = options.valgrindargs.concat(
        ["--xml-file="+options.valgrindXmlFileBase + '_' + testname + '.xml']);
      var newargs=valgrindopts.concat([run]).concat(args);      
      var cmdline = options.valgrind;
      instanceInfo.pid = executeExternal(cmdline, newargs);
    }
    else {
      instanceInfo.pid = executeExternal(fs.join("bin","arangod"), args);
    }
  }

  // Wait until the server/coordinator is up:
  var url = endpointToURL(endpoint);
  while (true) {
    wait(0.5);
    var r = download(url+"/_api/version","",makeAuthorisationHeaders(options));
    if (!r.error && r.code === 200) {
      break;
    }
  }

  instanceInfo.endpoint = endpoint;
  instanceInfo.url = url;

  return instanceInfo;
}

function shutdownInstance (instanceInfo, options) {
  if (options.cluster) {
    instanceInfo.kickstarter.shutdown();
    if (options.cleanup) {
      instanceInfo.kickstarter.cleanup();
    }
  }
  else {
    download(instanceInfo.url+"/_admin/shutdown","",
             makeAuthorisationHeaders(options));
    wait(10);
    killExternal(instanceInfo.pid);
  }
  if (options.cleanup) {
    fs.removeDirectoryRecursive(instanceInfo.tmpDataDir);
  }
}

function makePath (path) {
  return fs.join.apply(null,path.split("/"));
}

var foundTests = false;

var tests_shell_common;
var tests_shell_server_only;
var tests_shell_client_only;
var tests_shell_server;
var tests_shell_client;
var tests_shell_server_ahuacatl;
var tests_shell_server_ahuacatl_extended;
var tests_shell_server_aql;
var tests_shell_server_aql_extended;
var tests_shell_server_aql_performance;

function findTests () {
  if (foundTests) {
    return;
  }
  tests_shell_common = _.filter(fs.list(makePath("js/common/tests")),
            function (p) {
              return p.substr(0,6) === "shell-" &&
                     p.substr(-3) === ".js";
            }).map(
            function(x) {
              return fs.join(makePath("js/common/tests"),x);
            }).sort();
  tests_shell_server_only = _.filter(fs.list(makePath("js/server/tests")),
            function (p) {
              return p.substr(0,6) === "shell-" &&
                     p.substr(-3) === ".js";
            }).map(
            function(x) {
              return fs.join(makePath("js/server/tests"),x);
            }).sort();
  tests_shell_client_only = _.filter(fs.list(makePath("js/client/tests")),
            function (p) {
              return p.substr(0,6) === "shell-" &&
                     p.substr(-3) === ".js";
            }).map(
            function(x) {
              return fs.join(makePath("js/client/tests"),x);
            }).sort();
  tests_shell_server_ahuacatl = _.filter(fs.list(makePath("js/server/tests")),
            function (p) {
              return p.substr(0,9) === "ahuacatl-" &&
                     p.substr(-3) === ".js" &&
                     p.indexOf("ranges-combined") === -1;
            }).map(
            function(x) {
              return fs.join(makePath("js/server/tests"),x);
            }).sort();
  tests_shell_server_ahuacatl_extended =
            _.filter(fs.list(makePath("js/server/tests")),
            function (p) {
              return p.substr(0,9) === "ahuacatl-" &&
                     p.substr(-3) === ".js" &&
                     p.indexOf("ranges-combined") !== -1;
            }).map(
            function(x) {
              return fs.join(makePath("js/server/tests"),x);
            }).sort();
  tests_shell_server_aql = _.filter(fs.list(makePath("js/server/tests")),
            function (p) {
              return p.substr(0,4) === "aql-" &&
                     p.substr(-3) === ".js" &&
                     p.indexOf("ranges-combined") === -1;
            }).map(
            function(x) {
              return fs.join(makePath("js/server/tests"),x);
            }).sort();
  tests_shell_server_aql_extended = 
            _.filter(fs.list(makePath("js/server/tests")),
            function (p) {
              return p.substr(0,4) === "aql-" &&
                     p.substr(-3) === ".js" &&
                     p.indexOf("ranges-combined") !== -1;
            }).map(
            function(x) {
              return fs.join(makePath("js/server/tests"),x);
            }).sort();
  tests_shell_server_aql_performance = 
            _.filter(fs.list(makePath("js/server/perftests")),
            function (p) {
              return p.substr(-3) === ".js";
            }).map(
            function(x) {
              return fs.join(makePath("js/server/perftests"),x);
            }).sort();

  tests_shell_server = tests_shell_common.concat(tests_shell_server_only);
  tests_shell_client = tests_shell_common.concat(tests_shell_client_only);
  foundTests = true;
}

function runThere (options, instanceInfo, file) {
  var r;
  try {
    var t;
    if (file.indexOf("-spec") === -1) {
      t = 'var runTest = require("jsunity").runTest; '+
          'return runTest('+JSON.stringify(file)+');';
    }
    else {
      var jasmineReportFormat = options.jasmineReportFormat || 'progress';
      t = 'var executeTestSuite = require("jasmine").executeTestSuite; '+
          'return executeTestSuite(['+JSON.stringify(file)+'],{"format": '+
          JSON.stringify(jasmineReportFormat)+'});';
    }
    var o = makeAuthorisationHeaders(options);
    o.method = "POST";
    o.timeout = 24*3600;
    o.returnBodyOnError = true;
    r = download(instanceInfo.url+"/_admin/execute?returnAsJSON=true",t,o);
    if (!r.error && r.code === 200) {
      r = JSON.parse(r.body);
    }
    else {
      print("Error on the remote arangod:\n" + r.body);
    }
  }
  catch (err) {
    r = err;
  }
  return r;
}

function executeAndWait (cmd, args) {
  var pid = executeExternal(cmd, args);
  var res = statusExternal(pid, true);
  if (res.status === "TERMINATED") {
    return { status: (res.exit === 0), message: ""};
  }
  else {
    return { status: (res === 0), message: res.exit};
  }
}

function runInArangosh (options, instanceInfo, file, addArgs) {
  var args = makeTestingArgsClient(options);
  var topDir = findTopDir();
  args.push("--server.endpoint");
  args.push(instanceInfo.endpoint);
  args.push("--javascript.unit-tests");
  args.push(fs.join(topDir,file));
  if (addArgs !== undefined) {
    args = args.concat(addArgs);
  }
  var arangosh = fs.join("bin","arangosh");
  return executeAndWait(arangosh, args);
}

function runArangoshCmd (options, instanceInfo, cmds) {
  var args = makeTestingArgsClient(options);
  args.push("--server.endpoint");
  args.push(instanceInfo.endpoint);
  args = args.concat(cmds);
  var arangosh = fs.join("bin","arangosh");
  return executeAndWait(arangosh, args);
}

function performTests(options, testList, testname) {
  var instanceInfo = startInstance("tcp", options, [], testname);
  var results = {};
  var i;
  var te;
  for (i = 0; i < testList.length; i++) {
    te = testList[i];
    print("\nTrying",te,"...");
    if ((te.indexOf("-cluster") === -1 || options.cluster) &&
        (te.indexOf("-noncluster") === -1 || options.cluster === false) &&
        (te.indexOf("-disabled") === -1)) {
      var r = runThere(options, instanceInfo, te);
      results[te] = r;
      if (r !== true && !options.force) {
        break;
      }
    }
    else {
      print("Skipped because of cluster/non-cluster or disabled.");
    }
  }
  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return results;
}

testFuncs.shell_server = function (options) {
  findTests();
  return performTests(options, tests_shell_server, 'tests_shell_server');
};

testFuncs.shell_server_only = function (options) {
  findTests();
  return performTests(options,
                      tests_shell_server_only,
                      'tests_shell_server_only');
};

testFuncs.shell_server_ahuacatl = function(options) {
  findTests();
  if (!options.skipAhuacatl) {
    if (options.skipRanges) {
      return performTests(options,
                          tests_shell_server_ahuacatl,
                          'tests_shell_server_ahuacatl');
    }
    return performTests(options,
                        tests_shell_server_ahuacatl.concat(
                          tests_shell_server_ahuacatl_extended),
                        'tests_shell_server_ahuacatl_extended');
  }
  return "skipped";
};

testFuncs.shell_server_aql = function(options) {
  findTests();
  if (!options.skipAql) {
    if (options.skipRanges) {
      return performTests(options,
                          tests_shell_server_aql,
                          'tests_shell_server_aql');
    }
    return performTests(options,
                        tests_shell_server_aql.concat(
                          tests_shell_server_aql_extended),
                        'tests_shell_server_aql_extended');
  }
  return "skipped";
};

testFuncs.shell_server_perf = function(options) {
  findTests();
  return performTests(options,
                      tests_shell_server_aql_performance,
                      'tests_shell_server_aql_performance');
};

testFuncs.shell_client = function(options) {
  findTests();
  var instanceInfo = startInstance("tcp", options, [], "shell_client");
  var results = {};
  var i;
  var te;
  for (i = 0; i < tests_shell_client.length; i++) {
    te = tests_shell_client[i];
    print("\nTrying",te,"...");
    if ((te.indexOf("-cluster") === -1 || options.cluster) &&
        (te.indexOf("-noncluster") === -1 || options.cluster === false) &&
        (te.indexOf("-disabled") === -1)) {
      var r = runInArangosh(options, instanceInfo, te);
      results[te] = r;
      if (r !== 0 && !options.force) {
        break;
      }
    }
    else {
      print("Skipped because of cluster/non-cluster.");
    }
  }
  print("Shutting down...");
  shutdownInstance(instanceInfo, options);
  print("done.");
  return results;
};

testFuncs.config = function () {
  var topDir = findTopDir();
  var results = {};
  var ts = ["arangod", "arangob", "arangodump", "arangoimp", "arangorestore",
            "arangosh"];
  var t;
  var i;
  for (i = 0; i < ts.length; i++) {
    t = ts[i];
    results[t] = executeAndWait(fs.join(topDir,"bin",t),
        ["--configuration", fs.join(topDir,"etc","arangodb",t+".conf"),
         "--help"]);
    print("Config test "+t+"...",results[t].status);
  }
  for (i = 0; i < ts.length; i++) {
    t = ts[i];
    results[t+"_rel"] = executeAndWait(fs.join(topDir,"bin",t),
        ["--configuration", fs.join(topDir,"etc","relative",
                                    t+".conf"), "--help"]);
    print("Config test "+t+" (relative)...",results[t+"_rel"].status);
  }

  return results;
};

testFuncs.boost = function (options) {
  var topDir = findTopDir();
  var results = {};
  if (!options.skipBoost) {
    results.basics = executeAndWait(fs.join(topDir,"UnitTests","basics_suite"),
                                    ["--show_progress"]);
  }
  if (!options.skipGeo) {
    results.geo_suite = executeAndWait(
                          fs.join(topDir,"UnitTests","geo_suite"),
                          ["--show_progress"]);
  }
  return results;
};

testFuncs.single = function (options) {
  var instanceInfo = startInstance("tcp", options, [], "single");
  var result = { };
  if (options.test !== undefined) {
    var te = options.test;
    result.test = te;
    if (!options.skipServer) {
      print("\nTrying",te,"on server...");
      result.server = runThere(options, instanceInfo, makePath(te));
    }
    if (!options.skipClient) {
      var topDir = findTopDir();
      var args = makeTestingArgsClient(options);
      args.push("--server.endpoint");
      args.push(instanceInfo.endpoint);
      args.push("--javascript.unit-tests");
      args.push(fs.join(topDir,te));
      print("\nTrying",te,"on client...");
      var arangosh = fs.join("bin","arangosh");
      result.client = executeAndWait(arangosh, args);
    }
  }
  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return result;
};

function rubyTests (options, ssl) {
  var instanceInfo;
  if (ssl) {
    instanceInfo = startInstance("ssl", options, [], "rubyTestsSsl");
  }
  else {
    instanceInfo = startInstance("tcp", options, [], "rubyTestsTcp");
  }

  var tmpname = fs.getTempFile()+".rb";
  fs.write(tmpname,'RSpec.configure do |c|\n'+
                   '  c.add_setting :ARANGO_SERVER\n'+
                   '  c.ARANGO_SERVER = "' +
                          instanceInfo.endpoint.substr(6) + '"\n'+
                   '  c.add_setting :ARANGO_SSL\n'+
                   '  c.ARANGO_SSL = "' + (ssl ? '1' : '0') + '"\n'+
                   '  c.add_setting :ARANGO_USER\n'+
                   '  c.ARANGO_USER = "' + options.username + '"\n'+
                   '  c.add_setting :ARANGO_PASSWORD\n'+
                   '  c.ARANGO_PASSWORD = "' + options.password + '"\n'+
                   'end\n');
  var logsdir = fs.join(findTopDir(),"logs");
  try {
    fs.makeDirectory(logsdir);
  }
  catch (err) {
  }
  var files = fs.list(fs.join("UnitTests","HttpInterface"));
  var result = {};
  var args;
  var i;
  for (i = 0; i < files.length; i++) {
    var n = files[i];
    if (n.substr(0,4) === "api-" && n.substr(-3) === ".rb") {
      print("Considering",n,"...");
      if ((n.indexOf("-cluster") === -1 || options.cluster) &&
          (n.indexOf("-noncluster") === -1 || options.cluster === false) &&
          n.indexOf("replication") === -1) {
        args = ["--color", "-I", fs.join("UnitTests","HttpInterface"),
                "--format", "d", "--require", tmpname,
                fs.join("UnitTests","HttpInterface",n)];
        var pid = executeExternal("rspec", args);
        var r = statusExternal(pid, true);

        if (r.status === "TERMINATED") {
          result[n] =  { status: (r.exit === 0), message: r.exit};
        }
        else {
          result[n] = { status: (r === 0), message: r.exit};
        }
        if (r.exit !== 0 && !options.force) {
          break;
        }
      }
      else {
        print("Skipped because of cluster/non-cluster or replication.");
      }
    }
  }

  print("Shutting down...");
  fs.remove(tmpname);
  shutdownInstance(instanceInfo,options);
  print("done.");
  return result;
}

testFuncs.http_server = function (options) {
  return rubyTests(options, false);
};

testFuncs.ssl_server = function (options) {
  return rubyTests(options, true);
};

function runArangoImp (options, instanceInfo, what) {
  var topDir = findTopDir();
  var args = ["--server.username",             options.username,
              "--server.password",             options.password,
              "--server.endpoint",             instanceInfo.endpoint,
              "--file",                        fs.join(topDir,what.data),
              "--collection",                  what.coll,
              "--type",                        what.type];
  if (what.create !== undefined) {
    args.push("--create-collection");
    args.push(what.create);
  }
  if (what.backslash !== undefined) {
    args.push("--backslash-escape");
    args.push(what.backslash);
  }
  if (what.separator !== undefined) {
    args.push("--separator");
    args.push(what.separator);
  }
  var arangoimp = fs.join("bin","arangoimp");
  return executeAndWait(arangoimp, args);
}

function runArangoDumpRestore (options, instanceInfo, which, database) {
  var args = ["--configuration",               "none",
              "--server.username",             options.username,
              "--server.password",             options.password,
              "--server.endpoint",             instanceInfo.endpoint,
              "--server.database",             database];
  var exe;
  if (which === "dump") {
    args.push("--output-directory");
    args.push(fs.join(instanceInfo.tmpDataDir,"dump"));
    exe = fs.join("bin","arangodump");
  }
  else {
    args.push("--input-directory");
    args.push(fs.join(instanceInfo.tmpDataDir,"dump"));
    exe = fs.join("bin","arangorestore");
  }
  return executeAndWait(exe, args);
}

function runArangoBenchmark (options, instanceInfo, cmds) {
  var args = ["--configuration",               "none",
              "--quiet",
              "--server.username",             options.username,
              "--server.password",             options.password,
              "--server.endpoint",             instanceInfo.endpoint];
  args = args.concat(cmds);
  var exe = fs.join("bin","arangob");
  return executeAndWait(exe, args);
}

var impTodo = [
  {id: "json1", data: makePath("UnitTests/import-1.json"),
   coll: "UnitTestsImportJson1", type: "json", create: undefined},
  {id: "json2", data: makePath("UnitTests/import-2.json"),
   coll: "UnitTestsImportJson2", type: "json", create: undefined},
  {id: "json3", data: makePath("UnitTests/import-3.json"),
   coll: "UnitTestsImportJson3", type: "json", create: undefined},
  {id: "json4", data: makePath("UnitTests/import-4.json"),
   coll: "UnitTestsImportJson4", type: "json", create: undefined},
  {id: "csv1", data: makePath("UnitTests/import-1.csv"),
   coll: "UnitTestsImportCsv1", type: "csv", create: "true"},
  {id: "csv2", data: makePath("UnitTests/import-2.csv"),
   coll: "UnitTestsImportCsv2", type: "csv", create: "true"},
  {id: "csv3", data: makePath("UnitTests/import-3.csv"),
   coll: "UnitTestsImportCsv3", type: "csv", create: "true"},
  {id: "csv4", data: makePath("UnitTests/import-4.csv"),
   coll: "UnitTestsImportCsv4", type: "csv", create: "true", separator: ";", backslash: true},
  {id: "tsv1", data: makePath("UnitTests/import-1.tsv"),
   coll: "UnitTestsImportTsv1", type: "tsv", create: "true"},
  {id: "tsv2", data: makePath("UnitTests/import-2.tsv"),
   coll: "UnitTestsImportTsv2", type: "tsv", create: "true"},
  {id: "edge", data: makePath("UnitTests/import-edges.json"),
   coll: "UnitTestsImportEdge", type: "json", create: "false"}
];

testFuncs.importing = function (options) {
  if (options.cluster) {
    print("Skipped because of cluster.");
    return {"ok":true, "skipped":0};
  }

  var instanceInfo = startInstance("tcp", options, [ ], "importing");

  var result = {};
  try {
    var r = runInArangosh(options, instanceInfo,
                          makePath("js/server/tests/import-setup.js"));
    result.setup = r;
    if (r !== 0) {
      throw "banana";
    }
    var i;
    for (i = 0; i < impTodo.length; i++) {
      r = runArangoImp(options, instanceInfo, impTodo[i]);
      result[impTodo[i].id] = r;
      if (r !== 0 && !options.force) {
        throw "banana";
      }
    }
    r = runInArangosh(options, instanceInfo,
                      makePath("js/server/tests/import.js"));
    result.check = r;
    r = runInArangosh(options, instanceInfo,
                      makePath("js/server/tests/import-teardown.js"));
    result.teardown = r;
  }
  catch (banana) {
    print("A banana of the following form was caught:",JSON.stringify(banana));
  }

  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return result;
};

testFuncs.upgrade = function (options) {
  if (options.cluster) {
    return true;
  }

  var result = {};

  var tmpDataDir = fs.getTempFile();
  fs.makeDirectoryRecursive(tmpDataDir);

  // We use the PortFinder to find a free port for our subinstance,
  // to this end, we have to fake a dummy dispatcher:
  var dispatcher = {endpoint: "tcp://localhost:", avoidPorts: [], id: "me"};
  var pf = new PortFinder([8529],dispatcher);
  var port = pf.next();
  var args = makeTestingArgs();
  args.push("--server.endpoint");
  var endpoint = "tcp://127.0.0.1:"+port;
  args.push(endpoint);
  args.push("--database.directory");
  args.push(fs.join(tmpDataDir,"data"));
  fs.makeDirectoryRecursive(fs.join(tmpDataDir,"data"));
  args.push("--upgrade");
  result.first = executeAndWait(fs.join("bin","arangod"), args);

  if (result.first !== 0 && !options.force) {
    return result;
  }
  result.second = executeAndWait(fs.join("bin","arangod"), args);

  fs.removeDirectoryRecursive(tmpDataDir);

  return result;
};

testFuncs.foxx_manager = function (options) {
  print("foxx_manager tests...");
  var instanceInfo = startInstance("tcp", options, [], "foxx_manager");
  var results = {};

  results.update = runArangoshCmd(options, instanceInfo,
                                  ["--configuration",
                                   "etc/relative/foxx-manager.conf",
                                   "update"]);
  if (results.update === 0 || options.force) {
    results.search = runArangoshCmd(options, instanceInfo,
                                    ["--configuration",
                                     "etc/relative/foxx-manager.conf",
                                     "search","itzpapalotl"]);
  }
  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return results;
};

testFuncs.dump = function (options) {
  var cluster;

  if (options.cluster) {
    cluster = "-cluster";
  }
  else {
    cluster = "";
  }
  print("dump tests...");
  var instanceInfo = startInstance("tcp",options, [], "dump_test");
  var results = {};
  results.setup = runInArangosh(options, instanceInfo,
       makePath("js/server/tests/dump-setup"+cluster+".js"));
  if (results.setup === 0) {
    results.dump = runArangoDumpRestore(options, instanceInfo, "dump",
                                        "UnitTestsDumpSrc");
    results.restore = runArangoDumpRestore(options, instanceInfo, "restore",
                                           "UnitTestsDumpDst");
    results.test = runInArangosh(options, instanceInfo,
       makePath("js/server/tests/dump"+cluster+".js"),
       [ "--server.database", "UnitTestsDumpDst" ]);
    results.tearDown = runInArangosh(options, instanceInfo,
       makePath("js/server/tests/dump-teardown"+cluster+".js"));
  }
  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return results;
};

var benchTodo = [
  ["--requests","10000","--concurrency","2","--test","version", "--keep-alive","false"],
  ["--requests","10000","--concurrency","2","--test","version", "--async","true"],
  ["--requests","20000","--concurrency","1","--test","version", "--async","true"],
  ["--requests","100000","--concurrency","2","--test","shapes", "--batch-size","16", "--complexity","2"],
  ["--requests","100000","--concurrency","2","--test","shapes-append", "--batch-size","16", "--complexity","4"],
  ["--requests","100000","--concurrency","2","--test","random-shapes", "--batch-size","16", "--complexity","2"],
  ["--requests","1000","--concurrency","2","--test","version", "--batch-size", "16"],
  ["--requests","100","--concurrency","1","--test","version", "--batch-size", "0"],
  ["--requests","100","--concurrency","2","--test","document", "--batch-size",
   "10", "--complexity", "1"],
  ["--requests","2000","--concurrency","2","--test","crud", "--complexity", "1"],
  ["--requests","4000","--concurrency","2","--test","crud-append", "--complexity", "4"],
  ["--requests","4000","--concurrency","2","--test","edge", "--complexity", "4"],
  ["--requests","5000","--concurrency","2","--test","hash","--complexity","1"],
  ["--requests","5000","--concurrency","2","--test","skiplist","--complexity","1"],
  ["--requests","500","--concurrency","3","--test","aqltrx","--complexity","1"],
  ["--requests","100","--concurrency","3","--test","counttrx"],
  ["--requests","500","--concurrency","3","--test","multitrx"]
];

testFuncs.arangob = function (options) {
  print("arangob tests...");
  var instanceInfo = startInstance("tcp",options, [], "arangobench");
  var results = {};
  var i,r;
  for (i = 0; i < benchTodo.length; i++) {
    // On the cluster we do not yet have working transaction functionality:
    if (!options.cluster ||
        (benchTodo[i].indexOf("counttrx") === -1 &&
         benchTodo[i].indexOf("multitrx") === -1)) {
      r = runArangoBenchmark(options, instanceInfo, benchTodo[i]);
      results[i] = r;
      if (r !== 0 && !options.force) {
        break;
      }
    }
  }
  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return results;
};

testFuncs.authentication = function (options) {
  print("Authentication tests...");
  var instanceInfo = startInstance("tcp", options,
                                   ["--server.disable-authentication", "false"],
                                   "authtest");
  var results = {};
  results.auth = runInArangosh(options, instanceInfo,
                               fs.join("js","client","tests","auth.js"));
  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return results;
};

var urlsTodo = [
  "/_api/",
  "/_api",
  "/_api/version",
  "/_admin/html",
  "/_admin/html/",
  "/test",
  "/the-big-fat-fox"
];

testFuncs.authentication_parameters = function (options) {
  print("Authentication with parameters tests...");
  var results = {};
  // With full authentication:
  var instanceInfo = startInstance("tcp", options,
                       ["--server.disable-authentication", "false",
                        "--server.authenticate-system-only", "false"],
                       "authparams");
  var r;
  var i;
  var expectAuthFullRC = [401, 401, 401, 401, 401, 401, 401];
  var all_ok = true;
  results["auth_full"] = {};
  for (i = 0; i < urlsTodo.length; i++) {
    r = download(instanceInfo.url+urlsTodo[i],"",{followRedirects:false,returnBodyOnError:true});
    if (r.code === expectAuthFullRC[i]) {
      results.auth_full[urlsTodo[i]] = { status: true, message: ""};
    }
    else {
      results.auth_full[urlsTodo[i]] = { 
        status: false,
        message: "we expected " + expectAuthFullRC[i] + " and we got " + r.code + " Full Status: " + JSON.stringify(r)
      };
      all_ok = false;
    }
  }
  results.auth_full.status = all_ok;

  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  // Only system authentication:
  instanceInfo = startInstance("tcp", options,
                   ["--server.disable-authentication", "false",
                    "--server.authenticate-system-only", "true"],
                   "authparams2");
  var expectAuthSystemRC = [401, 401, 401, 401, 401, 404, 404];
  all_ok = true;
  results["auth_system"] = {};
  for (i = 0;i < urlsTodo.length;i++) {
    r = download(instanceInfo.url+urlsTodo[i],"",{followRedirects:false,returnBodyOnError:true});
    if (r.code === expectAuthSystemRC[i]) {
      results.auth_system[urlsTodo[i]] = { status: true, message: ""};
    }
    else {
      results.auth_system[urlsTodo[i]] = { 
        status: false,
        message: "we expected " + expectAuthSystemRC[i] + " and we got " + r.code + " Full Status: " + JSON.stringify(r)
      };
      all_ok = false;
    }
  }
  results.auth_system.status = all_ok;

  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  // No authentication:
  instanceInfo = startInstance("tcp", options,
                   ["--server.disable-authentication", "true",
                    "--server.authenticate-system-only", "true"],
                   "authparams3");
  var expectAuthNoneRC = [404, 404, 200, 301, 301, 404, 404];
  results["auth_none"] = {};
  all_ok = true;
  for (i = 0;i < urlsTodo.length;i++) {
    r = download(instanceInfo.url+urlsTodo[i],"",{followRedirects:false,returnBodyOnError:true});
    if (r.code === expectAuthNoneRC[i]) {
      results.auth_none[urlsTodo[i]] = { status: true, message: ""};
    }
    else {
      results.auth_none[urlsTodo[i]] = { 
        status: false,
        message: "we expected " + expectAuthNoneRC[i] + " and we got " + r.code + " Full Status: " + JSON.stringify(r)
      };
      all_ok = false;
    }
  }
  results.auth_none.status = all_ok;

  print("Shutting down...");
  shutdownInstance(instanceInfo,options);
  print("done.");
  return results;
};

var internalMembers = ["code", "error", "status", "duration", "failed", "total"];

function unitTestPrettyPrintResults(r) {
  for (var testrun in r) {    
    if (r.hasOwnProperty(testrun) && (testrun !== 'all_ok')) {
      print("Testrun: " + testrun);
      for (var test in  r[testrun]) {
        if (r[testrun].hasOwnProperty(test) && (test !== 'ok')) {
          if (r[testrun][test].status) {
            print("     " + test + ": Success");
          }
          else {
            print("     " + test + ": Fail");
            for (var oneTest in r[testrun][test]) {
              if ((r[testrun][test].hasOwnProperty(oneTest)) && 
                  (internalMembers.indexOf(oneTest) === -1) &&
                  (!r[testrun][test][oneTest].status)) {
                print("          -> " + oneTest + " Failed; Verbose message:");
                print(r[testrun][test][oneTest].message);
              }
            }
          }
        }
      }
    }
  }
  print("Overal state: " + ((r.all_ok === true) ? "Success" : "Fail"));
}


var allTests =
  [
    "config",
    "boost",
    "shell_server",
    "shell_server_ahuacatl",
    "shell_server_aql",
    "shell_server_perf",
    "http_server",
    "ssl_server",
    "shell_client",
    "dump",
    "arangob",
    "importing",
    "upgrade",
    "foxx_manager",
    "authentication",
    "authentication_parameters"
  ];

function printUsage () {
  print("Usage: UnitTest( which, options )");
  print('       where "which" is one of:');
  print('         "all": do all tests');
  var i;
  for (i in testFuncs) {
    if (testFuncs.hasOwnProperty(i)) {
      print('         "'+i+'"');
    }
  }
  print('       and options can contain the following boolean properties:');
  print('         "force": continue despite a failed test');
  print('         "skipBoost": skip the boost unittests');
  print('         "skipGeo": skip the geo index tests');
  print('         "skipAhuacatl": skip the ahuacatl tests');
  print('         "skipAql": skip the AQL tests');
  print('         "skipRanges": skip the ranges tests');
  print('         "valgrind": arangods are run with valgrind');
  print('         "cluster": tests are run on a small local cluster');
  print('         "test": name of test to run for "single" test');
  print('         "skipClient": for "single" test');
  print('         "skipServer": for "single" test');
}

function UnitTest (which, options) {
  var allok = true;
  var results = {};
  if (typeof options !== "object") {
    options = {};
  }
  _.defaults(options, optionsDefaults);
  if (which === undefined) {
    printUsage();
    return;
  }
  var jsonReply = options.jsonReply
  delete(options.jsonReply);
  var i;
  var ok;
  if (which === "all") {
    var n;
    for (n = 0;n < allTests.length;n++) {
      print("Doing test",allTests[n],"with options",options);
      results[allTests[n]] = r = testFuncs[allTests[n]](options);
      ok = true;
      for (i in r) {
        if (r.hasOwnProperty(i)) {
          if (r[i].status !== true) {
            ok = false;
          }
        }
      }
      r.ok = ok;
      if (!ok) {
        allok = false;
      }
      results.all_ok = allok;
    }
    results.all_ok = allok;
    if (jsonReply === true ) {
      return results;
    }
    else {
      unitTestPrettyPrintResults(results);
      return allok;
    }
  }
  else if (!testFuncs.hasOwnProperty(which)) {
    printUsage();
    throw 'Unknown test "' + which + '"';
  }
  else {
    var r;
    results[which] = r = testFuncs[which](options);
    ok = true;
    for (i in r) {
      if (r.hasOwnProperty(i) &&
          (which !== "single" || i !== "test")) {
        if (r[i].status !== true) {
          ok = false;
          allok = false;
        }
      }
    }
    r.ok = ok;
    results.all_ok = ok;
    if (jsonReply === true ) {
      return results;
    }
    else {
      unitTestPrettyPrintResults(results);
      return allok;
    }
  }
}

exports.UnitTest = UnitTest;
exports.unitTestPrettyPrintResults = unitTestPrettyPrintResults;
// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @\\}\\)"
// End:
