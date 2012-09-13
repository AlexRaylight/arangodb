////////////////////////////////////////////////////////////////////////////////
/// @brief abstract base request handler
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2010-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "RestVocbaseBaseHandler.h"

#include "Basics/StringUtils.h"
#include "BasicsC/conversions.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/strings.h"
#include "Rest/HttpRequest.h"
#include "ResultGenerator/OutputGenerator.h"
#include "ShapedJson/shaped-json.h"
#include "Variant/VariantArray.h"
#include "Variant/VariantBoolean.h"
#include "Variant/VariantInt32.h"
#include "Variant/VariantString.h"
#include "Variant/VariantUInt64.h"
#include "VocBase/document-collection.h"
#include "VocBase/simple-collection.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::rest;
using namespace triagens::arango;

// -----------------------------------------------------------------------------
// --SECTION--                                         REST_VOCBASE_BASE_HANDLER
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                    private macros
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief macro to check duplicate allocation of _response object, resulting in
/// memleaks
////////////////////////////////////////////////////////////////////////////////

#define CHECK_RESPONSE \
  if (_response != 0) { \
    LOG_WARNING("multi responses created in the same handler. potential memleak"); \
  }

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public constants
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief result RES-OK
////////////////////////////////////////////////////////////////////////////////

LoggerData::Extra const RestVocbaseBaseHandler::RES_OK;

////////////////////////////////////////////////////////////////////////////////
/// @brief result RES-ERR
////////////////////////////////////////////////////////////////////////////////

LoggerData::Extra const RestVocbaseBaseHandler::RES_ERR;

////////////////////////////////////////////////////////////////////////////////
/// @brief result RES-ERR
////////////////////////////////////////////////////////////////////////////////

LoggerData::Extra const RestVocbaseBaseHandler::RES_FAIL;

////////////////////////////////////////////////////////////////////////////////
/// @brief document path
////////////////////////////////////////////////////////////////////////////////

string RestVocbaseBaseHandler::DOCUMENT_PATH = "/_api/document";

////////////////////////////////////////////////////////////////////////////////
/// @brief document path
////////////////////////////////////////////////////////////////////////////////

string RestVocbaseBaseHandler::EDGE_PATH = "/_api/edge";

////////////////////////////////////////////////////////////////////////////////
/// @brief collection path
////////////////////////////////////////////////////////////////////////////////

string RestVocbaseBaseHandler::COLLECTION_PATH = "/_api/collection";

////////////////////////////////////////////////////////////////////////////////
/// @brief documents import path
////////////////////////////////////////////////////////////////////////////////

string RestVocbaseBaseHandler::DOCUMENT_IMPORT_PATH = "/_api/import";

////////////////////////////////////////////////////////////////////////////////
/// @brief batch path
////////////////////////////////////////////////////////////////////////////////

string RestVocbaseBaseHandler::BATCH_PATH = "/_api/batch";


////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

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

RestVocbaseBaseHandler::RestVocbaseBaseHandler (HttpRequest* request, TRI_vocbase_t* vocbase)
  : RestBaseHandler(request),
    _vocbase(vocbase),
    _timing(),
    _timingResult(RES_FAIL) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

RestVocbaseBaseHandler::~RestVocbaseBaseHandler () {
  LOGGER_REQUEST_IN_END_I(_timing) << _timingResult;
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
/// @brief generates ok message without content
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateOk () {
  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::NO_CONTENT);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates created message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateCreated (TRI_voc_cid_t cid, TRI_voc_did_t did, TRI_voc_rid_t rid) {
  string cidStr = StringUtils::itoa(cid);
  string didStr = StringUtils::itoa(did);
  string ridStr = StringUtils::itoa(rid);
  string handle = cidStr + "/" + didStr;

  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::CREATED);

  _response->setContentType("application/json; charset=utf-8");
  _response->setHeader("ETag", "\"" + ridStr + "\"");
  _response->setHeader("location", DOCUMENT_PATH + "/" + handle);

  _response->body()
    .appendText("{\"error\":false,\"_id\":\"")
    .appendText(handle.c_str())
    .appendText("\",\"_rev\":")
    .appendInteger(rid)
    .appendText("}");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates accepted message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateAccepted (TRI_voc_cid_t cid, TRI_voc_did_t did, TRI_voc_rid_t rid) {
  string cidStr = StringUtils::itoa(cid);
  string didStr = StringUtils::itoa(did);
  string ridStr = StringUtils::itoa(rid);
  string handle = cidStr + "/" + didStr;

  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::ACCEPTED);

  _response->setContentType("application/json; charset=utf-8");
  _response->setHeader("ETag", "\"" + ridStr + "\"");
  _response->setHeader("location", DOCUMENT_PATH + "/" + handle);

  _response->body()
    .appendText("{\"error\":false,\"_id\":\"")
    .appendText(handle.c_str())
    .appendText("\",\"_rev\":")
    .appendInteger(rid)
    .appendText("}");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates deleted message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateDeleted (TRI_voc_cid_t cid, TRI_voc_did_t did, TRI_voc_rid_t rid) {
  string cidStr = StringUtils::itoa(cid);
  string didStr = StringUtils::itoa(did);
  string ridStr = StringUtils::itoa(rid);
  string handle = cidStr + "/" + didStr;

  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::OK);

  _response->setContentType("application/json; charset=utf-8");

  _response->body()
    .appendText("{\"error\":false,\"_id\":\"")
    .appendText(handle.c_str())
    .appendText("\",\"_rev\":")
    .appendInteger(rid)
    .appendText("}");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates updated message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateUpdated (TRI_voc_cid_t cid, TRI_voc_did_t did, TRI_voc_rid_t rid) {
  string cidStr = StringUtils::itoa(cid);
  string didStr = StringUtils::itoa(did);
  string ridStr = StringUtils::itoa(rid);
  string handle = cidStr + "/" + didStr;

  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::OK);

  _response->setContentType("application/json; charset=utf-8");

  _response->body()
    .appendText("{\"error\":false,\"_id\":\"")
    .appendText(handle.c_str())
    .appendText("\",\"_rev\":")
    .appendInteger(rid)
    .appendText("}");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates document not found error message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateDocumentNotFound (TRI_voc_cid_t cid, string const& did) {
  string location = DOCUMENT_PATH + "/" + StringUtils::itoa(cid) + TRI_DOCUMENT_HANDLE_SEPARATOR_STR + did;

  generateError(HttpResponse::NOT_FOUND,
                TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND,
                "document " + location + " not found");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates conflict message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateConflict (string const& cid, string const& did) {
  generateError(HttpResponse::CONFLICT, 
                TRI_ERROR_ARANGO_CONFLICT,
                "document " + DOCUMENT_PATH + "/" + cid + "/" + did + " has been altered");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates not implemented
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateNotImplemented (string const& path) {
  generateError(HttpResponse::NOT_IMPLEMENTED, 
                TRI_ERROR_NOT_IMPLEMENTED,
                "'" + path + "' not implemented");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates forbidden
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateForbidden () {
  generateError(HttpResponse::FORBIDDEN, 
                TRI_ERROR_FORBIDDEN,
                "operation forbidden");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates precondition failed
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generatePreconditionFailed (TRI_voc_cid_t cid, TRI_voc_did_t did, TRI_voc_rid_t rid) {
  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::PRECONDITION_FAILED);

  VariantArray* result = new VariantArray();
  result->add("error", new VariantBoolean(true));
  result->add("code", new VariantInt32((int32_t) HttpResponse::PRECONDITION_FAILED));
  result->add("errorNum", new VariantInt32((int32_t) TRI_ERROR_ARANGO_CONFLICT));
  result->add("errorMessage", new VariantString("precondition failed"));
  result->add("_id", new VariantString(StringUtils::itoa(cid) + TRI_DOCUMENT_HANDLE_SEPARATOR_STR + StringUtils::itoa(did)));
  result->add("_rev", new VariantUInt64(rid));

  string contentType;
  bool ok = OutputGenerator::output(selectResultGenerator(_request), _response->body(), result, contentType);

  if (ok) {
    _response->setContentType(contentType);
  }
  else {
    delete _response;
    generateError(HttpResponse::SERVER_ERROR, TRI_ERROR_INTERNAL, "cannot generate response");
  }

  delete result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates not modified
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateNotModified (string const& etag) {
  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::NOT_MODIFIED);

  _response->setHeader("ETag", "\"" + etag + "\"");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates next entry from a result set
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateDocument (TRI_doc_mptr_t const* document,
                                               const TRI_voc_cid_t cid,
                                               TRI_shaper_t* shaper,
                                               const bool generateDocument) {
  if (document == 0) {
    generateError(HttpResponse::SERVER_ERROR, 
                  TRI_ERROR_INTERNAL,
                  "document pointer is null, should not happen");
    return;
  }

  // add document identifier to buffer
  TRI_string_buffer_t buffer;

  string id = StringUtils::itoa(cid) + TRI_DOCUMENT_HANDLE_SEPARATOR_STR + StringUtils::itoa(document->_did);

  TRI_json_t augmented;
  TRI_InitArrayJson(TRI_UNKNOWN_MEM_ZONE, &augmented);

  TRI_json_t* _id = TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, id.c_str());

  if (_id) {
    TRI_Insert2ArrayJson(TRI_UNKNOWN_MEM_ZONE, &augmented, "_id", _id);
  }

  TRI_json_t* _rev = TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, document->_rid);

  if (_rev) {
    TRI_Insert2ArrayJson(TRI_UNKNOWN_MEM_ZONE, &augmented, "_rev", _rev);
  }

  TRI_df_marker_type_t type = ((TRI_df_marker_t*) document->_data)->_type;

  if (type == TRI_DOC_MARKER_EDGE) {
    TRI_doc_edge_marker_t* marker = (TRI_doc_edge_marker_t*) document->_data;
    string from = StringUtils::itoa(marker->_fromCid) + TRI_DOCUMENT_HANDLE_SEPARATOR_STR + StringUtils::itoa(marker->_fromDid);
    string to = StringUtils::itoa(marker->_toCid) + TRI_DOCUMENT_HANDLE_SEPARATOR_STR + StringUtils::itoa(marker->_toDid);

    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, &augmented, "_from", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, from.c_str()));
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, &augmented, "_to", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, to.c_str()));
  }

  // convert object to string
  TRI_InitStringBuffer(&buffer, TRI_UNKNOWN_MEM_ZONE);

  TRI_shaped_json_t shapedJson;
  TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->_data);
  TRI_StringifyAugmentedShapedJson(shaper, &buffer, &shapedJson, &augmented);

  TRI_DestroyJson(TRI_UNKNOWN_MEM_ZONE, &augmented);

  if (_id) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, _id);
  }

  if (_rev) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, _rev);
  }

  // and generate a response
  CHECK_RESPONSE
  _response = new HttpResponse(HttpResponse::OK);
  _response->setContentType("application/json; charset=utf-8");
  _response->setHeader("ETag", "\"" + StringUtils::itoa(document->_rid) + "\"");

  if (generateDocument) {
    _response->body().appendText(TRI_BeginStringBuffer(&buffer), TRI_LengthStringBuffer(&buffer));
  }
  else {
    _response->headResponse(TRI_LengthStringBuffer(&buffer));
  }

  TRI_AnnihilateStringBuffer(&buffer);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates collection not found error message
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateCollectionNotFound (string const& cid) {
  generateError(HttpResponse::NOT_FOUND, 
                TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND,
                "collection " + COLLECTION_PATH + "/" + cid + " not found");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generate an approriate error message for the collection-related 
/// error that occurred
////////////////////////////////////////////////////////////////////////////////

void RestVocbaseBaseHandler::generateCollectionError (const string& collection, 
                                                      const int res) {
  if (TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND == res) {
    if (collection.empty()) {
      // no collection name specified
      generateError(HttpResponse::BAD, TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND, "no collection name specified"); 
    }
    else {
      // collection name specified but collection not found
      generateCollectionNotFound(collection);
    }

    return;
  }

  // other errors
  generateError(HttpResponse::SERVER_ERROR, res);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts the revision
////////////////////////////////////////////////////////////////////////////////

TRI_voc_rid_t RestVocbaseBaseHandler::extractRevision (char const* header, char const* parameter) {
  bool found;
  char const* etag = _request->header(header, found);

  if (found) {
    char const* s = etag;
    char const* e = etag + strlen(etag);

    while (s < e && (s[0] == ' ' || s[0] == '\t')) {
      ++s;
    }

    while (s < e && (e[-1] == ' ' || e[-1] == '\t')) {
      --e;
    }

    if (s + 1 < e && s[0] == '"' && e[-1] == '"') {
      return TRI_UInt64String2(s + 1, e - s - 2);
    }
    else {
      return 0;
    }
  }

  if (parameter == 0) {
    return 0;
  }
  else {
    etag = _request->value(parameter, found);

    if (found) {
      return TRI_UInt64String(etag);
    }
    else {
      return 0;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts the update policy
////////////////////////////////////////////////////////////////////////////////

TRI_doc_update_policy_e RestVocbaseBaseHandler::extractUpdatePolicy () {
  bool found;
  char const* policy = _request->value("policy", found);

  if (found) {
    if (TRI_CaseEqualString(policy, "error")) {
      return TRI_DOC_UPDATE_ERROR;
    }
    else if (TRI_CaseEqualString(policy, "last")) {
      return TRI_DOC_UPDATE_LAST_WRITE;
    }
    else {
      return TRI_DOC_UPDATE_ILLEGAL;
    }
  }
  else {
    return TRI_DOC_UPDATE_ERROR;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parses the body
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* RestVocbaseBaseHandler::parseJsonBody () {
  char* errmsg = 0;
  TRI_json_t* json = TRI_Json2String(TRI_UNKNOWN_MEM_ZONE, _request->body(), &errmsg);

  if (json == 0) {
    if (errmsg == 0) {
      generateError(HttpResponse::BAD, 
                    TRI_ERROR_HTTP_CORRUPTED_JSON,
                    "cannot parse json object");
    }
    else {
      generateError(HttpResponse::BAD, 
                    TRI_ERROR_HTTP_CORRUPTED_JSON,
                    errmsg);

      TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, errmsg);
    }

    return 0;
  }

  if (TRI_HasDuplicateKeyJson(json)) {
    generateError(HttpResponse::BAD, 
                  TRI_ERROR_HTTP_CORRUPTED_JSON,
                  "cannot parse json object");
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return 0;
  }

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parses a document handle
////////////////////////////////////////////////////////////////////////////////

int RestVocbaseBaseHandler::parseDocumentId (string const& handle,
                                             TRI_voc_cid_t& cid,
                                             TRI_voc_did_t& did) {
  vector<string> split;
  int res;

  split = StringUtils::split(handle, '/');

  if (split.size() != 2) {
    return TRI_set_errno(TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  cid = TRI_UInt64String(split[0].c_str());
  res = TRI_errno();

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  did = TRI_UInt64String(split[1].c_str());

  return TRI_errno();
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                           HANDLER
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

bool RestVocbaseBaseHandler::isDirect () {
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
