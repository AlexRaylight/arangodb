////////////////////////////////////////////////////////////////////////////////
/// @brief V8-vocbase bridge
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2014 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "v8-voccursor.h"
#include "v8-vocbaseprivate.h"

#include "VocBase/general-cursor.h"

#include "Ahuacatl/ahuacatl-codegen.h"
#include "Ahuacatl/ahuacatl-collections.h"
#include "Ahuacatl/ahuacatl-result.h"
#include "Utils/AhuacatlTransaction.h"
#include "Aql/ExecutionBlock.h"


using namespace std;
using namespace triagens::basics;
using namespace triagens::arango;
using namespace triagens::rest;

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

static v8::Handle<v8::Value> WrapGeneralCursor (void* cursor);


////////////////////////////////////////////////////////////////////////////////
/// @brief wrapped class for general cursors
///
/// Layout:
/// - SLOT_CLASS_TYPE
/// - SLOT_CLASS
////////////////////////////////////////////////////////////////////////////////

static int32_t const WRP_GENERAL_CURSOR_TYPE = 3;



////////////////////////////////////////////////////////////////////////////////
/// @brief function that encapsulates execution of an AQL query
////////////////////////////////////////////////////////////////////////////////
v8::Handle<v8::Value> ExecuteQueryNativeAhuacatl (TRI_aql_context_t* context,
						  TRI_json_t const* parameters) {
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
  AhuacatlTransaction<V8TransactionContext<true>> trx(context->_vocbase, context);

  int res = trx.begin();

  if (res != TRI_ERROR_NO_ERROR) {
    // check if there is some error data registered in the transaction
    string const errorData = trx.getErrorData();

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

  // generate code
  size_t codeLength = 0;
  char* code = TRI_GenerateCodeAql(context, &codeLength);

  if (code == nullptr ||
      context->_error._code != TRI_ERROR_NO_ERROR) {
    v8::Handle<v8::Object> errorObject = CreateErrorObjectAhuacatl(&context->_error);

    return scope.Close(v8::ThrowException(errorObject));
  }

  TRI_ASSERT(codeLength > 0);
  // execute code
  v8::Handle<v8::Value> result = TRI_ExecuteJavaScriptString(v8::Context::GetCurrent(),
                                                             v8::String::New(code, (int) codeLength),
                                                             TRI_V8_SYMBOL("query"),
                                                             false);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, code);

  if (result.IsEmpty()) {
    // force a rollback
    trx.abort();
  }
  else {
    // commit / finish
    trx.finish(TRI_ERROR_NO_ERROR);
  }

  // return the result as a javascript array
  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief run a query and return the results as a cursor
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> ExecuteQueryCursorAhuacatl (TRI_vocbase_t* const vocbase,
                                                         TRI_aql_context_t* const context,
                                                         TRI_json_t const* parameters,
                                                         bool doCount,
                                                         uint32_t batchSize,
                                                         double cursorTtl) {
  v8::HandleScope scope;
  v8::TryCatch tryCatch;

  v8::Handle<v8::Value> result = ExecuteQueryNativeAhuacatl(context, parameters);

  if (tryCatch.HasCaught()) {
    if (tryCatch.CanContinue()) {
      return scope.Close(v8::ThrowException(tryCatch.Exception()));
    }
    else {
      TRI_v8_global_t* v8g = static_cast<TRI_v8_global_t*>(v8::Isolate::GetCurrent()->GetData());

      v8g->_canceled = true;
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

  if (json == nullptr) {
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  TRI_general_cursor_result_t* cursorResult = TRI_CreateResultAql(json);

  if (cursorResult == nullptr) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  // extra return values
  TRI_json_t* extra = nullptr;
  if (resultObject->Has(TRI_V8_SYMBOL("extra"))) {
    extra = TRI_ObjectToJson(resultObject->Get(TRI_V8_SYMBOL("extra")));
  }

  TRI_general_cursor_t* cursor = TRI_CreateGeneralCursor(vocbase, cursorResult, doCount, batchSize, cursorTtl, extra);

  if (cursor == nullptr) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, cursorResult);
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    if (extra != nullptr) {
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, extra);
    }
    TRI_V8_EXCEPTION_MEMORY(scope);
  }

  TRI_ASSERT(cursor != nullptr);

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

  TRI_ASSERT(cursor != 0);

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

////////////////////////////////////////////////////////////////////////////////
/// @brief generates a general cursor from a list
////////////////////////////////////////////////////////////////////////////////
static v8::Handle<v8::Value> JS_CreateCursor (v8::Arguments const& argv) {
  v8::HandleScope scope;

  TRI_vocbase_t* vocbase = GetContextVocBase();

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(scope, TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (argv.Length() < 1) {
    TRI_V8_EXCEPTION_USAGE(scope, "CREATE_CURSOR(<list>, <doCount>, <batchSize>, <ttl>)");
  }

  if (! argv[0]->IsArray()) {
    TRI_V8_TYPE_ERROR(scope, "<list> must be a list");
  }

  // extract objects
  v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(argv[0]);
  TRI_json_t* json = TRI_ObjectToJson(array);

  if (json == nullptr) {
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

  double ttl = 0.0;
  if (argv.Length() >= 4) {
    ttl = TRI_ObjectToDouble(argv[3]);
  }

  if (ttl <= 0.0) {
    ttl = 30.0; // default ttl
  }

  // create a cursor
  TRI_general_cursor_t* cursor = nullptr;
  TRI_general_cursor_result_t* cursorResult = TRI_CreateResultAql(json);

  if (cursorResult != nullptr) {
    cursor = TRI_CreateGeneralCursor(vocbase, cursorResult, doCount, batchSize, ttl, 0);

    if (cursor == nullptr) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, cursorResult);
      TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    }
  }
  else {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
  }

  if (cursor == nullptr) {
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
        TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

        v8g->_canceled = true;
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

  TRI_PersistGeneralCursor(UnwrapGeneralCursor(argv.Holder()));
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
        TRI_v8_global_t* v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();

        v8g->_canceled = true;
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

  if (vocbase == nullptr) {
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


  // .............................................................................
  // generate the general cursor template
  // .............................................................................
void TRI_InitV8cursor (v8::Handle<v8::Context> context,
			    TRI_server_t* server,
			    TRI_vocbase_t* vocbase,
			    JSLoader* loader,
			    const size_t threadNumber,
			    TRI_v8_global_t* v8g){
  v8::Handle<v8::ObjectTemplate> rt;
  v8::Handle<v8::FunctionTemplate> ft;
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
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

  // cursor functions. not intended to be used by end users
  TRI_AddGlobalFunctionVocbase(context, "CURSOR", JS_Cursor, true);
  TRI_AddGlobalFunctionVocbase(context, "CREATE_CURSOR", JS_CreateCursor, true);
  TRI_AddGlobalFunctionVocbase(context, "DELETE_CURSOR", JS_DeleteCursor, true);
}
