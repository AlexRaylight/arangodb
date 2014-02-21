////////////////////////////////////////////////////////////////////////////////
/// @brief document request handler
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triAGENS GmbH, Cologne, Germany
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
/// @author Copyright 2010-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "RestEdgeHandler.h"

#include "Basics/StringUtils.h"
#include "BasicsC/conversions.h"
#include "BasicsC/tri-strings.h"
#include "Rest/HttpRequest.h"
#include "VocBase/document-collection.h"
#include "VocBase/edge-collection.h"

#ifdef TRI_ENABLE_CLUSTER
#include "Cluster/ServerState.h"
#include "Cluster/ClusterInfo.h"
#include "Cluster/ClusterMethods.h"
#endif

using namespace std;
using namespace triagens::basics;
using namespace triagens::rest;
using namespace triagens::arango;

////////////////////////////////////////////////////////////////////////////////
/// @brief free a string if defined, nop otherwise
////////////////////////////////////////////////////////////////////////////////

#define FREE_STRING(zone, what)                                           \
  if (what != 0) {                                                        \
    TRI_Free(zone, what);                                                 \
    what = 0;                                                             \
  }
// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

RestEdgeHandler::RestEdgeHandler (HttpRequest* request)  
  : RestDocumentHandler(request) {
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 protected methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an edge
///
/// @RESTHEADER{POST /_api/edge,creates an edge}
///
/// @RESTBODYPARAM{edge-document,json,required}
/// A JSON representation of the edge document must be passed as the body of
/// the POST request. This JSON object may contain the edge's document key in
/// the `_key` attribute if needed.
///
/// @RESTQUERYPARAMETERS
///
/// @RESTQUERYPARAM{collection,string,required}
/// Creates a new edge in the collection identified by `collection` name.
///
/// @RESTQUERYPARAM{createCollection,boolean,optional}
/// If this parameter has a value of `true` or `yes`, then the collection is
/// created if it does not yet exist. Other values will be ignored so the
/// collection must be present for the operation to succeed.
///
/// Note: this flag is not supported in a cluster. Using it will result in an
/// error.
///
/// @RESTQUERYPARAM{waitForSync,boolean,optional}
/// Wait until the edge document has been synced to disk.
///
/// @RESTQUERYPARAM{from,string,required}
/// The document handle of the start point must be passed in `from` handle.
///
/// @RESTQUERYPARAM{to,string,required}
/// The document handle of the end point must be passed in `to` handle.
///
/// @RESTDESCRIPTION
/// Creates a new edge document in the collection named `collection`. A JSON
/// representation of the document must be passed as the body of the POST
/// request.
///
/// The `from` and `to` handles are immutable once the edge has been created.
///
/// In all other respects the method works like `POST /document`, see
/// @ref RestDocument for details.
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{201}
/// is returned if the edge was created successfully and `waitForSync` was
/// `true`.
///
/// @RESTRETURNCODE{202}
/// is returned if the edge was created successfully.
///
/// @RESTRETURNCODE{400}
/// is returned if the body does not contain a valid JSON representation of an
/// edge, or if the collection specified is not an edge collection. 
/// The response body contains an error document in this case.
///
/// @RESTRETURNCODE{404}
/// is returned if the collection specified by `collection` is unknown.  The
/// response body contains an error document in this case.
///
/// @EXAMPLES
///
/// Create an edge and read it back:
///
/// @EXAMPLE_ARANGOSH_RUN{RestEdgeCreateEdge}
///     var Graph = require("org/arangodb/graph").Graph;
///     var g = new Graph("graph", "vertices", "edges");
///     g.addVertex(1);
///     g.addVertex(2);
///     var url = "/_api/edge/?collection=edges&from=vertices/1&to=vertices/2";
/// 
///     var response = logCurlRequest("POST", url, { "name": "Emil" });
/// 
///     assert(response.code === 202);
///
///     logJsonResponse(response);
///     var body = response.body.replace(/\\/g, '');
///     var edge_id = JSON.parse(body)._id;
///     var response2 = logCurlRequest("GET", "/_api/edge/" + edge_id);
/// 
///     assert(response2.code === 200);
///
///     logJsonResponse(response2);
///     db._drop("edges");
///     db._drop("vertices");
///     db._graphs.remove("graph");
/// @END_EXAMPLE_ARANGOSH_RUN
////////////////////////////////////////////////////////////////////////////////

bool RestEdgeHandler::createDocument () {
  vector<string> const& suffix = _request->suffix();

  if (suffix.size() != 0) {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_SUPERFLUOUS_SUFFICES,
                  "superfluous suffix, expecting " + EDGE_PATH + "?collection=<identifier>");
    return false;
  }

  // extract the from
  bool found;
  char const* from = _request->value("from", found);
  if (! found || *from == '\0') {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_BAD_PARAMETER,
                  "'from' is missing, expecting " + EDGE_PATH + "?collection=<identifier>&from=<from-handle>&to=<to-handle>");
    return false;
  }

  // extract the to
  char const* to = _request->value("to", found);

  if (! found || *to == '\0') {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_HTTP_BAD_PARAMETER,
                  "'to' is missing, expecting " + EDGE_PATH + "?collection=<identifier>&from=<from-handle>&to=<to-handle>");
    return false;
  }

  // extract the cid
  const string& collection = _request->value("collection", found);

  if (! found || collection.empty()) {
    generateError(HttpResponse::BAD,
                  TRI_ERROR_ARANGO_COLLECTION_PARAMETER_MISSING,
                  "'collection' is missing, expecting " + DOCUMENT_PATH + "?collection=<identifier>");
    return false;
  }

  const bool waitForSync = extractWaitForSync();

  TRI_json_t* json = parseJsonBody();

  if (! TRI_IsArrayJson(json)) {
    if (json != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }

    generateTransactionError(collection, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
    return false;
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    // json will be freed inside!
    return createDocumentCoordinator(collection, waitForSync, json,
                                     from, to);
  }
#endif

  if (! checkCreateCollection(collection, getCollectionType())) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return false;
  }

  // find and load collection given by name or identifier
  SingleCollectionWriteTransaction<StandaloneTransaction<RestTransactionContext>, 1> trx(_vocbase, _resolver, collection);

  // .............................................................................
  // inside write transaction
  // .............................................................................

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    generateTransactionError(collection, res);
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return false;
  }
  
  if (trx.primaryCollection()->base._info._type != TRI_COL_TYPE_EDGE) {
    // check if we are inserting with the EDGE handler into a non-EDGE collection    
    generateError(HttpResponse::BAD, TRI_ERROR_ARANGO_COLLECTION_TYPE_INVALID);
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return false;
  }

  const TRI_voc_cid_t cid = trx.cid();

  // edge
  TRI_document_edge_t edge;
  edge._fromCid = cid;
  edge._toCid   = cid;
  edge._fromKey = 0;
  edge._toKey   = 0;

  string wrongPart;
  // Note that in a DBserver in a cluster, the following call will
  // actually parse the first part of `from` as a cluster-wide
  // collection name, exactly as it is needed here!
  res = parseDocumentId(from, edge._fromCid, edge._fromKey);

  if (res != TRI_ERROR_NO_ERROR) {
    wrongPart = "'from'";
  }
  else {
    // Note that in a DBserver in a cluster, the following call will
    // actually parse the first part of `from` as a cluster-wide
    // collection name, exactly as it is needed here!
    res = parseDocumentId(to, edge._toCid, edge._toKey);
    if (res != TRI_ERROR_NO_ERROR) {
      wrongPart = "'to'";
    }
  }

  if (res != TRI_ERROR_NO_ERROR) {
    FREE_STRING(TRI_CORE_MEM_ZONE, edge._fromKey);
    FREE_STRING(TRI_CORE_MEM_ZONE, edge._toKey);
    
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

    if (res == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
      generateError(HttpResponse::NOT_FOUND, res, wrongPart + " does not point to a valid collection");
    }
    else {
      generateError(HttpResponse::BAD, res, wrongPart + " is not a document handle");
    }
    return false;
  }

  // .............................................................................
  // inside write transaction
  // .............................................................................

  // will hold the result
  TRI_doc_mptr_t document;
  res = trx.createEdge(&document, json, waitForSync, &edge);
  const bool wasSynchronous = trx.synchronous();
  res = trx.finish(res);
  
  FREE_STRING(TRI_CORE_MEM_ZONE, edge._fromKey);
  FREE_STRING(TRI_CORE_MEM_ZONE, edge._toKey);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  // .............................................................................
  // outside write transaction
  // .............................................................................

  if (res != TRI_ERROR_NO_ERROR) {
    generateTransactionError(collection, res);
    return false;
  }

  assert(document._key != 0);

  // generate result
  if (wasSynchronous) {
    generateCreated(cid, document._key, document._rid);
  }
  else {
    generateAccepted(cid, document._key, document._rid);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a document (an edge), coordinator case in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
bool RestEdgeHandler::createDocumentCoordinator (string const& collname,
                                                 bool waitForSync,
                                                 TRI_json_t* json,
                                                 char const* from,
                                                 char const* to) {
  string const& dbname = _request->databaseName();

  triagens::rest::HttpResponse::HttpResponseCode responseCode;
  map<string, string> resultHeaders;
  string resultBody;

  int error = triagens::arango::createEdgeOnCoordinator(
            dbname, collname, waitForSync, json, from, to,
            responseCode, resultHeaders, resultBody);

  if (error != TRI_ERROR_NO_ERROR) {
    generateTransactionError(collname.c_str(), error);
    return false;
  }
  // Essentially return the response we got from the DBserver, be it
  // OK or an error:
  _response = createResponse(responseCode);
  triagens::arango::mergeResponseHeaders(_response, resultHeaders);
  _response->body().appendText(resultBody.c_str(), resultBody.size());
  return responseCode >= triagens::rest::HttpResponse::BAD;
}
#endif


////////////////////////////////////////////////////////////////////////////////
/// @brief reads a single edge
///
/// @RESTHEADER{GET /_api/edge/`document-handle`,reads an edge}
///
/// @RESTURLPARAMETERS
///
/// @RESTURLPARAM{document-handle,string,required}
/// The handle of the edge document.
///
/// @RESTHEADERPARAMETERS
///
/// @RESTHEADERPARAM{If-None-Match,string,optional}
/// If the "If-None-Match" header is given, then it must contain exactly one
/// etag. The edge is returned if it has a different revision than the
/// given etag. Otherwise an `HTTP 304` is returned.
///
/// @RESTHEADERPARAM{If-Match,string,optional}
/// If the "If-Match" header is given, then it must contain exactly one
/// etag. The edge is returned if it has the same revision ad the
/// given etag. Otherwise a `HTTP 412` is returned. As an alternative
/// you can supply the etag in an attribute `rev` in the URL.
///
/// @RESTDESCRIPTION
/// Returns the edge identified by `document-handle`. The returned
/// edge contains a few special attributes: 
///
/// - `_id` contains the document handle
///
/// - `_rev` contains the revision
///
/// - `_from` and `to` contain the document handles of the connected 
///   vertex documents
///
/// @RESTRETURNCODES
/// 
/// @RESTRETURNCODE{200}
/// is returned if the edge was found
///
/// @RESTRETURNCODE{304}
/// is returned if the "If-None-Match" header is given and the edge has 
/// the same version
///
/// @RESTRETURNCODE{404}
/// is returned if the edge or collection was not found
///
/// @RESTRETURNCODE{412}
/// is returned if a "If-Match" header or `rev` is given and the found
/// document has a different version. The response will also contain the found
/// document's current revision in the `_rev` attribute. Additionally, the 
/// attributes `_id` and `_key` will be returned.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief reads all edges from collection
///
/// @RESTHEADER{GET /_api/edge,reads all edges from collection}
///
/// @RESTQUERYPARAMETERS
///
/// @RESTQUERYPARAM{collection,string,required}
/// The name of the collection.
///
/// @RESTDESCRIPTION
/// Returns a list of all URI for all edges from the collection identified
/// by `collection`.
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{200}
/// All went good.
///
/// @RESTRETURNCODE{404}
/// The collection does not exist.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief reads a single edge head
///
/// @RESTHEADER{HEAD /_api/edge/`document-handle`,reads an edge header}
///
/// @RESTURLPARAMETERS
///
/// @RESTURLPARAM{document-handle,string,required}
/// The handle of the edge document.
///
/// @RESTQUERYPARAMETERS
///
/// @RESTQUERYPARAM{rev,string,optional}
/// You can conditionally fetch an edge document based on a target revision id by
/// using the `rev` URL parameter.
/// 
/// @RESTHEADERPARAMETERS
///
/// @RESTHEADERPARAM{If-Match,string,optional}
/// You can conditionally fetch an edge document based on a target revision id by
/// using the `if-match` HTTP header.
/// 
/// @RESTDESCRIPTION
/// Like `GET`, but only returns the header fields and not the body. You
/// can use this call to get the current revision of an edge document or check if
/// it was deleted.
///
/// @RESTRETURNCODES
/// 
/// @RESTRETURNCODE{200}
/// is returned if the edge document was found
///
/// @RESTRETURNCODE{304}
/// is returned if the "If-None-Match" header is given and the edge document has
/// same version
///
/// @RESTRETURNCODE{404}
/// is returned if the edge document or collection was not found
///
/// @RESTRETURNCODE{412}
/// is returned if a "If-Match" header or `rev` is given and the found
/// document has a different version. The response will also contain the found
/// document's current revision in the `etag` header.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief replaces an edge
///
/// @RESTHEADER{PUT /_api/edge/`document-handle`,replaces an edge}
///
/// @RESTBODYPARAM{edge,json,required}
/// A JSON representation of the new edge data.
///
/// @RESTURLPARAMETERS
///
/// @RESTURLPARAM{document-handle,string,required}
/// The handle of the edge document.
/// 
/// @RESTQUERYPARAMETERS
///
/// @RESTQUERYPARAM{waitForSync,boolean,optional}
/// Wait until edge document has been synced to disk.
///
/// @RESTQUERYPARAM{rev,string,optional}
/// You can conditionally replace an edge document based on a target revision id by
/// using the `rev` URL parameter.
/// 
/// @RESTQUERYPARAM{policy,string,optional}
/// To control the update behavior in case there is a revision mismatch, you
/// can use the `policy` parameter (see below).
///
/// @RESTHEADERPARAMETERS
///
/// @RESTHEADERPARAM{If-Match,string,optional}
/// You can conditionally replace an edge document based on a target revision id by
/// using the `if-match` HTTP header.
/// 
/// @RESTDESCRIPTION
/// Completely updates (i.e. replaces) the edge document identified by `document-handle`.
/// If the edge document exists and can be updated, then a `HTTP 201` is returned
/// and the "ETag" header field contains the new revision of the edge document.
///
/// If the new edge document passed in the body of the request contains the
/// `document-handle` in the attribute `_id` and the revision in `_rev`,
/// these attributes will be ignored. Only the URI and the "ETag" header are
/// relevant in order to avoid confusion when using proxies. Note that the attributes
/// `_from` and `_to` of an edge are immutable and cannot be updated either.
///
/// Optionally, the URL parameter `waitForSync` can be used to force
/// synchronisation of the edge document replacement operation to disk even in case
/// that the `waitForSync` flag had been disabled for the entire collection.
/// Thus, the `waitForSync` URL parameter can be used to force synchronisation
/// of just specific operations. To use this, set the `waitForSync` parameter
/// to `true`. If the `waitForSync` parameter is not specified or set to
/// `false`, then the collection's default `waitForSync` behavior is
/// applied. The `waitForSync` URL parameter cannot be used to disable
/// synchronisation for collections that have a default `waitForSync` value
/// of `true`.
///
/// The body of the response contains a JSON object with the information about
/// the handle and the revision.  The attribute `_id` contains the known
/// `document-handle` of the updated edge document, the attribute `_rev`
/// contains the new revision of the edge document.
///
/// If the edge document does not exist, then a `HTTP 404` is returned and the
/// body of the response contains an error document.
///
/// There are two ways for specifying the targeted revision id for
/// conditional replacements (i.e. replacements that will only be executed if
/// the revision id found in the database matches the revision id specified
/// in the request):
/// - specifying the target revision in the `rev` URL query parameter
/// - specifying the target revision in the `if-match` HTTP header
///
/// Specifying a target revision is optional, however, if done, only one of the
/// described mechanisms must be used (either the `rev` URL parameter or the
/// `if-match` HTTP header).
/// Regardless which mechanism is used, the parameter needs to contain the target
/// revision id as returned in the `_rev` attribute of an edge document or
/// by an HTTP `etag` header.
///
/// For example, to conditionally replace an edge document based on a specific revision
/// id, you can use the following request:
/// 
/// - PUT /_api/document/`document-handle`?rev=`etag`
///
/// If a target revision id is provided in the request (e.g. via the `etag` value
/// in the `rev` URL query parameter above), ArangoDB will check that
/// the revision id of the edge document found in the database is equal to the target
/// revision id provided in the request. If there is a mismatch between the revision
/// id, then by default a `HTTP 412` conflict is returned and no replacement is
/// performed.
///
/// The conditional update behavior can be overriden with the `policy` URL query parameter:
///
/// - PUT /_api/document/`document-handle`?policy=`policy`
///
/// If `policy` is set to `error`, then the behavior is as before: replacements
/// will fail if the revision id found in the database does not match the target
/// revision id specified in the request.
///
/// If `policy` is set to `last`, then the replacement will succeed, even if the
/// revision id found in the database does not match the target revision id specified
/// in the request. You can use the `last` `policy` to force replacements.
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{201}
/// is returned if the edge document was replaced successfully and `waitForSync` was
/// `true`.
///
/// @RESTRETURNCODE{202}
/// is returned if the edge document was replaced successfully and `waitForSync` was
/// `false`.
///
/// @RESTRETURNCODE{400}
/// is returned if the body does not contain a valid JSON representation of an edge
/// document or if applied to a non-edge collection. The response body contains an 
/// error document in this case.
///
/// @RESTRETURNCODE{404}
/// is returned if the collection or the edge document was not found
///
/// @RESTRETURNCODE{412}
/// is returned if a "If-Match" header or `rev` is given and the found
/// document has a different version. The response will also contain the found
/// document's current revision in the `_rev` attribute. Additionally, the 
/// attributes `_id` and `_key` will be returned.
///////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief updates an edge
///
/// @RESTHEADER{PATCH /_api/edge/`document-handle`,patches an edge}
///
/// @RESTBODYPARAM{document,json,required}
/// A JSON representation of the edge update.
///
/// @RESTURLPARAMETERS
///
/// @RESTURLPARAM{document-handle,string,required}
/// The handle of the edge document.
///
/// @RESTQUERYPARAMETERS
///
/// @RESTQUERYPARAM{keepNull,boolean,optional}
/// If the intention is to delete existing attributes with the patch command, 
/// the URL query parameter `keepNull` can be used with a value of `false`.
/// This will modify the behavior of the patch command to remove any attributes
/// from the existing edge document that are contained in the patch document with an
/// attribute value of `null`.
///
/// @RESTQUERYPARAM{waitForSync,boolean,optional}
/// Wait until edge document has been synced to disk.
///
/// @RESTQUERYPARAM{rev,string,optional}
/// You can conditionally patch an edge document based on a target revision id by
/// using the `rev` URL parameter.
/// 
/// @RESTQUERYPARAM{policy,string,optional}
/// To control the update behavior in case there is a revision mismatch, you
/// can use the `policy` parameter.
///
/// @RESTHEADERPARAMETERS
///
/// @RESTHEADERPARAM{If-Match,string,optional}
/// You can conditionally patch an edge document based on a target revision id by
/// using the `if-match` HTTP header.
/// 
/// @RESTDESCRIPTION
/// Partially updates the edge document identified by `document-handle`.
/// The body of the request must contain a JSON document with the attributes
/// to patch (the patch document). All attributes from the patch document will
/// be added to the existing edge document if they do not yet exist, and overwritten
/// in the existing edge document if they do exist there.
///
/// Setting an attribute value to `null` in the patch document will cause a
/// value of `null` be saved for the attribute by default. 
///
/// Note that internal attributes such as `_key`, `_from` and `_to` are immutable
/// once set and cannot be updated.
///
/// Optionally, the URL parameter `waitForSync` can be used to force
/// synchronisation of the edge document update operation to disk even in case
/// that the `waitForSync` flag had been disabled for the entire collection.
/// Thus, the `waitForSync` URL parameter can be used to force synchronisation
/// of just specific operations. To use this, set the `waitForSync` parameter
/// to `true`. If the `waitForSync` parameter is not specified or set to
/// `false`, then the collection's default `waitForSync` behavior is
/// applied. The `waitForSync` URL parameter cannot be used to disable
/// synchronisation for collections that have a default `waitForSync` value
/// of `true`.
///
/// The body of the response contains a JSON object with the information about
/// the handle and the revision. The attribute `_id` contains the known
/// `document-handle` of the updated edge document, the attribute `_rev`
/// contains the new edge document revision.
///
/// If the edge document does not exist, then a `HTTP 404` is returned and the
/// body of the response contains an error document.
///
/// You can conditionally update an edge document based on a target revision id by
/// using either the `rev` URL parameter or the `if-match` HTTP header.
/// To control the update behavior in case there is a revision mismatch, you
/// can use the `policy` parameter. This is the same as when replacing
/// edge documents (see replacing documents for details).
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{201}
/// is returned if the document was patched successfully and `waitForSync` was
/// `true`.
///
/// @RESTRETURNCODE{202}
/// is returned if the document was patched successfully and `waitForSync` was
/// `false`.
///
/// @RESTRETURNCODE{400}
/// is returned if the body does not contain a valid JSON representation or when
/// applied on an non-edge collection. The response body contains an error document 
/// in this case.
///
/// @RESTRETURNCODE{404}
/// is returned if the collection or the edge document was not found
///
/// @RESTRETURNCODE{412}
/// is returned if a "If-Match" header or `rev` is given and the found
/// document has a different version. The response will also contain the found
/// document's current revision in the `_rev` attribute. Additionally, the 
/// attributes `_id` and `_key` will be returned.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes an edge
///
/// @RESTHEADER{DELETE /_api/edge/`document-handle`,deletes an edge}
///
/// @RESTURLPARAMETERS
///
/// @RESTURLPARAM{document-handle,string,required}
/// Deletes the edge document identified by `document-handle`. 
/// 
/// @RESTQUERYPARAMETERS
///
/// @RESTQUERYPARAM{rev,string,optional}
/// You can conditionally delete an edge document based on a target revision id by
/// using the `rev` URL parameter.
/// 
/// @RESTQUERYPARAM{policy,string,optional}
/// To control the update behavior in case there is a revision mismatch, you
/// can use the `policy` parameter. This is the same as when replacing edge
/// documents (see replacing edge documents for more details).
///
/// @RESTQUERYPARAM{waitForSync,boolean,optional}
/// Wait until edge document has been synced to disk.
///
/// @RESTHEADERPARAMETERS
///
/// @RESTHEADERPARAM{If-Match,string,optional}
/// You can conditionally delete an edge document based on a target revision id by
/// using the `if-match` HTTP header.
/// 
/// @RESTDESCRIPTION
/// The body of the response contains a JSON object with the information about
/// the handle and the revision.  The attribute `_id` contains the known
/// `document-handle` of the deleted edge document, the attribute `_rev`
/// contains the edge document revision.
///
/// If the `waitForSync` parameter is not specified or set to
/// `false`, then the collection's default `waitForSync` behavior is
/// applied. The `waitForSync` URL parameter cannot be used to disable
/// synchronisation for collections that have a default `waitForSync` value
/// of `true`.
///
/// @RESTRETURNCODES
///
/// @RESTRETURNCODE{200}
/// is returned if the edge document was deleted successfully and `waitForSync` was
/// `true`.
///
/// @RESTRETURNCODE{202}
/// is returned if the edge document was deleted successfully and `waitForSync` was
/// `false`.
///
/// @RESTRETURNCODE{404}
/// is returned if the collection or the edge document was not found.
/// The response body contains an error document in this case.
///
/// @RESTRETURNCODE{412}
/// is returned if a "If-Match" header or `rev` is given and the found
/// document has a different version. The response will also contain the found
/// document's current revision in the `_rev` attribute. Additionally, the 
/// attributes `_id` and `_key` will be returned.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
