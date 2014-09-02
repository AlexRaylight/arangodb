/*global module, require, exports */

////////////////////////////////////////////////////////////////////////////////
/// @brief Foxx BaseMiddleware
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
/// @author Lucas Dohmen
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var BaseMiddleware;

////////////////////////////////////////////////////////////////////////////////
/// @fn JSF_foxx_BaseMiddleware_initializer
/// @brief The Base Middleware
///
/// The `BaseMiddleware` manipulates the request and response
/// objects to give you a nicer API.
////////////////////////////////////////////////////////////////////////////////

BaseMiddleware = function () {
  'use strict';
  var middleware = function (request, response, options, next) {
    var responseFunctions,
      requestFunctions,
      trace,
      _ = require("underscore"),
      console = require("console"),
      actions = require("org/arangodb/actions");

    requestFunctions = {

////////////////////////////////////////////////////////////////////////////////
/// @startDocuBlock JSF_foxx_BaseMiddleware_request_body
///
/// `request.body()`
///
/// Get the JSON parsed body of the request. If you need the raw version, please
/// refer to the *rawBody* function.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

      body: function () {
        var requestBody = this.requestBody || '{}';
        return JSON.parse(requestBody);
      },

////////////////////////////////////////////////////////////////////////////////
/// @startDocuBlock JSF_foxx_BaseMiddleware_request_rawBody
///
/// `request.rawBody()`
///
/// The raw request body, not parsed. Just a String.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

      rawBody: function () {
        return this.requestBody;
      },

////////////////////////////////////////////////////////////////////////////////
/// @startDocuBlock JSF_foxx_BaseMiddleware_request_params
///
/// `request.params(key)`
///
/// Get the parameters of the request. This process is two-fold:
///
/// * If you have defined an URL like */test/:id* and the user requested
///   */test/1*, the call *params("id")* will return *1*.
/// * If you have defined an URL like */test* and the user gives a query
///   component, the query parameters will also be returned.  So for example if
///   the user requested */test?a=2*, the call *params("a")* will return *2*.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

      params: function (key) {
        var ps = {};
        _.extend(ps, this.urlParameters);
        _.extend(ps, this.parameters);
        return ps[key];
      }
    };

    responseFunctions = {

////////////////////////////////////////////////////////////////////////////////
/// @startDocuBlock JSF_foxx_BaseMiddleware_response_status
///
/// `response.status(code)`
///
/// Set the status *code* of your response, for example:
///
/// @EXAMPLES
///
/// ```
/// response.status(404);
/// ```
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

      status: function (code) {
        this.responseCode = code;
      },

////////////////////////////////////////////////////////////////////////////////
/// @startDocuBlock JSF_foxx_BaseMiddleware_response_set
///
/// `response.set(key, value)`
///
/// Set a header attribute, for example:
///
/// @EXAMPLES
///
/// ```js
/// response.set("Content-Length", 123);
/// response.set("Content-Type", "text/plain");
/// ```
///
/// or alternatively:
///
/// ```js
/// response.set({
///   "Content-Length": "123",
///   "Content-Type": "text/plain"
/// });
/// ```
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

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

////////////////////////////////////////////////////////////////////////////////
/// @startDocuBlock JSF_foxx_BaseMiddleware_response_json
///
/// `response.json(object)`
///
/// Set the content type to JSON and the body to the JSON encoded *object*
/// you provided.
///
/// @EXAMPLES
///
/// ```js
/// response.json({'born': 'December 12, 1915'});
/// ```
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

      json: function (obj) {
        this.contentType = "application/json";
        this.body = JSON.stringify(obj);
      }
    };

////////////////////////////////////////////////////////////////////////////////
/// @fn JSF_foxx_BaseMiddleware_response_trace
/// @brief trace
////////////////////////////////////////////////////////////////////////////////

    trace = options.isDevelopment;
    if (!trace && options.hasOwnProperty("options")) {
      trace = options.options.trace;
    }

    if (trace) {
      console.log("%s, incoming request from %s: %s",
                  options.mount,
                  request.client.address,
                  actions.stringifyRequestAddress(request));
    }

    _.extend(request, requestFunctions);
    _.extend(response, responseFunctions);

    next();

    if (trace) {
      if (response.hasOwnProperty("body")) {
        var bodyLength = 0;
        if (response.body !== undefined) {
          if (typeof response.body === "string") {
            bodyLength = parseInt(response.body.length, 10);
          } else {
            try {
              bodyLength = String(response.body).length;
            } catch (err) {
              // ignore if this fails
            }
          }
        }
        console.log("%s, outgoing response with status %s of type %s, body length: %d",
                    options.mount,
                    response.responseCode,
                    response.contentType,
                    bodyLength);
      } else if (response.hasOwnProperty("bodyFromFile")) {
        console.log("%s, outgoing response with status %s of type %s, body file: %s",
                    options.mount,
                    response.responseCode,
                    response.contentType,
                    response.bodyFromFile);
      } else {
        console.log("%s, outgoing response with status %s of type %s, no body",
                    options.mount,
                    response.responseCode,
                    response.contentType);
      }
    }
  };

  return {
    stringRepresentation: String(middleware),
    functionRepresentation: middleware
  };
};

exports.BaseMiddleware = BaseMiddleware;

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

/// Local Variables:
/// mode: outline-minor
/// outline-regexp: "/// @brief\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\|/\\*jslint"
/// End:
