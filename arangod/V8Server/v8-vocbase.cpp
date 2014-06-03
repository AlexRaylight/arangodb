////////////////////////////////////////////////////////////////////////////////
/// @brief V8-vocbase bridge
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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "v8-vocbase.h"

#include "BasicsC/common.h"

#include "Ahuacatl/ahuacatl-codegen.h"
#include "Ahuacatl/ahuacatl-collections.h"
#include "Ahuacatl/ahuacatl-context.h"
#include "Ahuacatl/ahuacatl-explain.h"
#include "Ahuacatl/ahuacatl-result.h"
#include "Basics/StringUtils.h"
#include "Basics/Utf8Helper.h"
#include "BasicsC/conversions.h"
#include "BasicsC/files.h"
#include "BasicsC/json.h"
#include "BasicsC/json-utilities.h"
#include "BasicsC/logging.h"
#include "BasicsC/tri-strings.h"
#include "CapConstraint/cap-constraint.h"
#include "FulltextIndex/fulltext-index.h"
#include "HttpServer/ApplicationEndpointServer.h"
#include "Replication/InitialSyncer.h"
#include "Rest/SslInterface.h"
#include "ShapedJson/shape-accessor.h"
#include "ShapedJson/shaped-json.h"
#include "Utils/AhuacatlGuard.h"
#include "Utils/AhuacatlTransaction.h"
#include "Utils/Barrier.h"
#include "Utils/CollectionNameResolver.h"
#include "Utils/DocumentHelper.h"
#include "Utils/EmbeddableTransaction.h"
#include "Utils/ExplicitTransaction.h"
#include "Utils/SingleCollectionReadOnlyTransaction.h"
#include "Utils/SingleCollectionWriteTransaction.h"
#include "Utils/StandaloneTransaction.h"
#include "Utils/V8TransactionContext.h"
#include "V8/v8-conv.h"
#include "V8/v8-execution.h"
#include "V8/v8-utils.h"
#include "VocBase/auth.h"
#include "VocBase/datafile.h"
#include "VocBase/document-collection.h"
#include "VocBase/edge-collection.h"
#include "VocBase/general-cursor.h"
#include "VocBase/key-generator.h"
#include "VocBase/replication-applier.h"
#include "VocBase/replication-logger.h"
#include "VocBase/server.h"
#include "VocBase/voc-shaper.h"
#include "VocBase/index.h"
#include "v8.h"
#include "V8/JSLoader.h"

#ifdef TRI_ENABLE_CLUSTER
#include "Basics/JsonHelper.h"
#include "Cluster/AgencyComm.h"
#include "Cluster/ClusterComm.h"
#include "Cluster/ClusterInfo.h"
#include "Cluster/ClusterMethods.h"
#include "Cluster/ServerState.h"
#endif

#include "unicode/timezone.h"
#include "unicode/utypes.h"
#include "unicode/datefmt.h"
#include "unicode/smpdtfmt.h"
#include "unicode/dtfmtsym.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::arango;
using namespace triagens::rest;

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

static v8::Handle<v8::Value> WrapGeneralCursor (void* cursor);

// -----------------------------------------------------------------------------
// --SECTION--                                                   private defines
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut for read-only transaction class type
////////////////////////////////////////////////////////////////////////////////

#define ReadTransactionType SingleCollectionReadOnlyTransaction<EmbeddableTransaction<V8TransactionContext> >

////////////////////////////////////////////////////////////////////////////////
/// @brief macro to make sure we won't continue if we are inside a transaction
////////////////////////////////////////////////////////////////////////////////

#define PREVENT_EMBEDDED_TRANSACTION(scope)                               \
  if (V8TransactionContext::isEmbedded()) {                               \
    TRI_V8_EXCEPTION(scope, TRI_ERROR_TRANSACTION_DISALLOWED_OPERATION);  \
  }

////////////////////////////////////////////////////////////////////////////////
/// @brief free a string if defined, nop otherwise
////////////////////////////////////////////////////////////////////////////////

#define FREE_STRING(zone, what)                                           \
  if (what != 0) {                                                        \
    TRI_Free(zone, what);                                                 \
    what = 0;                                                             \
  }

// -----------------------------------------------------------------------------
// --SECTION--                                                 private constants
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief slot for a "barrier"
////////////////////////////////////////////////////////////////////////////////

static int const SLOT_BARRIER = 2;

////////////////////////////////////////////////////////////////////////////////
/// @brief slot for a "collection"
////////////////////////////////////////////////////////////////////////////////

static int const SLOT_COLLECTION = 2;

////////////////////////////////////////////////////////////////////////////////
/// @brief wrapped class for TRI_vocbase_t
///
/// Layout:
/// - SLOT_CLASS_TYPE
/// - SLOT_CLASS
////////////////////////////////////////////////////////////////////////////////

static int32_t const WRP_VOCBASE_TYPE = 1;

////////////////////////////////////////////////////////////////////////////////
/// @brief wrapped class for TRI_vocbase_col_t
///
/// Layout:
/// - SLOT_CLASS_TYPE
/// - SLOT_CLASS
/// - SLOT_COLLECTION
////////////////////////////////////////////////////////////////////////////////

static int32_t const WRP_VOCBASE_COL_TYPE = 2;

////////////////////////////////////////////////////////////////////////////////
/// @brief wrapped class for general cursors
///
/// Layout:
/// - SLOT_CLASS_TYPE
/// - SLOT_CLASS
////////////////////////////////////////////////////////////////////////////////

static int32_t const WRP_GENERAL_CURSOR_TYPE = 3;

////////////////////////////////////////////////////////////////////////////////
/// @brief wrapped class for TRI_shaped_json_t
///
/// Layout:
/// - SLOT_CLASS_TYPE
/// - SLOT_CLASS
/// - SLOT_BARRIER
////////////////////////////////////////////////////////////////////////////////

static int32_t const WRP_SHAPED_JSON_TYPE = 4;

// -----------------------------------------------------------------------------
// --SECTION--                                                  HELPER FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief cluster coordinator case, parse a key and possible revision
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static int ParseKeyAndRef (v8::Handle<v8::Value> const arg, 
                           string& key,
                           TRI_voc_rid_t& rev) {
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  rev = 0;
  if (arg->IsString()) {
    key = TRI_ObjectToString(arg);
  }
  else if (arg->IsObject()) {
    v8::Handle<v8::Object> obj = v8::Handle<v8::Object>::Cast(arg);

    if (obj->Has(v8g->_KeyKey) && obj->Get(v8g->_KeyKey)->IsString()) {
      key = TRI_ObjectToString(obj->Get(v8g->_KeyKey));
    }
    else if (obj->Has(v8g->_IdKey) && obj->Get(v8g->_IdKey)->IsString()) {
      key = TRI_ObjectToString(obj->Get(v8g->_IdKey));
      // part after / will be taken below
    }
    else {
      return TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID;
    }
    if (obj->Has(v8g->_RevKey) && obj->Get(v8g->_RevKey)->IsString()) {
      rev = TRI_ObjectToUInt64(obj->Get(v8g->_RevKey), true);
    }
  }
  else {
    return TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID;
  }

  size_t pos = key.find('/');
  if (pos != string::npos) {
    key = key.substr(pos+1);
  }
  return TRI_ERROR_NO_ERROR;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief free a coordinator collection
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static void FreeCoordinatorCollection (TRI_vocbase_col_t* collection) {
  TRI_DestroyReadWriteLock(&collection->_lock); 
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief convert a collection info into a TRI_vocbase_col_t
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static TRI_vocbase_col_t* CoordinatorCollection (TRI_vocbase_t* vocbase,
                                                 CollectionInfo const& ci) {
  TRI_vocbase_col_t* c = (TRI_vocbase_col_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_vocbase_col_t), false);

  if (c == 0) {
    return 0;
  }
  
  c->_isLocal    = false;
  c->_vocbase    = vocbase;
  c->_type       = ci.type();
  c->_cid        = ci.id();
  c->_planId     = ci.id();
  c->_status     = ci.status();
  c->_collection = 0;

  std::string const name = ci.name(); 

  memset(c->_name, 0, TRI_COL_NAME_LENGTH + 1); 
  memcpy(c->_name, name.c_str(), name.size());
  memset(c->_path, 0, TRI_COL_PATH_LENGTH + 1); 
  
  memset(c->_dbName, 0, TRI_COL_NAME_LENGTH + 1); 
  memcpy(c->_dbName, vocbase->_name, strlen(vocbase->_name));

  c->_canDrop   = true;
  c->_canUnload = true;
  c->_canRename = true;
  
  if (TRI_IsSystemNameCollection(c->_name)) {
    // a few system collections have special behavior
    if (TRI_EqualString(c->_name, TRI_COL_NAME_REPLICATION) ||
        TRI_EqualString(c->_name, TRI_COL_NAME_TRANSACTION) ||
        TRI_EqualString(c->_name, TRI_COL_NAME_USERS) ||
        TRI_IsPrefixString(c->_name, TRI_COL_NAME_STATISTICS)) {
      // these collections cannot be dropped or renamed
      c->_canDrop   = false;
      c->_canRename = false;
      
      // the replication collection cannot be unloaded manually)
      // (this would make the server hang)
      c->_canUnload = ! TRI_EqualString(c->_name, TRI_COL_NAME_REPLICATION);
    }
  }

  TRI_InitReadWriteLock(&c->_lock);

  return c;
}
#endif

#ifdef TRI_ENABLE_CLUSTER
struct CollectionGuard {
  CollectionGuard (TRI_vocbase_col_t* collection) 
    : _collection(collection) {
  }

  ~CollectionGuard () {
    if (_collection != 0 && ! _collection->_isLocal) {
      FreeCoordinatorCollection(_collection);
    } 
  }

  TRI_vocbase_col_t* _collection;
};
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief get all cluster collections
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static TRI_vector_pointer_t GetCollectionsCluster (TRI_vocbase_t* vocbase) {
  TRI_vector_pointer_t result;
  TRI_InitVectorPointer(&result, TRI_UNKNOWN_MEM_ZONE);
  
  std::vector<shared_ptr<CollectionInfo> > const& collections 
      = ClusterInfo::instance()->getCollections(vocbase->_name);

  for (size_t i = 0, n = collections.size(); i < n; ++i) {
    TRI_vocbase_col_t* c = CoordinatorCollection(vocbase, *(collections[i]));

    if (c != 0) {
      TRI_PushBackVectorPointer(&result, c);
    }
  }
     
  return result;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief get all cluster collection names
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static TRI_vector_string_t GetCollectionNamesCluster (TRI_vocbase_t* vocbase) {
  TRI_vector_string_t result;
  TRI_InitVectorString(&result, TRI_UNKNOWN_MEM_ZONE);
  
  std::vector<shared_ptr<CollectionInfo> > const& collections 
      = ClusterInfo::instance()->getCollections(vocbase->_name);

  for (size_t i = 0, n = collections.size(); i < n; ++i) {
    string const& name = collections[i]->name();
    char* s = TRI_DuplicateString2Z(TRI_UNKNOWN_MEM_ZONE, name.c_str(),
                                                          name.size());

    if (s != 0) {
      TRI_PushBackVectorString(&result, s);
    }
  }
     
  return result;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief create a v8 collection id value from the internal collection id
////////////////////////////////////////////////////////////////////////////////

static inline v8::Handle<v8::Value> V8CollectionId (const TRI_voc_cid_t cid) {
  char buffer[21];
  size_t len = TRI_StringUInt64InPlace((uint64_t) cid, (char*) &buffer);

  return v8::String::New((const char*) buffer, (int) len);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a v8 tick id value from the internal tick id
////////////////////////////////////////////////////////////////////////////////

static inline v8::Handle<v8::Value> V8TickId (const TRI_voc_tick_t tick) {
  char buffer[21];
  size_t len = TRI_StringUInt64InPlace((uint64_t) tick, (char*) &buffer);

  return v8::String::New((const char*) buffer, (int) len);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a v8 revision id value from the internal revision id
////////////////////////////////////////////////////////////////////////////////

static inline v8::Handle<v8::Value> V8RevisionId (const TRI_voc_rid_t rid) {
  char buffer[21];
  size_t len = TRI_StringUInt64InPlace((uint64_t) rid, (char*) &buffer);

  return v8::String::New((const char*) buffer, (int) len);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a v8 document id value from the parameters
////////////////////////////////////////////////////////////////////////////////

static inline v8::Handle<v8::Value> V8DocumentId (const string& collectionName,
                                                  const string& key) {
  const string id = DocumentHelper::assembleDocumentId(collectionName, key);

  return v8::String::New(id.c_str(), (int) id.size());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extract the forceSync flag from the arguments
/// must specify the argument index starting from 1
////////////////////////////////////////////////////////////////////////////////

static bool ExtractForceSync (v8::Arguments const& argv,
                              const int index) {
  assert(index > 0);

  const bool forceSync = (argv.Length() >= index && TRI_ObjectToBoolean(argv[index - 1]));
  return forceSync;
}

TRI_doc_update_policy_e ExtractUpdatePolicy (bool overwrite) {
  TRI_doc_update_policy_e policy ;

  if (overwrite) {
    // overwrite!
    policy = TRI_DOC_UPDATE_LAST_WRITE;
  }
  else {
    policy = TRI_DOC_UPDATE_CONFLICT;
  }

  return policy;
}
////////////////////////////////////////////////////////////////////////////////
/// @brief wraps a C++ into a v8::Object
////////////////////////////////////////////////////////////////////////////////

template<class T>
static v8::Handle<v8::Object> WrapClass (v8::Persistent<v8::ObjectTemplate> classTempl, 
                                         int32_t type, T* y) {

  // handle scope for temporary handles
  v8::HandleScope scope;

  // create the new handle to return, and set its template type
  v8::Handle<v8::Object> result = classTempl->NewInstance();

  if (result.IsEmpty()) {
    // error
    return scope.Close(result);
  }

  // set the c++ pointer for unwrapping later
  result->SetInternalField(SLOT_CLASS_TYPE, v8::Integer::New(type));
  result->SetInternalField(SLOT_CLASS, v8::External::New(y));

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the vocbase pointer from the current V8 context
////////////////////////////////////////////////////////////////////////////////

static inline TRI_vocbase_t* GetContextVocBase () {
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  assert(v8g->_vocbase != 0);
  TRI_vocbase_t* vocbase = static_cast<TRI_vocbase_t*>(v8g->_vocbase);

  return vocbase;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if argument is a document identifier
////////////////////////////////////////////////////////////////////////////////

static bool ParseDocumentHandle (v8::Handle<v8::Value> const arg,
                                 string& collectionName,
                                 TRI_voc_key_t& key) {
  assert(collectionName == "");

  if (! arg->IsString()) {
    return false;
  }

  // the handle must always be an ASCII string. These is no need to normalise it first
  v8::String::Utf8Value str(arg);

  if (*str == 0) {
    return false;
  }

  // collection name / document key
  size_t split;
  if (TRI_ValidateDocumentIdKeyGenerator(*str, &split)) {
    collectionName = string(*str, split);
    key = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, *str + split + 1, str.length() - split - 1);
    return true;
  }

  // document key only
  if (TRI_ValidateKeyKeyGenerator(*str)) {
    key = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, *str, str.length());
    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts a document key from a document
////////////////////////////////////////////////////////////////////////////////

static int ExtractDocumentKey (v8::Handle<v8::Value> const arg,
                               TRI_voc_key_t& key) {
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  key = 0;

  if (arg->IsObject() && ! arg->IsArray()) {
    v8::Local<v8::Object> obj = arg->ToObject();

    if (obj->Has(v8g->_KeyKey)) {
      v8::Handle<v8::Value> v = obj->Get(v8g->_KeyKey);

      if (v->IsString()) {
        // string key
        // keys must not contain any special characters, so it is not necessary
        // to normalise them first
        v8::String::Utf8Value str(v);

        if (*str == 0) {
          return TRI_ERROR_ARANGO_DOCUMENT_KEY_BAD;
        }

        key = TRI_DuplicateString2(*str, str.length());
        return TRI_ERROR_NO_ERROR;
      }
      else {
        return TRI_ERROR_ARANGO_DOCUMENT_KEY_BAD;
      }
    }
    else {
      return TRI_ERROR_ARANGO_DOCUMENT_KEY_MISSING;
    }
  }
  else {
    // anything else than an object will be rejected
    return TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parse document or document handle from a v8 value (string | object)
////////////////////////////////////////////////////////////////////////////////

static bool ExtractDocumentHandle (v8::Handle<v8::Value> const val,
                                   string& collectionName,
                                   TRI_voc_key_t& key,
                                   TRI_voc_rid_t& rid) {
  // reset the collection identifier and the revision
  collectionName = "";
  rid = 0;

  // extract the document identifier and revision from a string
  if (val->IsString()) {
    return ParseDocumentHandle(val, collectionName, key);
  }

  // extract the document identifier and revision from a document object
  if (val->IsObject()) {
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

    v8::Handle<v8::Object> obj = val->ToObject();
    v8::Handle<v8::Value> didVal = obj->Get(v8g->_IdKey);

    if (! ParseDocumentHandle(didVal, collectionName, key)) {
      return false;
    }

    if (! obj->Has(v8g->_RevKey)) {
      return true;
    }

    rid = TRI_ObjectToUInt64(obj->Get(v8g->_RevKey), true);

    if (rid == 0) {
      return false;
    }

    return true;
  }

  // unknown value type. give up
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief check if a name belongs to a collection
////////////////////////////////////////////////////////////////////////////////

static bool EqualCollection (CollectionNameResolver const& resolver,
                             string const& collectionName,
                             TRI_vocbase_col_t const* collection) {
  if (collectionName == StringUtils::itoa(collection->_cid)) {
    return true;
  }

  if (collectionName == string(collection->_name)) {
    return true;
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    if (collectionName == resolver.getCollectionNameCluster(collection->_cid)) {
      return true;
    }
    return false;
  }
#endif
  if (collectionName == resolver.getCollectionName(collection->_cid)) {
    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parse document or document handle from a v8 value (string | object)
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ParseDocumentOrDocumentHandle (TRI_vocbase_t* vocbase,
                                                            CollectionNameResolver const& resolver,
                                                            TRI_vocbase_col_t const*& collection,
                                                            TRI_voc_key_t& key,
                                                            TRI_voc_rid_t& rid,
                                                            v8::Handle<v8::Value> const val) {
  v8::HandleScope scope;

  assert(key == 0);

  // reset the collection identifier and the revision
  string collectionName = "";
  rid = 0;

  // try to extract the collection name, key, and revision from the object passed
  if (! ExtractDocumentHandle(val, collectionName, key, rid)) {
    return scope.Close(TRI_CreateErrorObject(__FILE__,
                                             __LINE__,
                                             TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD));
  }

  // we have at least a key, we also might have a collection name
  assert(key != 0);


  if (collectionName == "") {
    // only a document key without collection name was passed
    if (collection == 0) {
      // we do not know the collection
      return scope.Close(TRI_CreateErrorObject(__FILE__,
                                               __LINE__,
                                               TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD));
    }
    // we use the current collection's name
    collectionName = resolver.getCollectionName(collection->_cid);
  }
  else {
    // we read a collection name from the document id
    // check cross-collection requests
    if (collection != 0) {
      if (! EqualCollection(resolver, collectionName, collection)) {
        return scope.Close(TRI_CreateErrorObject(__FILE__,
                                                 __LINE__,
                                                 TRI_ERROR_ARANGO_CROSS_COLLECTION_REQUEST));
      }
    }
  }

  assert(collectionName != "");

  if (collection == 0) {
    // no collection object was passed, now check the user-supplied collection name
    
    TRI_vocbase_col_t const* col = 0;

#ifdef TRI_ENABLE_CLUSTER
    if (ServerState::instance()->isCoordinator()) {
      ClusterInfo* ci = ClusterInfo::instance();
      shared_ptr<CollectionInfo> const& c = ci->getCollection(vocbase->_name, collectionName);
      col = CoordinatorCollection(vocbase, *c);

      if (col != 0 && col->_cid == 0) {
        FreeCoordinatorCollection(const_cast<TRI_vocbase_col_t*>(col));
        col = 0;
      }
    }
    else {
      col = resolver.getCollectionStruct(collectionName);
    }

#else
    col = resolver.getCollectionStruct(collectionName);
#endif

    if (col == 0) {
      // collection not found
      return scope.Close(TRI_CreateErrorObject(__FILE__,
                                               __LINE__,
                                               TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND));
    }

    collection = col;
  }

  assert(collection != 0);

  v8::Handle<v8::Value> empty;
  return scope.Close(empty);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if argument is an index identifier
////////////////////////////////////////////////////////////////////////////////

static bool IsIndexHandle (v8::Handle<v8::Value> const arg,
                           string& collectionName,
                           TRI_idx_iid_t& iid) {

  assert(collectionName == "");
  assert(iid == 0);

  if (arg->IsNumber()) {
    // numeric index id
    iid = (TRI_idx_iid_t) arg->ToNumber()->Value();
    return true;
  }

  if (! arg->IsString()) {
    return false;
  }

  v8::String::Utf8Value str(arg);

  if (*str == 0) {
    return false;
  }

  size_t split;
  if (TRI_ValidateIndexIdIndex(*str, &split)) {
    collectionName = string(*str, split);
    iid = TRI_UInt64String2(*str + split + 1, str.length() - split - 1);
    return true;
  }

  if (TRI_ValidateIdIndex(*str)) {
    iid = TRI_UInt64String2(*str, str.length());
    return true;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief weak reference callback for collections
////////////////////////////////////////////////////////////////////////////////

static void WeakCollectionCallback (v8::Isolate* isolate,
                                    v8::Persistent<v8::Value> object,
                                    void* parameter) {
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  TRI_vocbase_col_t* collection = (TRI_vocbase_col_t*) parameter;
  
  v8g->_hasDeadObjects = true;

  v8::HandleScope scope; // do not remove, will fail otherwise!!

  // decrease the reference-counter for the database
  TRI_ReleaseVocBase(collection->_vocbase);
  
  // find the persistent handle
  v8::Persistent<v8::Value> persistent = v8g->JSCollections[collection];
  v8g->JSCollections.erase(collection);

  if (! collection->_isLocal) {
    FreeCoordinatorCollection(collection);
  }

  // dispose and clear the persistent handle
  persistent.Dispose(isolate);
  persistent.Clear();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief loads a collection for usage
////////////////////////////////////////////////////////////////////////////////

static TRI_vocbase_col_t const* UseCollection (v8::Handle<v8::Object> collection,
                                               v8::Handle<v8::Object>* err) {

  int res = TRI_ERROR_INTERNAL;
  TRI_vocbase_col_t* col = TRI_UnwrapClass<TRI_vocbase_col_t>(collection, WRP_VOCBASE_COL_TYPE);

  if (col != 0) {
#ifdef TRI_ENABLE_CLUSTER
    if (! col->_isLocal) {
      *err = TRI_CreateErrorObject(__FILE__,
                                   __LINE__,
                                   TRI_ERROR_NOT_IMPLEMENTED);
      TRI_set_errno(TRI_ERROR_NOT_IMPLEMENTED);
      return 0;
    }
#endif

    res = TRI_UseCollectionVocBase(col->_vocbase, col);

    if (res == TRI_ERROR_NO_ERROR && 
        col->_collection != 0) {
      // no error
      return col;
    }
  }

  // some error occurred
  *err = TRI_CreateErrorObject(__FILE__, __LINE__, res, "cannot use/load collection", true);
  TRI_set_errno(res);
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief releases a collection
////////////////////////////////////////////////////////////////////////////////

static void ReleaseCollection (TRI_vocbase_col_t const* collection) {
  TRI_ReleaseCollectionVocBase(collection->_vocbase, const_cast<TRI_vocbase_col_t*>(collection));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the index representation
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> IndexRep (string const& collectionName, 
                                       TRI_json_t const* idx) {
  v8::HandleScope scope;

  assert(idx != 0);

  v8::Handle<v8::Object> rep = TRI_ObjectJson(idx)->ToObject();

  string iid = TRI_ObjectToString(rep->Get(TRI_V8_SYMBOL("id")));
  const string id = collectionName + TRI_INDEX_HANDLE_SEPARATOR_STR + iid;
  rep->Set(TRI_V8_SYMBOL("id"), v8::String::New(id.c_str(), (int) id.size()));

  return scope.Close(rep);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extract the unique flag from the data
////////////////////////////////////////////////////////////////////////////////

bool ExtractBoolFlag (v8::Handle<v8::Object> const obj,
                      char const* name,
                      bool defaultValue) {
  // extract unique flag
  if (obj->Has(TRI_V8_SYMBOL(name))) {
    return TRI_ObjectToBoolean(obj->Get(TRI_V8_SYMBOL(name)));
  }

  return defaultValue;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process the fields list and add them to the json
////////////////////////////////////////////////////////////////////////////////

int ProcessBitarrayIndexFields (v8::Handle<v8::Object> const obj,
                                TRI_json_t* json, 
                                bool create) {
  vector<string> fields;
      
  TRI_json_t* fieldJson = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
  
  if (fieldJson == 0) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }

  int res = TRI_ERROR_NO_ERROR;

  if (obj->Has(TRI_V8_SYMBOL("fields")) && obj->Get(TRI_V8_SYMBOL("fields"))->IsArray()) {
    // "fields" is a list of fields
    v8::Handle<v8::Array> fieldList = v8::Handle<v8::Array>::Cast(obj->Get(TRI_V8_SYMBOL("fields")));

    const uint32_t n = fieldList->Length();

    for (uint32_t i = 0; i < n; ++i) {
      if (! fieldList->Get(i)->IsArray()) {
        res = TRI_ERROR_BAD_PARAMETER;
        break;
      }
    
      v8::Handle<v8::Array> fieldPair = v8::Handle<v8::Array>::Cast(fieldList->Get(i));

      if (fieldPair->Length() != 2) {
        res = TRI_ERROR_BAD_PARAMETER;
        break;
      }

      const string f = TRI_ObjectToString(fieldPair->Get(0));

      if (f.empty() || (create && f[0] == '_')) {
        // accessing internal attributes is disallowed
        res = TRI_ERROR_BAD_PARAMETER;
        break;
      }

      if (std::find(fields.begin(), fields.end(), f) != fields.end()) {
        // duplicate attribute name
        res = TRI_ERROR_ARANGO_INDEX_BITARRAY_CREATION_FAILURE_DUPLICATE_ATTRIBUTES;
        break;
      }

      if (! fieldPair->Get(1)->IsArray()) {
        // parameter at uneven position must be a list
        res = TRI_ERROR_BAD_PARAMETER;
        break;
      }

      fields.push_back(f);

      TRI_json_t* pair = TRI_CreateList2Json(TRI_UNKNOWN_MEM_ZONE, 2);

      if (pair == 0) {
        res = TRI_ERROR_OUT_OF_MEMORY;
        break;
      }

      // key
      TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, pair, TRI_CreateString2CopyJson(TRI_UNKNOWN_MEM_ZONE, f.c_str(), f.size()));

      // value
      TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, pair, TRI_ObjectToJson(fieldPair->Get(1)));

      // add the pair to the fields list
      TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fieldJson, pair);
    }
  }

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, fieldJson);
    return res;
  }
      
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fieldJson);
  
  if (fields.empty()) {
    return TRI_ERROR_BAD_PARAMETER;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process the fields list and add them to the json
////////////////////////////////////////////////////////////////////////////////

int ProcessIndexFields (v8::Handle<v8::Object> const obj,
                        TRI_json_t* json,
                        int numFields,
                        bool create) {
  vector<string> fields;

  if (obj->Has(TRI_V8_SYMBOL("fields")) && obj->Get(TRI_V8_SYMBOL("fields"))->IsArray()) {
    // "fields" is a list of fields
    v8::Handle<v8::Array> fieldList = v8::Handle<v8::Array>::Cast(obj->Get(TRI_V8_SYMBOL("fields")));

    const uint32_t n = fieldList->Length();
    
    for (uint32_t i = 0; i < n; ++i) {
      if (! fieldList->Get(i)->IsString()) {
        return TRI_ERROR_BAD_PARAMETER;
      }

      const string f = TRI_ObjectToString(fieldList->Get(i));

      if (f.empty() || (create && f[0] == '_')) {
        // accessing internal attributes is disallowed
        return TRI_ERROR_BAD_PARAMETER;
      }

      if (std::find(fields.begin(), fields.end(), f) != fields.end()) {
        // duplicate attribute name
        return TRI_ERROR_BAD_PARAMETER;
      }

      fields.push_back(f);
    }
  }
  
  if (fields.empty() || 
      (numFields > 0 && (int) fields.size() != numFields)) {
    return TRI_ERROR_BAD_PARAMETER;
  }

  TRI_json_t* fieldJson = TRI_ObjectToJson(obj->Get(TRI_V8_SYMBOL("fields")));

  if (fieldJson == 0) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
     
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fieldJson);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process the geojson flag and add it to the json
////////////////////////////////////////////////////////////////////////////////

int ProcessIndexGeoJsonFlag (v8::Handle<v8::Object> const obj,
                            TRI_json_t* json) {
  bool geoJson = ExtractBoolFlag(obj, "geoJson", false);
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "geoJson", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, geoJson));

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process the unique flag and add it to the json
////////////////////////////////////////////////////////////////////////////////

int ProcessIndexUniqueFlag (v8::Handle<v8::Object> const obj,
                            TRI_json_t* json,
                            bool fillConstraint = false) {
  bool unique = ExtractBoolFlag(obj, "unique", false);
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, unique));
  if (fillConstraint) {
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "constraint", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, unique));
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process the ignoreNull flag and add it to the json
////////////////////////////////////////////////////////////////////////////////

int ProcessIndexIgnoreNullFlag (v8::Handle<v8::Object> const obj,
                                TRI_json_t* json) {
  bool ignoreNull = ExtractBoolFlag(obj, "ignoreNull", false);
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "ignoreNull", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, ignoreNull));
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process the undefined flag and add it to the json
////////////////////////////////////////////////////////////////////////////////

int ProcessIndexUndefinedFlag (v8::Handle<v8::Object> const obj,
                               TRI_json_t* json) {
  bool undefined = ExtractBoolFlag(obj, "undefined", false);
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "undefined", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, undefined));

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a geo1 index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexGeo1 (v8::Handle<v8::Object> const obj,
                                 TRI_json_t* json,
                                 bool create) {
  int res = ProcessIndexFields(obj, json, 1, create);
  ProcessIndexUniqueFlag(obj, json, true);
  ProcessIndexIgnoreNullFlag(obj, json);
  ProcessIndexGeoJsonFlag(obj, json);
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a geo2 index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexGeo2 (v8::Handle<v8::Object> const obj,
                                 TRI_json_t* json,
                                 bool create) {
  int res = ProcessIndexFields(obj, json, 2, create);
  ProcessIndexUniqueFlag(obj, json, true);
  ProcessIndexIgnoreNullFlag(obj, json);
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a hash index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexHash (v8::Handle<v8::Object> const obj,
                                 TRI_json_t* json,
                                 bool create) {
  int res = ProcessIndexFields(obj, json, 0, create);
  ProcessIndexUniqueFlag(obj, json);
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a skiplist index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexSkiplist (v8::Handle<v8::Object> const obj,
                                     TRI_json_t* json,
                                     bool create) {
  int res = ProcessIndexFields(obj, json, 0, create);
  ProcessIndexUniqueFlag(obj, json);
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a bitarray index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexBitarray (v8::Handle<v8::Object> const obj,
                                     TRI_json_t* json,
                                     bool create) {
  int res = ProcessBitarrayIndexFields(obj, json, create);
  ProcessIndexUndefinedFlag(obj, json);
  
  // bitarrays are always non-unique
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, false));

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a fulltext index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexFulltext (v8::Handle<v8::Object> const obj,
                                     TRI_json_t* json,
                                     bool create) {
  int res = ProcessIndexFields(obj, json, 1, create);

  // handle "minLength" attribute
  int minWordLength = TRI_FULLTEXT_MIN_WORD_LENGTH_DEFAULT;
  if (obj->Has(TRI_V8_SYMBOL("minLength")) && obj->Get(TRI_V8_SYMBOL("minLength"))->IsNumber()) {
    minWordLength = (int) TRI_ObjectToInt64(obj->Get(TRI_V8_SYMBOL("minLength")));
  }
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "minLength", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, minWordLength));

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of a cap constraint
////////////////////////////////////////////////////////////////////////////////

static int EnhanceJsonIndexCap (v8::Handle<v8::Object> const obj,
                                TRI_json_t* json) {
  // handle "size" attribute
  size_t count = 0;
  if (obj->Has(TRI_V8_SYMBOL("size")) && obj->Get(TRI_V8_SYMBOL("size"))->IsNumber()) {
    int64_t value = TRI_ObjectToInt64(obj->Get(TRI_V8_SYMBOL("size")));
  
    if (value < 0 || value > UINT32_MAX) {
      return TRI_ERROR_BAD_PARAMETER;
    }
    count = (size_t) value;
  }

  // handle "byteSize" attribute
  int64_t byteSize = 0;
  if (obj->Has(TRI_V8_SYMBOL("byteSize")) && obj->Get(TRI_V8_SYMBOL("byteSize"))->IsNumber()) {
    byteSize = TRI_ObjectToInt64(obj->Get(TRI_V8_SYMBOL("byteSize")));
  }

  if (count == 0 && byteSize <= 0) {
    return TRI_ERROR_BAD_PARAMETER;
  }
    
  if (byteSize < 0 || (byteSize > 0 && byteSize < TRI_CAP_CONSTRAINT_MIN_SIZE)) {
    return TRI_ERROR_BAD_PARAMETER;
  }
  
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "size", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, (double) count));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "byteSize", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, (double) byteSize));

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief enhances the json of an index
////////////////////////////////////////////////////////////////////////////////

static int EnhanceIndexJson (v8::Arguments const& argv,
                             TRI_json_t*& json,
                             bool create) {
  v8::Handle<v8::Object> obj = argv[0].As<v8::Object>();

  // extract index type  
  TRI_idx_type_e type = TRI_IDX_TYPE_UNKNOWN;

  if (obj->Has(TRI_V8_SYMBOL("type")) && obj->Get(TRI_V8_SYMBOL("type"))->IsString()) {
    TRI_Utf8ValueNFC typeString(TRI_UNKNOWN_MEM_ZONE, obj->Get(TRI_V8_SYMBOL("type")));

    if (*typeString == 0) {
      return TRI_ERROR_OUT_OF_MEMORY;
    }

    string t(*typeString);
    // rewrite type "geo" into either "geo1" or "geo2", depending on the number of fields
    if (t == "geo") {
      t = "geo1";

      if (obj->Has(TRI_V8_SYMBOL("fields")) && obj->Get(TRI_V8_SYMBOL("fields"))->IsArray()) {
        v8::Handle<v8::Array> f = v8::Handle<v8::Array>::Cast(obj->Get(TRI_V8_SYMBOL("fields")));
        if (f->Length() == 2) {
          t = "geo2";
        }
      }
    }

    type = TRI_TypeIndex(t.c_str());
  }

  if (type == TRI_IDX_TYPE_UNKNOWN) {
    return TRI_ERROR_BAD_PARAMETER;
  }
  
  if (create) {
    if (type == TRI_IDX_TYPE_PRIMARY_INDEX ||
        type == TRI_IDX_TYPE_EDGE_INDEX) {
      // creating these indexes yourself is forbidden
      return TRI_ERROR_FORBIDDEN;
    }
  }

  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (json == 0) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  
  if (obj->Has(TRI_V8_SYMBOL("id"))) {
    uint64_t id = TRI_ObjectToUInt64(obj->Get(TRI_V8_SYMBOL("id")), true);
    if (id > 0) {
      char* idString = TRI_StringUInt64(id);
      TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, idString));
      TRI_FreeString(TRI_CORE_MEM_ZONE, idString);
    }
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, 
                       json, 
                       "type", 
                       TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, TRI_TypeNameIndex(type)));
  
  int res = TRI_ERROR_INTERNAL;

  switch (type) {
    case TRI_IDX_TYPE_UNKNOWN: 
    case TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX: {
      res = TRI_ERROR_BAD_PARAMETER;
      break;
    }

    case TRI_IDX_TYPE_PRIMARY_INDEX:
    case TRI_IDX_TYPE_EDGE_INDEX: {
      break;
    }

    case TRI_IDX_TYPE_GEO1_INDEX:
      res = EnhanceJsonIndexGeo1(obj, json, create);
      break;
    case TRI_IDX_TYPE_GEO2_INDEX:
      res = EnhanceJsonIndexGeo2(obj, json, create);
      break;
    case TRI_IDX_TYPE_HASH_INDEX:
      res = EnhanceJsonIndexHash(obj, json, create);
      break;
    case TRI_IDX_TYPE_SKIPLIST_INDEX:
      res = EnhanceJsonIndexSkiplist(obj, json, create);
      break;
    case TRI_IDX_TYPE_BITARRAY_INDEX:
      res = EnhanceJsonIndexBitarray(obj, json, create);
      break;
    case TRI_IDX_TYPE_FULLTEXT_INDEX: 
      res = EnhanceJsonIndexFulltext(obj, json, create);
      break;
    case TRI_IDX_TYPE_CAP_CONSTRAINT: 
      res = EnhanceJsonIndexCap(obj, json);
      break;
  }
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ensures an index, coordinator case
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> EnsureIndexCoordinator (TRI_vocbase_col_t const* collection,
                                                     TRI_json_t const* json,
                                                     bool create) {
  v8::HandleScope scope;

  assert(collection != 0);
  assert(json != 0);

  string const databaseName(collection->_dbName);
  string const cid = StringUtils::itoa(collection->_cid);
  // TODO: protect against races on _name
  string const collectionName(collection->_name);

  TRI_json_t* resultJson = 0; 
  string errorMsg;
  int res = ClusterInfo::instance()->ensureIndexCoordinator(databaseName, 
                                                            cid, 
                                                            json,
                                                            create,
                                                            &IndexComparator,
                                                            resultJson,
                                                            errorMsg, 
                                                            360.0);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, errorMsg);
  }

  if (resultJson == 0) {
    if (! create) {
      // did not find a suitable index
      return scope.Close(v8::Null());
    }

    TRI_V8_EXCEPTION_MEMORY(scope);
  }
  
  v8::Handle<v8::Value> ret = IndexRep(collectionName, resultJson);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, resultJson);
  
  return scope.Close(ret);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief ensures an index, locally
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> EnsureIndexLocal (TRI_vocbase_col_t const* collection,
                                               TRI_json_t const* json,
                                               bool create) {
  v8::HandleScope scope;

  assert(collection != 0);
  assert(json != 0);
  
  // extract type
  TRI_json_t* value = TRI_LookupArrayJson(json, "type");
  assert(TRI_IsStringJson(value));

  TRI_idx_type_e type = TRI_TypeIndex(value->_value._string.data);
 
  // extract unique
  bool unique = false;
  value = TRI_LookupArrayJson(json, "unique");
  if (TRI_IsBooleanJson(value)) {
    unique = value->_value._boolean;
  }
  
  TRI_vector_pointer_t attributes;
  TRI_InitVectorPointer(&attributes, TRI_CORE_MEM_ZONE);

  TRI_vector_pointer_t values;
  TRI_InitVectorPointer(&values, TRI_CORE_MEM_ZONE);
 
  // extract id 
  TRI_idx_iid_t iid = 0;
  value = TRI_LookupArrayJson(json, "id");
  if (TRI_IsStringJson(value)) {
    iid = TRI_UInt64String2(value->_value._string.data, value->_value._string.length - 1);
  }

  // extract fields
  value = TRI_LookupArrayJson(json, "fields");
  if (TRI_IsListJson(value)) {
    // note: "fields" is not mandatory for all index types

    if (type == TRI_IDX_TYPE_BITARRAY_INDEX) {
      // copy all field names (attributes) plus the values (json)
      for (size_t i = 0; i < value->_value._objects._length; ++i) {
        // add attribute
        TRI_json_t const* v = TRI_LookupListJson(value, i);

        if (TRI_IsListJson(v) && v->_value._objects._length == 2) {
          // key
          TRI_json_t const* key = TRI_LookupListJson(v, 0);
          if (TRI_IsStringJson(key)) {
            TRI_PushBackVectorPointer(&attributes, key->_value._string.data);
          }

          // value
          TRI_json_t const* value = TRI_LookupListJson(v, 1);
          if (TRI_IsListJson(value)) {
            TRI_PushBackVectorPointer(&values, (void*) value);
          }
        }
      }
    }
    else {
      // copy all field names (attributes)
      for (size_t i = 0; i < value->_value._objects._length; ++i) {
        TRI_json_t const* v = (TRI_json_t const*) TRI_AtVector(&value->_value._objects, i);

        assert(TRI_IsStringJson(v));
        TRI_PushBackVectorPointer(&attributes, v->_value._string.data);
      }
    }
  }
  
  CollectionNameResolver resolver(collection->_vocbase);
  ReadTransactionType trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVectorPointer(&values);
    TRI_DestroyVectorPointer(&attributes);
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_document_collection_t* document = (TRI_document_collection_t*) trx.primaryCollection();
  const string collectionName = string(collection->_name);

  bool created = false;
  TRI_index_t* idx = 0;

  switch (type) {
    case TRI_IDX_TYPE_UNKNOWN:
    case TRI_IDX_TYPE_PRIMARY_INDEX:
    case TRI_IDX_TYPE_EDGE_INDEX:
    case TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX: {
      // these indexes cannot be created directly
      TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
    }

    case TRI_IDX_TYPE_GEO1_INDEX: {
      assert(attributes._length == 1);
      
      bool ignoreNull = false;
      TRI_json_t* value = TRI_LookupArrayJson(json, "ignoreNull");
      if (TRI_IsBooleanJson(value)) {
        ignoreNull = value->_value._boolean;
      }
      
      bool geoJson = false;
      value = TRI_LookupArrayJson(json, "geoJson");
      if (TRI_IsBooleanJson(value)) {
        geoJson = value->_value._boolean;
      }

      if (create) {
        idx = TRI_EnsureGeoIndex1DocumentCollection(document, 
                                                    iid,
                                                    (char const*) TRI_AtVectorPointer(&attributes, 0),
                                                    geoJson, 
                                                    unique, 
                                                    ignoreNull, 
                                                    &created, 
                                                    TRI_GetIdServer());
      }
      else {
        idx = TRI_LookupGeoIndex1DocumentCollection(document, 
                                                    (char const*) TRI_AtVectorPointer(&attributes, 0),
                                                     geoJson, 
                                                     unique, 
                                                     ignoreNull);
      }
      break;
    }

    case TRI_IDX_TYPE_GEO2_INDEX: {
      assert(attributes._length == 2);
      
      bool ignoreNull = false;
      TRI_json_t* value = TRI_LookupArrayJson(json, "ignoreNull");
      if (TRI_IsBooleanJson(value)) {
        ignoreNull = value->_value._boolean;
      }

      if (create) {
        idx = TRI_EnsureGeoIndex2DocumentCollection(document, 
                                                    iid,
                                                    (char const*) TRI_AtVectorPointer(&attributes, 0),
                                                    (char const*) TRI_AtVectorPointer(&attributes, 1),
                                                    unique, 
                                                    ignoreNull, 
                                                    &created, 
                                                    TRI_GetIdServer());
      }
      else {
        idx = TRI_LookupGeoIndex2DocumentCollection(document, 
                                                    (char const*) TRI_AtVectorPointer(&attributes, 0),
                                                    (char const*) TRI_AtVectorPointer(&attributes, 1),
                                                     unique, 
                                                     ignoreNull);
      }
      break;
    }

    case TRI_IDX_TYPE_HASH_INDEX: {
      assert(attributes._length > 0);

      if (create) {
        idx = TRI_EnsureHashIndexDocumentCollection(document,
                                                    iid, 
                                                    &attributes, 
                                                    unique, 
                                                    &created, 
                                                    TRI_GetIdServer());
      }
      else {
        idx = TRI_LookupHashIndexDocumentCollection(document, 
                                                    &attributes, 
                                                    unique);
      }

      break;
    }

    case TRI_IDX_TYPE_SKIPLIST_INDEX: {
      assert(attributes._length > 0);

      if (create) {
        idx = TRI_EnsureSkiplistIndexDocumentCollection(document, 
                                                        iid,
                                                        &attributes, 
                                                        unique, 
                                                        &created, 
                                                        TRI_GetIdServer());
      }
      else {
        idx = TRI_LookupSkiplistIndexDocumentCollection(document, 
                                                        &attributes, 
                                                        unique);
      }
      break;
    }
      
    case TRI_IDX_TYPE_FULLTEXT_INDEX: {
      assert(attributes._length == 1);

      int minWordLength = TRI_FULLTEXT_MIN_WORD_LENGTH_DEFAULT;
      TRI_json_t* value = TRI_LookupArrayJson(json, "minLength");
      if (TRI_IsNumberJson(value)) {
        minWordLength = (int) value->_value._number;
      }

      if (create) {
        idx = TRI_EnsureFulltextIndexDocumentCollection(document,
                                                        iid, 
                                                        (char const*) TRI_AtVectorPointer(&attributes, 0),
                                                        false, 
                                                        minWordLength, 
                                                        &created, 
                                                        TRI_GetIdServer());
      }
      else {
        idx = TRI_LookupFulltextIndexDocumentCollection(document, 
                                                        (char const*) TRI_AtVectorPointer(&attributes, 0),
                                                        false,
                                                        minWordLength);
      }
      break;
    }

    case TRI_IDX_TYPE_BITARRAY_INDEX: {
      assert(attributes._length > 0);

      bool supportUndefined = false;
      TRI_json_t* value = TRI_LookupArrayJson(json, "undefined");
      if (TRI_IsBooleanJson(value)) {
        supportUndefined = value->_value._boolean;
      }

      if (create) {
        int errorCode = TRI_ERROR_NO_ERROR;
        char* errorStr = 0;

        idx = TRI_EnsureBitarrayIndexDocumentCollection(document,
                                                        iid,
                                                        &attributes, 
                                                        &values, 
                                                        supportUndefined, 
                                                        &created, 
                                                        &errorCode, 
                                                        &errorStr,
                                                        TRI_GetIdServer());
        if (errorCode != 0) {
          TRI_set_errno(errorCode);
        }
      
        if (errorStr != 0) {
          TRI_FreeString(TRI_CORE_MEM_ZONE, errorStr);
        }
      }
      else {
        idx = TRI_LookupBitarrayIndexDocumentCollection(document,
                                                        &attributes);
      }
      break;
    }

    case TRI_IDX_TYPE_CAP_CONSTRAINT: {
      size_t size = 0;
      TRI_json_t* value = TRI_LookupArrayJson(json, "size");
      if (TRI_IsNumberJson(value)) {
        size = (size_t) value->_value._number;
      }

      int64_t byteSize = 0;
      value = TRI_LookupArrayJson(json, "byteSize");
      if (TRI_IsNumberJson(value)) {
        byteSize = (int64_t) value->_value._number;
      }

      if (create) {
        idx = TRI_EnsureCapConstraintDocumentCollection(document,
                                                        iid,
                                                        size, 
                                                        byteSize, 
                                                        &created, 
                                                        TRI_GetIdServer());
      }
      else {
        idx = TRI_LookupCapConstraintDocumentCollection(document);
      }
      break;
    }
  }

  if (idx == 0 && create) {
    // something went wrong during creation
    int res = TRI_errno();
    TRI_DestroyVectorPointer(&values);
    TRI_DestroyVectorPointer(&attributes);

    TRI_V8_EXCEPTION(scope, res);
  }
  
  TRI_DestroyVectorPointer(&values);
  TRI_DestroyVectorPointer(&attributes);


  if (idx == 0 && ! create) {
    // no index found
    return scope.Close(v8::Null());
  }

  // found some index to return
  TRI_json_t* indexJson = idx->json(idx);

  if (indexJson == 0) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }
  
  v8::Handle<v8::Value> ret = IndexRep(collectionName, indexJson);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, indexJson);
  
  if (ret->IsObject()) {
    ret->ToObject()->Set(v8::String::New("isNewlyCreated"), v8::Boolean::New(create && created));
  }

  return scope.Close(ret);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief ensures an index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> EnsureIndex (v8::Arguments const& argv,
                                          bool create,
                                          char const* functionName) {
  v8::HandleScope scope;
  
  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  if (argv.Length() != 1 || ! argv[0]->IsObject()) {
    string name(functionName);
    name.append("(<description>)");
    TRI_V8_EXCEPTION_USAGE(scope, name.c_str());
  }

  TRI_json_t* json = 0;
  int res = EnhanceIndexJson(argv, json, create);

  
#ifdef TRI_ENABLE_CLUSTER 
  if (res == TRI_ERROR_NO_ERROR &&
      ServerState::instance()->isCoordinator()) {
    string const dbname(collection->_dbName);
    // TODO: someone might rename the collection while we're reading its name...
    string const collname(collection->_name);
    shared_ptr<CollectionInfo> const& c = ClusterInfo::instance()->getCollection(dbname, collname);

    if (c->empty()) {
      TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
    }

    // check if there is an attempt to create a unique index on non-shard keys
    if (create) {
      TRI_json_t const* v = TRI_LookupArrayJson(json, "unique");

      if (TRI_IsBooleanJson(v) && v->_value._boolean) {
        // unique index, now check if fields and shard keys match
        TRI_json_t const* flds = TRI_LookupArrayJson(json, "fields");

        if (TRI_IsListJson(flds) && c->numberOfShards() > 1) {
          vector<string> const& shardKeys = c->shardKeys();
          size_t const n = flds->_value._objects._length;

          if (shardKeys.size() != n) {
            res = TRI_ERROR_CLUSTER_UNSUPPORTED;
          }
          else {
            for (size_t i = 0; i < n; ++i) {
              TRI_json_t const* f = TRI_LookupListJson(flds, i);

              if (! TRI_IsStringJson(f)) {
                res = TRI_ERROR_INTERNAL;
                continue;
              }
              else {
                if (! TRI_EqualString(f->_value._string.data, shardKeys[i].c_str())) {
                  res = TRI_ERROR_CLUSTER_UNSUPPORTED;
                }
              }
            }
          }
        }
      }
    }

  }
#endif


  if (res != TRI_ERROR_NO_ERROR) {
    if (json != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
    TRI_V8_EXCEPTION(scope, res);
  }

  assert(json != 0);
  
  v8::Handle<v8::Value> ret;

#ifdef TRI_ENABLE_CLUSTER
  // ensure an index, coordinator case
  if (ServerState::instance()->isCoordinator()) {
    ret = EnsureIndexCoordinator(collection, json, create);
  }
  else {
    ret = EnsureIndexLocal(collection, json, create);
  }
#else
  ret = EnsureIndexLocal(collection, json, create);
#endif

  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  return scope.Close(ret);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a document, coordinator case in a cluster
///
/// If generateDocument is false, this implements ".exists" rather than
/// ".document".
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> DocumentVocbaseColCoordinator (TRI_vocbase_col_t const* collection,
                                                            v8::Arguments const& argv,
                                                            bool generateDocument) {
  v8::HandleScope scope;

  // First get the initial data:
  string const dbname(collection->_dbName);
  // TODO: someone might rename the collection while we're reading its name...
  string const collname(collection->_name);

  string key;
  TRI_voc_rid_t rev = 0;
  int error = ParseKeyAndRef(argv[0], key, rev);
  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }

  triagens::rest::HttpResponse::HttpResponseCode responseCode;
  map<string, string> headers;
  map<string, string> resultHeaders;
  string resultBody;

  error = triagens::arango::getDocumentOnCoordinator(
            dbname, collname, key, rev, headers,
            generateDocument,
            responseCode, resultHeaders, resultBody);

  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }

  // report what the DBserver told us: this could now be 200 or
  // 404/412
  // For the error processing we have to distinguish whether we are in
  // the ".exists" case (generateDocument==false) or the ".document" case
  // (generateDocument==true).
  TRI_json_t* json = 0;
  if (generateDocument) {
    json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, resultBody.c_str());
  }
  if (responseCode >= triagens::rest::HttpResponse::BAD) {
    if (!TRI_IsArrayJson(json)) {
      if (generateDocument) {
        if (0 != json) {
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
        }
        TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
      }
      else {
        return scope.Close(v8::False());
      }
    }
    if (generateDocument) {
      int errorNum = 0;
      string errorMessage;
      if (0 != json) {
        TRI_json_t* subjson = TRI_LookupArrayJson(json, "errorNum");
        if (0 != subjson && TRI_IsNumberJson(subjson)) {
          errorNum = static_cast<int>(subjson->_value._number);
        }
        subjson = TRI_LookupArrayJson(json, "errorMessage");
        if (0 != subjson && TRI_IsStringJson(subjson)) {
          errorMessage = string(subjson->_value._string.data,
                                subjson->_value._string.length-1);
        }
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }
      TRI_V8_EXCEPTION_MESSAGE(scope, errorNum, errorMessage);
    }
    else {
      return scope.Close(v8::False());
    }
  }
  if (generateDocument) {
    v8::Handle<v8::Value> ret = TRI_ObjectJson(json);

    if (0 != json) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
    return scope.Close(ret);
  }
  else {
    // Note that for this case we will never get a 304 "NOT_MODIFIED"
    if (json != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
    return scope.Close(v8::True());
  }
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a document and returns it
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> DocumentVocbaseCol (bool useCollection,
                                                 v8::Arguments const& argv) {
  v8::HandleScope scope;

  // first and only argument should be a document idenfifier
  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "document(<document-handle>)");
  }

  TRI_voc_key_t key = 0;
  TRI_voc_rid_t rid;
  TRI_vocbase_t* vocbase;
  TRI_vocbase_col_t const* col = 0;

  if (useCollection) {
    // called as db.collection.document()
    col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

    if (col == 0) {
      TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
    }

    vocbase = col->_vocbase;
  }
  else {
    // called as db._document()
    vocbase = GetContextVocBase();
  }

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }


  CollectionNameResolver resolver(vocbase);
  v8::Handle<v8::Value> err = ParseDocumentOrDocumentHandle(vocbase, resolver, col, key, rid, argv[0]);

  CollectionGuard g(useCollection ? 0 : const_cast<TRI_vocbase_col_t*>(col));

  if (key == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  if (! err.IsEmpty()) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    return scope.Close(v8::ThrowException(err));
  }
  
  assert(col != 0);
  assert(key != 0);

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(DocumentVocbaseColCoordinator(col, argv, true));
  }
#endif

  ReadTransactionType trx(vocbase, resolver, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }

  bool usedBarrier = false;
  TRI_barrier_t* barrier = TRI_CreateBarrierElement(&(trx.primaryCollection()->_barrierList));

  if (barrier == 0) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  assert(barrier != 0);

  v8::Handle<v8::Value> result;
  TRI_doc_mptr_t document;
  res = trx.read(&document, key);

  if (res == TRI_ERROR_NO_ERROR) {
    result = TRI_WrapShapedJson<ReadTransactionType >(trx, col->_cid, &document, barrier, usedBarrier);
  }

  res = trx.finish(res);
  TRI_FreeString(TRI_CORE_MEM_ZONE, key);

  if (! usedBarrier) {
    TRI_FreeBarrier(barrier);
  }

  if (res != TRI_ERROR_NO_ERROR || document._data == nullptr) {
    if (res == TRI_ERROR_NO_ERROR) {
      res = TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND;
    }

    TRI_V8_EXCEPTION(scope, res);
  }

  if (rid != 0 && document._rid != rid) {
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_ERROR_ARANGO_CONFLICT, "revision not found");
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a document and returns whether it exists
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ExistsVocbaseCol (bool useCollection,
                                               v8::Arguments const& argv) {
  v8::HandleScope scope;

  // first and only argument should be a document idenfifier
  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "exists(<document-handle>)");
  }

  TRI_voc_key_t key = 0;
  TRI_voc_rid_t rid;
  TRI_vocbase_t* vocbase;
  TRI_vocbase_col_t const* col = 0;

  if (useCollection) {
    // called as db.collection.exists()
    col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

    if (col == 0) {
      TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
    }

    vocbase = col->_vocbase;
  }
  else {
    // called as db._exists()
    vocbase = GetContextVocBase();
  }

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  CollectionNameResolver resolver(vocbase);
  v8::Handle<v8::Value> err = ParseDocumentOrDocumentHandle(vocbase, resolver, col, key, rid, argv[0]);
  
  CollectionGuard g(useCollection ? 0 : const_cast<TRI_vocbase_col_t*>(col));
  
  if (key == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  if (! err.IsEmpty()) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    
    // check if we got an error object in return
    if (err->IsObject()) {
      // yes
      v8::Handle<v8::Object> e = v8::Handle<v8::Object>::Cast(err);

      // get the error object's error code
      if (e->Has(v8::String::New("errorNum"))) {
        // if error code is "collection not found", we'll return false
        if ((int) TRI_ObjectToInt64(e->Get(v8::String::New("errorNum"))) == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
          return scope.Close(v8::False());
        }
      }
    }

    // for any other error that happens, we'll rethrow it
    return scope.Close(v8::ThrowException(err));
  }

  assert(col != 0);
  assert(key != 0);

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(DocumentVocbaseColCoordinator(col, argv, false));
  }
#endif

  ReadTransactionType trx(vocbase, resolver, col->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Value> result;
  TRI_doc_mptr_t document;
  res = trx.read(&document, key);
  res = trx.finish(res);
  
  TRI_FreeString(TRI_CORE_MEM_ZONE, key);

  if (res != TRI_ERROR_NO_ERROR || document._data == nullptr) {
    if (res == TRI_ERROR_NO_ERROR) {
      res = TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND;
    }
  }

  if (res == TRI_ERROR_NO_ERROR && rid != 0 && document._rid != rid) {
    res = TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND;
  }

  if (res == TRI_ERROR_NO_ERROR) {
    return scope.Close(v8::True());
  }
  else if (res == TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND) {
    return scope.Close(v8::False());
  }

  TRI_V8_EXCEPTION(scope, res);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief modifies a document, coordinator case in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> ModifyVocbaseColCoordinator (
                                  TRI_vocbase_col_t const* collection,
                                  TRI_doc_update_policy_e policy,
                                  bool waitForSync,
                                  bool isPatch,
                                  bool keepNull, // only counts if isPatch==true
                                  v8::Arguments const& argv) {
  v8::HandleScope scope;

  assert(collection != 0);

  // First get the initial data:
  string const dbname(collection->_dbName);
  string const collname(collection->_name);

  string key;
  TRI_voc_rid_t rev = 0;
  int error = ParseKeyAndRef(argv[0], key, rev);
  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }

  TRI_json_t* json = TRI_ObjectToJson(argv[1]);
  if (0 == json || json->_type != TRI_JSON_ARRAY) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  triagens::rest::HttpResponse::HttpResponseCode responseCode;
  map<string, string> headers;
  map<string, string> resultHeaders;
  string resultBody;

  error = triagens::arango::modifyDocumentOnCoordinator(
        dbname, collname, key, rev, policy, waitForSync, isPatch,
        keepNull, json, headers, responseCode, resultHeaders, resultBody);
  // Note that the json has been freed inside!
  
  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }

  // report what the DBserver told us: this could now be 201/202 or
  // 400/404
  json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, resultBody.c_str());
  if (responseCode >= triagens::rest::HttpResponse::BAD) {
    if (!TRI_IsArrayJson(json)) {
      if (0 != json) {
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }
      TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
    }
    int errorNum = 0;
    TRI_json_t* subjson = TRI_LookupArrayJson(json, "errorNum");
    if (0 != subjson && TRI_IsNumberJson(subjson)) {
      errorNum = static_cast<int>(subjson->_value._number);
    }
    string errorMessage;
    subjson = TRI_LookupArrayJson(json, "errorMessage");
    if (0 != subjson && TRI_IsStringJson(subjson)) {
      errorMessage = string(subjson->_value._string.data,
                            subjson->_value._string.length-1);
    }
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION_MESSAGE(scope, errorNum, errorMessage);
  }
  v8::Handle<v8::Value> ret = TRI_ObjectJson(json);
  if (0 != json) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }
  return scope.Close(ret);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief internal struct which is used for reading the different option
///        parameters for the replace function
////////////////////////////////////////////////////////////////////////////////

struct ReplaceOptions {
  bool overwrite = true;
  bool waitForSync = false;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief replaces a document
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ReplaceVocbaseCol (bool useCollection,
                                                v8::Arguments const& argv) {
  v8::HandleScope scope;
  ReplaceOptions options;
  TRI_doc_update_policy_e policy = TRI_DOC_UPDATE_ERROR;

  // check the arguments
  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "replace(<document>, <data>, {overwrite: booleanValue, waitForSync: booleanValue})");
  }
  
  // we're only accepting "real" object documents
  if (! argv[1]->IsObject() || argv[1]->IsArray()) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  if (argv.Length() > 2) {
    if (argv[2]->IsObject()) {
      v8::Handle<v8::Object> optionsObject = argv[2].As<v8::Object>();
      if (optionsObject->Has(v8::String::New("overwrite"))) {
        options.overwrite = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("overwrite")));
        policy = ExtractUpdatePolicy(options.overwrite);
      } 
      if (optionsObject->Has(v8::String::New("waitForSync"))) {
        options.waitForSync = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("waitForSync")));
      } 
    } else {// old variant replace(<document>, <data>, <overwrite>, <waitForSync>)
      if (argv.Length() > 2 ) {
        options.overwrite = TRI_ObjectToBoolean(argv[2]);
        policy = ExtractUpdatePolicy(options.overwrite);
      }
      if (argv.Length() > 3 ) {
        options.waitForSync = TRI_ObjectToBoolean(argv[3]);
      }
    }
  }

  TRI_voc_key_t key = 0;
  TRI_voc_rid_t rid;
  TRI_voc_rid_t actualRevision = 0;

  TRI_vocbase_t* vocbase;
  TRI_vocbase_col_t const* col = 0;

  if (useCollection) {
    // called as db.collection.replace()
    col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

    if (col == 0) {
      TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
    }

    vocbase = col->_vocbase;
  }
  else {
    // called as db._replace()
    vocbase = GetContextVocBase();
  }
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  CollectionNameResolver resolver(vocbase);
  v8::Handle<v8::Value> err = ParseDocumentOrDocumentHandle(vocbase, resolver, col, key, rid, argv[0]);
  
  CollectionGuard g(useCollection ? 0 : const_cast<TRI_vocbase_col_t*>(col));
  
  if (key == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  if (! err.IsEmpty()) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    return scope.Close(v8::ThrowException(err));
  }

  assert(col != 0);
  assert(key != nullptr);

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(ModifyVocbaseColCoordinator(col, 
                                                   policy, 
                                                   options.waitForSync,
                                                   false,  // isPatch
                                                   true,   // keepNull, does not matter
                                                   argv));
  }
#endif

  SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, 1> trx(vocbase, resolver, col->_cid);
  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }
    
  TRI_primary_collection_t* primary = trx.primaryCollection();
  TRI_memory_zone_t* zone = primary->_shaper->_memoryZone;

  TRI_doc_mptr_t document;
  
  // we must lock here, because below we are
  // - reading the old document in coordinator case
  // - creating a shape, which might trigger a write into the collection  
  trx.lockWrite();

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isDBserver()) {
    // compare attributes in shardKeys
    const string cidString = StringUtils::itoa(primary->base._info._planId);

    TRI_json_t* json = TRI_ObjectToJson(argv[1]);

    if (json == 0) {
      TRI_FreeString(TRI_CORE_MEM_ZONE, key);
      TRI_V8_EXCEPTION_MEMORY(scope);
    }
  
    res = trx.read(&document, key);

    if (res != TRI_ERROR_NO_ERROR || document._data == nullptr) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      TRI_FreeString(TRI_CORE_MEM_ZONE, key);
      TRI_V8_EXCEPTION(scope, res);
    }
  
    TRI_shaped_json_t shaped;
    TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, document._data);
    TRI_json_t* old = TRI_JsonShapedJson(primary->_shaper, &shaped);

    if (old == 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      TRI_FreeString(TRI_CORE_MEM_ZONE, key);
      TRI_V8_EXCEPTION_MEMORY(scope);
    }

    if (shardKeysChanged(col->_dbName, cidString, old, json, false)) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, old);
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      TRI_FreeString(TRI_CORE_MEM_ZONE, key);
      TRI_V8_EXCEPTION(scope, TRI_ERROR_CLUSTER_MUST_NOT_CHANGE_SHARDING_ATTRIBUTES);
    } 
    
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, old);
  }
#endif

  TRI_shaped_json_t* shaped = TRI_ShapedJsonV8Object(argv[1], primary->_shaper, true, true);

  if (shaped == 0) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_errno(), "<data> cannot be converted into JSON shape");
  }
  
  Barrier barrier(primary);

  res = trx.updateDocument(key, &document, shaped, policy, options.waitForSync, rid, &actualRevision);

  res = trx.finish(res);
  
  TRI_FreeShapedJson(zone, shaped);
  TRI_FreeString(TRI_CORE_MEM_ZONE, key);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  assert(document._data != nullptr);
  char const* docKey = TRI_EXTRACT_MARKER_KEY(&document);

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  v8::Handle<v8::Object> result = v8::Object::New();
  result->Set(v8g->_IdKey, V8DocumentId(resolver.getCollectionName(col->_cid), docKey));
  result->Set(v8g->_RevKey, V8RevisionId(document._rid));
  result->Set(v8g->_OldRevKey, V8RevisionId(actualRevision));
  result->Set(v8g->_KeyKey, v8::String::New(docKey));
    
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves a document
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> SaveVocbaseCol (
    SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, 1>* trx,
    TRI_vocbase_col_t* col,
    v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 1 || argv.Length() > 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "save(<data>, [<waitForSync>])");
  }
  
  const bool forceSync = ExtractForceSync(argv, 2);

  // set document key
  TRI_voc_key_t key = 0;
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  int res;

  if (argv[0]->IsObject()) {
    res = ExtractDocumentKey(argv[0]->ToObject(), key);

    if (res != TRI_ERROR_NO_ERROR && res != TRI_ERROR_ARANGO_DOCUMENT_KEY_MISSING) {
      TRI_V8_EXCEPTION(scope, res);
    }
  }
  else {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  TRI_primary_collection_t* primary = trx->primaryCollection();
  TRI_memory_zone_t* zone = primary->_shaper->_memoryZone;

  trx->lockWrite();

  TRI_shaped_json_t* shaped = TRI_ShapedJsonV8Object(argv[0], primary->_shaper, true, true);

  if (shaped == nullptr) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_errno(), "<data> cannot be converted into JSON shape");
  }

  Barrier barrier(primary);

  TRI_doc_mptr_t document;
  res = trx->createDocument(key, &document, shaped, forceSync);

  res = trx->finish(res);
  
  TRI_FreeShapedJson(zone, shaped); 
    
  FREE_STRING(TRI_CORE_MEM_ZONE, key);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  assert(document._data != nullptr);
  char const* docKey = TRI_EXTRACT_MARKER_KEY(&document);

  v8::Handle<v8::Object> result = v8::Object::New();
  
  result->Set(v8g->_IdKey, V8DocumentId(trx->resolver().getCollectionName(col->_cid), docKey));
  result->Set(v8g->_RevKey, V8RevisionId(document._rid));
  result->Set(v8g->_KeyKey, v8::String::New(docKey));
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves a new edge document
///
/// @FUN{@FA{edge-collection}.save(@FA{from}, @FA{to}, @FA{document})}
///
/// Saves a new edge and returns the document-handle. @FA{from} and @FA{to}
/// must be documents or document references.
///
/// @FUN{@FA{edge-collection}.save(@FA{from}, @FA{to}, @FA{document}, @FA{waitForSync})}
///
/// The optional @FA{waitForSync} parameter can be used to force
/// synchronisation of the document creation operation to disk even in case
/// that the @LIT{waitForSync} flag had been disabled for the entire collection.
/// Thus, the @FA{waitForSync} parameter can be used to force synchronisation
/// of just specific operations. To use this, set the @FA{waitForSync} parameter
/// to @LIT{true}. If the @FA{waitForSync} parameter is not specified or set to
/// @LIT{false}, then the collection's default @LIT{waitForSync} behavior is
/// applied. The @FA{waitForSync} parameter cannot be used to disable
/// synchronisation for collections that have a default @LIT{waitForSync} value
/// of @LIT{true}.
///
/// @EXAMPLES
///
/// @TINYEXAMPLE{shell_create-edge,create an edge}
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> SaveEdgeCol (
    SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, 1>* trx,
    TRI_vocbase_col_t* col,
    v8::Arguments const& argv) {
  v8::HandleScope scope;
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  if (argv.Length() < 3 || argv.Length() > 4) {
    TRI_V8_EXCEPTION_USAGE(scope, "save(<from>, <to>, <data>, [<waitForSync>])");
  }

  CollectionNameResolver resolver(col->_vocbase);

  // set document key
  TRI_voc_key_t key = nullptr;
  int res;

  if (argv[2]->IsObject() && ! argv[2]->IsArray()) {
    res = ExtractDocumentKey(argv[2]->ToObject(), key);

    if (res != TRI_ERROR_NO_ERROR && res != TRI_ERROR_ARANGO_DOCUMENT_KEY_MISSING) {
      TRI_V8_EXCEPTION(scope, res);
    }
  }
  else {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  const bool forceSync = ExtractForceSync(argv, 4);

  TRI_document_edge_t edge;
  // the following values are defaults that will be overridden below
  edge._fromCid = 0;
  edge._toCid   = 0;
  edge._fromKey = nullptr;
  edge._toKey   = nullptr;

  // extract from
  res = TRI_ParseVertex(resolver, edge._fromCid, edge._fromKey, argv[0], false);

  if (res != TRI_ERROR_NO_ERROR) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }

  // extract to
  res = TRI_ParseVertex(resolver, edge._toCid, edge._toKey, argv[1], false);
  
  if (res != TRI_ERROR_NO_ERROR) {
    FREE_STRING(TRI_CORE_MEM_ZONE, edge._fromKey);
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_primary_collection_t* primary = trx->primaryCollection();
  TRI_memory_zone_t* zone = primary->_shaper->_memoryZone;

  trx->lockWrite();
  // extract shaped data
  TRI_shaped_json_t* shaped = TRI_ShapedJsonV8Object(argv[2], primary->_shaper, true, true);

  if (shaped == nullptr) {
    FREE_STRING(TRI_CORE_MEM_ZONE, edge._fromKey);
    FREE_STRING(TRI_CORE_MEM_ZONE, edge._toKey);
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_errno(), "<data> cannot be converted into JSON shape");
  }

  TRI_doc_mptr_t document;
  res = trx->createEdge(key, &document, shaped, forceSync, &edge);

  Barrier barrier(primary);

  res = trx->finish(res);
   
  TRI_FreeShapedJson(zone, shaped);
  FREE_STRING(TRI_CORE_MEM_ZONE, edge._fromKey);
  FREE_STRING(TRI_CORE_MEM_ZONE, edge._toKey);
  FREE_STRING(TRI_CORE_MEM_ZONE, key);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  assert(document._data != nullptr);

  char const* docKey = TRI_EXTRACT_MARKER_KEY(&document);
  v8::Handle<v8::Object> result = v8::Object::New();
  result->Set(v8g->_IdKey, V8DocumentId(resolver.getCollectionName(col->_cid), docKey));
  result->Set(v8g->_RevKey, V8RevisionId(document._rid));
  result->Set(v8g->_KeyKey, v8::String::New(docKey));

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief internal struct which is used for reading the different option
///        parameters for the update function
////////////////////////////////////////////////////////////////////////////////

struct UpdateOptions {
  bool overwrite = true;
  bool keepNull = true;
  bool waitForSync = false;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief updates (patches) a document
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> UpdateVocbaseCol (bool useCollection,
                                               v8::Arguments const& argv) {
  v8::HandleScope scope;
  UpdateOptions options;
  TRI_doc_update_policy_e policy = TRI_DOC_UPDATE_ERROR;

  // check the arguments
  if (argv.Length() < 2 || argv.Length() > 5) {
    TRI_V8_EXCEPTION_USAGE(scope, "update(<document>, <data>, {overwrite: booleanValue, keepNull: booleanValue, waitForSync: booleanValue})");
  }

  if (argv.Length() > 2) {
    if (argv[2]->IsObject()) {
      v8::Handle<v8::Object> optionsObject = argv[2].As<v8::Object>();
      if (optionsObject->Has(v8::String::New("overwrite"))) {
        options.overwrite = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("overwrite")));
        policy = ExtractUpdatePolicy(options.overwrite);
      } 
      if (optionsObject->Has(v8::String::New("keepNull"))) {
        options.keepNull = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("keepNull")));
      } 
      if (optionsObject->Has(v8::String::New("waitForSync"))) {
        options.waitForSync = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("waitForSync")));
      } 
    } else { // old variant update(<document>, <data>, <overwrite>, <keepNull>, <waitForSync>)
      if (argv.Length() > 2 ) {
        options.overwrite = TRI_ObjectToBoolean(argv[2]);
        policy = ExtractUpdatePolicy(options.overwrite);
      }
      if (argv.Length() > 3 ) {
        options.keepNull = TRI_ObjectToBoolean(argv[3]);
      }
      if (argv.Length() > 4 ) {
        options.waitForSync = TRI_ObjectToBoolean(argv[4]);
      }
    }
  }
// LOG_ERROR ( "overwrite %d  keepNull %d waitForSync %d" , options.overwrite , options.keepNull , options.waitForSync);
  // delete null attributes
  // default value: null values are saved as Null

  TRI_voc_key_t key = 0;
  TRI_voc_rid_t rid;
  TRI_voc_rid_t actualRevision = 0;
  TRI_vocbase_t* vocbase;
  TRI_vocbase_col_t const* col = 0;

  if (useCollection) {
    // called as db.collection.update()
    col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

    if (col == 0) {
      TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
    }

    vocbase = col->_vocbase;
  }
  else {
    // called as db._update()
    vocbase = GetContextVocBase();
  }
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  CollectionNameResolver resolver(vocbase);
  v8::Handle<v8::Value> err = ParseDocumentOrDocumentHandle(vocbase, resolver, col, key, rid, argv[0]);
  
  CollectionGuard g(useCollection ? 0 : const_cast<TRI_vocbase_col_t*>(col));
  
  if (key == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }
  
  if (! err.IsEmpty()) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    return scope.Close(v8::ThrowException(err));
  }
  
  assert(col != 0);
  assert(key != 0);

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(ModifyVocbaseColCoordinator(col, 
                                                   policy, 
                                                   options.waitForSync,
                                                   true,  // isPatch
                                                   options.keepNull,
                                                   argv));
  }
#endif

  if (! argv[1]->IsObject() || argv[1]->IsArray()) {
    // we're only accepting "real" object documents
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  TRI_json_t* json = TRI_ObjectToJson(argv[1]);

  if (json == 0) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_errno(), "<data> is no valid JSON");
  }


  SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, 1> trx(vocbase, resolver, col->_cid);
  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }

  // we must use a write-lock that spans both the initial read and the update.
  // otherwise the operation is not atomic
  trx.lockWrite();

  TRI_doc_mptr_t document;
  res = trx.read(&document, key);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_primary_collection_t* primary = trx.primaryCollection();
  TRI_memory_zone_t* zone = primary->_shaper->_memoryZone;

  TRI_shaped_json_t shaped;
  TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, document._data);
  TRI_json_t* old = TRI_JsonShapedJson(primary->_shaper, &shaped);

  if (old == 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isDBserver()) {
    // compare attributes in shardKeys
    const string cidString = StringUtils::itoa(primary->base._info._planId);

    if (shardKeysChanged(col->_dbName, cidString, old, json, true)) {
      TRI_FreeJson(primary->_shaper->_memoryZone, old);
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      FREE_STRING(TRI_CORE_MEM_ZONE, key);
    
      TRI_V8_EXCEPTION(scope, TRI_ERROR_CLUSTER_MUST_NOT_CHANGE_SHARDING_ATTRIBUTES);
    } 
  }
#endif

  TRI_json_t* patchedJson = TRI_MergeJson(TRI_UNKNOWN_MEM_ZONE, old, json, ! options.keepNull);
  TRI_FreeJson(zone, old);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  if (patchedJson == 0) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  res = trx.updateDocument(key, &document, patchedJson, policy, options.waitForSync, rid, &actualRevision);

  Barrier barrier(primary);
  res = trx.finish(res);

  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, patchedJson);
  FREE_STRING(TRI_CORE_MEM_ZONE, key);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  assert(document._data != nullptr);
  char const* docKey = TRI_EXTRACT_MARKER_KEY(&document);

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  v8::Handle<v8::Object> result = v8::Object::New();
  result->Set(v8g->_IdKey, V8DocumentId(resolver.getCollectionName(col->_cid), docKey));
  result->Set(v8g->_RevKey, V8RevisionId(document._rid));
  result->Set(v8g->_OldRevKey, V8RevisionId(actualRevision));
  result->Set(v8g->_KeyKey, v8::String::New(docKey));

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a document, coordinator case in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> RemoveVocbaseColCoordinator (TRI_vocbase_col_t const* collection,
                                                          TRI_doc_update_policy_e policy,
                                                          bool waitForSync,
                                                          v8::Arguments const& argv) {
  v8::HandleScope scope;

  // First get the initial data:
  string const dbname(collection->_dbName);
  string const collname(collection->_name);

  string key;
  TRI_voc_rid_t rev = 0;
  int error = ParseKeyAndRef(argv[0], key, rev);

  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }

  triagens::rest::HttpResponse::HttpResponseCode responseCode;
  map<string, string> resultHeaders;
  string resultBody;
  map<string, string> headers;

  error = triagens::arango::deleteDocumentOnCoordinator(
            dbname, collname, key, rev, policy, waitForSync, headers,
            responseCode, resultHeaders, resultBody);
    
  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }
  // report what the DBserver told us: this could now be 200/202 or
  // 404/412
  TRI_json_t* json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, resultBody.c_str());
  if (responseCode >= triagens::rest::HttpResponse::BAD) {
    if (! TRI_IsArrayJson(json)) {
      if (0 != json) {
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }
      TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
    }
    int errorNum = 0;
    TRI_json_t* subjson = TRI_LookupArrayJson(json, "errorNum");
    if (0 != subjson && TRI_IsNumberJson(subjson)) {
      errorNum = static_cast<int>(subjson->_value._number);
    }
    string errorMessage;
    subjson = TRI_LookupArrayJson(json, "errorMessage");
    if (0 != subjson && TRI_IsStringJson(subjson)) {
      errorMessage = string(subjson->_value._string.data,
                            subjson->_value._string.length-1);
    }
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  
    if (errorNum == TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND && 
        policy == TRI_DOC_UPDATE_LAST_WRITE) {
      // this is not considered an error
      return scope.Close(v8::False());
    }

    TRI_V8_EXCEPTION_MESSAGE(scope, errorNum, errorMessage);
  }

  if (0 != json) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }

  return scope.Close(v8::True());
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief internal struct which is used for reading the different option
///        parameters for the remove function
////////////////////////////////////////////////////////////////////////////////

struct RemoveOptions {
  bool overwrite = true;
  bool waitForSync = false;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a document
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> RemoveVocbaseCol (bool useCollection,
                                               v8::Arguments const& argv) {
  v8::HandleScope scope;
  ReplaceOptions options;
  TRI_doc_update_policy_e policy = TRI_DOC_UPDATE_ERROR;

  // check the arguments
  if (argv.Length() < 1 || argv.Length() > 3) {
    TRI_V8_EXCEPTION_USAGE(scope, "remove(<document>, <data>, {overwrite: booleanValue, waitForSync: booleanValue})");
  }

  if (argv.Length() > 1) {
    if (argv[1]->IsObject()) {
      v8::Handle<v8::Object> optionsObject = argv[1].As<v8::Object>();
      if (optionsObject->Has(v8::String::New("overwrite"))) {
        options.overwrite = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("overwrite")));
        policy = ExtractUpdatePolicy(options.overwrite);
      } 
      if (optionsObject->Has(v8::String::New("waitForSync"))) {
        options.waitForSync = TRI_ObjectToBoolean(optionsObject->Get(v8::String::New("waitForSync")));
      } 
    } else {// old variant replace(<document>, <data>, <overwrite>, <waitForSync>)
      if (argv.Length() > 1 ) {
        options.overwrite = TRI_ObjectToBoolean(argv[1]);
        policy = ExtractUpdatePolicy(options.overwrite);
      }
      if (argv.Length() > 2 ) {
        options.waitForSync = TRI_ObjectToBoolean(argv[2]);
      }
    }
  }

  TRI_voc_key_t key = 0;
  TRI_voc_rid_t rid;
  TRI_voc_rid_t actualRevision = 0;
  TRI_vocbase_t* vocbase;
  TRI_vocbase_col_t const* col = 0;

  if (useCollection) {
    // called as db.collection.remove()
    col = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

    if (col == 0) {
      TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
    }

    vocbase = col->_vocbase;
  }
  else {
    // called as db._remove()
    vocbase = GetContextVocBase();
  }
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  CollectionNameResolver resolver(vocbase);
  v8::Handle<v8::Value> err = ParseDocumentOrDocumentHandle(vocbase, resolver, col, key, rid, argv[0]);
  
  CollectionGuard g(useCollection ? 0 : const_cast<TRI_vocbase_col_t*>(col));
  
  if (key == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD);
  }

  if (! err.IsEmpty()) {
    FREE_STRING(TRI_CORE_MEM_ZONE, key);
    return scope.Close(v8::ThrowException(err));
  }

  assert(col != 0);
  assert(key != 0);

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(RemoveVocbaseColCoordinator(col, policy, options.waitForSync, argv));
  }
#endif

  SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, 1> trx(vocbase, resolver, col->_cid);
  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    TRI_V8_EXCEPTION(scope, res);
  }

  res = trx.deleteDocument(key, policy, options.waitForSync, rid, &actualRevision);
  res = trx.finish(res);
  
  TRI_FreeString(TRI_CORE_MEM_ZONE, key);

  if (res != TRI_ERROR_NO_ERROR) {
    if (res == TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND && policy == TRI_DOC_UPDATE_LAST_WRITE) {
      return scope.Close(v8::False());
    }
    else {
      TRI_V8_EXCEPTION(scope, res);
    }
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a collection on the coordinator
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER

static v8::Handle<v8::Value> CreateCollectionCoordinator (
                                  v8::Arguments const& argv,
                                  TRI_col_type_e collectionType,
                                  std::string const& databaseName,
                                  TRI_col_info_t& parameter,
                                  TRI_vocbase_t* vocbase) {
  v8::HandleScope scope;
  
  string const name = TRI_ObjectToString(argv[0]);
  
  if (! TRI_IsAllowedNameCollection(parameter._isSystem, name.c_str())) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_ILLEGAL_NAME);
  }

  bool allowUserKeys = true;
  uint64_t numberOfShards = 1;
  vector<string> shardKeys;
      
  // default shard key
  shardKeys.push_back("_key");
  
  if (2 <= argv.Length()) {
    if (! argv[1]->IsObject()) {
      TRI_V8_TYPE_ERROR(scope, "<properties> must be an object");
    }

    v8::Handle<v8::Object> p = argv[1]->ToObject();
    
    if (p->Has(TRI_V8_SYMBOL("keyOptions")) && p->Get(TRI_V8_SYMBOL("keyOptions"))->IsObject()) {
      v8::Handle<v8::Object> o = v8::Handle<v8::Object>::Cast(p->Get(TRI_V8_SYMBOL("keyOptions")));

      if (o->Has(TRI_V8_SYMBOL("type"))) {
        string const type = TRI_ObjectToString(o->Get(TRI_V8_SYMBOL("type")));

        if (type != "" && type != "traditional") {
          // invalid key generator
          TRI_V8_EXCEPTION_MESSAGE(scope, 
                                   TRI_ERROR_CLUSTER_UNSUPPORTED, 
                                   "non-traditional key generators are not supported for sharded collections");
        }
      }

      if (o->Has(TRI_V8_SYMBOL("allowUserKeys"))) {
        allowUserKeys = TRI_ObjectToBoolean(o->Get(TRI_V8_SYMBOL("allowUserKeys")));
      }
    }

    if (p->Has(TRI_V8_SYMBOL("numberOfShards"))) {
      numberOfShards = TRI_ObjectToUInt64(p->Get(TRI_V8_SYMBOL("numberOfShards")), false);
    }
    
    if (p->Has(TRI_V8_SYMBOL("shardKeys"))) {
      shardKeys.clear();

      if (p->Get(TRI_V8_SYMBOL("shardKeys"))->IsArray()) {
        v8::Handle<v8::Array> k = v8::Handle<v8::Array>::Cast(p->Get(TRI_V8_SYMBOL("shardKeys")));

        for (uint32_t i = 0 ; i < k->Length(); ++i) {
          v8::Handle<v8::Value> v = k->Get(i);
          if (v->IsString()) {
            string const key = TRI_ObjectToString(v);

            // system attributes are not allowed (except _key)
            if (! key.empty() && (key[0] != '_' || key == "_key")) {
              shardKeys.push_back(key);
            }
          }
        }
      }
    }
  }

  if (numberOfShards == 0 || numberOfShards > 1000) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "invalid number of shards");
  }
    
  if (shardKeys.empty() || shardKeys.size() > 8) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "invalid number of shard keys");
  }

  ClusterInfo* ci = ClusterInfo::instance();

  // fetch a unique id for the new collection plus one for each shard to create
  uint64_t const id = ci->uniqid(1 + numberOfShards);

  // collection id is the first unique id we got
  string const cid = StringUtils::itoa(id);

  // fetch list of available servers in cluster, and shuffle them randomly
  vector<string> dbServers = ci->getCurrentDBServers();

  if (dbServers.empty()) {
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_ERROR_INTERNAL, "no database servers found in cluster");
  }

  random_shuffle(dbServers.begin(), dbServers.end());

  // now create the shards
  std::map<std::string, std::string> shards; 
  for (uint64_t i = 0; i < numberOfShards; ++i) {
    // determine responsible server
    string serverId = dbServers[i % dbServers.size()];

    // determine shard id
    string shardId = "s" + StringUtils::itoa(id + 1 + i);

    shards.insert(std::make_pair(shardId, serverId));
  }
 
  // now create the JSON for the collection 
  TRI_json_t* json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (json == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateString2CopyJson(TRI_UNKNOWN_MEM_ZONE, cid.c_str(), cid.size()));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "name", TRI_CreateString2CopyJson(TRI_UNKNOWN_MEM_ZONE, name.c_str(), name.size()));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, (int) collectionType));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "status", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, (int) TRI_VOC_COL_STATUS_LOADED));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "deleted", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, parameter._deleted));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "doCompact", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, parameter._doCompact));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "isSystem", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, parameter._isSystem));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "isVolatile", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, parameter._isVolatile)); 
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "waitForSync", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, parameter._waitForSync));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "journalSize", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, parameter._maximalSize));

  TRI_json_t* keyOptions = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (keyOptions != 0) {
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, keyOptions, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "traditional"));
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, keyOptions, "allowUserKeys", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, allowUserKeys));

    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "keyOptions", TRI_CopyJson(TRI_UNKNOWN_MEM_ZONE, keyOptions));
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "shardKeys", JsonHelper::stringList(TRI_UNKNOWN_MEM_ZONE, shardKeys));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "shards", JsonHelper::stringObject(TRI_UNKNOWN_MEM_ZONE, shards));

  TRI_json_t* indexes = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  if (indexes == 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  // create a dummy primary index
  TRI_index_t* idx = TRI_CreatePrimaryIndex(0);

  if (idx == 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, indexes);
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  TRI_json_t* idxJson = idx->json(idx);
  TRI_FreeIndex(idx);

  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, indexes, TRI_CopyJson(TRI_UNKNOWN_MEM_ZONE, idxJson));
  TRI_FreeJson(TRI_CORE_MEM_ZONE, idxJson);

  if (collectionType == TRI_COL_TYPE_EDGE) {
    // create a dummy edge index
    idx = TRI_CreateEdgeIndex(0, id);

    if (idx == 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, indexes);
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
    }

    idxJson = idx->json(idx);
    TRI_FreeIndex(idx);

    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, indexes, TRI_CopyJson(TRI_UNKNOWN_MEM_ZONE, idxJson));
    TRI_FreeJson(TRI_CORE_MEM_ZONE, idxJson);
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "indexes", indexes);

  string errorMsg;
  int myerrno = ci->createCollectionCoordinator(databaseName, 
                                                cid, 
                                                numberOfShards, 
                                                json,
                                                errorMsg, 
                                                240.0);

  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  if (myerrno != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, myerrno, errorMsg);
  }
  ci->loadPlannedCollections();

  shared_ptr<CollectionInfo> const& c = ci->getCollection(databaseName, cid);
  TRI_vocbase_col_t* newcoll = CoordinatorCollection(vocbase, *c);
  return scope.Close(TRI_WrapCollection(newcoll));
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief create a collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> CreateVocBase (v8::Arguments const& argv, 
                                            TRI_col_type_e collectionType) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // ...........................................................................
  // We require exactly 1 or exactly 2 arguments -- anything else is an error
  // ...........................................................................

  if (argv.Length() < 1 || argv.Length() > 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "_create(<name>, <properties>)");
  }

  PREVENT_EMBEDDED_TRANSACTION(scope); 


  // set default journal size
  TRI_voc_size_t effectiveSize = vocbase->_settings.defaultMaximalSize;

  // extract the name
  const string name = TRI_ObjectToString(argv[0]);

  // extract the parameters
  TRI_col_info_t parameter;
    
  if (2 <= argv.Length()) {
    if (! argv[1]->IsObject()) {
      TRI_V8_TYPE_ERROR(scope, "<properties> must be an object");
    }

    v8::Handle<v8::Object> p = argv[1]->ToObject();
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

    if (p->Has(v8g->JournalSizeKey)) {
      double s = TRI_ObjectToDouble(p->Get(v8g->JournalSizeKey));

      if (s < TRI_JOURNAL_MINIMAL_SIZE) {
        TRI_V8_EXCEPTION_PARAMETER(scope, "<properties>.journalSize is too small");
      }

      // overwrite journal size with user-specified value
      effectiveSize = (TRI_voc_size_t) s;
    }

    // get optional values
    TRI_json_t* keyOptions = 0;
    if (p->Has(v8g->KeyOptionsKey)) {
      keyOptions = TRI_ObjectToJson(p->Get(v8g->KeyOptionsKey));
    }

    // TRI_InitCollectionInfo will copy keyOptions
    TRI_InitCollectionInfo(vocbase, &parameter, name.c_str(), collectionType, effectiveSize, keyOptions);

    if (keyOptions != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, keyOptions);
    }
    
    if (p->Has(v8::String::New("planId"))) {
      parameter._planId = TRI_ObjectToUInt64(p->Get(v8::String::New("planId")), true);
    }

    if (p->Has(v8g->WaitForSyncKey)) {
      parameter._waitForSync = TRI_ObjectToBoolean(p->Get(v8g->WaitForSyncKey));
    }

    if (p->Has(v8g->DoCompactKey)) {
      parameter._doCompact = TRI_ObjectToBoolean(p->Get(v8g->DoCompactKey));
    }
    else {
      // default value for compaction
      parameter._doCompact = true;
    }

    if (p->Has(v8g->IsSystemKey)) {
      parameter._isSystem = TRI_ObjectToBoolean(p->Get(v8g->IsSystemKey));
    }

    if (p->Has(v8g->IsVolatileKey)) {
#ifdef TRI_HAVE_ANONYMOUS_MMAP
      parameter._isVolatile = TRI_ObjectToBoolean(p->Get(v8g->IsVolatileKey));
#else
      TRI_FreeCollectionInfoOptions(&parameter);
      TRI_V8_EXCEPTION_PARAMETER(scope, "volatile collections are not supported on this platform");
#endif
    }

    if (parameter._isVolatile && parameter._waitForSync) {
      // the combination of waitForSync and isVolatile makes no sense
      TRI_FreeCollectionInfoOptions(&parameter);
      TRI_V8_EXCEPTION_PARAMETER(scope, "volatile collections do not support the waitForSync option");
    }

  }
  else {
    TRI_InitCollectionInfo(vocbase, &parameter, name.c_str(), collectionType, effectiveSize, 0);
  }


#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    v8::Handle<v8::Value> result = CreateCollectionCoordinator(argv, collectionType, vocbase->_name, parameter, vocbase);
    TRI_FreeCollectionInfoOptions(&parameter);

    return scope.Close(result);
  }
#endif

  TRI_vocbase_col_t const* collection = TRI_CreateCollectionVocBase(vocbase, 
                                                                    &parameter, 
                                                                    0, 
                                                                    TRI_GetIdServer());

  TRI_FreeCollectionInfoOptions(&parameter);

  if (collection == 0) {
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_errno(), "cannot create collection");
  }

  v8::Handle<v8::Value> result = TRI_WrapCollection(collection);

  if (result.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an Ahuacatl error in a javascript object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Object> CreateErrorObjectAhuacatl (TRI_aql_error_t* error) {
  v8::HandleScope scope;

  char* message = TRI_GetErrorMessageAql(error);

  if (message != 0) {
    std::string str(message);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, message);

    return scope.Close(TRI_CreateErrorObject(error->_file, error->_line, TRI_GetErrorCodeAql(error), str));
  }

  return scope.Close(TRI_CreateErrorObject(error->_file, error->_line, TRI_ERROR_OUT_OF_MEMORY));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief function that encapsulates execution of an AQL query
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ExecuteQueryNativeAhuacatl (TRI_aql_context_t* const context,
                                                         const TRI_json_t* const parameters) {
  v8::HandleScope scope;

  // parse & validate
  // bind values
  if (! TRI_ValidateQueryContextAql(context) ||
      ! TRI_BindQueryContextAql(context, parameters) ||
      ! TRI_SetupCollectionsContextAql(context)) {
    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&context->_error);

    return scope.Close(v8::ThrowException(errorObject));
  }

  // note: a query is not necessarily collection-based.
  // this means that the _collections array might contain 0 collections!
  CollectionNameResolver resolver(context->_vocbase);
  AhuacatlTransaction<EmbeddableTransaction<V8TransactionContext> > trx(context->_vocbase, resolver, context);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    // check if there is some error data registered in the transaction
    const string errorData = trx.getErrorData();

    if (errorData.empty()) {
      // no error data. return a regular error message
      TRI_V8_EXCEPTION(scope, res);
    }
    else {
      // there is specific error data. return a more tailored error message
      const string errorMsg = "cannot execute query: " + string(TRI_errno_string(res)) + ": '" + errorData + "'";
      return scope.Close(v8::ThrowException(TRI_CreateErrorObject(__FILE__,
                                                                  __LINE__,
                                                                  res,
                                                                  errorMsg)));
    }
  }

  // optimise
  if (! TRI_OptimiseQueryContextAql(context)) {
    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&context->_error);

    return scope.Close(v8::ThrowException(errorObject));
  }

  // add barriers for all collections used
  if (! TRI_AddBarrierCollectionsAql(context)) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot add barrier");
  }


  // generate code
  size_t codeLength = 0;
  char* code = TRI_GenerateCodeAql(context, &codeLength);

  if (code == 0 || 
      context->_error._code != TRI_ERROR_NO_ERROR) {
    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&context->_error);

    return scope.Close(v8::ThrowException(errorObject));
  }

  assert(codeLength > 0);
  // execute code
  v8::Handle<v8::Value> result = TRI_ExecuteJavaScriptString(v8::Context::GetCurrent(), 
                                                             v8::String::New(code, (int) codeLength), 
                                                             TRI_V8_SYMBOL("query"), 
                                                             false);

  trx.finish(TRI_ERROR_NO_ERROR);
  
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, code);

  // return the result as a javascript array
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief run a query and return the results as a cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ExecuteQueryCursorAhuacatl (TRI_vocbase_t* const vocbase,
                                                         TRI_aql_context_t* const context,
                                                         const TRI_json_t* const parameters,
                                                         const bool doCount,
                                                         const uint32_t batchSize) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;

  v8::Handle<v8::Value> result = ExecuteQueryNativeAhuacatl(context, parameters);

  if (tryCatch.HasCaught()) {
    if (tryCatch.CanContinue()) {
      return scope.Close(v8::ThrowException(tryCatch.Exception()));
    }
    else {
      return scope.Close(result);
    }
  }
  
  if (! result->IsObject()) {
    // some error happened
    return scope.Close(result);
  }
  
  v8::Handle<v8::Object> resultObject = v8::Handle<v8::Object>::Cast(result);
  if (! resultObject->Has(TRI_V8_SYMBOL("docs"))) {
    // some error happened
    return scope.Close(result);
  }

  v8::Handle<v8::Value> docs = resultObject->Get(TRI_V8_SYMBOL("docs"));

  if (! docs->IsArray()) {
    // some error happened
    return scope.Close(result);
  }

  // result is an array...
  v8::Handle<v8::Array> r = v8::Handle<v8::Array>::Cast(docs);

  if (r->Length() <= batchSize) {
    // return the array value as it is. this is a performance optimisation
    return scope.Close(result);
  }

  // return the result as a cursor object. 
  // transform the result into JSON first
  TRI_json_t* json = TRI_ObjectToJson(docs);

  if (json == 0) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  TRI_general_cursor_result_t* cursorResult = TRI_CreateResultAql(json);

  if (cursorResult == 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION_MEMORY(scope);
  }
  
  // extra return values
  TRI_json_t* extra = 0;
  if (resultObject->Has(TRI_V8_SYMBOL("extra"))) {
    extra = TRI_ObjectToJson(resultObject->Get(TRI_V8_SYMBOL("extra")));
  }

  TRI_general_cursor_t* cursor = TRI_CreateGeneralCursor(vocbase, cursorResult, doCount, batchSize, extra);

  if (cursor == 0) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, cursorResult);
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    if (extra != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, extra);
    }
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  assert(cursor != 0);

  v8::Handle<v8::Value> cursorObject = WrapGeneralCursor(cursor);

  if (cursorObject.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(cursorObject);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                   GENERAL CURSORS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief weak reference callback for general cursors
////////////////////////////////////////////////////////////////////////////////

static void WeakGeneralCursorCallback (v8::Isolate* isolate,
                                       v8::Persistent<v8::Value> object,
                                       void* parameter) {
  v8::HandleScope scope; // do not remove, will fail otherwise!!
  
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  
  v8g->_hasDeadObjects = true;
  
  TRI_general_cursor_t* cursor = (TRI_general_cursor_t*) parameter;
  
  TRI_ReleaseGeneralCursor(cursor);
  
  // decrease the reference-counter for the database
  TRI_ReleaseVocBase(cursor->_vocbase);

  // dispose and clear the persistent handle
  object.Dispose(isolate);
  object.Clear();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stores a general cursor in a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> WrapGeneralCursor (void* cursor) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;

  assert(cursor != 0);

  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) isolate->GetData();

  v8::Handle<v8::Object> result = v8g->GeneralCursorTempl->NewInstance();

  if (! result.IsEmpty()) {
    TRI_general_cursor_t* c = (TRI_general_cursor_t*) cursor;
    TRI_UseGeneralCursor(c);

    // increase the reference-counter for the database
    TRI_UseVocBase(c->_vocbase);

    v8::Persistent<v8::Value> persistent = v8::Persistent<v8::Value>::New(isolate, v8::External::New(cursor));

    if (tryCatch.HasCaught()) {
      return scope.Close(v8::Undefined());
    }

    result->SetInternalField(SLOT_CLASS_TYPE, v8::Integer::New(WRP_GENERAL_CURSOR_TYPE));
    result->SetInternalField(SLOT_CLASS, persistent);

    persistent.MakeWeak(isolate, cursor, WeakGeneralCursorCallback);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts a cursor from a V8 object
////////////////////////////////////////////////////////////////////////////////

static TRI_general_cursor_t* UnwrapGeneralCursor (v8::Handle<v8::Object> cursorObject) {
  return TRI_UnwrapClass<TRI_general_cursor_t>(cursorObject, WRP_GENERAL_CURSOR_TYPE);
}

// -----------------------------------------------------------------------------
// --SECTION--                                              javascript functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief executes a transaction
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Transaction (v8::Arguments const& argv) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;

  if (argv.Length() != 1 || ! argv[0]->IsObject()) {
    TRI_V8_EXCEPTION_USAGE(scope, "TRANSACTION(<object>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // treat the argument as an object from now on
  v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(argv[0]);

  // extract the properties from the object
  
  // "lockTimeout"
  double lockTimeout = (double) (TRI_TRANSACTION_DEFAULT_LOCK_TIMEOUT / 1000000ULL);

  if (object->Has(TRI_V8_SYMBOL("lockTimeout"))) {
    static const string timeoutError = "<lockTimeout> must be a valid numeric value"; 
    
    if (! object->Get(TRI_V8_SYMBOL("lockTimeout"))->IsNumber()) {
      TRI_V8_EXCEPTION_PARAMETER(scope, timeoutError);
    }

    lockTimeout = (double) TRI_ObjectToDouble(object->Get(TRI_V8_SYMBOL("lockTimeout")));

    if (lockTimeout < 0.0) {
      TRI_V8_EXCEPTION_PARAMETER(scope, timeoutError);
    }
  }

  // "waitForSync"
  bool waitForSync = false;

  if (object->Has(TRI_V8_SYMBOL("waitForSync"))) {
    if (! object->Get(TRI_V8_SYMBOL("waitForSync"))->IsBoolean()) {
      TRI_V8_EXCEPTION_PARAMETER(scope, "<waitForSync> must be a boolean value");
    }
    
    waitForSync = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("waitForSync")));
  }
  
  // "replicate"
  bool replicate = true;

  if (object->Has(TRI_V8_SYMBOL("replicate"))) {
    if (! object->Get(TRI_V8_SYMBOL("replicate"))->IsBoolean()) {
      TRI_V8_EXCEPTION_PARAMETER(scope, "<replicate> must be a boolean value");
    }
    
    replicate = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("replicate")));
  }

  // "collections"
  static const string collectionError = "missing/invalid collections definition for transaction";

  if (! object->Has(TRI_V8_SYMBOL("collections")) || ! object->Get(TRI_V8_SYMBOL("collections"))->IsObject()) {
    TRI_V8_EXCEPTION_PARAMETER(scope, collectionError);
  }
  
  // extract collections
  v8::Handle<v8::Array> collections = v8::Handle<v8::Array>::Cast(object->Get(TRI_V8_SYMBOL("collections")));

  if (collections.IsEmpty()) {
    TRI_V8_EXCEPTION_PARAMETER(scope, collectionError);
  }
  
  bool isValid = true;
  vector<string> readCollections;
  vector<string> writeCollections;

  // collections.read
  if (collections->Has(TRI_V8_SYMBOL("read"))) {
    if (collections->Get(TRI_V8_SYMBOL("read"))->IsArray()) {
      v8::Handle<v8::Array> names = v8::Handle<v8::Array>::Cast(collections->Get(TRI_V8_SYMBOL("read")));

      for (uint32_t i = 0 ; i < names->Length(); ++i) {
        v8::Handle<v8::Value> collection = names->Get(i);
        if (! collection->IsString()) {
          isValid = false;
          break;
        }

        readCollections.push_back(TRI_ObjectToString(collection));
      }
    }
    else if (collections->Get(TRI_V8_SYMBOL("read"))->IsString()) {
      readCollections.push_back(TRI_ObjectToString(collections->Get(TRI_V8_SYMBOL("read"))));
    }
    else {
      isValid = false;
    }
  }
  
  // collections.write
  if (collections->Has(TRI_V8_SYMBOL("write"))) {
    if (collections->Get(TRI_V8_SYMBOL("write"))->IsArray()) {
      v8::Handle<v8::Array> names = v8::Handle<v8::Array>::Cast(collections->Get(TRI_V8_SYMBOL("write")));

      for (uint32_t i = 0 ; i < names->Length(); ++i) {
        v8::Handle<v8::Value> collection = names->Get(i);
        if (! collection->IsString()) {
          isValid = false;
          break;
        }
      
        writeCollections.push_back(TRI_ObjectToString(collection));
      }
    }
    else if (collections->Get(TRI_V8_SYMBOL("write"))->IsString()) {
      writeCollections.push_back(TRI_ObjectToString(collections->Get(TRI_V8_SYMBOL("write"))));
    }
    else {
      isValid = false;
    }
  }

  if (! isValid) {
    TRI_V8_EXCEPTION_PARAMETER(scope, collectionError);
  }

  // extract the "action" property
  static const string actionError = "missing/invalid action definition for transaction";

  if (! object->Has(TRI_V8_SYMBOL("action"))) {
    TRI_V8_EXCEPTION_PARAMETER(scope, actionError);
  }

  // function parameters
  v8::Handle<v8::Value> params;

  if (object->Has(TRI_V8_SYMBOL("params"))) {
    params = v8::Handle<v8::Array>::Cast(object->Get(TRI_V8_SYMBOL("params")));
  }
  else {
    params = v8::Undefined();
  }
  
  if (params.IsEmpty()) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }
  

  v8::Handle<v8::Object> current = v8::Context::GetCurrent()->Global();

  // callback function
  v8::Handle<v8::Function> action;

  if (object->Get(TRI_V8_SYMBOL("action"))->IsFunction()) {
    action = v8::Handle<v8::Function>::Cast(object->Get(TRI_V8_SYMBOL("action")));
  }
  else if (object->Get(TRI_V8_SYMBOL("action"))->IsString()) {
    // get built-in Function constructor (see ECMA-262 5th edition 15.3.2)
    v8::Local<v8::Function> ctor = v8::Local<v8::Function>::Cast(current->Get(v8::String::New("Function")));

    // Invoke Function constructor to create function with the given body and no arguments
    string body = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("action"))->ToString());
    body = "return (" + body + ")(params);";
    v8::Handle<v8::Value> args[2] = { v8::String::New("params"), v8::String::New(body.c_str(), (int) body.size()) };
    v8::Local<v8::Object> function = ctor->NewInstance(2, args);

    action = v8::Local<v8::Function>::Cast(function);
  }
  else {
    TRI_V8_EXCEPTION_PARAMETER(scope, actionError);
  }
  
  if (action.IsEmpty()) {
    TRI_V8_EXCEPTION_PARAMETER(scope, actionError);
  }


  // start actual transaction
  CollectionNameResolver resolver(vocbase);
  ExplicitTransaction<StandaloneTransaction<V8TransactionContext> > trx(vocbase, 
                                                                        resolver, 
                                                                        readCollections, 
                                                                        writeCollections, 
                                                                        lockTimeout,
                                                                        waitForSync,
                                                                        replicate); 

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Value> args = params;
  v8::Handle<v8::Value> result = action->Call(current, 1, &args);
    
  if (tryCatch.HasCaught()) {
    trx.abort();

    if (tryCatch.CanContinue()) {
      return scope.Close(v8::ThrowException(tryCatch.Exception()));
    }
    else {
      return scope.Close(result);
    }
  }

  res = trx.commit();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief normalize UTF 16 strings
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_normalize_string (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "NORMALIZE_STRING(<string>)");
  }

  return scope.Close(TRI_normalize_V8_Obj(argv[0]));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief compare two UTF 16 strings
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_compare_string (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "COMPARE_STRING(<left string>, <right string>)");
  }

  v8::String::Value left(argv[0]);
  v8::String::Value right(argv[1]);

  // ..........................................................................
  // Take note here: we are assuming that the ICU type UChar is two bytes.
  // There is no guarantee that this will be the case on all platforms and
  // compilers.
  // ..........................................................................
  int result = Utf8Helper::DefaultUtf8Helper.compareUtf16(*left, left.length(), *right, right.length());

  return scope.Close(v8::Integer::New(result));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get list of timezones
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_getIcuTimezones (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "TIMEZONES()");
  }

  v8::Handle<v8::Array> result = v8::Array::New();
  
  UErrorCode status = U_ZERO_ERROR;
  
  StringEnumeration* timeZones = TimeZone::createEnumeration();
  if (timeZones) {  
    int32_t idsCount = timeZones->count(status);
    
    for (int32_t i = 0; i < idsCount && U_ZERO_ERROR == status; ++i) {
      int32_t resultLength;
      const char* str = timeZones->next(&resultLength, status);
      result->Set(i, v8::String::New(str, resultLength));    
    }
    
    delete timeZones;
  }
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get list of locales
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_getIcuLocales (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "LOCALES()");
  }

  v8::Handle<v8::Array> result = v8::Array::New();
  
  int32_t count = 0;
  const Locale* locales = Locale::getAvailableLocales(count);
  if (locales) { 
    
    for (int32_t i = 0; i < count; ++i) {
      const Locale* l = locales + i;      
      const char* str = l->getBaseName();
      
      result->Set(i, v8::String::New(str, (int) strlen(str)));    
    }
  }
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief format datetime
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_formatDatetime (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "FORMAT_DATETIME(<datetime in sec>, <pattern>, [<timezone>, [<locale>]])");
  }

  int64_t datetime = TRI_ObjectToInt64(argv[0]);
  v8::String::Value pattern(argv[1]);

  TimeZone* tz = 0;
  if (argv.Length() > 2) {
    v8::String::Value value(argv[2]);
    
    // ..........................................................................
    // Take note here: we are assuming that the ICU type UChar is two bytes.
    // There is no guarantee that this will be the case on all platforms and
    // compilers.
    // ..........................................................................

    UnicodeString ts((const UChar *) *value, value.length());            
    tz = TimeZone::createTimeZone(ts);
  }
  else {
    tz = TimeZone::createDefault();  
  }
  
  Locale locale;
  if (argv.Length() > 3) {
    string name = TRI_ObjectToString(argv[3]);     
    locale = Locale::createFromName(name.c_str());
  }
  else {
    // use language of default collator
    string name = Utf8Helper::DefaultUtf8Helper.getCollatorLanguage();
    locale = Locale::createFromName(name.c_str());
  }
  
  UnicodeString formattedString;
  UErrorCode status = U_ZERO_ERROR;
  UnicodeString aPattern((const UChar *) *pattern, pattern.length());
  DateFormatSymbols* ds = new DateFormatSymbols(locale, status);
  SimpleDateFormat* s = new SimpleDateFormat(aPattern, ds, status);
  s->setTimeZone(*tz);
  s->format((UDate) (datetime * 1000), formattedString);
    
  string resultString;
  formattedString.toUTF8String(resultString);
  delete s;
  delete tz;
  
  return scope.Close(v8::String::New(resultString.c_str(), (int) resultString.length()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parse datetime
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_parseDatetime (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "PARSE_DATETIME(<datetime string>, <pattern>, [<timezone>, [<locale>]])");
  }

  v8::String::Value datetimeString(argv[0]);
  v8::String::Value pattern(argv[1]);

  TimeZone* tz = 0;
  if (argv.Length() > 2) {
    v8::String::Value value(argv[2]);
    
    // ..........................................................................
    // Take note here: we are assuming that the ICU type UChar is two bytes.
    // There is no guarantee that this will be the case on all platforms and
    // compilers.
    // ..........................................................................

    UnicodeString ts((const UChar *) *value, value.length());            
    tz = TimeZone::createTimeZone(ts);
  }
  else {
    tz = TimeZone::createDefault();  
  }
  
  Locale locale;
  if (argv.Length() > 3) {
    string name = TRI_ObjectToString(argv[3]);     
    locale = Locale::createFromName(name.c_str());
  }
  else {
    // use language of default collator
    string name = Utf8Helper::DefaultUtf8Helper.getCollatorLanguage();
    locale = Locale::createFromName(name.c_str());
  }
  
  UnicodeString formattedString((const UChar *) *datetimeString, datetimeString.length());
  UErrorCode status = U_ZERO_ERROR;
  UnicodeString aPattern((const UChar *) *pattern, pattern.length());
  DateFormatSymbols* ds = new DateFormatSymbols(locale, status);
  SimpleDateFormat* s = new SimpleDateFormat(aPattern, ds, status);
  s->setTimeZone(*tz);
  
  UDate udate = s->parse(formattedString, status);
  
  delete s;
  delete tz;
  
  return scope.Close(v8::Number::New(udate / 1000));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reloads the authentication info, coordinator case
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static bool ReloadAuthCoordinator (TRI_vocbase_t* vocbase) {
  TRI_json_t* json = 0;
  bool result;

  int res = usersOnCoordinator(string(vocbase->_name), 
                               json);

  if (res == TRI_ERROR_NO_ERROR) {
    assert(json != 0);

    result = TRI_PopulateAuthInfo(vocbase, json);
  }
  else { 
    result = false;
  }
    
  if (json != 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }

  return result;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief reloads the authentication info from collection _users
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ReloadAuth (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "RELOAD_AUTH()");
  }

  bool result;
#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    result = ReloadAuthCoordinator(vocbase);
  }
  else {
    result = TRI_ReloadAuthInfo(vocbase);
  }

#else
  result = TRI_ReloadAuthInfo(vocbase);
#endif

  return scope.Close(v8::Boolean::New(result));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generates a general cursor from a list
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CreateCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (argv.Length() < 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "CREATE_CURSOR(<list>, <doCount>, <batchSize>)");
  }

  if (! argv[0]->IsArray()) {
    TRI_V8_TYPE_ERROR(scope, "<list> must be a list");
  }
    
  // extract objects
  v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(argv[0]);
  TRI_json_t* json = TRI_ObjectToJson(array);

  if (json == 0) {
    TRI_V8_TYPE_ERROR(scope, "cannot convert <list> to JSON");
  }

  // return number of total records in cursor?
  bool doCount = false;

  if (argv.Length() >= 2) {
    doCount = TRI_ObjectToBoolean(argv[1]);
  }

  // maximum number of results to return at once
  uint32_t batchSize = 1000;

  if (argv.Length() >= 3) {
    int64_t maxValue = TRI_ObjectToInt64(argv[2]);

    if (maxValue > 0 && maxValue < (int64_t) UINT32_MAX) {
      batchSize = (uint32_t) maxValue;
    }
  }

  // create a cursor
  TRI_general_cursor_t* cursor = 0;
  TRI_general_cursor_result_t* cursorResult = TRI_CreateResultAql(json);

  if (cursorResult != 0) {
    cursor = TRI_CreateGeneralCursor(vocbase, cursorResult, doCount, batchSize, 0);

    if (cursor == 0) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, cursorResult);
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
  }
  else {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }

  if (cursor == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot create cursor");
  }

  v8::Handle<v8::Value> cursorObject = WrapGeneralCursor(cursor);

  if (cursorObject.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(cursorObject);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a general cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DisposeGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "dispose()");
  }

  bool found = TRI_DropGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  return scope.Close(v8::Boolean::New(found));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the id of a general cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_IdGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "id()");
  }

  TRI_voc_tick_t id = TRI_IdGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (id != 0) {
    return scope.Close(V8TickId(id));
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of results
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CountGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "count()");
  }

  size_t length = TRI_CountGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  return scope.Close(v8::Number::New((double) length));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the next result from the general cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_NextGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "next()");
  }


  v8::Handle<v8::Value> value;
  TRI_general_cursor_t* cursor;

  cursor = (TRI_general_cursor_t*) TRI_UseGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (cursor != 0) {
    bool result = false;

    TRI_LockGeneralCursor(cursor);

    if (cursor->_length == 0) {
      TRI_UnlockGeneralCursor(cursor);
      TRI_ReleaseGeneralCursor(cursor);

      return scope.Close(v8::Undefined());
    }

    // exceptions must be caught in the following part because we hold an exclusive
    // lock that might otherwise not be freed
    v8::TryCatch tryCatch;

    try {
      TRI_general_cursor_row_t row = cursor->next(cursor);

      if (row == 0) {
        value = v8::Undefined();
      }
      else {
        value = TRI_ObjectJson((TRI_json_t*) row);
        result = true;
      }
    }
    catch (...) {
    }

    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    if (result && ! tryCatch.HasCaught()) {
      return scope.Close(value);
    }

    if (tryCatch.HasCaught()) {
      if (tryCatch.CanContinue()) {
        return scope.Close(v8::ThrowException(tryCatch.Exception()));
      }
      else {
        return scope.Close(v8::Undefined());
      }
    }
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief persist the general cursor for usage in subsequent requests
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_PersistGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "persist()");
  }

  TRI_PersistGeneralCursor(UnwrapGeneralCursor(argv.Holder()), 30.0);
  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return all following rows from the cursor in one go
///
/// This function constructs multiple rows at once and should be preferred over
/// hasNext()...next() when iterating over bigger result sets
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ToArrayGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "toArray()");
  }

  v8::Handle<v8::Array> rows = v8::Array::New();
  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (cursor != 0) {
    bool result = false;

    TRI_LockGeneralCursor(cursor);

    // exceptions must be caught in the following part because we hold an exclusive
    // lock that might otherwise not be freed
    v8::TryCatch tryCatch;

    try {
      uint32_t max = (uint32_t) cursor->getBatchSize(cursor);

      for (uint32_t i = 0; i < max; ++i) {
        TRI_general_cursor_row_t row = cursor->next(cursor);
        if (row == 0) {
          break;
        }
        rows->Set(i, TRI_ObjectJson((TRI_json_t*) row));
      }

      result = true;
    }
    catch (...) {
    }

    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    if (result && ! tryCatch.HasCaught()) {
      return scope.Close(rows);
    }

    if (tryCatch.HasCaught()) {
      if (tryCatch.CanContinue()) {
        return scope.Close(v8::ThrowException(tryCatch.Exception()));
      }
      else {
        return scope.Close(v8::Undefined());
      }
    }
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief alias for toArray()
/// @deprecated
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_GetRowsGeneralCursor (v8::Arguments const& argv) {
  return JS_ToArrayGeneralCursor(argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return max number of results per transfer for cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_GetBatchSizeGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "getBatchSize()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (cursor != 0) {
    TRI_LockGeneralCursor(cursor);
    uint32_t max = cursor->getBatchSize(cursor);
    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    return scope.Close(v8::Number::New((double) max));
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return extra data for cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_GetExtraGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "getExtra()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (cursor != 0) {
    TRI_LockGeneralCursor(cursor);
    TRI_json_t* extra = cursor->getExtra(cursor);

    if (extra != 0 && extra->_type == TRI_JSON_ARRAY) {
      TRI_UnlockGeneralCursor(cursor);
      TRI_ReleaseGeneralCursor(cursor);

      return scope.Close(TRI_ObjectJson(extra));
    }

    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    return scope.Close(v8::Undefined());
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return if count flag was set for cursor
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_HasCountGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "hasCount()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (cursor != 0) {
    TRI_LockGeneralCursor(cursor);
    bool hasCount = cursor->hasCount(cursor);
    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    return scope.Close(hasCount ? v8::True() : v8::False());
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if the cursor is exhausted
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_HasNextGeneralCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "hasNext()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(argv.Holder()));

  if (cursor != 0) {
    TRI_LockGeneralCursor(cursor);
    bool hasNext = cursor->hasNext(cursor);
    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    return scope.Close(v8::Boolean::New(hasNext));
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a (persistent) cursor by its id
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_Cursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "CURSOR(<cursor-identifier>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // get the id
  v8::Handle<v8::Value> idArg = argv[0]->ToString();

  if (! idArg->IsString()) {
    TRI_V8_TYPE_ERROR(scope, "expecting a string for <cursor-identifier>)");
  }

  const string idString = TRI_ObjectToString(idArg);
  uint64_t id = TRI_UInt64String(idString.c_str());

  TRI_general_cursor_t* cursor = TRI_FindGeneralCursor(vocbase, (TRI_voc_tick_t) id);

  if (cursor == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_CURSOR_NOT_FOUND);
  }

  v8::Handle<v8::Value> cursorObject = WrapGeneralCursor(cursor);

  if (cursorObject.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(cursorObject);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief delete a (persistent) cursor by its id
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DeleteCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "DELETE_CURSOR(<cursor-identifier>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  // get the id
  v8::Handle<v8::Value> idArg = argv[0]->ToString();

  if (! idArg->IsString()) {
    TRI_V8_TYPE_ERROR(scope, "expecting a string for <cursor-identifier>)");
  }

  const string idString = TRI_ObjectToString(idArg);
  uint64_t id = TRI_UInt64String(idString.c_str());
  
  bool found = TRI_RemoveGeneralCursor(vocbase, id);

  return scope.Close(v8::Boolean::New(found));
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       REPLICATION
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication logger manually
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StartLoggerReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_LOGGER_START()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationLogger == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }

  int res = TRI_StartReplicationLogger(vocbase->_replicationLogger);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot start replication logger");
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication logger manually
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StopLoggerReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_LOGGER_STOP()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationLogger == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }

  int res = TRI_StopReplicationLogger(vocbase->_replicationLogger);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot stop replication logger");
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the state of the replication logger
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StateLoggerReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_LOGGER_STATE()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationLogger == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }
  
  TRI_json_t* json = TRI_JsonReplicationLogger(vocbase->_replicationLogger);

  if (json == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  v8::Handle<v8::Value> result = TRI_ObjectJson(json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure the replication logger manually
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ConfigureLoggerReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  if (vocbase->_replicationLogger == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }
  
  if (argv.Length() == 0) {
    // no argument: return the current configuration
    
    TRI_replication_logger_configuration_t config;
    
    TRI_ReadLockReadWriteLock(&vocbase->_replicationLogger->_statusLock);
    TRI_CopyConfigurationReplicationLogger(&vocbase->_replicationLogger->_configuration, &config);
    TRI_ReadUnlockReadWriteLock(&vocbase->_replicationLogger->_statusLock);
  
    TRI_json_t* json = TRI_JsonConfigurationReplicationLogger(&config);
    
    if (json == 0) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }

    v8::Handle<v8::Value> result = TRI_ObjectJson(json);
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
  
    return scope.Close(result);
  }

  else {
    // set the configuration

    if (argv.Length() != 1 || ! argv[0]->IsObject()) {
      TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_LOGGER_CONFIGURE(<configuration>)");
    }

    TRI_replication_logger_configuration_t config;

    // fill with previous configuration
    TRI_ReadLockReadWriteLock(&vocbase->_replicationLogger->_statusLock);
    TRI_CopyConfigurationReplicationLogger(&vocbase->_replicationLogger->_configuration, &config);
    TRI_ReadUnlockReadWriteLock(&vocbase->_replicationLogger->_statusLock);

    // treat the argument as an object from now on
    v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(argv[0]);
    
    if (object->Has(TRI_V8_SYMBOL("autoStart"))) {
      if (object->Get(TRI_V8_SYMBOL("autoStart"))->IsBoolean()) {
        config._autoStart = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("autoStart")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("logRemoteChanges"))) {
      if (object->Get(TRI_V8_SYMBOL("logRemoteChanges"))->IsBoolean()) {
        config._logRemoteChanges = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("logRemoteChanges")));
      }
    }
    
    if (object->Has(TRI_V8_SYMBOL("maxEvents"))) {
      config._maxEvents = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("maxEvents")), true);
    }
    
    if (object->Has(TRI_V8_SYMBOL("maxEventsSize"))) {
      config._maxEventsSize = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("maxEventsSize")), true);
    }

    int res = TRI_ConfigureReplicationLogger(vocbase->_replicationLogger, &config);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_V8_EXCEPTION(scope, res);
    }
    
    TRI_json_t* json = TRI_JsonConfigurationReplicationLogger(&config);

    if (json == 0) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }

    v8::Handle<v8::Value> result = TRI_ObjectJson(json);
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
  
    return scope.Close(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sync data from a remote master
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_SynchroniseReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_SYNCHRONISE(<config>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
    
  // treat the argument as an object from now on
  v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(argv[0]);

  string endpoint;
  if (object->Has(TRI_V8_SYMBOL("endpoint"))) {
    endpoint = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("endpoint")));
  }
  
  string database;
  if (object->Has(TRI_V8_SYMBOL("database"))) {
    database = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("database")));
  }
  else {
    database = string(vocbase->_name);
  }

  string username;
  if (object->Has(TRI_V8_SYMBOL("username"))) {
    username = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("username")));
  }
  
  string password;
  if (object->Has(TRI_V8_SYMBOL("password"))) {
    password = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("password")));
  }

  map<string, bool> restrictCollections;
  if (object->Has(TRI_V8_SYMBOL("restrictCollections")) && object->Get(TRI_V8_SYMBOL("restrictCollections"))->IsArray()) {
    v8::Handle<v8::Array> a = v8::Handle<v8::Array>::Cast(object->Get(TRI_V8_SYMBOL("restrictCollections")));

    const uint32_t n = a->Length();

    for (uint32_t i = 0; i < n; ++i) {
      v8::Handle<v8::Value> cname = a->Get(i);

      if (cname->IsString()) {
        restrictCollections.insert(pair<string, bool>(TRI_ObjectToString(cname), true));
      }
    }
  }
  
  string restrictType;
  if (object->Has(TRI_V8_SYMBOL("restrictType"))) {
    restrictType = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("restrictType")));
  }
  
  bool verbose = true;
  if (object->Has(TRI_V8_SYMBOL("verbose"))) {
    verbose = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("verbose")));
  }

  if (endpoint.empty()) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "<endpoint> must be a valid endpoint")
  }
  
  if ((restrictType.empty() && ! restrictCollections.empty()) ||
      (! restrictType.empty() && restrictCollections.empty()) ||
      (! restrictType.empty() && restrictType != "include" && restrictType != "exclude")) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "invalid value for <restrictCollections> or <restrictType>");
  }

  TRI_replication_applier_configuration_t config;
  TRI_InitConfigurationReplicationApplier(&config);
  config._endpoint = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, endpoint.c_str(), endpoint.size());
  config._database = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, database.c_str(), database.size());
  config._username = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, username.c_str(), username.size());
  config._password = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, password.c_str(), password.size());
    
  if (object->Has(TRI_V8_SYMBOL("chunkSize"))) {
    if (object->Get(TRI_V8_SYMBOL("chunkSize"))->IsNumber()) {
      config._chunkSize = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("chunkSize")), true);
    }
  }

  string errorMsg = "";
  InitialSyncer syncer(vocbase, &config, restrictCollections, restrictType, verbose);
  TRI_DestroyConfigurationReplicationApplier(&config);

  int res = TRI_ERROR_NO_ERROR;
  v8::Handle<v8::Object> result = v8::Object::New();

  try {
    res = syncer.run(errorMsg);

    result->Set(v8::String::New("lastLogTick"), V8TickId(syncer.getLastLogTick()));

    map<TRI_voc_cid_t, string>::const_iterator it;
    map<TRI_voc_cid_t, string> const& c = syncer.getProcessedCollections();

    uint32_t j = 0;
    v8::Handle<v8::Array> collections = v8::Array::New();
    for (it = c.begin(); it != c.end(); ++it) {
      const string cidString = StringUtils::itoa((*it).first);

      v8::Handle<v8::Object> ci = v8::Object::New();
      ci->Set(TRI_V8_SYMBOL("id"), v8::String::New(cidString.c_str(), (int) cidString.size()));
      ci->Set(TRI_V8_SYMBOL("name"), v8::String::New((*it).second.c_str(), (int) (*it).second.size()));

      collections->Set(j++, ci);
    }

    result->Set(v8::String::New("collections"), collections);
  }
  catch (...) {
  }
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot sync from remote endpoint: " + errorMsg);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the server's id
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ServerIdReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  const string serverId = StringUtils::itoa(TRI_GetIdServer());
  return scope.Close(v8::String::New(serverId.c_str(), (int) serverId.size()));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure the replication applier manually
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ConfigureApplierReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  if (vocbase->_replicationApplier == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }
  
  if (argv.Length() == 0) {
    // no argument: return the current configuration
    
    TRI_replication_applier_configuration_t config;
    TRI_InitConfigurationReplicationApplier(&config);
    
    TRI_ReadLockReadWriteLock(&vocbase->_replicationApplier->_statusLock);
    TRI_CopyConfigurationReplicationApplier(&vocbase->_replicationApplier->_configuration, &config);
    TRI_ReadUnlockReadWriteLock(&vocbase->_replicationApplier->_statusLock);
  
    TRI_json_t* json = TRI_JsonConfigurationReplicationApplier(&config);
    TRI_DestroyConfigurationReplicationApplier(&config);
    
    if (json == 0) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }

    v8::Handle<v8::Value> result = TRI_ObjectJson(json);
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
  
    return scope.Close(result);
  }

  else {
    // set the configuration

    if (argv.Length() != 1 || ! argv[0]->IsObject()) {
      TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_APPLIER_CONFIGURE(<configuration>)");
    }

    TRI_replication_applier_configuration_t config;
    TRI_InitConfigurationReplicationApplier(&config);

    // fill with previous configuration
    TRI_ReadLockReadWriteLock(&vocbase->_replicationApplier->_statusLock);
    TRI_CopyConfigurationReplicationApplier(&vocbase->_replicationApplier->_configuration, &config);
    TRI_ReadUnlockReadWriteLock(&vocbase->_replicationApplier->_statusLock);


    // treat the argument as an object from now on
    v8::Handle<v8::Object> object = v8::Handle<v8::Object>::Cast(argv[0]);

    if (object->Has(TRI_V8_SYMBOL("endpoint"))) {
      if (object->Get(TRI_V8_SYMBOL("endpoint"))->IsString()) {
        string endpoint = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("endpoint")));
    
        if (config._endpoint != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._endpoint);
        }
        config._endpoint = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, endpoint.c_str(), endpoint.size());
      }
    }
    
    if (object->Has(TRI_V8_SYMBOL("database"))) {
      if (object->Get(TRI_V8_SYMBOL("database"))->IsString()) {
        string database = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("database")));
    
        if (config._database != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._database);
        }
        config._database = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, database.c_str(), database.size());
      }
    }
    else {
      if (config._database == 0) {
        // no database set, use current
        config._database = TRI_DuplicateStringZ(TRI_CORE_MEM_ZONE, vocbase->_name);
      }
    }

    assert(config._database != 0);
    
    if (object->Has(TRI_V8_SYMBOL("username"))) {
      if (object->Get(TRI_V8_SYMBOL("username"))->IsString()) {
        string username = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("username")));
    
        if (config._username != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._username);
        }
        config._username = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, username.c_str(), username.size());
      }
    }
    
    if (object->Has(TRI_V8_SYMBOL("password"))) {
      if (object->Get(TRI_V8_SYMBOL("password"))->IsString()) {
        string password = TRI_ObjectToString(object->Get(TRI_V8_SYMBOL("password")));
    
        if (config._password != 0) {
          TRI_Free(TRI_CORE_MEM_ZONE, config._password);
        }
        config._password = TRI_DuplicateString2Z(TRI_CORE_MEM_ZONE, password.c_str(), password.size());
      }
    }

    if (object->Has(TRI_V8_SYMBOL("requestTimeout"))) {
      if (object->Get(TRI_V8_SYMBOL("requestTimeout"))->IsNumber()) {
        config._requestTimeout = TRI_ObjectToDouble(object->Get(TRI_V8_SYMBOL("requestTimeout")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("connectTimeout"))) {
      if (object->Get(TRI_V8_SYMBOL("connectTimeout"))->IsNumber()) {
        config._connectTimeout = TRI_ObjectToDouble(object->Get(TRI_V8_SYMBOL("connectTimeout")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("ignoreErrors"))) {
      if (object->Get(TRI_V8_SYMBOL("ignoreErrors"))->IsNumber()) {
        config._ignoreErrors = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("ignoreErrors")), false);
      }
    }

    if (object->Has(TRI_V8_SYMBOL("maxConnectRetries"))) {
      if (object->Get(TRI_V8_SYMBOL("maxConnectRetries"))->IsNumber()) {
        config._maxConnectRetries = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("maxConnectRetries")), false);
      }
    }
    
    if (object->Has(TRI_V8_SYMBOL("sslProtocol"))) {
      if (object->Get(TRI_V8_SYMBOL("sslProtocol"))->IsNumber()) {
        config._sslProtocol = (uint32_t) TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("sslProtocol")), false);
      }
    }
    
    if (object->Has(TRI_V8_SYMBOL("chunkSize"))) {
      if (object->Get(TRI_V8_SYMBOL("chunkSize"))->IsNumber()) {
        config._chunkSize = TRI_ObjectToUInt64(object->Get(TRI_V8_SYMBOL("chunkSize")), true);
      }
    }

    if (object->Has(TRI_V8_SYMBOL("autoStart"))) {
      if (object->Get(TRI_V8_SYMBOL("autoStart"))->IsBoolean()) {
        config._autoStart = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("autoStart")));
      }
    }

    if (object->Has(TRI_V8_SYMBOL("adaptivePolling"))) {
      if (object->Get(TRI_V8_SYMBOL("adaptivePolling"))->IsBoolean()) {
        config._adaptivePolling = TRI_ObjectToBoolean(object->Get(TRI_V8_SYMBOL("adaptivePolling")));
      }
    }

    int res = TRI_ConfigureReplicationApplier(vocbase->_replicationApplier, &config);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_DestroyConfigurationReplicationApplier(&config);
      TRI_V8_EXCEPTION(scope, res);
    }

    TRI_json_t* json = TRI_JsonConfigurationReplicationApplier(&config);
    TRI_DestroyConfigurationReplicationApplier(&config);
    
    if (json == 0) {
      TRI_V8_EXCEPTION_MEMORY(scope);
    }

    v8::Handle<v8::Value> result = TRI_ObjectJson(json);
    TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

    return scope.Close(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start the replication applier manually
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StartApplierReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }
  
  if (argv.Length() > 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_APPLIER_START(<from>)");
  }

  TRI_voc_tick_t initialTick = 0;
  bool useTick = false;

  if (argv.Length() == 1) {
    initialTick = TRI_ObjectToUInt64(argv[0], true); 
    useTick = true;
  }

  int res = TRI_StartReplicationApplier(vocbase->_replicationApplier,
                                        initialTick,
                                        useTick);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot start replication applier");
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication applier manually
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StopApplierReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_APPLIER_STOP()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }

  int res = TRI_StopReplicationApplier(vocbase->_replicationApplier, true);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot stop replication applier");
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the state of the replication applier
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StateApplierReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_APPLIER_STATE()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationLogger == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }

  TRI_json_t* json = TRI_JsonReplicationApplier(vocbase->_replicationApplier);
  
  if (json == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_OUT_OF_MEMORY);
  }

  v8::Handle<v8::Value> result = TRI_ObjectJson(json);
  TRI_FreeJson(TRI_CORE_MEM_ZONE, json);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stop the replication applier and "forget" all state
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ForgetApplierReplication (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "REPLICATION_APPLIER_FORGET()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (vocbase->_replicationApplier == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }
  
  int res = TRI_ForgetReplicationApplier(vocbase->_replicationApplier);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  return scope.Close(v8::True());
}

// -----------------------------------------------------------------------------
// --SECTION--                                                          AHUACATL
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates code for an AQL query and runs it
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RunAhuacatl (v8::Arguments const& argv) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;
  const uint32_t argc = argv.Length();

  if (argc < 1 || argc > 4) {
    TRI_V8_EXCEPTION_USAGE(scope, "AHUACATL_RUN(<querystring>, <bindvalues>, <cursorOptions>, <options>)"); 
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // get the query string
  v8::Handle<v8::Value> queryArg = argv[0];

  if (! queryArg->IsString()) {
    TRI_V8_TYPE_ERROR(scope, "expecting string for <querystring>");
  }

  const string queryString = TRI_ObjectToString(queryArg);
  
  // bind parameters
  TRI_json_t* parameters = 0;

  if (argc > 1 && argv[1]->IsObject()) {
    parameters = TRI_ObjectToJson(argv[1]);
  }

  // cursor options
  // -------------------------------------------------

  // return number of total records in cursor?
  bool doCount = false;
  
  // maximum number of results to return at once
  uint32_t batchSize = UINT32_MAX;

  if (argc > 2 && argv[2]->IsObject()) {
    // treat the argument as an object from now on
    v8::Handle<v8::Object> options = v8::Handle<v8::Object>::Cast(argv[2]);
  
    if (options->Has(TRI_V8_SYMBOL("count"))) {
      doCount = TRI_ObjectToBoolean(options->Get(TRI_V8_SYMBOL("count")));
    }
    
    if (options->Has(TRI_V8_SYMBOL("batchSize"))) {
      int64_t maxValue = TRI_ObjectToInt64(options->Get(TRI_V8_SYMBOL("batchSize")));
     
      if (maxValue > 0 && maxValue < (int64_t) UINT32_MAX) {
        batchSize = (uint32_t) maxValue;
      }
    }
  }
  
  // user options
  // -------------------------------------------------

  TRI_json_t* userOptions = 0;
  if (argc > 3 && argv[3]->IsObject()) {
    // treat the argument as an object from now on
    v8::Handle<v8::Object> options = v8::Handle<v8::Object>::Cast(argv[3]);

    userOptions = TRI_ObjectToJson(options);
  }

  AhuacatlGuard context(vocbase, queryString, userOptions);

  if (! context.valid()) {
    if (userOptions != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, userOptions);
    }

    if (parameters != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
    }

    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  v8::Handle<v8::Value> result;
  result = ExecuteQueryCursorAhuacatl(vocbase, context.ptr(), parameters, doCount, batchSize);
  int res = context.ptr()->_error._code;
  
  if (res == TRI_ERROR_REQUEST_CANCELED) {
    result = CreateErrorObjectAhuacatl(&(context.ptr()->_error)); 
  }
   
  context.free();
  
  if (userOptions != 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, userOptions);
  }
    
  if (parameters != 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
  }

  if (tryCatch.HasCaught()) {
    if (tryCatch.CanContinue()) {
      if (tryCatch.Exception()->IsObject() && v8::Handle<v8::Array>::Cast(tryCatch.Exception())->HasOwnProperty(v8::String::New("errorNum"))) {
        // we already have an ArangoError object
        return scope.Close(v8::ThrowException(tryCatch.Exception()));
      }

      // create a new error object
      v8::Handle<v8::Object> errorObject = TRI_CreateErrorObject(
        __FILE__,
        __LINE__,
        TRI_ERROR_QUERY_SCRIPT,
        TRI_ObjectToString(tryCatch.Exception()).c_str());
      return scope.Close(v8::ThrowException(errorObject));
    }
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief explains an AQL query
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ExplainAhuacatl (v8::Arguments const& argv) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;
  const uint32_t argc = argv.Length();

  if (argc < 1 || argc > 3) {
    TRI_V8_EXCEPTION_USAGE(scope, "AHUACATL_EXPLAIN(<querystring>, <bindvalues>, <performoptimisations>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // get the query string
  v8::Handle<v8::Value> queryArg = argv[0];
  if (! queryArg->IsString()) {
    TRI_V8_TYPE_ERROR(scope, "expecting string for <querystring>");
  }

  const string queryString = TRI_ObjectToString(queryArg);

  // bind parameters
  TRI_json_t* parameters = 0;
  if (argc > 1) {
    // parameters may still be null afterwards!
    parameters = TRI_ObjectToJson(argv[1]);
  }

  AhuacatlGuard guard(vocbase, queryString, 0);

  if (! guard.valid()) {
    if (parameters != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
    }
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  TRI_aql_context_t* context = guard.ptr();

  bool performOptimisations = true;
  if (argc > 2) {
    // turn off optimisations ?
    performOptimisations = TRI_ObjectToBoolean(argv[2]);
  }

  TRI_json_t* explain = 0;

  if (! TRI_ValidateQueryContextAql(context) ||
      ! TRI_BindQueryContextAql(context, parameters) ||
      ! TRI_SetupCollectionsContextAql(context)) {
  
    if (parameters != 0) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
    }

    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&context->_error);
    return scope.Close(v8::ThrowException(errorObject));
  }
    
  if (parameters != 0) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, parameters);
  }

  // note: a query is not necessarily collection-based.
  // this means that the _collections array might contain 0 collections!
  CollectionNameResolver resolver(vocbase);
  AhuacatlTransaction<EmbeddableTransaction<V8TransactionContext> > trx(vocbase, resolver, context);

  int res = trx.begin();
  
  if (res != TRI_ERROR_NO_ERROR) {
    // check if there is some error data registered in the transaction
    const string errorData = trx.getErrorData();

    if (errorData.empty()) {
      // no error data. return a regular error message
      TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot explain query");
    }
    else {
      // there is specific error data. return a more tailored error message
      const string errorMsg = "cannot explain query: " + string(TRI_errno_string(res)) + ": '" + errorData + "'";
      return scope.Close(v8::ThrowException(TRI_CreateErrorObject(__FILE__,
                                                                  __LINE__,
                                                                  res,
                                                                  errorMsg)));
    }
  }

  if ((performOptimisations && ! TRI_OptimiseQueryContextAql(context)) ||
      ! (explain = TRI_ExplainAql(context))) {
    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&context->_error);
    return scope.Close(v8::ThrowException(errorObject));
  }

  trx.finish(TRI_ERROR_NO_ERROR);

  assert(explain);

  v8::Handle<v8::Value> result;
  result = TRI_ObjectJson(explain);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, explain);
  guard.free();

  if (tryCatch.HasCaught()) {
    if (tryCatch.CanContinue()) {
      if (tryCatch.Exception()->IsObject() && v8::Handle<v8::Array>::Cast(tryCatch.Exception())->HasOwnProperty(v8::String::New("errorNum"))) {
        // we already have an ArangoError object
        return scope.Close(v8::ThrowException(tryCatch.Exception()));
      }

      // create a new error object
      v8::Handle<v8::Object> errorObject = TRI_CreateErrorObject(
        __FILE__,
        __LINE__,
        TRI_ERROR_QUERY_SCRIPT,
        TRI_ObjectToString(tryCatch.Exception()).c_str());
      return scope.Close(v8::ThrowException(errorObject));
    }
    else {
      return scope.Close(result);
    }
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parses an AQL query and returns the parse result
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ParseAhuacatl (v8::Arguments const& argv) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "AHUACATL_PARSE(<querystring>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }


  // get the query string
  v8::Handle<v8::Value> queryArg = argv[0];

  if (!queryArg->IsString()) {
    TRI_V8_TYPE_ERROR(scope, "expecting string for <querystring>");
  }

  string queryString = TRI_ObjectToString(queryArg);

  AhuacatlGuard context(vocbase, queryString, 0);

  if (! context.valid()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  // parse & validate
  if (! TRI_ValidateQueryContextAql(context.ptr())) {
    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&(context.ptr())->_error);
    return scope.Close(v8::ThrowException(errorObject));
  }

  // setup result
  v8::Handle<v8::Object> result = v8::Object::New();

  result->Set(v8::String::New("parsed"), v8::True());

  // return the bind parameter names
  result->Set(v8::String::New("parameters"), TRI_ArrayAssociativePointer(&(context.ptr())->_parameters._names));
  // return the collection names
  result->Set(v8::String::New("collections"), TRI_ArrayAssociativePointer(&(context.ptr())->_collectionNames));
  context.free();

  if (tryCatch.HasCaught()) {
    if (tryCatch.CanContinue()) {
      if (tryCatch.Exception()->IsObject() && v8::Handle<v8::Array>::Cast(tryCatch.Exception())->HasOwnProperty(v8::String::New("errorNum"))) {
        // we already have an ArangoError object
        return scope.Close(v8::ThrowException(tryCatch.Exception()));
      }

      // create a new error object
      v8::Handle<v8::Object> errorObject = TRI_CreateErrorObject(
        __FILE__,
        __LINE__,
        TRI_ERROR_QUERY_SCRIPT,
        TRI_ObjectToString(tryCatch.Exception()).c_str());
      return scope.Close(v8::ThrowException(errorObject));
    }
    else {
      return scope.Close(result);
    }
  }

  return scope.Close(result);
}

// -----------------------------------------------------------------------------
// --SECTION--                                          TRI_DATAFILE_T FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                              javascript functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief migrate an "old" collection to a newer version
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_UpgradeVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // some typedefs for deprecated markers, only used inside this function
  typedef uint64_t voc_did_t;

  typedef struct {
    TRI_df_marker_t base;

    voc_did_t _did;        // this is the tick for a create, but not an update
    TRI_voc_rid_t _rid;    // this is the tick for an create and update
    TRI_voc_tid_t _sid;

    TRI_shape_sid_t _shape;
  }
  doc_document_marker_t_deprecated;

  typedef struct {
    doc_document_marker_t_deprecated base;

    TRI_voc_cid_t _toCid;
    voc_did_t _toDid;

    TRI_voc_cid_t _fromCid;
    voc_did_t _fromDid;
  }
  doc_edge_marker_t_deprecated;

  typedef struct {
    TRI_df_marker_t base;

    voc_did_t _did;        // this is the tick for a create, but not an update
    TRI_voc_rid_t _rid;    // this is the tick for an create and update
    TRI_voc_tid_t _sid;
  }
  doc_deletion_marker_t_deprecated;

  ssize_t writeResult;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "upgrade()");
  }

  TRI_vocbase_col_t const* collection;

  // extract the collection
  v8::Handle<v8::Object> err;
  collection = UseCollection(argv.Holder(), &err);

  if (collection == 0) {
    return scope.Close(v8::ThrowException(err));
  }

  TRI_primary_collection_t* primary = collection->_collection;

  TRI_collection_t* col = &primary->base;

#ifdef TRI_ENABLE_LOGGER
  const char* name = col->_info._name;
#endif
  TRI_col_version_t version = col->_info._version;

  if (version >= 3) {
    LOG_ERROR("Cannot upgrade collection '%s' with version '%d' in directory '%s'", name, version, col->_directory);
    ReleaseCollection(collection);
    return scope.Close(v8::False());
  }

  LOG_INFO("Upgrading collection '%s' with version '%d' in directory '%s'", name, version, col->_directory);

  // get all filenames
  size_t i;
  TRI_vector_pointer_t files;
  TRI_InitVectorPointer(&files, TRI_UNKNOWN_MEM_ZONE);
  for (i = 0; i < col->_datafiles._length; ++i) {
    TRI_datafile_t* df = (TRI_datafile_t*) TRI_AtVectorPointer(&col->_datafiles, i);
    TRI_PushBackVectorPointer(&files, df);
  }
  for (i = 0; i < col->_journals._length; ++i) {
    TRI_datafile_t* df = (TRI_datafile_t*) TRI_AtVectorPointer(&col->_journals, i);
    TRI_PushBackVectorPointer(&files, df);
  }
  for (i = 0; i < col->_compactors._length; ++i) {
    TRI_datafile_t* df = (TRI_datafile_t*) TRI_AtVectorPointer(&col->_compactors, i);
    TRI_PushBackVectorPointer(&files, df);
  }

  // convert each file
  for (size_t j = 0; j < files._length; ++j) {
    int fd, fdout;

    TRI_datafile_t* df = (TRI_datafile_t*) TRI_AtVectorPointer(&files, j);

    int64_t fileSize = TRI_SizeFile(df->_filename);
    int64_t writtenSize = 0;

    LOG_INFO("convert file '%s' (size = %lld)", df->_filename, (long long) fileSize);

    fd = TRI_OPEN(df->_filename, O_RDONLY);
    if (fd < 0) {
      LOG_ERROR("could not open file '%s' for reading", df->_filename);

      TRI_DestroyVectorPointer(&files);
      ReleaseCollection(collection);

      return scope.Close(v8::False());
    }

    ostringstream outfile;
    outfile << df->_filename << ".new";

    fdout = TRI_CREATE(outfile.str().c_str(), O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
    if (fdout < 0) {
      LOG_ERROR("could not open file '%s' for writing", outfile.str().c_str());

      TRI_DestroyVectorPointer(&files);
      ReleaseCollection(collection);

      TRI_CLOSE(fd);
      return scope.Close(v8::False());
    }

    //LOG_INFO("fd: %d, fdout: %d", fd, fdout);

    TRI_df_marker_t marker;

    while (true) {
      // read marker header
      ssize_t bytesRead = TRI_READ(fd, &marker, sizeof(marker));

      if (bytesRead == 0) {
        // eof
        break;
      }

      if (bytesRead < (ssize_t) sizeof(marker)) {
        // eof
        LOG_WARNING("bytesRead = %d < sizeof(marker) = %d", (int) bytesRead, (int) sizeof(marker));
        break;
      }

      if (marker._size == 0) {
        // eof
        break;
      }

      if (bytesRead == sizeof(marker)) {
        // read marker body

        if (marker._size < sizeof(marker)) {
          // eof
          LOG_WARNING("marker._size = %d < sizeof(marker) = %d", (int) marker._size, (int) sizeof(marker));
          break;
        }

        off_t paddedSize = TRI_DF_ALIGN_BLOCK(marker._size);

        char* payload = new char[paddedSize];

        // copy header

        memcpy(payload, &marker, sizeof(marker));

        if (marker._size > sizeof(marker)) {
          //int r = ::read(fd, p + sizeof(marker), marker._size - sizeof(marker));
          int r = TRI_READ(fd, payload + sizeof(marker), paddedSize - sizeof(marker));
          if (r < (int) (paddedSize - sizeof(marker))) {
            LOG_WARNING("read less than paddedSize - sizeof(marker) = %d", r);
            break;
          }
        }

        if ((int) marker._type == 0) {
          // eof
          break;
        }

        switch (marker._type) {
          case TRI_DOC_MARKER_DOCUMENT: {
            doc_document_marker_t_deprecated* oldMarker = (doc_document_marker_t_deprecated*) payload;
            TRI_doc_document_key_marker_t newMarker;
            TRI_voc_size_t newMarkerSize = sizeof(TRI_doc_document_key_marker_t);

            char* body = ((char*) oldMarker) + sizeof(doc_document_marker_t_deprecated);
            TRI_voc_size_t bodySize = oldMarker->base._size - sizeof(doc_document_marker_t_deprecated);
            TRI_voc_size_t bodySizePadded = paddedSize - sizeof(doc_document_marker_t_deprecated);

            char* keyBody;
            TRI_voc_size_t keyBodySize;

            TRI_voc_size_t keySize;
            char didBuffer[33];

            memset(&newMarker, 0, newMarkerSize);

            sprintf(didBuffer,"%llu", (unsigned long long) oldMarker->_did);
            keySize = (TRI_voc_size_t) (strlen(didBuffer) + 1);
            keyBodySize = TRI_DF_ALIGN_BLOCK(keySize);
            keyBody = (char*) TRI_Allocate(TRI_CORE_MEM_ZONE, keyBodySize, true);
            TRI_CopyString(keyBody, didBuffer, keySize);

            newMarker._rid = oldMarker->_rid;
            newMarker._tid = 0;
            newMarker._shape = oldMarker->_shape;
            newMarker._offsetKey = newMarkerSize;
            newMarker._offsetJson = newMarkerSize + keyBodySize;

            newMarker.base._type = TRI_DOC_MARKER_KEY_DOCUMENT;
            newMarker.base._tick = oldMarker->base._tick;
            newMarker.base._size = newMarkerSize + keyBodySize + bodySize;
            TRI_FillCrcKeyMarkerDatafile(df, &newMarker.base, newMarkerSize, keyBody, keyBodySize, body, bodySize);

            writeResult = TRI_WRITE(fdout, &newMarker, sizeof(newMarker));
            writeResult = TRI_WRITE(fdout, keyBody, keyBodySize);
            writeResult = TRI_WRITE(fdout, body, bodySizePadded);

            //LOG_INFO("found doc marker, type: '%d', did: '%d', rid: '%d', size: '%d', crc: '%d'", marker._type, oldMarker->_did, oldMarker->_rid,newMarker.base._size,newMarker.base._crc);

            TRI_Free(TRI_CORE_MEM_ZONE, keyBody);

            writtenSize += sizeof(newMarker) + keyBodySize + bodySizePadded;
            break;
          }

          case TRI_DOC_MARKER_EDGE: {
            doc_edge_marker_t_deprecated* oldMarker = (doc_edge_marker_t_deprecated*) payload;
            TRI_doc_edge_key_marker_t newMarker;
            TRI_voc_size_t newMarkerSize = sizeof(TRI_doc_edge_key_marker_t);

            char* body = ((char*) oldMarker) + sizeof(doc_edge_marker_t_deprecated);
            TRI_voc_size_t bodySize = oldMarker->base.base._size - sizeof(doc_edge_marker_t_deprecated);
            TRI_voc_size_t bodySizePadded = paddedSize - sizeof(doc_edge_marker_t_deprecated);

            char* keyBody;
            TRI_voc_size_t keyBodySize;

            size_t keySize;
            size_t toSize;
            size_t fromSize;

            char didBuffer[33];
            char toDidBuffer[33];
            char fromDidBuffer[33];

            memset(&newMarker, 0, newMarkerSize);

            sprintf(didBuffer,"%llu", (unsigned long long) oldMarker->base._did);
            sprintf(toDidBuffer,"%llu", (unsigned long long) oldMarker->_toDid);
            sprintf(fromDidBuffer,"%llu", (unsigned long long) oldMarker->_fromDid);

            keySize = strlen(didBuffer) + 1;
            toSize = strlen(toDidBuffer) + 1;
            fromSize = strlen(fromDidBuffer) + 1;

            keyBodySize = (TRI_voc_size_t) (TRI_DF_ALIGN_BLOCK(keySize + toSize + fromSize));
            keyBody = (char*) TRI_Allocate(TRI_CORE_MEM_ZONE, keyBodySize, true);

            TRI_CopyString(keyBody,                    didBuffer,     keySize);
            TRI_CopyString(keyBody + keySize,          toDidBuffer,   toSize);
            TRI_CopyString(keyBody + keySize + toSize, fromDidBuffer, fromSize);

            newMarker.base._rid = oldMarker->base._rid;
            newMarker.base._tid = 0;
            newMarker.base._shape = oldMarker->base._shape;
            newMarker.base._offsetKey = newMarkerSize;
            newMarker.base._offsetJson = newMarkerSize + keyBodySize;

            newMarker._offsetToKey = (uint16_t) (newMarkerSize + keySize);
            newMarker._offsetFromKey = (uint16_t) (newMarkerSize + keySize + toSize);
            newMarker._toCid = oldMarker->_toCid;
            newMarker._fromCid = oldMarker->_fromCid;

            newMarker.base.base._size = newMarkerSize + keyBodySize + bodySize;
            newMarker.base.base._type = TRI_DOC_MARKER_KEY_EDGE;
            newMarker.base.base._tick = oldMarker->base.base._tick;
            TRI_FillCrcKeyMarkerDatafile(df, &newMarker.base.base, newMarkerSize, keyBody, keyBodySize, body, bodySize);

            writeResult = TRI_WRITE(fdout, &newMarker, newMarkerSize);
            (void) writeResult;
            writeResult = TRI_WRITE(fdout, keyBody, keyBodySize);
            (void) writeResult;
            writeResult = TRI_WRITE(fdout, body, bodySizePadded);
            (void) writeResult;

            //LOG_INFO("found edge marker, type: '%d', did: '%d', rid: '%d', size: '%d', crc: '%d'", marker._type, oldMarker->base._did, oldMarker->base._rid,newMarker.base.base._size,newMarker.base.base._crc);

            TRI_Free(TRI_CORE_MEM_ZONE, keyBody);

            writtenSize += newMarkerSize + keyBodySize + bodySizePadded;
            break;
          }

          case TRI_DOC_MARKER_DELETION: {
            doc_deletion_marker_t_deprecated* oldMarker = (doc_deletion_marker_t_deprecated*) payload;
            TRI_doc_deletion_key_marker_t newMarker;
            TRI_voc_size_t newMarkerSize = sizeof(TRI_doc_deletion_key_marker_t);

            TRI_voc_size_t keyBodySize;
            char* keyBody;
            TRI_voc_size_t keySize;
            char didBuffer[33];

            memset(&newMarker, 0, newMarkerSize);

            sprintf(didBuffer,"%llu", (unsigned long long) oldMarker->_did);
            keySize = (TRI_voc_size_t) (strlen(didBuffer) + 1);
            keyBodySize = TRI_DF_ALIGN_BLOCK(keySize);
            keyBody = (char*) TRI_Allocate(TRI_CORE_MEM_ZONE, keyBodySize, true);
            TRI_CopyString(keyBody, didBuffer, keySize);

            newMarker._rid = oldMarker->_rid;
            newMarker._tid = 0;
            newMarker._offsetKey = newMarkerSize;

            newMarker.base._size = newMarkerSize + keyBodySize;
            newMarker.base._type = TRI_DOC_MARKER_KEY_DELETION;
            newMarker.base._tick = oldMarker->base._tick;
            TRI_FillCrcKeyMarkerDatafile(df, &newMarker.base, newMarkerSize, keyBody, keyBodySize, NULL, 0);

            writeResult = TRI_WRITE(fdout, &newMarker, newMarkerSize);
            (void) writeResult;
            writeResult = TRI_WRITE(fdout, (char*) keyBody, keyBodySize);
            (void) writeResult;

            //LOG_INFO("found deletion marker, type: '%d', did: '%d', rid: '%d'", marker._type, oldMarker->_did, oldMarker->_rid);

            TRI_Free(TRI_CORE_MEM_ZONE, keyBody);

            writtenSize += newMarker.base._size;
            break;
          }

          default: {
            // copy other types without modification
            writeResult = TRI_WRITE(fdout, payload, paddedSize);
            (void) writeResult;
            writtenSize += paddedSize;
            //LOG_INFO("found marker, type: '%d'", marker._type);

          }
        }

        delete [] payload;
      }
      else {
        LOG_ERROR("Could not read data from file '%s' while upgrading collection '%s'.", df->_filename, name);
        LOG_ERROR("Remove collection manually.");
        TRI_CLOSE(fd);
        TRI_CLOSE(fdout);

        TRI_DestroyVectorPointer(&files);
        ReleaseCollection(collection);

        return scope.Close(v8::False());
      }
    }

    // fill up
    if (writtenSize < fileSize) {
      const int max = 10000;
      char b[max];
      memset(b, 0, max);

      while (writtenSize + max < fileSize) {
        writeResult = TRI_WRITE(fdout, b, max);
        (void) writeResult;
        writtenSize += max;
      }

      if (writtenSize < fileSize) {
        writeResult = TRI_WRITE(fdout, b, (TRI_write_t) (fileSize - writtenSize));
        (void) writeResult;
      }
    }

    // file converted!
    TRI_CLOSE(fd);
    TRI_CLOSE(fdout);
  }


  int ok = TRI_ERROR_NO_ERROR;

  for (size_t j = 0; j < files._length; ++j) {
    ostringstream outfile1;
    ostringstream outfile2;

    TRI_datafile_t* df = (TRI_datafile_t*) TRI_AtVectorPointer(&files, j);
    outfile1 << df->_filename << ".old";

    ok = TRI_RenameFile(df->_filename, outfile1.str().c_str());
    if (ok != TRI_ERROR_NO_ERROR) {
      LOG_ERROR("Could not rename file '%s' while upgrading collection '%s'.", df->_filename, name);
      break;
    }

    outfile2 << df->_filename << ".new";

    ok = TRI_RenameFile(outfile2.str().c_str(), df->_filename);
    if (ok != TRI_ERROR_NO_ERROR) {
      LOG_ERROR("Could not rename file '%s' while upgrading collection '%s'.", outfile2.str().c_str(), name);
      break;
    }
  }

  //  int TRI_UnlinkFile (char const* filename);


  TRI_DestroyVectorPointer(&files);

  ReleaseCollection(collection);

  if (ok != TRI_ERROR_NO_ERROR) {
    return scope.Close(v8::False());
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns information about the datafiles
///
/// @FUN{@FA{collection}.datafileScan(@FA{path})}
///
/// Returns information about the datafiles. The collection must be unloaded.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DatafileScanVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "datafileScan(<path>)");
  }

  string path = TRI_ObjectToString(argv[0]);

  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);

  if (collection->_status != TRI_VOC_COL_STATUS_UNLOADED &&
      collection->_status != TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_COLLECTION_NOT_UNLOADED);
  }

  TRI_df_scan_t scan = TRI_ScanDatafile(path.c_str());

  // build result
  v8::Handle<v8::Object> result = v8::Object::New();

  result->Set(v8::String::New("currentSize"), v8::Number::New(scan._currentSize));
  result->Set(v8::String::New("maximalSize"), v8::Number::New(scan._maximalSize));
  result->Set(v8::String::New("endPosition"), v8::Number::New(scan._endPosition));
  result->Set(v8::String::New("numberMarkers"), v8::Number::New(scan._numberMarkers));
  result->Set(v8::String::New("status"), v8::Number::New(scan._status));
  result->Set(v8::String::New("isSealed"), v8::Boolean::New(scan._isSealed));

  v8::Handle<v8::Array> entries = v8::Array::New();
  result->Set(v8::String::New("entries"), entries);

  for (size_t i = 0;  i < scan._entries._length;  ++i) {
    TRI_df_scan_entry_t* entry = (TRI_df_scan_entry_t*) TRI_AtVector(&scan._entries, i);

    v8::Handle<v8::Object> o = v8::Object::New();

    o->Set(v8::String::New("position"), v8::Number::New(entry->_position));
    o->Set(v8::String::New("size"), v8::Number::New(entry->_size));
    o->Set(v8::String::New("tick"), V8TickId(entry->_tick));
    o->Set(v8::String::New("type"), v8::Number::New((int) entry->_type));
    o->Set(v8::String::New("status"), v8::Number::New((int) entry->_status));

    entries->Set((uint32_t) i, o);
  }

  TRI_DestroyDatafileScan(&scan);

  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
  return scope.Close(result);
}

// -----------------------------------------------------------------------------
// --SECTION--                                       TRI_VOCBASE_COL_T FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                              javascript functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief ensures that an index exists
///
/// @FUN{@FA{collection}.ensureIndex(@FA{index-description})}
///
/// Ensures that an index according to the @FA{index-description} exists. A
/// new index will be created if none exists with the given description.
///
/// The @FA{index-description} must contain at least a `type` attribute. 
/// `type` can be one of the following values:
/// - `hash`: hash index
/// - `skiplist`: skiplist index
/// - `fulltext`: fulltext index
/// - `bitarray`: bitarray index
/// - `geo1`: geo index, with one attribute
/// - `geo2`: geo index, with two attributes
/// - `cap`: cap constraint
///
/// Other attributes may be necessary, depending on the index type.
///
/// Calling this method returns an index object. Whether or not the index 
/// object existed before the call is indicated in the return attribute 
/// `isNewlyCreated`.
///
/// @EXAMPLES
///
/// @code
/// arango> db.example.ensureIndex({ type: "hash", fields: [ "name" ], unique: true });
/// { 
///   "id" : "example/30242599562", 
///   "type" : "hash", 
///   "unique" : true, 
///   "fields" : [ 
///     "name" 
///    ], 
///   "isNewlyCreated" : true 
/// }
/// @endcode
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_EnsureIndexVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  PREVENT_EMBEDDED_TRANSACTION(scope);  
  
  return scope.Close(EnsureIndex(argv, true, "ensureIndex"));  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_LookupIndexVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  return scope.Close(EnsureIndex(argv, false, "lookupIndex"));  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief counts the number of documents in a result set
///
/// @FUN{@FA{collection}.count()}
///
/// Returns the number of living documents in the collection.
///
/// @EXAMPLES
///
/// @verbinclude shell-collection-count
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CountVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "count()");
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    // First get the initial data:
    string const dbname(collection->_dbName);
  
    // TODO: someone might rename the collection while we're reading its name...
    string const collname(collection->_name);

    uint64_t count = 0;
    int error = triagens::arango::countOnCoordinator(dbname, collname, count);

    if (error != TRI_ERROR_NO_ERROR) {
      TRI_V8_EXCEPTION(scope, error);
    }

    return scope.Close(v8::Number::New((double) count));
  }
#endif  

  CollectionNameResolver resolver(collection->_vocbase);
  ReadTransactionType trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  TRI_primary_collection_t* primary = trx.primaryCollection();

  // READ-LOCK start
  trx.lockRead();

  const TRI_voc_size_t s = primary->size(primary);

  trx.finish(res);
  // READ-LOCK end

  return scope.Close(v8::Number::New((double) s));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns information about the datafiles
///
/// @FUN{@FA{collection}.datafiles()}
///
/// Returns information about the datafiles. The collection must be unloaded.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DatafilesVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, collection);

  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);

  if (collection->_status != TRI_VOC_COL_STATUS_UNLOADED &&
      collection->_status != TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_COLLECTION_NOT_UNLOADED);
  }

  TRI_col_file_structure_t structure = TRI_FileStructureCollectionDirectory(collection->_path);

  // release lock
  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);

  // build result
  v8::Handle<v8::Object> result = v8::Object::New();

  // journals
  v8::Handle<v8::Array> journals = v8::Array::New();
  result->Set(v8::String::New("journals"), journals);

  for (size_t i = 0;  i < structure._journals._length;  ++i) {
    journals->Set((uint32_t) i, v8::String::New(structure._journals._buffer[i]));
  }

  // compactors
  v8::Handle<v8::Array> compactors = v8::Array::New();
  result->Set(v8::String::New("compactors"), compactors);

  for (size_t i = 0;  i < structure._compactors._length;  ++i) {
    compactors->Set((uint32_t) i, v8::String::New(structure._compactors._buffer[i]));
  }

  // datafiles
  v8::Handle<v8::Array> datafiles = v8::Array::New();
  result->Set(v8::String::New("datafiles"), datafiles);

  for (size_t i = 0;  i < structure._datafiles._length;  ++i) {
    datafiles->Set((uint32_t) i, v8::String::New(structure._datafiles._buffer[i]));
  }

  // free result
  TRI_DestroyFileStructureCollection(&structure);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a document
/// @startDocuBlock documents_collectionName
/// `collection.document(document)`
///
/// The *document* method finds a document given its identifier or a document
/// object containing the *_id* or *_key* attribute. The method returns
/// the document if it can be found. 
///
/// An error is thrown if *_rev* is specified but the document found has a
/// different revision already. An error is also thrown if no document exists 
/// with the given *_id* or *_key* value.
///
/// Please note that if the method is executed on the arangod server (e.g. from 
/// inside a Foxx application), an immutable document object will be returned
/// for performance reasons. It is not possible to change attributes of this 
/// immutable object. To update or patch the returned document, it needs to be
/// cloned/copied into a regular JavaScript object first. This is not necessary
/// if the *document* method is called from out of arangosh or from any other 
/// client.
///
/// `collection.document(document-handle)`
///
/// As before. Instead of document a *document-handle* can be passed as
/// first argument.
///
/// *Examples*
///
/// Returns the document for a document-handle:
///
/// @code
/// arango> db.example.document("example/2873916");
/// { "_id" : "example/2873916", "_key" : "2873916", "_rev" : "2873916", "Hello" : "World" }
/// @endcode
///
/// An error is raised if the document is unknown:
///
/// @code
/// arango> db.example.document("example/123456");
/// JavaScript exception in file '(arango)' at 1,12:
///   [ArangoError 1202: document not found: document not found]
/// !db.example.document("example/123456");
/// !           ^
/// @endcode
///
/// An error is raised if the handle is invalid:
///
/// @code
/// arango> db.example.document("");
/// JavaScript exception in file '(arango)' at 1,28: [ArangoError 1205: illegal document handle]
/// !db.example.document("");
/// !           ^
/// @endcode
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DocumentVocbaseCol (v8::Arguments const& argv) {
  return DocumentVocbaseCol(true, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief drops a collection, case of a coordinator in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> DropVocbaseColCoordinator (TRI_vocbase_col_t* collection) {
  v8::HandleScope scope;

  if (! collection->_canDrop) { 
    TRI_V8_EXCEPTION(scope, TRI_ERROR_FORBIDDEN);
  }
  
  string const databaseName(collection->_dbName);
  string const cid = StringUtils::itoa(collection->_cid);
  
  ClusterInfo* ci = ClusterInfo::instance();
  string errorMsg;
 
  int res = ci->dropCollectionCoordinator(databaseName, cid, errorMsg, 120.0);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, errorMsg);
  }

  collection->_status = TRI_VOC_COL_STATUS_DELETED;
  
  return scope.Close(v8::Undefined());
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief drops a collection
/// @startDocuBlock collection_drop
/// `collection.drop()`
///
/// Drops a *collection* and all its indexes.
///
/// *EXAMPLES*
///
/// @verbinclude shell_collection-drop
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DropVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  PREVENT_EMBEDDED_TRANSACTION(scope);  
 
#ifdef TRI_ENABLE_CLUSTER
  // If we are a coordinator in a cluster, we have to behave differently:
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(DropVocbaseColCoordinator(collection));
  }
#endif

  int res = TRI_DropCollectionVocBase(collection->_vocbase, collection, TRI_GetIdServer());

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot drop collection");
  }
    
  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief drops an index, coordinator case
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> DropIndexCoordinator (CollectionNameResolver const& resolver,
                                                   TRI_vocbase_col_t const* collection,
                                                   v8::Handle<v8::Value> const val) {
  v8::HandleScope scope;
  
  string collectionName = "";
  TRI_idx_iid_t iid = 0;
  
  // extract the index identifier from a string
  if (val->IsString() || val->IsStringObject() || val->IsNumber()) {
    if (! IsIndexHandle(val, collectionName, iid)) {
      TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_INDEX_HANDLE_BAD);
    }
  }

  // extract the index identifier from an object
  else if (val->IsObject()) {
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

    v8::Handle<v8::Object> obj = val->ToObject();
    v8::Handle<v8::Value> iidVal = obj->Get(v8g->IdKey);

    if (! IsIndexHandle(iidVal, collectionName, iid)) {
      TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_INDEX_HANDLE_BAD);
    }
  }

  if (collectionName != "") {
    if (! EqualCollection(resolver, collectionName, collection)) {
      TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_CROSS_COLLECTION_REQUEST);
    }
  }


  string const databaseName(collection->_dbName);
  string const cid = StringUtils::itoa(collection->_cid);
  string errorMsg;
    
  int res = ClusterInfo::instance()->dropIndexCoordinator(databaseName, cid, iid, errorMsg, 0.0);

  return scope.Close(v8::Boolean::New(res == TRI_ERROR_NO_ERROR));
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief drops an index
///
/// @FUN{@FA{collection}.dropIndex(@FA{index})}
///
/// Drops the index. If the index does not exist, then @LIT{false} is
/// returned. If the index existed and was dropped, then @LIT{true} is
/// returned. Note that you cannot drop some special indexes (e.g. the primary 
/// index of a collection or the edge index of an edge collection).
///
/// @FUN{@FA{collection}.dropIndex(@FA{index-handle})}
///
/// Same as above. Instead of an index an index handle can be given.
///
/// @EXAMPLES
///
/// @verbinclude shell_index-drop-index
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DropIndexVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  PREVENT_EMBEDDED_TRANSACTION(scope);  
  
  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "dropIndex(<index-handle>)");
  }

  CollectionNameResolver resolver(collection->_vocbase);

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(DropIndexCoordinator(resolver, collection, argv[0]));
  }
#endif

  ReadTransactionType trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }
  
  TRI_document_collection_t* document = (TRI_document_collection_t*) trx.primaryCollection();

  v8::Handle<v8::Object> err;
  TRI_index_t* idx = TRI_LookupIndexByHandle(collection, argv[0], true, &err);

  if (idx == 0) {
    if (err.IsEmpty()) {
      return scope.Close(v8::False());
    }
    else {
      return scope.Close(v8::ThrowException(err));
    }
  }
   
  if (idx->_iid == 0) {
    return scope.Close(v8::False());
  }

  if (idx->_type == TRI_IDX_TYPE_PRIMARY_INDEX || 
      idx->_type == TRI_IDX_TYPE_EDGE_INDEX) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_FORBIDDEN);
  }

  // .............................................................................
  // inside a write transaction, write-lock is acquired by TRI_DropIndex...
  // .............................................................................

  bool ok = TRI_DropIndexDocumentCollection(document, idx->_iid, TRI_GetIdServer());

  // .............................................................................
  // outside a write transaction
  // .............................................................................

  return scope.Close(v8::Boolean::New(ok));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks whether a document exists
/// @startDocuBlock documents_collectionExists
/// `collection.exists(document)`
///
/// The *exists* method determines whether a document exists given its 
/// identifier.  Instead of returning the found document or an error, this
/// method will return either *true* or *false*. It can thus be used 
/// for easy existence checks.
///
/// The *document* method finds a document given its identifier.  It returns
/// the document. Note that the returned document contains two
/// pseudo-attributes, namely *_id* and *_rev*. *_id* contains the
/// document-handle and *_rev* the revision of the document.
///
/// No error will be thrown if the sought document or collection does not 
/// exist.
/// Still this method will throw an error if used improperly, e.g. when called
/// with a non-document handle, a non-document, or when a cross-collection
/// request is performed.
///
/// `collection.exists(document-handle)`
///
/// As before. Instead of document a *document-handle* can be passed as
/// first argument.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ExistsVocbaseCol (v8::Arguments const& argv) {
  return ExistsVocbaseCol(true, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief fetch the figures for a sharded collection
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static TRI_doc_collection_info_t* GetFiguresCoordinator (TRI_vocbase_col_t* collection) {
  assert(collection != 0);
  
  string const databaseName(collection->_dbName);
  string const cid = StringUtils::itoa(collection->_cid);
   
  TRI_doc_collection_info_t* result = 0;

  int res = figuresOnCoordinator(databaseName, cid, result);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(res);
    return 0;
  }

  return result;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief fetch the figures for a local collection
////////////////////////////////////////////////////////////////////////////////

static TRI_doc_collection_info_t* GetFigures (TRI_vocbase_col_t* collection) {
  assert(collection != 0);
  
  CollectionNameResolver resolver(collection->_vocbase);
  ReadTransactionType trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_set_errno(res);
    return 0;
  }

  // READ-LOCK start
  trx.lockRead();

  TRI_primary_collection_t* primary = collection->_collection;
  TRI_doc_collection_info_t* info = primary->figures(primary);

  res = trx.finish(res);
  // READ-LOCK end
 
  return info;
} 

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the figures of a collection
/// @startDocuBlock clollection_figures
/// `collection.figures()`
///
/// Returns an object containing all collection figures.
///
/// * *alive.count*: The number of living documents.
/// * *alive.size*: The total size in bytes used by all
///   living documents.
/// * *dead.count*: The number of dead documents.
/// * *dead.size*: The total size in bytes used by all
///   dead documents.
/// * *dead.deletion*: The total number of deletion markers.
/// * *datafiles.count*: The number of active datafiles.
/// * *datafiles.fileSize*: The total filesize of the active datafiles
///   (in bytes).
/// * *journals.count*: The number of journal files.
/// * *journals.fileSize*: The total filesize of the journal files
///   (in bytes).
/// * *compactors.count*: The number of compactor files.
/// * *compactors.fileSize*: The total filesize of the compactor files
///   (in bytes).
/// * *shapefiles.count*: The number of shape files. This value is
///   deprecated and kept for compatibility reasons only. The value will always 
///   be 0.
/// * *shapefiles.fileSize*: The total filesize of the shape files. This 
///   value is deprecated and kept for compatibility reasons only. The value will 
///   always be 0.
/// * *shapes.count*: The total number of shapes used in the collection.
///   This includes shapes that are not in use anymore.
/// * *shapes.size*: The total size of all shapes (in bytes). This includes
///   shapes that are not in use anymore.
/// * *attributes.count*: The total number of attributes used in the 
///   collection. Note: the value includes data of attributes that are not in use 
///   anymore.
/// * *attributes.size*: The total size of the attribute data (in bytes).
///   Note: the value includes data of attributes that are not in use anymore.
/// * *indexes.count*: The total number of indexes defined for the
///   collection, including the pre-defined indexes (e.g. primary index).
/// * *indexes.size*: The total memory allocated for indexes in bytes.
///
/// *Examples*
///
/// @verbinclude shell_collection-figures
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_FiguresVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  v8::Handle<v8::Object> result = v8::Object::New();

#ifdef TRI_ENABLE_CLUSTER
  TRI_doc_collection_info_t* info;

  if (ServerState::instance()->isCoordinator()) {
    info = GetFiguresCoordinator(collection);
  }
  else {
    info = GetFigures(collection);
  }
#else
  TRI_doc_collection_info_t* info = GetFigures(collection);
#endif

  if (info == 0) {
    TRI_V8_EXCEPTION(scope, TRI_errno());
  }

  assert(info != 0);

  if (info == NULL) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  v8::Handle<v8::Object> alive = v8::Object::New();

  result->Set(v8::String::New("alive"), alive);
  alive->Set(v8::String::New("count"), v8::Number::New((double) info->_numberAlive));
  alive->Set(v8::String::New("size"), v8::Number::New((double) info->_sizeAlive));

  v8::Handle<v8::Object> dead = v8::Object::New();

  result->Set(v8::String::New("dead"), dead);
  dead->Set(v8::String::New("count"), v8::Number::New((double) info->_numberDead));
  dead->Set(v8::String::New("size"), v8::Number::New((double) info->_sizeDead));
  dead->Set(v8::String::New("deletion"), v8::Number::New((double) info->_numberDeletion));

  // datafile info
  v8::Handle<v8::Object> dfs = v8::Object::New();

  result->Set(v8::String::New("datafiles"), dfs);
  dfs->Set(v8::String::New("count"), v8::Number::New((double) info->_numberDatafiles));
  dfs->Set(v8::String::New("fileSize"), v8::Number::New((double) info->_datafileSize));

  // journal info
  v8::Handle<v8::Object> js = v8::Object::New();

  result->Set(v8::String::New("journals"), js);
  js->Set(v8::String::New("count"), v8::Number::New((double) info->_numberJournalfiles));
  js->Set(v8::String::New("fileSize"), v8::Number::New((double) info->_journalfileSize));
  
  // compactors info
  v8::Handle<v8::Object> cs = v8::Object::New();

  result->Set(v8::String::New("compactors"), cs);
  cs->Set(v8::String::New("count"), v8::Number::New((double) info->_numberCompactorfiles));
  cs->Set(v8::String::New("fileSize"), v8::Number::New((double) info->_compactorfileSize));
  
  // shapefiles info
  v8::Handle<v8::Object> sf = v8::Object::New();

  result->Set(v8::String::New("shapefiles"), sf);
  sf->Set(v8::String::New("count"), v8::Number::New((double) info->_numberShapefiles));
  sf->Set(v8::String::New("fileSize"), v8::Number::New((double) info->_shapefileSize));

  // shape info
  v8::Handle<v8::Object> shapes = v8::Object::New();
  result->Set(v8::String::New("shapes"), shapes);
  shapes->Set(v8::String::New("count"), v8::Number::New((double) info->_numberShapes));
  shapes->Set(v8::String::New("size"), v8::Number::New((double) info->_sizeShapes));
  
  // attributes info
  v8::Handle<v8::Object> attributes = v8::Object::New();
  result->Set(v8::String::New("attributes"), attributes);
  attributes->Set(v8::String::New("count"), v8::Number::New((double) info->_numberAttributes));
  attributes->Set(v8::String::New("size"), v8::Number::New((double) info->_sizeAttributes));

  v8::Handle<v8::Object> indexes = v8::Object::New();
  result->Set(v8::String::New("indexes"), indexes);
  indexes->Set(v8::String::New("count"), v8::Number::New((double) info->_numberIndexes));
  indexes->Set(v8::String::New("size"), v8::Number::New((double) info->_sizeIndexes));

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, info);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns information about the indexes, coordinator case
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> GetIndexesCoordinator (TRI_vocbase_col_t const* collection) {
  v8::HandleScope scope;
    
  string const databaseName(collection->_dbName);
  string const cid = StringUtils::itoa(collection->_cid);
  string const collectionName(collection->_name);
    
  shared_ptr<CollectionInfo> c = ClusterInfo::instance()->getCollection(databaseName, cid);
  
  if ((*c).empty()) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND);
  }

  v8::Handle<v8::Array> ret = v8::Array::New();

  TRI_json_t const* json = (*c).getIndexes();
  if (TRI_IsListJson(json)) {
    uint32_t j = 0;

    for (size_t i = 0;  i < json->_value._objects._length; ++i) {
      TRI_json_t const* v = TRI_LookupListJson(json, i);

      if (v != 0) {
        ret->Set(j++, IndexRep(collectionName, v));
      }
    }
  }

  return scope.Close(ret);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief returns information about the indexes
///
/// @FUN{getIndexes()}
///
/// Returns a list of all indexes defined for the collection. 
///
/// @EXAMPLES
///
/// @verbinclude shell_index-read-all
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_GetIndexesVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(GetIndexesCoordinator(collection));
  }
#endif

  CollectionNameResolver resolver(collection->_vocbase);
  ReadTransactionType trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }
  
  // READ-LOCK start
  trx.lockRead();

  TRI_document_collection_t* document = (TRI_document_collection_t*) trx.primaryCollection();
  const string collectionName = string(collection->_name);

  // get list of indexes
  TRI_vector_pointer_t* indexes = TRI_IndexesDocumentCollection(document);

  trx.finish(res);
  // READ-LOCK end

  if (indexes == 0) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  v8::Handle<v8::Array> result = v8::Array::New();

  uint32_t n = (uint32_t) indexes->_length;

  for (uint32_t i = 0, j = 0;  i < n;  ++i) {
    TRI_json_t* idx = (TRI_json_t*) indexes->_buffer[i];

    if (idx != 0) {
      result->Set(j++, IndexRep(collectionName, idx));
      TRI_FreeJson(TRI_CORE_MEM_ZONE, idx);
    }
  }

  TRI_FreeVectorPointer(TRI_CORE_MEM_ZONE, indexes);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief loads a collection
/// @startDocuBlock collection_load
/// `collection.load()`
///
/// Loads a collection into memory.
///
/// *Examples*
///
/// @verbinclude shell_collection-load
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_LoadVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    TRI_vocbase_col_t const* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

    if (collection == 0) {
      TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
    }

    string const databaseName(collection->_dbName);
    string const cid = StringUtils::itoa(collection->_cid);
    
    int res = ClusterInfo::instance()->setCollectionStatusCoordinator(databaseName, cid, TRI_VOC_COL_STATUS_LOADED);

    if (res != TRI_ERROR_NO_ERROR) {
      TRI_V8_EXCEPTION(scope, res);
    }
  
    return scope.Close(v8::Undefined());
  }
#endif

  v8::Handle<v8::Object> err;
  TRI_vocbase_col_t const* collection = UseCollection(argv.Holder(), &err);

  if (collection == 0) {
    return scope.Close(v8::ThrowException(err));
  }

  ReleaseCollection(collection);
  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the name of a collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_NameVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
#ifdef TRI_ENABLE_CLUSTER
  if (! collection->_isLocal) {
    v8::Handle<v8::Value> result = v8::String::New(collection->_name);
    return scope.Close(result);
  }
#endif

  // this copies the name into a new place so we can safely access it later
  // if we wouldn't do this, we would risk other threads modifying the name while
  // we're reading it
  char* name = TRI_GetCollectionNameByIdVocBase(collection->_vocbase, collection->_cid);

  if (name == 0) {
    return scope.Close(v8::Undefined());
  }

  v8::Handle<v8::Value> result = v8::String::New(name);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, name);

  return scope.Close(result);
}

#ifdef TRI_ENABLE_CLUSTER

static v8::Handle<v8::Value> JS_PlanIdVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t const* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(V8CollectionId(collection->_cid));
  }

  return scope.Close(V8CollectionId(collection->_planId));
}

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief gets or sets the properties of a collection
/// @startDocuBlock collection_properties
/// `collection.properties()`
///
/// Returns an object containing all collection properties.
///
/// * *waitForSync*: If *true* creating a document will only return
///   after the data was synced to disk.
///
/// * *journalSize* : The size of the journal in bytes.
///
/// * *isVolatile*: If *true* then the collection data will be
///   kept in memory only and ArangoDB will not write or sync the data
///   to disk.
///
/// * *keyOptions* (optional) additional options for key generation. This is
///   a JSON array containing the following attributes (note: some of the
///   attributes are optional):
///   * *type*: the type of the key generator used for the collection.
///   * *allowUserKeys*: if set to *true*, then it is allowed to supply
///     own key values in the *_key* attribute of a document. If set to
///     *false*, then the key generator will solely be responsible for
///     generating keys and supplying own key values in the *_key* attribute
///     of documents is considered an error.
///   * *increment*: increment value for *autoincrement* key generator.
///     Not used for other key generator types.
///   * *offset*: initial offset value for *autoincrement* key generator.
///     Not used for other key generator types.
///
/// In a cluster setup, the result will also contain the following attributes:
///
/// * *numberOfShards*: the number of shards of the collection.
///
/// * *shardKeys*: contains the names of document attributes that are used to 
///   determine the target shard for documents. 
///
/// `collection.properties(properties)`
///
/// Changes the collection properties. *properties* must be a object with
/// one or more of the following attribute(s):
///
/// * *waitForSync*: If *true* creating a document will only return
///   after the data was synced to disk.
///
/// * *journalSize* : The size of the journal in bytes.
///
/// *Note*: it is not possible to change the journal size after the journal or
/// datafile has been created. Changing this parameter will only effect newly
/// created journals. Also note that you cannot lower the journal size to less
/// then size of the largest document already stored in the collection.
///
/// *Note*: some other collection properties, such as *type*, *isVolatile*,
/// or *keyOptions* cannot be changed once the collection is created.
///
/// *Examples*
///
/// Read all properties
///
/// @verbinclude shell_collection-properties
///
/// Change a property
///
/// @verbinclude shell_collection-properties-change
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_PropertiesVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  TRI_vocbase_col_t const* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    std::string const databaseName = std::string(collection->_dbName);
    TRI_col_info_t info = ClusterInfo::instance()->getCollectionProperties(databaseName, StringUtils::itoa(collection->_cid));

    if (0 < argv.Length()) {
      v8::Handle<v8::Value> par = argv[0];

      if (par->IsObject()) {
        v8::Handle<v8::Object> po = par->ToObject();

        // extract doCompact flag
        if (po->Has(v8g->DoCompactKey)) {
          info._doCompact = TRI_ObjectToBoolean(po->Get(v8g->DoCompactKey));
        }

        // extract sync flag
        if (po->Has(v8g->WaitForSyncKey)) {
          info._waitForSync = TRI_ObjectToBoolean(po->Get(v8g->WaitForSyncKey));
        }

        // extract the journal size
        if (po->Has(v8g->JournalSizeKey)) {
          info._maximalSize = (TRI_voc_size_t) TRI_ObjectToUInt64(po->Get(v8g->JournalSizeKey), false);

          if (info._maximalSize < TRI_JOURNAL_MINIMAL_SIZE) {
            if (info._keyOptions != 0) {
              TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, info._keyOptions);
            }
            TRI_V8_EXCEPTION_PARAMETER(scope, "<properties>.journalSize too small");
          }
        }

        if (po->Has(v8g->IsVolatileKey)) {
          if (TRI_ObjectToBoolean(po->Get(v8g->IsVolatileKey)) != info._isVolatile) {
            if (info._keyOptions != 0) {
              TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, info._keyOptions);
            }
            TRI_V8_EXCEPTION_PARAMETER(scope, "isVolatile option cannot be changed at runtime");
          }
        }

        if (info._isVolatile && info._waitForSync) {
          if (info._keyOptions != 0) {
            TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, info._keyOptions);
          }
          TRI_V8_EXCEPTION_PARAMETER(scope, "volatile collections do not support the waitForSync option");
        }
      }
    
      int res = ClusterInfo::instance()->setCollectionPropertiesCoordinator(databaseName, StringUtils::itoa(collection->_cid), &info);

      if (res != TRI_ERROR_NO_ERROR) {
        if (info._keyOptions != 0) {
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, info._keyOptions);
        }
        TRI_V8_EXCEPTION(scope, res);
      }
    }


    // return the current parameter set
    v8::Handle<v8::Object> result = v8::Object::New();

    result->Set(v8g->DoCompactKey, info._doCompact ? v8::True() : v8::False());
    result->Set(v8g->IsSystemKey, info._isSystem ? v8::True() : v8::False());
    result->Set(v8g->IsVolatileKey, info._isVolatile ? v8::True() : v8::False());
    result->Set(v8g->JournalSizeKey, v8::Number::New(info._maximalSize));
    result->Set(v8g->WaitForSyncKey, info._waitForSync ? v8::True() : v8::False());

    shared_ptr<CollectionInfo> c = ClusterInfo::instance()->getCollection(databaseName, StringUtils::itoa(collection->_cid));
    v8::Handle<v8::Array> shardKeys = v8::Array::New();
    const vector<string> sks = (*c).shardKeys();
    for (size_t i = 0; i < sks.size(); ++i) {
      shardKeys->Set((uint32_t) i, v8::String::New(sks[i].c_str(), (int) sks[i].size()));
    }
    result->Set(v8::String::New("shardKeys"), shardKeys);
    result->Set(v8::String::New("numberOfShards"), v8::Number::New((*c).numberOfShards()));

    if (info._keyOptions != 0) {
      result->Set(v8g->KeyOptionsKey, TRI_ObjectJson(info._keyOptions)->ToObject());
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, info._keyOptions);
    }

    return scope.Close(result);
  }
#endif

  v8::Handle<v8::Object> err;
  collection = UseCollection(argv.Holder(), &err);

  if (collection == 0) {
    return scope.Close(v8::ThrowException(err));
  }

  TRI_primary_collection_t* primary = collection->_collection;
  TRI_collection_t* base = &primary->base;

  TRI_document_collection_t* document = (TRI_document_collection_t*) primary;

  // check if we want to change some parameters
  if (0 < argv.Length()) {
    v8::Handle<v8::Value> par = argv[0];

    if (par->IsObject()) {
      v8::Handle<v8::Object> po = par->ToObject();

      // get the old values
      TRI_LOCK_JOURNAL_ENTRIES_DOC_COLLECTION(document);

      TRI_voc_size_t maximalSize = base->_info._maximalSize;
      bool doCompact     = base->_info._doCompact;
      bool waitForSync   = base->_info._waitForSync;

      TRI_UNLOCK_JOURNAL_ENTRIES_DOC_COLLECTION(document);

      // extract doCompact flag
      if (po->Has(v8g->DoCompactKey)) {
        doCompact = TRI_ObjectToBoolean(po->Get(v8g->DoCompactKey));
      }

      // extract sync flag
      if (po->Has(v8g->WaitForSyncKey)) {
        waitForSync = TRI_ObjectToBoolean(po->Get(v8g->WaitForSyncKey));
      }

      // extract the journal size
      if (po->Has(v8g->JournalSizeKey)) {
        maximalSize = (TRI_voc_size_t) TRI_ObjectToUInt64(po->Get(v8g->JournalSizeKey), false);

        if (maximalSize < TRI_JOURNAL_MINIMAL_SIZE) {
          ReleaseCollection(collection);
          TRI_V8_EXCEPTION_PARAMETER(scope, "<properties>.journalSize too small");
        }
      }

      if (po->Has(v8g->IsVolatileKey)) {
        if (TRI_ObjectToBoolean(po->Get(v8g->IsVolatileKey)) != base->_info._isVolatile) {
          ReleaseCollection(collection);
          TRI_V8_EXCEPTION_PARAMETER(scope, "isVolatile option cannot be changed at runtime");
        }
      }

      if (base->_info._isVolatile && waitForSync) {
        // the combination of waitForSync and isVolatile makes no sense
        ReleaseCollection(collection);
        TRI_V8_EXCEPTION_PARAMETER(scope, "volatile collections do not support the waitForSync option");
      }

      // update collection
      TRI_col_info_t newParameter;

      newParameter._doCompact   = doCompact;
      newParameter._maximalSize = maximalSize;
      newParameter._waitForSync = waitForSync;

      // try to write new parameter to file
      int res = TRI_UpdateCollectionInfo(base->_vocbase, base, &newParameter);

      if (res != TRI_ERROR_NO_ERROR) {
        ReleaseCollection(collection);
        TRI_V8_EXCEPTION(scope, res);
      }

      TRI_json_t* json = TRI_CreateJsonCollectionInfo(&base->_info);
      TRI_LogChangePropertiesCollectionReplication(base->_vocbase, 
                                                   base->_info._cid, 
                                                   base->_info._name, 
                                                   json, 
                                                   TRI_GetIdServer()); 
      TRI_FreeJson(TRI_CORE_MEM_ZONE, json);
    }
  }

  // return the current parameter set
  v8::Handle<v8::Object> result = v8::Object::New();

  result->Set(v8g->DoCompactKey, base->_info._doCompact ? v8::True() : v8::False());
  result->Set(v8g->IsSystemKey, base->_info._isSystem ? v8::True() : v8::False());
  result->Set(v8g->IsVolatileKey, base->_info._isVolatile ? v8::True() : v8::False());
  result->Set(v8g->JournalSizeKey, v8::Number::New(base->_info._maximalSize));

  TRI_json_t* keyOptions = primary->_keyGenerator->toJson(primary->_keyGenerator);

  if (keyOptions != 0) {
    result->Set(v8g->KeyOptionsKey, TRI_ObjectJson(keyOptions)->ToObject());

    TRI_FreeJson(TRI_CORE_MEM_ZONE, keyOptions);
  }
  else {
    result->Set(v8g->KeyOptionsKey, v8::Array::New());
  }
  result->Set(v8g->WaitForSyncKey, base->_info._waitForSync ? v8::True() : v8::False());

  ReleaseCollection(collection);
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document
/// @startDocuBlock documents_documentRemove
/// `collection.remove(document)`
///
/// Removes a document. If there is revision mismatch, then an error is thrown.
///
/// `collection.remove(document, true)`
///
/// Removes a document. If there is revision mismatch, then mismatch is ignored
/// and document is deleted. The function returns *true* if the document
/// existed and was deleted. It returns *false*, if the document was already
/// deleted.
///
/// `collection.remove(document, true, waitForSync)`
///
/// The optional *waitForSync* parameter can be used to force synchronization
/// of the document deletion operation to disk even in case that the
/// *waitForSync* flag had been disabled for the entire collection.  Thus,
/// the *waitForSync* parameter can be used to force synchronisation of just
/// specific operations. To use this, set the *waitForSync* parameter to
/// *true*. If the *waitForSync* parameter is not specified or set to
/// *false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// `collection.remove(document-handle, data)`
///
/// As before. Instead of document a *document-handle* can be passed as
/// first argument.
///
/// *Examples*
///
/// Remove a document:
///
/// @code
/// arango> a1 = db.example.save({ a : 1 });
/// { "_id" : "example/3449537", "_key" : "3449537", "_rev" : "3449537" }
/// arango> db.example.document(a1);
/// { "_id" : "example/3449537", "_key" : "3449537", "_rev" : "3449537", "a" : 1 }
/// arango> db.example.remove(a1);
/// true
/// arango> db.example.document(a1);
/// JavaScript exception in file '(arango)' at 1,12: [ArangoError 1202: document not found: document not found]
/// !db.example.document(a1);
/// !           ^
/// @endcode
///
/// Remove a document with a conflict:
///
/// @code
/// arango> a1 = db.example.save({ a : 1 });
/// { "_id" : "example/3857139", "_key" : "3857139", "_rev" : "3857139" }
/// arango> a2 = db.example.replace(a1, { a : 2 });
/// { "_id" : "example/3857139", "_key" : "3857139", "_rev" : "3922675", "_oldRev" : 3857139 }
/// arango> db.example.remove(a1);
/// JavaScript exception in file '(arango)' at 1,18: [ArangoError 1200: conflict: cannot remove document]
/// !db.example.remove(a1);
/// !                 ^
/// arango> db.example.remove(a1, true);
/// true
/// arango> db.example.document(a1);
/// JavaScript exception in file '(arango)' at 1,12: [ArangoError 1202: document not found: document not found]
/// !db.example.document(a1);
/// !           ^
/// @endcode
/// endCodeBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RemoveVocbaseCol (v8::Arguments const& argv) {
  return RemoveVocbaseCol(true, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief renames a collection
/// @startDocuBlock collection_rename
/// `collection.rename(new-name)`
///
/// Renames a collection using the *new-name*. The *new-name* must not
/// already be used for a different collection. *new-name* must also be a
/// valid collection name. For more information on valid collection names please refer
/// to the [naming conventions](../NamingConventions/README.md).
///
/// If renaming fails for any reason, an error is thrown.
///
/// Note: this method is not available in a cluster.
///
/// *Examples*
///
/// @verbinclude shell_collection-rename
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RenameVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "rename(<name>)");
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    // renaming a collection in a cluster is unsupported
    TRI_V8_EXCEPTION(scope, TRI_ERROR_CLUSTER_UNSUPPORTED);
  }
#endif

  string const name = TRI_ObjectToString(argv[0]);

  // second parameter "override" is to override renaming restrictions, e.g.
  // renaming from a system collection name to a non-system collection name and
  // vice versa. this parameter is not publicly exposed but used internally
  bool override = false;
  if (argv.Length() > 1) {
    override = TRI_ObjectToBoolean(argv[1]);
  }

  if (name.empty()) {
    TRI_V8_EXCEPTION_PARAMETER(scope, "<name> must be non-empty");
  }

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  PREVENT_EMBEDDED_TRANSACTION(scope);  

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    // renaming a collection in a cluster is unsupported
    TRI_V8_EXCEPTION(scope, TRI_ERROR_CLUSTER_UNSUPPORTED);
  }
#endif

  int res = TRI_RenameCollectionVocBase(collection->_vocbase, 
                                        collection, 
                                        name.c_str(), 
                                        override,
                                        TRI_GetIdServer());

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot rename collection");
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replaces a document
/// @startDocuBlock documents_collectionReplace
/// `collection.replace(document, data)`
///
/// Replaces an existing *document*. The *document* must be a document in
/// the current collection. This document is then replaced with the
/// *data* given as second argument.
///
/// The method returns a document with the attributes *_id*, *_rev* and
/// *{_oldRev*.  The attribute *_id* contains the document handle of the
/// updated document, the attribute *_rev* contains the document revision of
/// the updated document, the attribute *_oldRev* contains the revision of
/// the old (now replaced) document.
///
/// If there is a conflict, i. e. if the revision of the *document* does not
/// match the revision in the collection, then an error is thrown.
///
/// @FUN{@FA{collection}.replace(@FA{document}, @FA{data}, true)} or
/// @FUN{@FA{collection}.replace(@FA{document}, @FA{data}, {@FA{overwrite}: true})}
///
/// As before, but in case of a conflict, the conflict is ignored and the old
/// document is overwritten.
///
/// `collection.replace(document, data, true, waitForSync)` or
/// `collection.replace(document, data, overwrite: true, waitForSync: true or false)` 
///
/// The optional *waitForSync* parameter can be used to force
/// synchronisation of the document replacement operation to disk even in case
/// that the *waitForSync* flag had been disabled for the entire collection.
/// Thus, the *waitForSync* parameter can be used to force synchronisation
/// of just specific operations. To use this, set the *waitForSync* parameter
/// to *true*. If the *waitForSync* parameter is not specified or set to
/// *false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// `collection.replace(document-handle, data)`
///
/// As before. Instead of document a @FA{document-handle} can be passed as
/// first argument.
///
/// *Examples*
///
/// Create and update a document:
///
/// @TINYEXAMPLE{shell_replace-document,replacing a document}
///
/// Use a document handle:
///
/// @TINYEXAMPLE{shell_replace-document-handle,replacing a document}
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ReplaceVocbaseCol (v8::Arguments const& argv) {
  return ReplaceVocbaseCol(true, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief fetch the revision for a local collection
////////////////////////////////////////////////////////////////////////////////

static int GetRevision (TRI_vocbase_col_t* collection,
                        TRI_voc_rid_t& rid) {
  assert(collection != 0);

  CollectionNameResolver resolver(collection->_vocbase);
  ReadTransactionType trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }

  // READ-LOCK start
  trx.lockRead();
  TRI_primary_collection_t* primary = collection->_collection;
  rid = primary->base._info._revision;

  trx.finish(res);
  // READ-LOCK end

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief fetch the revision for a sharded collection
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static int GetRevisionCoordinator (TRI_vocbase_col_t* collection,
                                   TRI_voc_rid_t& rid) {
  assert(collection != 0);
  
  string const databaseName(collection->_dbName);
  string const cid = StringUtils::itoa(collection->_cid);
   
  int res = revisionOnCoordinator(databaseName, cid, rid);

  return res;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the revision id of a collection
/// @startDocuBlock collection_load
/// `collection.revision()`
///
/// Returns the revision id of the collection
///
/// The revision id is updated when the document data is modified, either by
/// inserting, deleting, updating or replacing documents in it.
///
/// The revision id of a collection can be used by clients to check whether
/// data in a collection has changed or if it is still unmodified since a
/// previous fetch of the revision id.
///
/// The revision id returned is a string value. Clients should treat this value
/// as an opaque string, and only use it for equality/non-equality comparisons.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RevisionVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
 
  TRI_voc_rid_t rid;
  int res;
   
#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    res = GetRevisionCoordinator(collection, rid);
  }
  else {
    res = GetRevision(collection, rid);
  }
#else
  res = GetRevision(collection, rid);
#endif

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  return scope.Close(V8RevisionId(rid));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief rotates the current journal of a collection
/// @startDocuBlock collection_rotate
/// `collection.rotate()`
///
/// Rotates the current journal of a collection (i.e. makes the journal a 
/// read-only datafile). The purpose of the rotation is to include the
/// datafile in a following compaction run and perform earlier garbage 
/// collection.
///
/// Note: this method is not available in a cluster.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RotateVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    // renaming a collection in a cluster is unsupported
    TRI_V8_EXCEPTION(scope, TRI_ERROR_CLUSTER_UNSUPPORTED);
  }
#endif

  v8::Handle<v8::Object> err;
  TRI_vocbase_col_t const* collection = UseCollection(argv.Holder(), &err);

  if (collection == 0) {
    return scope.Close(v8::ThrowException(err));
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, collection);

  TRI_document_collection_t* document = (TRI_document_collection_t*) collection->_collection;

  int res = TRI_RotateJournalDocumentCollection(document);

  ReleaseCollection(collection);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "could not rotate journal");
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief updates a document
/// @startDocuBlock documents_collectionUpdate
/// `collection.update(document, data, overwrite, keepNull, waitForSync)` or
/// `collection.update(document, data, 
/// overwrite: true or false, keepNull: true or false, waitForSync: true or false)` 
///
/// Updates an existing *document*. The *document* must be a document in
/// the current collection. This document is then patched with the
/// *data* given as second argument. The optional *overwrite* parameter can
/// be used to control the behavior in case of version conflicts (see below).
/// The optional *keepNull* parameter can be used to modify the behavior when
/// handling *null* values. Normally, *null* values are stored in the
/// database. By setting the *keepNull* parameter to *false*, this behavior
/// can be changed so that all attributes in *data* with *null* values will
/// be removed from the target document.
///
/// The optional *waitForSync* parameter can be used to force
/// synchronisation of the document update operation to disk even in case
/// that the *waitForSync* flag had been disabled for the entire collection.
/// Thus, the *waitForSync* parameter can be used to force synchronisation
/// of just specific operations. To use this, set the *waitForSync* parameter
/// to *true*. If the *waitForSync* parameter is not specified or set to
/// *false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// The method returns a document with the attributes *_id*, *_rev* and
/// *_oldRev*.  The attribute *_id* contains the document handle of the
/// updated document, the attribute *_rev* contains the document revision of
/// the updated document, the attribute *_oldRev* contains the revision of
/// the old (now replaced) document.
///
/// If there is a conflict, i. e. if the revision of the *document* does not
/// match the revision in the collection, then an error is thrown.
///
/// `collection.update(document, data, true)`
///
/// As before, but in case of a conflict, the conflict is ignored and the old
/// document is overwritten.
///
/// collection.update(document-handle, data)`
///
/// As before. Instead of document a document-handle can be passed as
/// first argument.
///
/// *Examples*
///
/// Create and update a document:
///
/// @TINYEXAMPLE{shell_update-document,updating a document}
///
/// Use a document handle:
///
/// @TINYEXAMPLE{shell_update-document-handle,updating a document}
///
/// Use the keepNull parameter to remove attributes with null values:
///
/// @TINYEXAMPLE{shell_update-document-keep-null,updating a document}
///
/// Patching array values:
///
/// @TINYEXAMPLE{shell_update-document-array,updating a document}
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_UpdateVocbaseCol (v8::Arguments const& argv) {
  return UpdateVocbaseCol(true, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves a document, coordinator case in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> SaveVocbaseColCoordinator (TRI_vocbase_col_t* collection,
                                                        v8::Arguments const& argv) {
  v8::HandleScope scope;

  // First get the initial data:
  string const dbname(collection->_dbName);

  // TODO: someone might rename the collection while we're reading its name...
  string const collname(collection->_name);

  // Now get the arguments:
  if (argv.Length() < 1 || argv.Length() > 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "save(<data>, [<waitForSync>])");
  }

  TRI_json_t* json = TRI_ObjectToJson(argv[0]);
  const bool waitForSync = ExtractForceSync(argv, 2);
  if (!TRI_IsArrayJson(json)) {
    if (0 != json) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  triagens::rest::HttpResponse::HttpResponseCode responseCode;
  map<string, string> headers;
  map<string, string> resultHeaders;
  string resultBody;

  int error = triagens::arango::createDocumentOnCoordinator(
            dbname, collname, waitForSync, json, headers,
            responseCode, resultHeaders, resultBody);
  // Note that the json has been freed inside!

  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }
  // report what the DBserver told us: this could now be 201/202 or
  // 400/404
  json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, resultBody.c_str());
  if (responseCode >= triagens::rest::HttpResponse::BAD) {
    if (!TRI_IsArrayJson(json)) {
      if (0 != json) {
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }
      TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
    }
    int errorNum = 0;
    TRI_json_t* subjson = TRI_LookupArrayJson(json, "errorNum");
    if (0 != subjson && TRI_IsNumberJson(subjson)) {
      errorNum = static_cast<int>(subjson->_value._number);
    }
    string errorMessage;
    subjson = TRI_LookupArrayJson(json, "errorMessage");
    if (0 != subjson && TRI_IsStringJson(subjson)) {
      errorMessage = string(subjson->_value._string.data,
                            subjson->_value._string.length-1);
    }
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION_MESSAGE(scope, errorNum, errorMessage);
  }
  v8::Handle<v8::Value> ret = TRI_ObjectJson(json);
  if (0 != json) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }
  return scope.Close(ret);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief extract a key from a v8 object
////////////////////////////////////////////////////////////////////////////////
  
static string GetId (v8::Handle<v8::Value> const arg) {
  if (arg->IsObject() && ! arg->IsArray()) {
    v8::Local<v8::Object> obj = arg->ToObject();
  
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

    if (obj->Has(v8g->_IdKey)) {
      return TRI_ObjectToString(obj->Get(v8g->_IdKey));
    }
  }
  
  return TRI_ObjectToString(arg);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves an edge, coordinator case in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> SaveEdgeColCoordinator (TRI_vocbase_col_t* collection,
                                                     v8::Arguments const& argv) {
  v8::HandleScope scope;

  // First get the initial data:
  string const dbname(collection->_dbName);

  // TODO: someone might rename the collection while we're reading its name...
  string const collname(collection->_name);

  // Now get the arguments:
  if (argv.Length() < 3 || argv.Length() > 4) {
    TRI_V8_EXCEPTION_USAGE(scope, "save(<from>, <to>, <data>, [<waitForSync>])");
  }

  string _from = GetId(argv[0]);
  string _to   = GetId(argv[1]);

  TRI_json_t* json = TRI_ObjectToJson(argv[2]);

  const bool waitForSync = ExtractForceSync(argv, 3);
  if (! TRI_IsArrayJson(json)) {
    if (0 != json) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DOCUMENT_TYPE_INVALID);
  }

  triagens::rest::HttpResponse::HttpResponseCode responseCode;
  map<string, string> resultHeaders;
  string resultBody;

  int error = triagens::arango::createEdgeOnCoordinator(
            dbname, collname, waitForSync, json, _from.c_str(), _to.c_str(),
            responseCode, resultHeaders, resultBody);
  // Note that the json has been freed inside!

  if (error != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, error);
  }
  // report what the DBserver told us: this could now be 201/202 or
  // 400/404
  json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, resultBody.c_str());
  if (responseCode >= triagens::rest::HttpResponse::BAD) {
    if (!TRI_IsArrayJson(json)) {
      if (0 != json) {
        TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
      }
      TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
    }
    int errorNum = 0;
    TRI_json_t* subjson = TRI_LookupArrayJson(json, "errorNum");
    if (0 != subjson && TRI_IsNumberJson(subjson)) {
      errorNum = static_cast<int>(subjson->_value._number);
    }
    string errorMessage;
    subjson = TRI_LookupArrayJson(json, "errorMessage");
    if (0 != subjson && TRI_IsStringJson(subjson)) {
      errorMessage = string(subjson->_value._string.data,
                            subjson->_value._string.length-1);
    }
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION_MESSAGE(scope, errorNum, errorMessage);
  }
  v8::Handle<v8::Value> ret = TRI_ObjectJson(json);
  if (0 != json) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }
  return scope.Close(ret);
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief saves a new document
/// @startDocuBlock documents_collectionSave
/// `collection.save(data)`
///
/// Creates a new document in the *collection* from the given *data*. The
/// *data* must be a hash array. It must not contain attributes starting
/// with *_*.
///
/// The method returns a document with the attributes *_id* and *_rev*.
/// The attribute *_id* contains the document handle of the newly created
/// document, the attribute *_rev* contains the document revision.
///
/// `collection.save(data, waitForSync)`
///
/// Creates a new document in the *collection* from the given *data* as
/// above. The optional *waitForSync* parameter can be used to force
/// synchronisation of the document creation operation to disk even in case
/// that the *waitForSync* flag had been disabled for the entire collection.
/// Thus, the *waitForSync* parameter can be used to force synchronisation
/// of just specific operations. To use this, set the *waitForSync* parameter
/// to *true*. If the *waitForSync* parameter is not specified or set to
/// *false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// *Examples*
///
/// @verbinclude shell_create-document
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_SaveVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    if ((TRI_col_type_e) collection->_type == TRI_COL_TYPE_DOCUMENT) {
      return scope.Close(SaveVocbaseColCoordinator(collection, argv));
    }
    else {
      return scope.Close(SaveEdgeColCoordinator(collection, argv));
    }
  }
#endif

  CollectionNameResolver resolver(collection->_vocbase);
  SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, 1> trx(collection->_vocbase, resolver, collection->_cid);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Value> result;

  if ((TRI_col_type_e) collection->_type == TRI_COL_TYPE_DOCUMENT) {
    result = SaveVocbaseCol(&trx, collection, argv);
  }
  else if ((TRI_col_type_e) collection->_type == TRI_COL_TYPE_EDGE) {
    result = SaveEdgeCol(&trx, collection, argv);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets a parameter attribute of a collection
///
/// This function does evil things so it is hidden
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_SetAttributeVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, collection);

  if (argv.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "setAttribute(<key>, <value>)");
  }

  string key = TRI_ObjectToString(argv[0]);
  string value = TRI_ObjectToString(argv[1]);

  TRI_WRITE_LOCK_STATUS_VOCBASE_COL(collection);
  TRI_col_info_t info;
  int res = TRI_LoadCollectionInfo(collection->_path, &info, false);

  if (res == TRI_ERROR_NO_ERROR) {
    if (key == "type") {
      info._type = (TRI_col_type_e) atoi(value.c_str());
    }
    else if (key == "version") {
      info._version = atoi(value.c_str());
    }
    else {
      res = TRI_ERROR_BAD_PARAMETER;
    }

    if (res == TRI_ERROR_NO_ERROR) {
      res = TRI_SaveCollectionInfo(collection->_path, &info, collection->_vocbase->_settings.forceSyncProperties);
    }
  }

  TRI_FreeCollectionInfoOptions(&info);

  TRI_WRITE_UNLOCK_STATUS_VOCBASE_COL(collection);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "setAttribute failed");
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the status of a collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_StatusVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    std::string const databaseName = std::string(collection->_dbName);

    shared_ptr<CollectionInfo> const& ci 
        = ClusterInfo::instance()->getCollection(databaseName, 
                                        StringUtils::itoa(collection->_cid));

    if ((*ci).empty()) {
      return scope.Close(v8::Number::New((int) TRI_VOC_COL_STATUS_DELETED));
    }
    return scope.Close(v8::Number::New((int) ci->status()));
  } 
  // fallthru intentional 
#endif

  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);
  TRI_vocbase_col_status_e status = collection->_status;
  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);

  return scope.Close(v8::Number::New((int) status));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief truncates a collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_TruncateVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  const bool forceSync = ExtractForceSync(argv, 1);

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, collection);
  
  CollectionNameResolver resolver(collection->_vocbase);
  SingleCollectionWriteTransaction<EmbeddableTransaction<V8TransactionContext>, UINT64_MAX> trx(collection->_vocbase, resolver, collection->_cid);
  int res = trx.begin();
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }
  
  TRI_barrier_t* barrier = TRI_CreateBarrierElement(&(trx.primaryCollection()->_barrierList));

  if (barrier == 0) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  res = trx.truncate(forceSync);
  res = trx.finish(res);

  TRI_FreeBarrier(barrier);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief truncates a datafile
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_TruncateDatafileVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }
  
  TRI_SHARDING_COLLECTION_NOT_YET_IMPLEMENTED(scope, collection);

  if (argv.Length() != 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "truncateDatafile(<datafile>, <size>)");
  }

  string path = TRI_ObjectToString(argv[0]);
  size_t size = (size_t) TRI_ObjectToInt64(argv[1]);

  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);

  if (collection->_status != TRI_VOC_COL_STATUS_UNLOADED &&
      collection->_status != TRI_VOC_COL_STATUS_CORRUPTED) {
    TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_COLLECTION_NOT_UNLOADED);
  }

  int res = TRI_TruncateDatafile(path.c_str(), (TRI_voc_size_t) size);

  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot truncate datafile");
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the type of a collection
///
/// @FUN{@FA{collection}.type()}
///
/// Returns the type of a collection. Possible values are:
/// - 2: document collection
/// - 3: edge collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_TypeVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    std::string const databaseName = std::string(collection->_dbName);
    
    shared_ptr<CollectionInfo> const& ci 
        = ClusterInfo::instance()->getCollection(databaseName,
                                      StringUtils::itoa(collection->_cid));
   
    if ((*ci).empty()) {
      return scope.Close(v8::Number::New((int) collection->_type));
    }
    return scope.Close(v8::Number::New((int) ci->type()));
  } 
  // fallthru intentional 
#endif


  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);
  TRI_col_type_e type = (TRI_col_type_e) collection->_type;
  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);

  return scope.Close(v8::Number::New((int) type));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief unloads a collection
/// @startDocuBlock collection_unload
/// `collection.unload()`
///
/// Starts unloading a collection from memory. Note that unloading is deferred
/// until all query have finished.
///
/// *Examples*
///
/// @verbinclude shell_collection-unload
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_UnloadVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

  int res;
   
#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    string const databaseName(collection->_dbName);
    
    res = ClusterInfo::instance()->setCollectionStatusCoordinator(databaseName, StringUtils::itoa(collection->_cid), TRI_VOC_COL_STATUS_UNLOADED);
  }
  else {
    res = TRI_UnloadCollectionVocBase(collection->_vocbase, collection, false);
  }
#else 

  res = TRI_UnloadCollectionVocBase(collection->_vocbase, collection, false);

#endif

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the version of a collection
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_VersionVocbaseCol (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_col_t* collection = TRI_UnwrapClass<TRI_vocbase_col_t>(argv.Holder(), WRP_VOCBASE_COL_TYPE);

  if (collection == 0) {
    TRI_V8_EXCEPTION_INTERNAL(scope, "cannot extract collection");
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(v8::Number::New((int) TRI_COL_VERSION_15));
  } 
  // fallthru intentional 
#endif
  
  TRI_col_info_t info;

  TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);
  int res = TRI_LoadCollectionInfo(collection->_path, &info, false);
  TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);

  TRI_FreeCollectionInfoOptions(&info);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, "cannot fetch collection info");
  }

  return scope.Close(v8::Number::New((int) info._version));
}

// -----------------------------------------------------------------------------
// --SECTION--                                           TRI_VOCBASE_T FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief wraps a TRI_vocbase_t
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Object> WrapVocBase (TRI_vocbase_t const* database) {
  v8::HandleScope scope;

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  v8::Handle<v8::Object> result = WrapClass(v8g->VocbaseTempl,
                                            WRP_VOCBASE_TYPE,
                                            const_cast<TRI_vocbase_t*>(database));
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects a collection from the vocbase
/// @startDocuBlock collection_databaseCollectionName
/// `db.collection-name`
///
/// Returns the collection with the given *collection-name*. If no such
/// collection exists, create a collection named *collection-name* with the
/// default properties.
///
/// *Examples*
///
/// @verbinclude shell_read-collection-short-cut
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> MapGetVocBase (v8::Local<v8::String> const name,
                                            const v8::AccessorInfo& info) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // convert the JavaScript string to a string
  v8::String::Utf8Value s(name);
  char* key = *s;

  size_t keyLength = s.length();
  if (keyLength > 2 && key[keyLength - 2] == '(') {
    keyLength -= 2;
    key[keyLength] = '\0';
  }


  // empty or null
  if (key == 0 || *key == '\0') {
    return scope.Close(v8::Handle<v8::Value>());
  }
  
  if (strcmp(key, "hasOwnProperty") == 0 ||  // this prevents calling the property getter again (i.e. recursion!)
      strcmp(key, "toString") == 0 ||
      strcmp(key, "toJSON") == 0) {
    return scope.Close(v8::Handle<v8::Value>());
  }

  TRI_vocbase_col_t* collection = 0;

  // generate a name under which the cached property is stored
  string cacheKey(key, keyLength);
  cacheKey.push_back('*');

  v8::Local<v8::String> cacheName = v8::String::New(cacheKey.c_str(), (int) cacheKey.size());
  v8::Handle<v8::Object> holder = info.Holder()->ToObject();
  
  if (*key == '_') {
    // special treatment for all properties starting with _
    v8::Local<v8::String> const l = v8::String::New(key);

    if (holder->HasRealNamedProperty(l)) {
      // some internal function inside db
      return scope.Close(v8::Handle<v8::Value>());
    }

    // something in the prototype chain?
    v8::Local<v8::Value> v = holder->GetRealNamedPropertyInPrototypeChain(l);

    if (! v.IsEmpty()) {
      if (! v->IsExternal()) {
        // something but an external... this means we can directly return this
        return scope.Close(v8::Handle<v8::Value>());
      }
    }
  }
  
  if (holder->HasRealNamedProperty(cacheName)) {
    v8::Handle<v8::Object> value = holder->GetRealNamedProperty(cacheName)->ToObject();
  
    collection = TRI_UnwrapClass<TRI_vocbase_col_t>(value, WRP_VOCBASE_COL_TYPE);

    // check if the collection is from the same database
    if (collection != 0 && collection->_vocbase == vocbase) {
      TRI_READ_LOCK_STATUS_VOCBASE_COL(collection);
      TRI_vocbase_col_status_e status = collection->_status;
      TRI_voc_cid_t cid = collection->_cid;
      TRI_READ_UNLOCK_STATUS_VOCBASE_COL(collection);
 
      // check if the collection is still alive 
      if (status != TRI_VOC_COL_STATUS_DELETED && cid > 0 && collection->_isLocal) {
        TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  

        if (value->Has(v8g->_IdKey)) {
          TRI_voc_cid_t cachedCid = (TRI_voc_cid_t) TRI_ObjectToUInt64(value->Get(v8g->_IdKey), true);

          if (cachedCid == cid) {
            // cache hit
            return scope.Close(value);
          }

          // cid has changed (i.e. collection has been dropped and re-created)
        }  
      }
    }

    // cache miss
    holder->Delete(cacheName);
  }
  
#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    shared_ptr<CollectionInfo> const& ci 
        = ClusterInfo::instance()->getCollection(vocbase->_name, std::string(key));

    if ((*ci).empty()) {
      collection = 0;
    }
    else {
      collection = CoordinatorCollection(vocbase, *ci);

      if (collection != 0 && collection->_cid == 0) {
        FreeCoordinatorCollection(collection);
        return scope.Close(v8::Handle<v8::Value>());
      }
    }
  }
  else {
    collection = TRI_LookupCollectionByNameVocBase(vocbase, key);
  }
#else
  // look up the collection
  collection = TRI_LookupCollectionByNameVocBase(vocbase, key);
#endif

  if (collection == 0) {
    if (*key == '_') {
      return scope.Close(v8::Handle<v8::Value>());
    }
      
    return scope.Close(v8::Undefined());
  }

  v8::Handle<v8::Value> result = TRI_WrapCollection(collection);

  if (result.IsEmpty()) {
    return scope.Close(v8::Undefined());
  }

  holder->Set(cacheName, result, v8::DontEnum);

  return scope.Close(result);
}

// -----------------------------------------------------------------------------
// --SECTION--                                              javascript functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief retrieves a collection from a V8 argument
////////////////////////////////////////////////////////////////////////////////
  
static TRI_vocbase_col_t* GetCollectionFromArgument (TRI_vocbase_t* vocbase,
                                                     v8::Handle<v8::Value> const val) {
  // number
  if (val->IsNumber() || val->IsNumberObject()) {
    uint64_t cid = (uint64_t) TRI_ObjectToUInt64(val, true);

    return TRI_LookupCollectionByIdVocBase(vocbase, cid);
  }

  string const name = TRI_ObjectToString(val);
  return TRI_LookupCollectionByNameVocBase(vocbase, name.c_str());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a single collection or null
/// @startDocuBlock collection_databaseName
/// `db._collection(collection-name)`
///
/// Returns the collection with the given name or null if no such collection
/// exists.
///
/// `db._collection(collection-identifier)`
///
/// Returns the collection with the given identifier or null if no such
/// collection exists. Accessing collections by identifier is discouraged for
/// end users. End users should access collections using the collection name.
///
/// *Examples*
///
/// Get a collection by name:
///
/// @verbinclude shell_read-collection-name
///
/// Get a collection by id:
///
/// @verbinclude shell_read-collection-id
///
/// Unknown collection:
///
/// @verbinclude shell_read-collection-unknown
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CollectionVocbase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // expecting one argument
  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "_collection(<name>|<identifier>)");
  }

  v8::Handle<v8::Value> val = argv[0];
  TRI_vocbase_col_t const* collection = 0;

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    string const name = TRI_ObjectToString(val);
    shared_ptr<CollectionInfo> const& ci 
        = ClusterInfo::instance()->getCollection(vocbase->_name, name);

    if ((*ci).id() == 0 || (*ci).empty()) {
      // not found
      return scope.Close(v8::Null());
    }

    collection = CoordinatorCollection(vocbase, *ci);
  }
  else {
    collection = GetCollectionFromArgument(vocbase, val);
  }
#else

  collection = GetCollectionFromArgument(vocbase, val);

#endif  

  if (collection == 0) {
    return scope.Close(v8::Null());
  }

  v8::Handle<v8::Value> result = TRI_WrapCollection(collection);

  if (result.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns all collections
/// @startDocuBlock collections_databaseName
/// `db._collections()`
///
/// Returns all collections of the given database.
///
/// *Examples*
///
/// @verbinclude shell_read-collection-all
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CollectionsVocbase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

#ifdef TRI_ENABLE_CLUSTER

  TRI_vector_pointer_t colls;

  // if we are a coordinator, we need to fetch the collection info from the agency
  if (ServerState::instance()->isCoordinator()) {
    colls = GetCollectionsCluster(vocbase);
  }
  else {
    colls = TRI_CollectionsVocBase(vocbase);
  }

#else
    
  TRI_vector_pointer_t colls = TRI_CollectionsVocBase(vocbase);

#endif

  bool error = false;
  // already create an array of the correct size
  v8::Handle<v8::Array> result = v8::Array::New();

  uint32_t n = (uint32_t) colls._length;
  for (uint32_t i = 0;  i < n;  ++i) {
    TRI_vocbase_col_t const* collection = (TRI_vocbase_col_t const*) colls._buffer[i];

    v8::Handle<v8::Value> c = TRI_WrapCollection(collection);

    if (c.IsEmpty()) {
      error = true;
      break;
    }

    result->Set(i, c);
  }

  TRI_DestroyVectorPointer(&colls);
      
  if (error) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns all collection names
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CompletionsVocbase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    return scope.Close(v8::Array::New());
  }

#ifdef TRI_ENABLE_CLUSTER
  TRI_vector_string_t names;
  if (ServerState::instance()->isCoordinator()) {
    if (ClusterInfo::instance()->doesDatabaseExist(vocbase->_name)) {
      names = GetCollectionNamesCluster(vocbase);
    }
    else {
      TRI_InitVectorString(&names, TRI_UNKNOWN_MEM_ZONE);
    }
  }
  else {
    names = TRI_CollectionNamesVocBase(vocbase);
  }

#else

  TRI_vector_string_t names = TRI_CollectionNamesVocBase(vocbase);

#endif

  size_t n = names._length;
  uint32_t j = 0;

  v8::Handle<v8::Array> result = v8::Array::New();
  // add collection names
  for (size_t i = 0;  i < n;  ++i) {
    char const* name = TRI_AtVectorString(&names, i);

    if (name != 0) {
      result->Set(j++, v8::String::New(name));
    }
  }

  TRI_DestroyVectorString(&names);

  // add function names. these are hard coded
  result->Set(j++, v8::String::New("_collection()"));
  result->Set(j++, v8::String::New("_collections()"));
  result->Set(j++, v8::String::New("_create()"));
  result->Set(j++, v8::String::New("_createDatabase()"));
  result->Set(j++, v8::String::New("_createDocumentCollection()"));
  result->Set(j++, v8::String::New("_createEdgeCollection()"));
  result->Set(j++, v8::String::New("_createStatement()"));
  result->Set(j++, v8::String::New("_document()"));
  result->Set(j++, v8::String::New("_drop()"));
  result->Set(j++, v8::String::New("_dropDatabase()"));
  result->Set(j++, v8::String::New("_executeTransaction()"));
  result->Set(j++, v8::String::New("_exists()"));
  result->Set(j++, v8::String::New("_id"));
  result->Set(j++, v8::String::New("_isSystem()"));
  result->Set(j++, v8::String::New("_listDatabases()"));
  result->Set(j++, v8::String::New("_name()"));
  result->Set(j++, v8::String::New("_path()"));
  result->Set(j++, v8::String::New("_query()"));
  result->Set(j++, v8::String::New("_remove()"));
  result->Set(j++, v8::String::New("_replace()"));
  result->Set(j++, v8::String::New("_update()"));
  result->Set(j++, v8::String::New("_useDatabase()"));
  result->Set(j++, v8::String::New("_version()"));

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new document or edge collection
/// @startDocuBlock collection_databaseCreate
/// `db._create(collection-name)`
///
/// Creates a new document collection named *collection-name*.
/// If the collection name already exists or if the name format is invalid, an
/// error is thrown. For more information on valid collection names please refer
/// to the [naming coventions](../NamingConvention/README.md).
///
/// `db._create(collection-name`, `properties)`
///
/// *properties* must be an object with the following attributes:
///
/// * *waitForSync* (optional, default *false*): If *true* creating
///   a document will only return after the data was synced to disk.
///
/// * *journalSize* (optional, default is a 
///   [configuration parameter](../CommandLindeOptions/Arangod.md): The maximal 
///   size of a journal or datafile.  Note that this also limits the maximal
///   size of a single object. Must be at least 1MB.
///
/// * *isSystem* (optional, default is *false*): If *true*, create a
///   system collection. In this case *collection-name* should start with
///   an underscore. End users should normally create non-system collections
///   only. API implementors may be required to create system collections in
///   very special occasions, but normally a regular collection will do.
///
/// * *isVolatile* (optional, default is *false*): If *true then the
///   collection data is kept in-memory only and not made persistent. Unloading
///   the collection will cause the collection data to be discarded. Stopping
///   or re-starting the server will also cause full loss of data in the
///   collection. Setting this option will make the resulting collection be
///   slightly faster than regular collections because ArangoDB does not
///   enforce any synchronisation to disk and does not calculate any CRC
///   checksums for datafiles (as there are no datafiles).
///
/// * *keyOptions* (optional): additional options for key generation. If
///   specified, then *keyOptions* should be a JSON array containing the
///   following attributes (note: some of them are optional):
///   * *type*: specifies the type of the key generator. The currently
///     available generators are *traditional* and *autoincrement*.
///   * *allowUserKeys*: if set to *true*, then it is allowed to supply
///     own key values in the *_key* attribute of a document. If set to
///     *false*, then the key generator will solely be responsible for
///     generating keys and supplying own key values in the *_key* attribute
///     of documents is considered an error.
///   * *{increment*: increment value for *autoincrement* key generator.
///     Not used for other key generator types.
///   * *offset*: initial offset value for *autoincrement* key generator.
///     Not used for other key generator types.
///
/// * *numberOfShards* (optional, default is *1*): in a cluster, this value
///   determines the number of shards to create for the collection. In a single
///   server setup, this option is meaningless.
///
/// * *shardKeys* (optional, default is *[ "_key" ]*): in a cluster, this
///   attribute determines which document attributes are used to determine the
///   target shard for documents. Documents are sent to shards based on the
///   values they have in their shard key attributes. The values of all shard
///   key attributes in a document are hashed, and the hash value is used to 
///   determine the target shard. Note that values of shard key attributes cannot
///   be changed once set.
///   This option is meaningless in a single server setup.
///
///   When choosing the shard keys, one must be aware of the following
///   rules and limitations: In a sharded collection with more than
///   one shard it is not possible to set up a unique constraint on
///   an attribute that is not the one and only shard key given in
///   *shardKeys*. This is because enforcing a unique constraint
///   would otherwise make a global index necessary or need extensive
///   communication for every single write operation. Furthermore, if
///   *_key* is not the one and only shard key, then it is not possible
///   to set the *_key* attribute when inserting a document, provided
///   the collection has more than one shard. Again, this is because
///   the database has to enforce the unique constraint on the *_key*
///   attribute and this can only be done efficiently if this is the
///   only shard key by delegating to the individual shards.
///
/// *Examples*
///
/// With defaults:
///
/// @verbinclude shell_create-collection
///
/// With properties:
///
/// @verbinclude shell_create-collection-properties
///
/// With a key generator:
///
/// @verbinclude shell_create-collection-keygen
///
/// With a special key option:
///
/// @verbinclude shell_create-collection-keyoptions
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CreateVocbase (v8::Arguments const& argv) {
  return CreateVocBase(argv, TRI_COL_TYPE_DOCUMENT);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new document collection
///
/// @FUN{db._createDocumentCollection(@FA{collection-name})}
///
/// @FUN{db._createDocumentCollection(@FA{collection-name}, @FA{properties})}
///
/// Creates a new document collection named @FA{collection-name}.
/// This is an alias for @ref JS_CreateVocbase, with the difference that the
/// collection type is not automatically detected.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CreateDocumentCollectionVocbase (v8::Arguments const& argv) {
  return CreateVocBase(argv, TRI_COL_TYPE_DOCUMENT);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new edge collection
///
/// @FUN{db._createEdgeCollection(@FA{collection-name})}
///
/// Creates a new edge collection named @FA{collection-name}. If the
/// collection name already exists, then an error is thrown. The default value
/// for @LIT{waitForSync} is @LIT{false}.
///
/// @FUN{db._createEdgeCollection(@FA{collection-name}, @FA{properties})}
///
/// @FA{properties} must be an object with the following attributes:
///
/// - @LIT{waitForSync} (optional, default @LIT{false}): If @LIT{true} creating
///   a document will only return after the data was synced to disk.
///
/// - @LIT{journalSize} (optional, default is a @ref CommandLineArangod
///   "configuration parameter"):  The maximal size of
///   a journal or datafile.  Note that this also limits the maximal
///   size of a single object. Must be at least 1MB.
///
/// @EXAMPLES
///
/// See @ref JS_CreateVocbase for examples.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CreateEdgeCollectionVocbase (v8::Arguments const& argv) {
  return CreateVocBase(argv, TRI_COL_TYPE_EDGE);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document
/// @startDocuBlock documents_collectionRemove
/// `db._remove(document)`
///
/// Removes a document. If there is revision mismatch, then an error is thrown.
///
/// `db._remove(document, true)`
///
/// Removes a document. If there is revision mismatch, then mismatch is ignored
/// and document is deleted. The function returns *true* if the document
/// existed and was deleted. It returns *false*, if the document was already
/// deleted.
///
/// `db._remove(document, true, waitForSync)` or
/// `db._remove(document, {overwrite: true or false, waitForSynca: true or false})`
///
/// The optional *waitForSync* parameter can be used to force synchronization
/// of the document deletion operation to disk even in case that the
/// *waitForSync* flag had been disabled for the entire collection.  Thus,
/// the *waitForSync* parameter can be used to force synchronisation of just
/// specific operations. To use this, set the *waitForSync* parameter to
/// *true*. If the *waitForSync* parameter is not specified or set to
/// *false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// `db._remove(document-handle, data)`
///
/// As before. Instead of document a *document-handle* can be passed as first
/// argument.
///
/// *Examples*
///
/// Remove a document:
///
/// @code
/// arango> a1 = db.example.save({ a : 1 });
/// { "_id" : "116308/4214943", "_rev" : "4214943" }
/// arango> db._remove(a1);
/// true
/// arango> db._remove(a1);
/// JavaScript exception in file '(arango)' at 1,4: [ArangoError 1202: document not found: cannot remove document]
/// !db._remove(a1);
/// !   ^
/// arango> db._remove(a1, true);
/// false
/// @endcode
///
/// Remove a document with a conflict:
///
/// @code
/// arango> a1 = db.example.save({ a : 1 });
/// { "_id" : "116308/4042634", "_rev" : "4042634" }
/// arango> a2 = db._replace(a1, { a : 2 });
/// { "_id" : "116308/4042634", "_rev" : "4108170", "_oldRev" : 4042634 }
/// arango> db._delete(a1);
/// JavaScript exception in file '(arango)' at 1,4: [ArangoError 1200: conflict: cannot delete document]
/// !db._delete(a1);
/// !   ^
/// arango> db._delete(a1, true);
/// true
/// arango> db._document(a1);
/// JavaScript exception in file '(arango)' at 1,4: [ArangoError 1202: document not found: document not found]
/// !db._document(a1);
/// !   ^
/// @endcode
///
/// Remove a document using new signature:
/// @code
/// arangod> db.example.save({ a:  1 } );
/// { 
///    "_id" : "example/11265325374", 
///   "_rev" : "11265325374", 
///   "_key" : "11265325374" 
/// }
/// arangod> db.example.remove("example/11265325374", {overwrite: true, waitForSync: false})
/// true
/// @endcode
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RemoveVocbase (v8::Arguments const& argv) {
  return RemoveVocbaseCol(false, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a document and returns it
/// @startDocuBlock documents_documentName
/// `db._document(document)`
///
/// This method finds a document given its identifier.  It returns the document
/// if the document exists. An error is throw if no document with the given
/// identifier exists, or if the specified *_rev* value does not match the 
/// current revision of the document.
///
/// Please note that if the method is executed on the arangod server (e.g. from 
/// inside a Foxx application), an immutable document object will be returned
/// for performance reasons. It is not possible to change attributes of this 
/// immutable object. To update or patch the returned document, it needs to be
/// cloned/copied into a regular JavaScript object first. This is not necessary
/// if the *_document* method is called from out of arangosh or from any 
/// other client.
///
/// `db._document(document-handle)`
///
/// As before. Instead of document a *document-handle* can be passed as
/// first argument.
///
/// *Examples*
///
/// Returns the document:
///
/// @verbinclude shell_read-document-db
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DocumentVocbase (v8::Arguments const& argv) {
  return DocumentVocbaseCol(false, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks whether a document exists
/// @startDocuBlock documents_documentExists
/// `db._exists(document)`
///
/// This method determines whether a document exists given its identifier.  
/// Instead of returning the found document or an error, this method will 
/// return either *true* or *false*. It can thus be used 
/// for easy existence checks.
///
/// No error will be thrown if the sought document or collection does not 
/// exist.
/// Still this method will throw an error if used improperly, e.g. when called
/// with a non-document handle.
///
/// `db._exists(document-handle)`
///
/// As before, but instead of a document a document-handle can be passed.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ExistsVocbase (v8::Arguments const& argv) {
  return ExistsVocbaseCol(false, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief replaces a document
/// @startDocuBlock documents_documentReplace
/// `db._replace(document, data)`
///
/// The method returns a document with the attributes *_id*, *_rev* and
/// *_oldRev*.  The attribute *_id* contains the document handle of the
/// updated document, the attribute *_rev* contains the document revision of
/// the updated document, the attribute *_oldRev* contains the revision of
/// the old (now replaced) document.
///
/// If there is a conflict, i. e. if the revision of the *document* does not
/// match the revision in the collection, then an error is thrown.
///
/// `db._replace(document, data, true)`
///
/// As before, but in case of a conflict, the conflict is ignored and the old
/// document is overwritten.
///
/// `db._replace(document, data, true, waitForSync)`
///
/// The optional *waitForSync* parameter can be used to force
/// synchronisation of the document replacement operation to disk even in case
/// that the *waitForSync* flag had been disabled for the entire collection.
/// Thus, the *waitForSync* parameter can be used to force synchronisation
/// of just specific operations. To use this, set the *waitForSync* parameter
/// to *true*. If the *waitForSync* parameter is not specified or set to
/// *false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// `db._replace(document-handle, data)`
///
/// As before. Instead of document a *document-handle* can be passed as
/// first argument.
///
/// *ExamplesÜ
///
/// Create and replace a document:
///
/// @TINYEXAMPLE{shell_replace-document-db,replacing a document}
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ReplaceVocbase (v8::Arguments const& argv) {
  return ReplaceVocbaseCol(false, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief update a document
/// @startDocuBlock documents_documentUpdate
/// `db._update(document, data, overwrite, keepNull, waitForSync)`
///
/// Updates an existing *document*. The *document* must be a document in
/// the current collection. This document is then patched with the
/// *data* given as second argument. The optional *overwrite* parameter can
/// be used to control the behavior in case of version conflicts (see below).
/// The optional *keepNull* parameter can be used to modify the behavior when
/// handling *null* values. Normally, *null* values are stored in the
/// database. By setting the *keepNull* parameter to *false*, this behavior
/// can be changed so that all attributes in *data* with *null* values will
/// be removed from the target document.
///
/// The optional *waitForSync* parameter can be used to force
/// synchronisation of the document update operation to disk even in case
/// that the *waitForSync* flag had been disabled for the entire collection.
/// Thus, the *waitForSync* parameter can be used to force synchronisation
/// of just specific operations. To use this, set the *waitForSync* parameter
/// to *true*. If the *waitForSync* parameter is not specified or set to
/// false*, then the collection's default *waitForSync* behavior is
/// applied. The *waitForSync* parameter cannot be used to disable
/// synchronisation for collections that have a default *waitForSync* value
/// of *true*.
///
/// The method returns a document with the attributes *_id*, *_rev* and
/// *_oldRev*. The attribute *_id* contains the document handle of the
/// updated document, the attribute *_rev* contains the document revision of
/// the updated document, the attribute *_oldRev* contains the revision of
/// the old (now replaced) document.
///
/// If there is a conflict, i. e. if the revision of the *document* does not
/// match the revision in the collection, then an error is thrown.
///
/// `db._update(document, data, true)`
///
/// As before, but in case of a conflict, the conflict is ignored and the old
/// document is overwritten.
///
/// `db._update(document-handle, data)`
///
/// As before. Instead of document a *document-handle* can be passed as
/// first argument.
///
/// *Examples*
///
/// Create and update a document:
///
/// @TINYEXAMPLE{shell_update-document-db,updating a document}
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_UpdateVocbase (v8::Arguments const& argv) {
  return UpdateVocbaseCol(false, argv);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the server version string
///
/// @FUN{@FA{db}._version()}
///
/// Returns the server version string.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_VersionServer (v8::Arguments const& argv) {
  v8::HandleScope scope;

  return scope.Close(v8::String::New(TRI_VERSION));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the path to database files
/// @startDocuBlock database_path
/// *db._path()*
///
/// Returns the filesystem path of the current database as a string. 
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_PathDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  return scope.Close(v8::String::New(vocbase->_path));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the database id
/// @startDocuBlock database_id
/// *db._id()*
///
/// Returns the id of the current database as a string. 
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_IdDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  return scope.Close(V8TickId(vocbase->_id));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the database name
/// @startDocuBlock database_name
/// *db._name()*
///
/// Returns the name of the current database as a string. 
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_NameDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  return scope.Close(v8::String::New(vocbase->_name));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the database type
/// @startDocuBlock database_isSystem
/// *db._isSystem()*
///
/// Returns whether the currently used database is the `_system` database.
/// The system database has some special privileges and properties, for example,
/// database management operations such as create or drop can only be executed
/// from within this database. Additionally, the `_system` database itself 
/// cannot be dropped.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_IsSystemDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  return scope.Close(v8::Boolean::New(TRI_IsSystemVocBase(vocbase)));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief change the current database
/// @startDocuBlock database_useDatabase
/// *db._useDatabase(name)*
///
/// Changes the current database to the database specified by *name*. Note
/// that the database specified by *name* must already exist.
///
/// Changing the database might be disallowed in some contexts, for example
/// server-side actions (including Foxx).
///
/// When performing this command from arangosh, the current credentials (username 
/// and password) will be re-used. These credentials might not be valid to
/// connect to the database specified by *name*. Additionally, the database
/// only be accessed from certain endpoints only. In this case, switching the
/// database might not work, and the connection / session should be closed and
/// restarted with different username and password credentials and/or 
/// endpoint data.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_UseDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._useDatabase(<name>)");
  }

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  

  if (! v8g->_allowUseDatabase) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_FORBIDDEN);
  }

  string const name = TRI_ObjectToString(argv[0]);
  
  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }

  if (TRI_EqualString(name.c_str(), vocbase->_name)) {
    // same database. nothing to do
    return scope.Close(WrapVocBase(vocbase));
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    vocbase = TRI_UseCoordinatorDatabaseServer((TRI_server_t*) v8g->_server, name.c_str());
  }
  else {
    // check if the other database exists, and increase its refcount
    vocbase = TRI_UseDatabaseServer((TRI_server_t*) v8g->_server, name.c_str());  
  }
#else
  vocbase = TRI_UseDatabaseServer((TRI_server_t*) v8g->_server, name.c_str());  
#endif  
  
  if (vocbase != 0) {
    // switch databases
    void* orig = v8g->_vocbase;
    assert(orig != 0);

    v8g->_vocbase = vocbase;

    if (orig != vocbase) {
      TRI_ReleaseDatabaseServer((TRI_server_t*) v8g->_server, (TRI_vocbase_t*) orig);
    }
    
    return scope.Close(WrapVocBase(vocbase));
  }

  TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the list of all existing databases in a coordinator
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> ListDatabasesCoordinator (v8::Arguments const& argv) {
  v8::HandleScope scope;

  // Arguments are already checked, there are 0 or 3.

  ClusterInfo* ci = ClusterInfo::instance();

  if (argv.Length() == 0) {
    vector<DatabaseID> list = ci->listDatabases(true);
    v8::Handle<v8::Array> result = v8::Array::New();
    for (size_t i = 0;  i < list.size();  ++i) {    
      result->Set((uint32_t) i, v8::String::New(list[i].c_str(), 
                                                (int) list[i].size()));
    }
    return scope.Close(result);
  }
  else {
    // We have to ask a DBServer, any will do:
    int tries = 0;
    vector<ServerID> DBServers;
    while (++tries <= 2) {
      DBServers = ci->getCurrentDBServers();

      if (! DBServers.empty()) {
        ServerID sid = DBServers[0];
        ClusterComm* cc = ClusterComm::instance();
        ClusterCommResult* res;
        map<string, string> headers;
        headers["Authentication"] = TRI_ObjectToString(argv[2]);
        res = cc->syncRequest("", 0, "server:" + sid, 
                              triagens::rest::HttpRequest::HTTP_REQUEST_GET,
                              "/_api/database/user", string(""), headers, 0.0);

        if (res->status == CL_COMM_SENT) {
          // We got an array back as JSON, let's parse it and build a v8
          StringBuffer& body = res->result->getBody();

          TRI_json_t* json = JsonHelper::fromString(body.c_str());
          delete res;

          if (json != 0 && JsonHelper::isArray(json)) {
            TRI_json_t const* dotresult = JsonHelper::getArrayElement(json, "result");

            if (dotresult != 0) {
              vector<string> list = JsonHelper::stringList(dotresult);
              TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
              v8::Handle<v8::Array> result = v8::Array::New();
              for (size_t i = 0;  i < list.size();  ++i) {    
                result->Set((uint32_t) i, v8::String::New(list[i].c_str(), 
                                                          (int) list[i].size()));
              }
              return scope.Close(result);
            }
            TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
          }
        }
        else {
          delete res;
        }
      }
      ci->loadCurrentDBServers();   // just in case some new have arrived
    }
    // Give up:
    return scope.Close(v8::Undefined());
  }
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief return the list of all existing databases
/// @startDocuBlock database_listDatabase
/// *db._listDatabases()*
///
/// Returns the list of all databases. This method can only be used from within
/// the *_system* database.
/// @endDocuBlock 
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ListDatabases (v8::Arguments const& argv) {
  v8::HandleScope scope;

  const uint32_t argc = argv.Length();
  if (argc != 0 && argc != 3) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._listDatabases()");
  }
  
  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  if (argc == 0 && 
      ! TRI_IsSystemVocBase(vocbase)) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_USE_SYSTEM_DATABASE);
  }

#ifdef TRI_ENABLE_CLUSTER
  // If we are a coordinator in a cluster, we have to behave differently:
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(ListDatabasesCoordinator(argv));
  }
#endif
  
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData(); 

  TRI_vector_string_t names;
  TRI_InitVectorString(&names, TRI_UNKNOWN_MEM_ZONE);

  int res;

  if (argc == 0) {
    // return all databases
    res = TRI_GetDatabaseNamesServer((TRI_server_t*) v8g->_server, &names);
  }
  else {
    // return all databases for a specific user
    string username = TRI_ObjectToString(argv[0]);
    string password = TRI_ObjectToString(argv[1]);
    res = TRI_GetUserDatabasesServer((TRI_server_t*) v8g->_server, username.c_str(), password.c_str(), &names);
  }
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVectorString(&names);
    TRI_V8_EXCEPTION(scope, res);
  }

  v8::Handle<v8::Array> result = v8::Array::New();
  for (size_t i = 0;  i < names._length;  ++i) {    
    result->Set((uint32_t) i, v8::String::New((char const*) TRI_AtVectorString(&names, i)));
  }

  TRI_DestroyVectorString(&names);
  
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new database, case of a coordinator in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER

////////////////////////////////////////////////////////////////////////////////
/// @brief helper function for the agency
///
/// `place` can be "/Target", "/Plan" or "/Current" and name is the database
/// name.
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> CreateDatabaseCoordinator (v8::Arguments const& argv) {
  v8::HandleScope scope;
  
  // First work with the arguments to create a JSON entry:
  string const name = TRI_ObjectToString(argv[0]);

  if (! TRI_IsAllowedNameVocBase(false, name.c_str())) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NAME_INVALID);
  }

  TRI_json_t* json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (0 == json) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  uint64_t const id = ClusterInfo::instance()->uniqid();

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id",
      TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE,
                               StringUtils::itoa(id).c_str()));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "name",
      TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE,
                               TRI_ObjectToString(argv[0]).c_str()));
  if (argv.Length() > 1) {
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "options",
                         TRI_ObjectToJson(argv[1]));
  }
  
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "coordinator",
      TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, ServerState::instance()->getId().c_str()));

  ClusterInfo* ci = ClusterInfo::instance();
  string errorMsg;

  int res = ci->createDatabaseCoordinator( name, json, errorMsg, 120.0);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, errorMsg);
  }

  // database was created successfully in agency

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  // now wait for heartbeat thread to create the database object
  TRI_vocbase_t* vocbase = 0;
  int tries = 0;

  while (++tries <= 6000) {
    vocbase = TRI_UseByIdCoordinatorDatabaseServer((TRI_server_t*) v8g->_server, id);

    if (vocbase != 0) {
      break;
    }

    // sleep
    usleep(10000);
  }

  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_INTERNAL);
  }

  // now run upgrade and copy users into context
  if (argv.Length() >= 3 && argv[2]->IsArray()) {
    v8::Handle<v8::Object> users = v8::Object::New();
    users->Set(v8::String::New("users"), argv[2]);

    v8::Context::GetCurrent()->Global()->Set(v8::String::New("UPGRADE_ARGS"), users);
  }
  else {
    v8::Context::GetCurrent()->Global()->Set(v8::String::New("UPGRADE_ARGS"), v8::Object::New());
  }

  if (TRI_V8RunVersionCheck(vocbase, (JSLoader*) v8g->_loader, v8::Context::GetCurrent())) {
    // version check ok
    TRI_V8InitialiseFoxx(vocbase, v8::Context::GetCurrent());
  }
  
  TRI_ReleaseVocBase(vocbase);

  return scope.Close(v8::True());
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new database
/// @startDocuBlock database_createDatabase
/// *db._createDatabase(name, options, users)*
///
/// Creates a new database with the name specified by *name*. 
/// There are restrictions for database names 
/// (see [DatabaseNames](../NamingConventions/DatabaseNames.md)).
///
/// Note that even if the database is created successfully, there will be no
/// change into the current database to the new database. Changing the current
/// database must explicitly be requested by using the 
/// *db._useDatabase* method.
///
/// The *options* attribute currently has no meaning and is reserved for
/// future use.
///
/// The optional *users* attribute can be used to create initial users for
/// the new database. If specified, it must be a list of user objects. Each user 
/// object can contain the following attributes:
///
/// * *username*: the user name as a string. This attribute is mandatory.
///
/// * *passwd*: the user password as a string. If not specified, then it defaults
///   to the empty string.
///
/// * *active*: a boolean flag indicating whether the user account should be
///   actived or not. The default value is *true*.
///
/// * *extra*: an optional JSON object with extra user information. The data
///   contained in *extra* will be stored for the user but not be interpreted
///   further by ArangoDB.
///
/// If no initial users are specified, a default user *root* will be created 
/// with an empty string password. This ensures that the new database will be 
/// accessible via HTTP after it is created.
///
/// This method can only be used from within the *_system* database.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_CreateDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 1 || argv.Length() > 3) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._createDatabase(<name>, <options>, <users>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
 
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (! TRI_IsSystemVocBase(vocbase)) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_USE_SYSTEM_DATABASE);
  }

#ifdef TRI_ENABLE_CLUSTER
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(CreateDatabaseCoordinator(argv));
  }
#endif
  

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  
 
  // get database defaults from server
  TRI_vocbase_defaults_t defaults;
  TRI_GetDatabaseDefaultsServer((TRI_server_t*) v8g->_server, &defaults);
  
  v8::Local<v8::String> keyRemoveOnDrop = v8::String::New("removeOnDrop");
  v8::Local<v8::String> keyDefaultMaximalSize = v8::String::New("defaultMaximalSize");
  v8::Local<v8::String> keyDefaultWaitForSync = v8::String::New("defaultWaitForSync");
  v8::Local<v8::String> keyForceSyncProperties = v8::String::New("forceSyncProperties");
  v8::Local<v8::String> keyRequireAuthentication = v8::String::New("requireAuthentication");
  v8::Local<v8::String> keyRequireAuthenticationUnixSockets = v8::String::New("requireAuthenticationUnixSockets");
  v8::Local<v8::String> keyAuthenticateSystemOnly = v8::String::New("authenticateSystemOnly");
  
  // overwrite database defaults from argv[2]
  if (argv.Length() > 1 && argv[1]->IsObject()) {
    v8::Handle<v8::Object> options = argv[1]->ToObject();

    if (options->Has(keyRemoveOnDrop)) {
      defaults.removeOnDrop = options->Get(keyRemoveOnDrop)->BooleanValue();
    }

    if (options->Has(keyDefaultMaximalSize)) {
      defaults.defaultMaximalSize = (TRI_voc_size_t) options->Get(keyDefaultMaximalSize)->IntegerValue();
    }

    if (options->Has(keyDefaultWaitForSync)) {
      defaults.defaultWaitForSync = options->Get(keyDefaultWaitForSync)->BooleanValue();
    }

    if (options->Has(keyForceSyncProperties)) {
      defaults.forceSyncProperties = options->Get(keyForceSyncProperties)->BooleanValue();
    }

    if (options->Has(keyRequireAuthentication)) {
      defaults.requireAuthentication = options->Get(keyRequireAuthentication)->BooleanValue();
    }

    if (options->Has(keyRequireAuthenticationUnixSockets)) {
      defaults.requireAuthenticationUnixSockets = options->Get(keyRequireAuthenticationUnixSockets)->BooleanValue();
    }
    
    if (options->Has(keyAuthenticateSystemOnly)) {
      defaults.authenticateSystemOnly = options->Get(keyAuthenticateSystemOnly)->BooleanValue();
    }
  }
  
  string const name = TRI_ObjectToString(argv[0]);

  TRI_vocbase_t* database;
  int res = TRI_CreateDatabaseServer((TRI_server_t*) v8g->_server, name.c_str(), &defaults, &database);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }

  assert(database != 0);

  // copy users into context
  if (argv.Length() >= 3 && argv[2]->IsArray()) {
    v8::Handle<v8::Object> users = v8::Object::New();
    users->Set(v8::String::New("users"), argv[2]);

    v8::Context::GetCurrent()->Global()->Set(v8::String::New("UPGRADE_ARGS"), users);
  }
  else {
    v8::Context::GetCurrent()->Global()->Set(v8::String::New("UPGRADE_ARGS"), v8::Object::New());
  }

  if (TRI_V8RunVersionCheck(database, (JSLoader*) v8g->_loader, v8::Context::GetCurrent())) {
    // version check ok
    TRI_V8InitialiseFoxx(database, v8::Context::GetCurrent());
  }

  // populate the authentication cache. otherwise no one can access the new database
  TRI_ReloadAuthInfo(database);
 
  // finally decrease the reference-counter 
  TRI_ReleaseVocBase(database);

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief drop a database, case of a coordinator in a cluster
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_CLUSTER
static v8::Handle<v8::Value> DropDatabaseCoordinator (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  // Arguments are already checked, there is exactly one argument
  string const name = TRI_ObjectToString(argv[0]);
  TRI_vocbase_t* vocbase = TRI_UseCoordinatorDatabaseServer((TRI_server_t*) v8g->_server, name.c_str());

  if (vocbase == 0) {
    // no such database
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  TRI_voc_tick_t const id = vocbase->_id;
  TRI_ReleaseVocBase(vocbase);


  ClusterInfo* ci = ClusterInfo::instance();
  string errorMsg;

  int res = ci->dropDatabaseCoordinator(name, errorMsg, 120.0);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION_MESSAGE(scope, res, errorMsg);
  }
  
  // now wait for heartbeat thread to drop the database object
  int tries = 0;

  while (++tries <= 6000) {
    TRI_vocbase_t* vocbase = TRI_UseByIdCoordinatorDatabaseServer((TRI_server_t*) v8g->_server, id);

    if (vocbase == 0) {
      // object has vanished
      break;
    }

    // sleep
    usleep(10000);
  }

  return scope.Close(v8::True());
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief drop an existing database
/// @startDocuBlock database_dropDatabase
/// *db._dropDatabase(name)*
///
/// Drops the database specified by *name*. The database specified by 
/// *name* must exist. 
///
/// Note that dropping databases is only possible from within the *_system* 
/// database. The *_system* database itself cannot be dropped.
/// 
/// Databases are dropped asynchronously, and will be physically removed if 
/// all clients have disconnected and references have been garbage-collected.
/// @endDocuBlock
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_DropDatabase (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._dropDatabase(<name>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (! TRI_IsSystemVocBase(vocbase)) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_USE_SYSTEM_DATABASE);
  }
  
#ifdef TRI_ENABLE_CLUSTER
  // If we are a coordinator in a cluster, we have to behave differently:
  if (ServerState::instance()->isCoordinator()) {
    return scope.Close(DropDatabaseCoordinator(argv));
  }
#endif

  string const name = TRI_ObjectToString(argv[0]);
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  

  int res = TRI_DropDatabaseServer((TRI_server_t*) v8g->_server, name.c_str());

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_V8_EXCEPTION(scope, res);
  }
  
  TRI_V8ReloadRouting(v8::Context::GetCurrent());
    
  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief configure a new endpoint
///
/// @FUN{CONFIGURE_ENDPOINT}
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ConfigureEndpoint (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 1 || argv.Length() > 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._configureEndpoint(<endpoint>, <databases>)");
  }
  
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  
  TRI_server_t* server = (TRI_server_t*) v8g->_server;
  ApplicationEndpointServer* s = static_cast<ApplicationEndpointServer*>(server->_applicationEndpointServer);

  if (s == 0) {
    // not implemented in console mode
    TRI_V8_EXCEPTION(scope, TRI_ERROR_NOT_IMPLEMENTED);
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  if (! TRI_IsSystemVocBase(vocbase)) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_USE_SYSTEM_DATABASE);
  }

  const string endpoint = TRI_ObjectToString(argv[0]);

  // register dbNames
  vector<string> dbNames;

  if (argv.Length() > 1) {
    if (! argv[1]->IsArray()) {
      TRI_V8_EXCEPTION_PARAMETER(scope, "<databases> must be a list");
    }

    v8::Handle<v8::Array> list = v8::Handle<v8::Array>::Cast(argv[1]);

    const uint32_t n = list->Length();
    for (uint32_t i = 0; i < n; ++i) {
      v8::Handle<v8::Value> name = list->Get(i);

      if (name->IsString()) {
        const string dbName = TRI_ObjectToString(name);

        if (! TRI_IsAllowedNameVocBase(true, dbName.c_str())) {
          TRI_V8_EXCEPTION_PARAMETER(scope, "<databases> must be a list of database names");
        }

        dbNames.push_back(dbName);
      }
      else {
        TRI_V8_EXCEPTION_PARAMETER(scope, "<databases> must be a list of database names");
      }
    }
  }

  bool result = s->addEndpoint(endpoint, dbNames, true);
    
  if (! result) {
    TRI_V8_EXCEPTION_MESSAGE(scope, TRI_ERROR_BAD_PARAMETER, "unable to bind to endpoint");
  }

  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a new endpoint
///
/// @FUN{REMOVE_ENDPOINT}
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_RemoveEndpoint (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() < 1 || argv.Length() > 2) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._removeEndpoint(<endpoint>)");
  }
  
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  
  TRI_server_t* server = (TRI_server_t*) v8g->_server;
  ApplicationEndpointServer* s = static_cast<ApplicationEndpointServer*>(server->_applicationEndpointServer);

  if (s == 0) {
    // not implemented in console mode
    TRI_V8_EXCEPTION(scope, TRI_ERROR_NOT_IMPLEMENTED);
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  if (! TRI_IsSystemVocBase(vocbase)) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_USE_SYSTEM_DATABASE);
  }

  bool result = s->removeEndpoint(TRI_ObjectToString(argv[0]));

  if (! result) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_ENDPOINT_NOT_FOUND);
  }
  
  return scope.Close(v8::True());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a list of all endpoints
///
/// @FUN{LIST_ENDPOINTS}
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JS_ListEndpoints (v8::Arguments const& argv) {
  v8::HandleScope scope;

  if (argv.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE(scope, "db._listEndpoints()");
  }
  
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  
  TRI_server_t* server = (TRI_server_t*) v8g->_server;
  ApplicationEndpointServer* s = static_cast<ApplicationEndpointServer*>(server->_applicationEndpointServer);

  if (s == 0) {
    // not implemented in console mode
    TRI_V8_EXCEPTION(scope, TRI_ERROR_NOT_IMPLEMENTED);
  }

  TRI_vocbase_t* vocbase = GetContextVocBase();
  
  if (vocbase == 0) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }
  
  if (! TRI_IsSystemVocBase(vocbase)) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_USE_SYSTEM_DATABASE);
  }

  const map<string, vector<string> >& endpoints = s->getEndpoints();

  v8::Handle<v8::Array> result = v8::Array::New();
  uint32_t j = 0;

  map<string, vector<string> >::const_iterator it;
  for (it = endpoints.begin(); it != endpoints.end(); ++it) {
    v8::Handle<v8::Array> dbNames = v8::Array::New();

    for (uint32_t i = 0; i < (*it).second.size(); ++i) {
      dbNames->Set(i, v8::String::New((*it).second.at(i).c_str()));
    }

    v8::Handle<v8::Object> item = v8::Object::New();
    item->Set(v8::String::New("endpoint"), v8::String::New((*it).first.c_str()));
    item->Set(v8::String::New("databases"), dbNames);
    
    result->Set(j++, item);
  }
  
  return scope.Close(result);
}

// -----------------------------------------------------------------------------
// --SECTION--                                             SHAPED JSON FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief weak reference callback for a barrier
////////////////////////////////////////////////////////////////////////////////

static void WeakBarrierCallback (v8::Isolate* isolate,
                                 v8::Persistent<v8::Value> object,
                                 void* parameter) {
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) isolate->GetData();
  TRI_barrier_blocker_t* barrier = (TRI_barrier_blocker_t*) parameter;
  
  v8g->_hasDeadObjects = true;

  LOG_TRACE("weak-callback for barrier called");

  // find the persistent handle
  v8::Persistent<v8::Value> persistent = v8g->JSBarriers[barrier];
  v8g->JSBarriers.erase(barrier);

  // dispose and clear the persistent handle
  persistent.Dispose(isolate);
  persistent.Clear();

  // get the vocbase pointer from the barrier
  TRI_vocbase_t* vocbase = barrier->base._container->_collection->base._vocbase;
  
  // free the barrier
  TRI_FreeBarrier(&barrier->base);
    
  if (vocbase != 0) {
    // decrease the reference-counter for the database
    TRI_ReleaseVocBase(vocbase);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects a named attribute from the shaped json
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> MapGetNamedShapedJson (v8::Local<v8::String> name,
                                                    const v8::AccessorInfo& info) {
  v8::HandleScope scope;

  // sanity check
  v8::Handle<v8::Object> self = info.Holder();

  if (self->InternalFieldCount() <= SLOT_BARRIER) {
    // we better not throw here... otherwise this will cause a segfault
    return scope.Close(v8::Handle<v8::Value>());
  }

  // get shaped json
  void* marker = TRI_UnwrapClass<void*>(self, WRP_SHAPED_JSON_TYPE);

  if (marker == 0) {
    return scope.Close(v8::Handle<v8::Value>());
  }

  // convert the JavaScript string to a string
  // we take the fast path here and don't normalize the string
  v8::String::Utf8Value const str(name);
  string const key(*str, (size_t) str.length());

  if (key.empty() || key[0] == '_' || strchr(key.c_str(), '.') != 0) {
    return scope.Close(v8::Handle<v8::Value>());
  }

  // get the underlying collection
  TRI_barrier_t* barrier = static_cast<TRI_barrier_t*>(v8::Handle<v8::External>::Cast(self->GetInternalField(SLOT_BARRIER))->Value());
  TRI_primary_collection_t* collection = barrier->_container->_collection;

  // get shape accessor
  TRI_shaper_t* shaper = collection->_shaper;
  TRI_shape_pid_t pid = shaper->lookupAttributePathByName(shaper, key.c_str());

  if (pid == 0) {
    return scope.Close(v8::Handle<v8::Value>());
  }

  TRI_shaped_json_t document;
  TRI_EXTRACT_SHAPED_JSON_MARKER(document, marker);

  TRI_shaped_json_t json;
  TRI_shape_t const* shape;

  bool ok = TRI_ExtractShapedJsonVocShaper(shaper, &document, 0, pid, &json, &shape);

  if (ok && shape != 0) {
    return scope.Close(TRI_JsonShapeData(shaper, shape, json._data.data, json._data.length));
  }
    
  // we must not throw a v8 exception here because this will cause follow up errors
  return scope.Close(v8::Handle<v8::Value>());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects the keys from the shaped json
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Array> KeysOfShapedJson (const v8::AccessorInfo& info) {
  v8::HandleScope scope;
  TRI_v8_global_t* v8g;

  v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  // sanity check
  v8::Handle<v8::Object> self = info.Holder();

  if (self->InternalFieldCount() <= SLOT_BARRIER) {
    return scope.Close(v8::Array::New());
  }

  // get shaped json
  void* marker = TRI_UnwrapClass<void*>(self, WRP_SHAPED_JSON_TYPE);

  if (marker == 0) {
    return scope.Close(v8::Array::New());
  }

  TRI_barrier_t* barrier = static_cast<TRI_barrier_t*>(v8::Handle<v8::External>::Cast(self->GetInternalField(SLOT_BARRIER))->Value());
  TRI_primary_collection_t* collection = barrier->_container->_collection;

  // check for array shape
  TRI_shaper_t* shaper = collection->_shaper;

  TRI_shape_sid_t sid;
  TRI_EXTRACT_SHAPE_IDENTIFIER_MARKER(sid, marker);

  TRI_shape_t const* shape = shaper->lookupShapeId(shaper, sid);

  if (shape == 0 || shape->_type != TRI_SHAPE_ARRAY) {
    return scope.Close(v8::Array::New());
  }

  TRI_array_shape_t const* s;
  TRI_shape_aid_t const* aids;
  char const* qtr;

  // shape is an array
  s = (TRI_array_shape_t const*) shape;

  // number of entries
  TRI_shape_size_t const n = s->_fixedEntries + s->_variableEntries;

  // calculate position of attribute ids
  qtr = (char const*) shape;
  qtr += sizeof(TRI_array_shape_t);
  qtr += n * sizeof(TRI_shape_sid_t);
  aids = (TRI_shape_aid_t const*) qtr;

  v8::Handle<v8::Array> result = v8::Array::New((int) n);
  uint32_t count = 0;

  for (TRI_shape_size_t i = 0;  i < n;  ++i, ++aids) {
    char const* att = shaper->lookupAttributeId(shaper, *aids);

    if (att != 0) {
      result->Set(count++, v8::String::New(att));
    }
  }

  result->Set(count++, v8g->_IdKey);
  result->Set(count++, v8g->_RevKey);
  result->Set(count++, v8g->_KeyKey);

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief check if a property is present
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Integer> PropertyQueryShapedJson (v8::Local<v8::String> name, 
                                                        const v8::AccessorInfo& info) {
  v8::HandleScope scope;

  v8::Handle<v8::Object> self = info.Holder();

  // sanity check
  if (self->InternalFieldCount() <= SLOT_BARRIER) {
    return scope.Close(v8::Handle<v8::Integer>());
  }

  // get shaped json
  void* marker = TRI_UnwrapClass<TRI_shaped_json_t>(self, WRP_SHAPED_JSON_TYPE);

  if (marker == 0) {
    return scope.Close(v8::Handle<v8::Integer>());
  }

  // convert the JavaScript string to a string
  string const key = TRI_ObjectToString(name);

  if (key.empty()) {
    return scope.Close(v8::Handle<v8::Integer>());
  }

  if (key[0] == '_') {
    if (key == "_id" || key == TRI_VOC_ATTRIBUTE_REV || key == TRI_VOC_ATTRIBUTE_KEY) {
      return scope.Close(v8::Handle<v8::Integer>(v8::Integer::New(v8::ReadOnly)));
    }
  }

  // get underlying collection
  TRI_barrier_t* barrier = static_cast<TRI_barrier_t*>(v8::Handle<v8::External>::Cast(self->GetInternalField(SLOT_BARRIER))->Value());
  TRI_primary_collection_t* collection = barrier->_container->_collection;

  // get shape accessor
  TRI_shaper_t* shaper = collection->_shaper;
  TRI_shape_pid_t pid = shaper->lookupAttributePathByName(shaper, key.c_str());

  if (pid == 0) {
    return scope.Close(v8::Handle<v8::Integer>());
  }

  TRI_shape_sid_t sid;
  TRI_EXTRACT_SHAPE_IDENTIFIER_MARKER(sid, marker);

  if (sid == 0) {
    // invalid shape
#ifdef TRI_ENABLE_MAINTAINER_MODE
    LOG_WARNING("invalid shape id '%llu' found for key '%s'", (unsigned long long) sid, key.c_str());
#endif
    return scope.Close(v8::Handle<v8::Integer>());
  }

  TRI_shape_access_t const* acc = TRI_FindAccessorVocShaper(shaper, sid, pid);

  // key not found
  if (acc == 0 || acc->_shape == 0) {
    return scope.Close(v8::Handle<v8::Integer>());
  }

  return scope.Close(v8::Handle<v8::Integer>(v8::Integer::New(v8::ReadOnly)));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief selects an indexed attribute from the shaped json
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> MapGetIndexedShapedJson (uint32_t idx,
                                                      const v8::AccessorInfo& info) {
  v8::HandleScope scope;
  
  char buffer[11];
  size_t len;

  len = TRI_StringUInt32InPlace(idx, (char*) &buffer);

  v8::Local<v8::String> strVal = v8::String::New((char*) &buffer, (int) len);

  return scope.Close(MapGetNamedShapedJson(strVal, info));
}

// -----------------------------------------------------------------------------
// --SECTION--                                                            MODULE
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief parse vertex handle from a v8 value (string | object)
////////////////////////////////////////////////////////////////////////////////

int TRI_ParseVertex (CollectionNameResolver const& resolver,
                     TRI_voc_cid_t& cid,
                     TRI_voc_key_t& key,
                     v8::Handle<v8::Value> const val,
                     bool translateName) {

  v8::HandleScope scope;

  assert(key == 0);

  // reset everything
  string collectionName = "";
  TRI_voc_rid_t rid = 0;

  // try to extract the collection name, key, and revision from the object passed
  if (! ExtractDocumentHandle(val, collectionName, key, rid)) {
    return TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD;
  }

  // we have at least a key, we also might have a collection name
  assert(key != 0);

  if (collectionName == "") {
    // we do not know the collection
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    key = 0;

    return TRI_ERROR_ARANGO_DOCUMENT_HANDLE_BAD;
  }
   
#ifdef TRI_ENABLE_CLUSTER
  if (translateName && ServerState::instance()->isDBserver()) { 
    cid = resolver.getCollectionIdCluster(collectionName);
  }
  else {
    cid = resolver.getCollectionId(collectionName);
  }
#else
  cid = resolver.getCollectionId(collectionName);
#endif

  if (cid == 0) {
    TRI_FreeString(TRI_CORE_MEM_ZONE, key);
    key = 0;
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an index identifier
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_LookupIndexByHandle (TRI_vocbase_col_t const* collection,
                                      v8::Handle<v8::Value> val,
                                      bool ignoreNotFound,
                                      v8::Handle<v8::Object>* err) {
  // reset the collection identifier
  string collectionName = "";
  TRI_idx_iid_t iid = 0;

  // assume we are already loaded
  assert(collection != 0);
  assert(collection->_collection != 0);

  // extract the index identifier from a string
  if (val->IsString() || val->IsStringObject() || val->IsNumber()) {
    if (! IsIndexHandle(val, collectionName, iid)) {
      *err = TRI_CreateErrorObject(__FILE__, __LINE__, TRI_ERROR_ARANGO_INDEX_HANDLE_BAD);
      return 0;
    }
  }

  // extract the index identifier from an object
  else if (val->IsObject()) {
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

    v8::Handle<v8::Object> obj = val->ToObject();
    v8::Handle<v8::Value> iidVal = obj->Get(v8g->IdKey);

    if (! IsIndexHandle(iidVal, collectionName, iid)) {
      *err = TRI_CreateErrorObject(__FILE__, __LINE__, TRI_ERROR_ARANGO_INDEX_HANDLE_BAD);
      return 0;
    }
  }

  if (collectionName != "") {
    CollectionNameResolver resolver(collection->_vocbase);
    if (! EqualCollection(resolver, collectionName, collection)) {
      // I wish this error provided me with more information!
      // e.g. 'cannot access index outside the collection it was defined in'
      *err = TRI_CreateErrorObject(__FILE__, __LINE__, TRI_ERROR_ARANGO_CROSS_COLLECTION_REQUEST);
      return 0;
    }
  }

  TRI_index_t* idx = TRI_LookupIndex(collection->_collection, iid);

  if (idx == 0) {
    if (! ignoreNotFound) {
      *err = TRI_CreateErrorObject(__FILE__, __LINE__, TRI_ERROR_ARANGO_INDEX_NOT_FOUND);
    }
  }

  return idx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief wraps a TRI_vocbase_col_t
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Object> TRI_WrapCollection (TRI_vocbase_col_t const* collection) {
  v8::HandleScope scope;

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  v8::Handle<v8::Object> result = v8g->VocbaseColTempl->NewInstance();
  
  if (! result.IsEmpty()) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    TRI_vocbase_col_t* c = const_cast<TRI_vocbase_col_t*>(collection);

    result->SetInternalField(SLOT_CLASS_TYPE, v8::Integer::New(WRP_VOCBASE_COL_TYPE));
    result->SetInternalField(SLOT_CLASS, v8::External::New(c));
  
    map< void*, v8::Persistent<v8::Value> >::iterator i = v8g->JSCollections.find(c);

    if (i == v8g->JSCollections.end()) {
      // increase the reference-counter for the database
      TRI_UseVocBase(collection->_vocbase);

      v8::Persistent<v8::Value> persistent = v8::Persistent<v8::Value>::New(isolate, v8::External::New(c));
      result->SetInternalField(SLOT_COLLECTION, persistent);
    
      v8g->JSCollections[c] = persistent;
      persistent.MakeWeak(isolate, c, WeakCollectionCallback);
    }
    else {
      result->SetInternalField(SLOT_COLLECTION, i->second);
    }

    result->Set(v8g->_IdKey, V8CollectionId(collection->_cid), v8::ReadOnly);
    result->Set(v8g->_DbNameKey, v8::String::New(collection->_dbName));
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add basic attributes (_key, _rev, _from, _to) to a document object
////////////////////////////////////////////////////////////////////////////////

template<class T>
static v8::Handle<v8::Object> AddBasicDocumentAttributes (T& trx,
                                                          TRI_voc_cid_t cid,
                                                          TRI_doc_mptr_t const* document,
                                                          v8::Handle<v8::Object> result) {
  v8::HandleScope scope;

  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

  // store the document reference
  TRI_voc_rid_t rid = document->_rid;
  char const* docKey = TRI_EXTRACT_MARKER_KEY(document);

  result->Set(v8g->_IdKey, V8DocumentId(trx.resolver().getCollectionName(cid), docKey), v8::ReadOnly);
  result->Set(v8g->_RevKey, V8RevisionId(rid), v8::ReadOnly);
  result->Set(v8g->_KeyKey, v8::String::New(docKey), v8::ReadOnly);

  TRI_df_marker_type_t type = ((TRI_df_marker_t*) document->_data)->_type;

  if (type == TRI_DOC_MARKER_KEY_EDGE) {
    TRI_doc_edge_key_marker_t* marker = (TRI_doc_edge_key_marker_t*) document->_data;

#ifndef TRI_ENABLE_CLUSTER
    result->Set(v8g->_FromKey, V8DocumentId(trx.resolver().getCollectionName(marker->_fromCid), ((char*) marker) + marker->_offsetFromKey));
    result->Set(v8g->_ToKey, V8DocumentId(trx.resolver().getCollectionName(marker->_toCid), ((char*) marker) + marker->_offsetToKey));
#else
    result->Set(v8g->_FromKey, V8DocumentId(trx.resolver().getCollectionNameCluster(marker->_fromCid), ((char*) marker) + marker->_offsetFromKey));
    result->Set(v8g->_ToKey, V8DocumentId(trx.resolver().getCollectionNameCluster(marker->_toCid), ((char*) marker) + marker->_offsetToKey));
#endif
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief wraps a TRI_shaped_json_t
/// note: the function updates the usedBarrier variable if the barrier was used
////////////////////////////////////////////////////////////////////////////////

template<class T>
v8::Handle<v8::Value> TRI_WrapShapedJson (T& trx,
                                          TRI_voc_cid_t cid,
                                          TRI_doc_mptr_t const* document,
                                          TRI_barrier_t* barrier, 
                                          bool& usedBarrier) {
  v8::HandleScope scope;
    
  TRI_ASSERT_MAINTAINER(document != nullptr);
  TRI_ASSERT_MAINTAINER(document->_data != nullptr);
  TRI_ASSERT_MAINTAINER(barrier != nullptr);

  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) isolate->GetData();

  assert(barrier != 0);
  
  bool doCopy = trx.mustCopyShapedJson();
  
  if (doCopy) {
    // we'll create a full copy of the document

    TRI_primary_collection_t* collection = barrier->_container->_collection;
    TRI_shaper_t* shaper = collection->_shaper;
  
    TRI_shaped_json_t json;
    TRI_EXTRACT_SHAPED_JSON_MARKER(json, document->_data);
  
    TRI_shape_t const* shape = shaper->lookupShapeId(shaper, json._sid);

    if (shape == 0) {
      return scope.Close(v8::Object::New());
    }

    v8::Handle<v8::Object> result = v8::Object::New();
    result = AddBasicDocumentAttributes<T>(trx, cid, document, result);
    
    v8::Handle<v8::Value> shaped = TRI_JsonShapeData(shaper, shape, json._data.data, json._data.length);

    if (! shaped.IsEmpty()) {
      // now copy the shaped json attributes into the result
      // this is done to ensure proper order (_key, _id, _rev etc. come first)
      v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(shaped);
      v8::Handle<v8::Array> names = array->GetOwnPropertyNames();
      const uint32_t n = names->Length();
      for (uint32_t j = 0; j < n; ++j) {
        v8::Handle<v8::Value> key = names->Get(j);
        result->Set(key, array->Get(key));
      }
    }

    return scope.Close(result);
  }

  // we'll create a document stub, with a pointer into the datafile
  
  // create the new handle to return, and set its template type
  v8::Handle<v8::Object> result = v8g->ShapedJsonTempl->NewInstance();

  if (result.IsEmpty()) {
    // error
    // TODO check for empty results
    return scope.Close(result);
  }

  void* data = const_cast<void*>(document->_data);

  // point the 0 index Field to the c++ pointer for unwrapping later
  result->SetInternalField(SLOT_CLASS_TYPE, v8::Integer::New(WRP_SHAPED_JSON_TYPE));
  result->SetInternalField(SLOT_CLASS, v8::External::New(data));
  
  map<void*, v8::Persistent<v8::Value> >::iterator i = v8g->JSBarriers.find(barrier);

  if (i == v8g->JSBarriers.end()) {
    // increase the reference-counter for the database
    TRI_UseVocBase(barrier->_container->_collection->base._vocbase);

    v8::Persistent<v8::Value> persistent = v8::Persistent<v8::Value>::New(isolate, v8::External::New(barrier));
    result->SetInternalField(SLOT_BARRIER, persistent);

    v8g->JSBarriers[barrier] = persistent;
    persistent.MakeWeak(isolate, barrier, WeakBarrierCallback);
  }
  else {
    result->SetInternalField(SLOT_BARRIER, i->second);
  }

  usedBarrier |= true;

  return scope.Close(AddBasicDocumentAttributes<T>(trx, cid, document, result));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the private WRP_VOCBASE_COL_TYPE value
////////////////////////////////////////////////////////////////////////////////

int32_t TRI_GetVocBaseColType () {
  return WRP_VOCBASE_COL_TYPE;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief run version check
////////////////////////////////////////////////////////////////////////////////

bool TRI_V8RunVersionCheck (void* vocbase, 
                            JSLoader* startupLoader,
                            v8::Handle<v8::Context> context) {
  assert(startupLoader != 0);
  
  v8::HandleScope scope;
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  void* orig = v8g->_vocbase;
  v8g->_vocbase = vocbase;      
      
  v8::Handle<v8::Value> result = startupLoader->executeGlobalScript(context, "server/version-check.js");
  bool ok = TRI_ObjectToBoolean(result);

  if (! ok) {
    ((TRI_vocbase_t*) vocbase)->_state = (sig_atomic_t) TRI_VOCBASE_STATE_FAILED_VERSION;
  }
 
  v8g->_vocbase = orig;
  
  return ok;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief run upgrade check
////////////////////////////////////////////////////////////////////////////////

int TRI_V8RunUpgradeCheck (void* vocbase, 
                           JSLoader* startupLoader,
                           v8::Handle<v8::Context> context) {
  assert(startupLoader != 0);
  
  v8::HandleScope scope;
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  void* orig = v8g->_vocbase;
  v8g->_vocbase = vocbase;      
      
  v8::Handle<v8::Value> result = startupLoader->executeGlobalScript(context, "server/upgrade-check.js");
  int code = (int) TRI_ObjectToInt64(result);

  v8g->_vocbase = orig;
  
  return code;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialize foxx
////////////////////////////////////////////////////////////////////////////////

void TRI_V8InitialiseFoxx (void* vocbase, 
                           v8::Handle<v8::Context> context) {
  void* orig = 0;

  {
    v8::HandleScope scope;      
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  
    orig = v8g->_vocbase;
    v8g->_vocbase = vocbase;      
  }
    
  v8::HandleScope scope;      
  TRI_ExecuteJavaScriptString(context,
                              v8::String::New("require(\"internal\").initializeFoxx()"),
                              v8::String::New("initialize foxx"),
                              false);
  {
    v8::HandleScope scope;
    TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();  
    v8g->_vocbase = orig;
  }  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reloads routing
////////////////////////////////////////////////////////////////////////////////

void TRI_V8ReloadRouting (v8::Handle<v8::Context> context) {
  v8::HandleScope scope;      

  TRI_ExecuteJavaScriptString(context,
                              v8::String::New("require('internal').executeGlobalContextFunction('reloadRouting')"),
                              v8::String::New("reload routing"),
                              false);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a TRI_vocbase_t global context
////////////////////////////////////////////////////////////////////////////////

void TRI_InitV8VocBridge (v8::Handle<v8::Context> context,
                          TRI_server_t* server,
                          TRI_vocbase_t* vocbase,
                          JSLoader* loader, 
                          const size_t threadNumber) {
  v8::HandleScope scope;

  // check the isolate
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  TRI_v8_global_t* v8g = TRI_CreateV8Globals(isolate);

  // register the server
  v8g->_server = server;

  // register the database
  v8g->_vocbase = vocbase;
  
  // register the startup loader
  v8g->_loader = loader;


  v8::Handle<v8::ObjectTemplate> rt;
  v8::Handle<v8::FunctionTemplate> ft;
  v8::Handle<v8::Template> pt;

  // .............................................................................
  // generate the TRI_vocbase_t template
  // .............................................................................

  ft = v8::FunctionTemplate::New();
  ft->SetClassName(TRI_V8_SYMBOL("ArangoDatabase"));

  rt = ft->InstanceTemplate();
  rt->SetInternalFieldCount(2);
  rt->SetNamedPropertyHandler(MapGetVocBase);

  // for any database function added here, be sure to add it to in function
  // JS_CompletionsVocbase, too for the auto-completion
  TRI_AddMethodVocbase(rt, "_collection", JS_CollectionVocbase);
  TRI_AddMethodVocbase(rt, "_collections", JS_CollectionsVocbase);
  TRI_AddMethodVocbase(rt, "_COMPLETIONS", JS_CompletionsVocbase, true);
  TRI_AddMethodVocbase(rt, "_create", JS_CreateVocbase, true);
  TRI_AddMethodVocbase(rt, "_createDocumentCollection", JS_CreateDocumentCollectionVocbase);
  TRI_AddMethodVocbase(rt, "_createEdgeCollection", JS_CreateEdgeCollectionVocbase);
  TRI_AddMethodVocbase(rt, "_document", JS_DocumentVocbase);
  TRI_AddMethodVocbase(rt, "_exists", JS_ExistsVocbase);
  TRI_AddMethodVocbase(rt, "_remove", JS_RemoveVocbase);
  TRI_AddMethodVocbase(rt, "_replace", JS_ReplaceVocbase);
  TRI_AddMethodVocbase(rt, "_update", JS_UpdateVocbase);

  TRI_AddMethodVocbase(rt, "_version", JS_VersionServer);

  TRI_AddMethodVocbase(rt, "_id", JS_IdDatabase);
  TRI_AddMethodVocbase(rt, "_isSystem", JS_IsSystemDatabase);
  TRI_AddMethodVocbase(rt, "_name", JS_NameDatabase);
  TRI_AddMethodVocbase(rt, "_path", JS_PathDatabase);
  TRI_AddMethodVocbase(rt, "_createDatabase", JS_CreateDatabase);
  TRI_AddMethodVocbase(rt, "_dropDatabase", JS_DropDatabase);
  TRI_AddMethodVocbase(rt, "_listDatabases", JS_ListDatabases);
  TRI_AddMethodVocbase(rt, "_useDatabase", JS_UseDatabase);
  
  v8g->VocbaseTempl = v8::Persistent<v8::ObjectTemplate>::New(isolate, rt);
  TRI_AddGlobalFunctionVocbase(context, "ArangoDatabase", ft->GetFunction());

  // .............................................................................
  // generate the TRI_shaped_json_t template
  // .............................................................................

  ft = v8::FunctionTemplate::New();
  ft->SetClassName(TRI_V8_SYMBOL("ShapedJson"));

  rt = ft->InstanceTemplate();
  rt->SetInternalFieldCount(3);

  // accessor for named properties (e.g. doc.abcdef)
  rt->SetNamedPropertyHandler(MapGetNamedShapedJson,    // NamedPropertyGetter,
                              0,                        // NamedPropertySetter setter = 0
                              PropertyQueryShapedJson,  // NamedPropertyQuery,
                              0,                        // NamedPropertyDeleter deleter = 0,
                              KeysOfShapedJson          // NamedPropertyEnumerator,
                                                        // Handle<Value> data = Handle<Value>());
                              );
  
  // accessor for indexed properties (e.g. doc[1])
  rt->SetIndexedPropertyHandler(MapGetIndexedShapedJson,  // IndexedPropertyGetter,
                                0,                        // IndexedPropertySetter setter = 0
                                0,                        // IndexedPropertyQuery,
                                0,                        // IndexedPropertyDeleter deleter = 0,
                                0                         // IndexedPropertyEnumerator,
                                                          // Handle<Value> data = Handle<Value>());
                              );

  v8g->ShapedJsonTempl = v8::Persistent<v8::ObjectTemplate>::New(isolate, rt);
  TRI_AddGlobalFunctionVocbase(context, "ShapedJson", ft->GetFunction());

  // .............................................................................
  // generate the TRI_vocbase_col_t template
  // .............................................................................

  ft = v8::FunctionTemplate::New();
  ft->SetClassName(TRI_V8_SYMBOL("ArangoCollection"));

  rt = ft->InstanceTemplate();
  rt->SetInternalFieldCount(3);

  TRI_AddMethodVocbase(rt, "count", JS_CountVocbaseCol);
  TRI_AddMethodVocbase(rt, "datafiles", JS_DatafilesVocbaseCol);
  TRI_AddMethodVocbase(rt, "datafileScan", JS_DatafileScanVocbaseCol);
  TRI_AddMethodVocbase(rt, "document", JS_DocumentVocbaseCol);
  TRI_AddMethodVocbase(rt, "drop", JS_DropVocbaseCol);
  TRI_AddMethodVocbase(rt, "dropIndex", JS_DropIndexVocbaseCol);
  TRI_AddMethodVocbase(rt, "ensureIndex", JS_EnsureIndexVocbaseCol);
  TRI_AddMethodVocbase(rt, "lookupIndex", JS_LookupIndexVocbaseCol);
  TRI_AddMethodVocbase(rt, "exists", JS_ExistsVocbaseCol);
  TRI_AddMethodVocbase(rt, "figures", JS_FiguresVocbaseCol);
  TRI_AddMethodVocbase(rt, "getIndexes", JS_GetIndexesVocbaseCol);
  TRI_AddMethodVocbase(rt, "load", JS_LoadVocbaseCol);
  TRI_AddMethodVocbase(rt, "name", JS_NameVocbaseCol);
#ifdef TRI_ENABLE_CLUSTER  
  TRI_AddMethodVocbase(rt, "planId", JS_PlanIdVocbaseCol);
#endif
  TRI_AddMethodVocbase(rt, "properties", JS_PropertiesVocbaseCol);
  TRI_AddMethodVocbase(rt, "remove", JS_RemoveVocbaseCol);
  TRI_AddMethodVocbase(rt, "revision", JS_RevisionVocbaseCol);
  TRI_AddMethodVocbase(rt, "rename", JS_RenameVocbaseCol);
  TRI_AddMethodVocbase(rt, "rotate", JS_RotateVocbaseCol);
  TRI_AddMethodVocbase(rt, "setAttribute", JS_SetAttributeVocbaseCol, true);
  TRI_AddMethodVocbase(rt, "status", JS_StatusVocbaseCol);
  TRI_AddMethodVocbase(rt, "TRUNCATE", JS_TruncateVocbaseCol, true);
  TRI_AddMethodVocbase(rt, "truncateDatafile", JS_TruncateDatafileVocbaseCol);
  TRI_AddMethodVocbase(rt, "type", JS_TypeVocbaseCol);
  TRI_AddMethodVocbase(rt, "unload", JS_UnloadVocbaseCol);
  TRI_AddMethodVocbase(rt, "upgrade", JS_UpgradeVocbaseCol, true);
  TRI_AddMethodVocbase(rt, "version", JS_VersionVocbaseCol);

  TRI_AddMethodVocbase(rt, "replace", JS_ReplaceVocbaseCol);
  TRI_AddMethodVocbase(rt, "save", JS_SaveVocbaseCol);
  TRI_AddMethodVocbase(rt, "update", JS_UpdateVocbaseCol);

  v8g->VocbaseColTempl = v8::Persistent<v8::ObjectTemplate>::New(isolate, rt);
  TRI_AddGlobalFunctionVocbase(context, "ArangoCollection", ft->GetFunction());

  // .............................................................................
  // generate the general cursor template
  // .............................................................................

  ft = v8::FunctionTemplate::New();
  ft->SetClassName(TRI_V8_SYMBOL("ArangoCursor"));

  rt = ft->InstanceTemplate();
  rt->SetInternalFieldCount(2);

  TRI_AddMethodVocbase(rt, "count", JS_CountGeneralCursor);
  TRI_AddMethodVocbase(rt, "dispose", JS_DisposeGeneralCursor);
  TRI_AddMethodVocbase(rt, "getBatchSize", JS_GetBatchSizeGeneralCursor);
  TRI_AddMethodVocbase(rt, "getExtra", JS_GetExtraGeneralCursor);
  TRI_AddMethodVocbase(rt, "getRows", JS_GetRowsGeneralCursor, true); // DEPRECATED, use toArray
  TRI_AddMethodVocbase(rt, "hasCount", JS_HasCountGeneralCursor);
  TRI_AddMethodVocbase(rt, "hasNext", JS_HasNextGeneralCursor);
  TRI_AddMethodVocbase(rt, "id", JS_IdGeneralCursor);
  TRI_AddMethodVocbase(rt, "next", JS_NextGeneralCursor);
  TRI_AddMethodVocbase(rt, "persist", JS_PersistGeneralCursor);
  TRI_AddMethodVocbase(rt, "toArray", JS_ToArrayGeneralCursor);

  v8g->GeneralCursorTempl = v8::Persistent<v8::ObjectTemplate>::New(isolate, rt);
  TRI_AddGlobalFunctionVocbase(context, "ArangoCursor", ft->GetFunction());

  // .............................................................................
  // generate global functions
  // .............................................................................

  // AQL functions. not intended to be used by end users
  TRI_AddGlobalFunctionVocbase(context, "AHUACATL_RUN", JS_RunAhuacatl, true);
  TRI_AddGlobalFunctionVocbase(context, "AHUACATL_EXPLAIN", JS_ExplainAhuacatl, true);
  TRI_AddGlobalFunctionVocbase(context, "AHUACATL_PARSE", JS_ParseAhuacatl, true);

  // cursor functions. not intended to be used by end users
  TRI_AddGlobalFunctionVocbase(context, "CURSOR", JS_Cursor, true);
  TRI_AddGlobalFunctionVocbase(context, "CREATE_CURSOR", JS_CreateCursor, true);
  TRI_AddGlobalFunctionVocbase(context, "DELETE_CURSOR", JS_DeleteCursor, true);
  
  // replication functions. not intended to be used by end users
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_LOGGER_START", JS_StartLoggerReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_LOGGER_STOP", JS_StopLoggerReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_LOGGER_STATE", JS_StateLoggerReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_LOGGER_CONFIGURE", JS_ConfigureLoggerReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_SYNCHRONISE", JS_SynchroniseReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_SERVER_ID", JS_ServerIdReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_APPLIER_CONFIGURE", JS_ConfigureApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_APPLIER_START", JS_StartApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_APPLIER_STOP", JS_StopApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_APPLIER_STATE", JS_StateApplierReplication, true);
  TRI_AddGlobalFunctionVocbase(context, "REPLICATION_APPLIER_FORGET", JS_ForgetApplierReplication, true);

  TRI_AddGlobalFunctionVocbase(context, "COMPARE_STRING", JS_compare_string);
  TRI_AddGlobalFunctionVocbase(context, "NORMALIZE_STRING", JS_normalize_string);
  TRI_AddGlobalFunctionVocbase(context, "TIMEZONES", JS_getIcuTimezones);
  TRI_AddGlobalFunctionVocbase(context, "LOCALES", JS_getIcuLocales);
  TRI_AddGlobalFunctionVocbase(context, "FORMAT_DATETIME", JS_formatDatetime);
  TRI_AddGlobalFunctionVocbase(context, "PARSE_DATETIME", JS_parseDatetime);

  TRI_AddGlobalFunctionVocbase(context, "CONFIGURE_ENDPOINT", JS_ConfigureEndpoint, true);
  TRI_AddGlobalFunctionVocbase(context, "REMOVE_ENDPOINT", JS_RemoveEndpoint, true);
  TRI_AddGlobalFunctionVocbase(context, "LIST_ENDPOINTS", JS_ListEndpoints, true);
  TRI_AddGlobalFunctionVocbase(context, "RELOAD_AUTH", JS_ReloadAuth, true);
  TRI_AddGlobalFunctionVocbase(context, "TRANSACTION", JS_Transaction, true);

  // .............................................................................
  // create global variables
  // .............................................................................
  
  v8::Handle<v8::Value> v = WrapVocBase(vocbase);
  if (v.IsEmpty()) {
    // TODO: raise an error here
    LOG_ERROR("out of memory when initialising VocBase");
  }
  else {
    TRI_AddGlobalVariableVocbase(context, "db", v);
  }

  // current thread number
  context->Global()->Set(TRI_V8_SYMBOL("THREAD_NUMBER"), v8::Number::New((double) threadNumber), v8::ReadOnly);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
