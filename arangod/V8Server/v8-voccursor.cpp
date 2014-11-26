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

#include "v8-vocbaseprivate.h"
#include "VocBase/general-cursor.h"
#include "v8-voccursor.h"
#include "Basics/conversions.h"
#include "V8/v8-conv.h"
#include "Utils/transactions.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::arango;
using namespace triagens::rest;

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief wrapped class for general cursors
///
/// Layout:
/// - SLOT_CLASS_TYPE
/// - SLOT_CLASS
////////////////////////////////////////////////////////////////////////////////

static int32_t const WRP_GENERAL_CURSOR_TYPE = 3;

// -----------------------------------------------------------------------------
// --SECTION--                                                   GENERAL CURSORS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief weak reference callback for general cursors
////////////////////////////////////////////////////////////////////////////////

static void WeakGeneralCursorCallback (const v8::WeakCallbackData<v8::External, v8::Persistent<v8::External>>& data) {
  fprintf(stderr, "WeakGeneralCursorCallback\n");
  auto isolate      = data.GetIsolate();
  auto persistent   = data.GetParameter();
  auto myConnection = v8::Local<v8::External>::New(isolate, *persistent);
  auto cursor       = static_cast<TRI_general_cursor_t*>(myConnection->Value());

  v8::HandleScope scope(isolate);

  TRI_GET_GLOBALS();
  v8g->_hasDeadObjects = true;

  TRI_ReleaseGeneralCursor(cursor);

  // decrease the reference-counter for the database
  TRI_ReleaseVocBase(cursor->_vocbase);
  fprintf(stderr, "Done - WeakGeneralCursorCallback\n");
}

////////////////////////////////////////////////////////////////////////////////
/// @brief stores a general cursor in a V8 object
////////////////////////////////////////////////////////////////////////////////

void TRI_WrapGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args,
                            TRI_general_cursor_t* cursor) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);
  v8::TryCatch tryCatch;

  TRI_ASSERT(cursor != nullptr);

  TRI_GET_GLOBALS();
  TRI_GET_GLOBAL(GeneralCursorTempl, v8::ObjectTemplate);
  v8::Handle<v8::Object> result = GeneralCursorTempl->NewInstance();

  if (! result.IsEmpty()) {
    v8::Persistent<v8::External> persistent;
    TRI_UseGeneralCursor(cursor);

    // increase the reference-counter for the database
    TRI_UseVocBase(cursor->_vocbase);

    auto externalCursor = v8::External::New(isolate, cursor);
    persistent.Reset(isolate, externalCursor);

    if (tryCatch.HasCaught()) {
      TRI_V8_RETURN_UNDEFINED();
    }

    result->SetInternalField(SLOT_CLASS_TYPE, v8::Integer::New(isolate, WRP_GENERAL_CURSOR_TYPE));
    result->SetInternalField(SLOT_CLASS, externalCursor);

    persistent.SetWeak(&persistent, WeakGeneralCursorCallback);
    fprintf(stderr, "TRI_WrapGeneralCursor - externalCursor %x cursor %x \n", externalCursor, cursor);
  }

  if (result.IsEmpty()) {
    TRI_V8_EXCEPTION_MEMORY();
  }

  TRI_V8_RETURN(result);

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

static void JS_CreateCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  if (args.Length() < 1) {
    TRI_V8_EXCEPTION_USAGE("CREATE_CURSOR(<list>, <doCount>, <batchSize>, <ttl>)");
  }

  if (! args[0]->IsArray()) {
    TRI_V8_TYPE_ERROR("<list> must be a list");
  }

  // extract objects
  v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(args[0]);
  TRI_json_t* json = TRI_ObjectToJson(isolate, array);

  if (json == nullptr) {
    TRI_V8_TYPE_ERROR("cannot convert <list> to JSON");
  }

  // return number of total records in cursor?
  bool doCount = false;

  if (args.Length() >= 2) {
    doCount = TRI_ObjectToBoolean(args[1]);
  }

  // maximum number of results to return at once
  uint32_t batchSize = 1000;

  if (args.Length() >= 3) {
    int64_t maxValue = TRI_ObjectToInt64(args[2]);

    if (maxValue > 0 && maxValue < (int64_t) UINT32_MAX) {
      batchSize = (uint32_t) maxValue;
    }
  }

  double ttl = 0.0;
  if (args.Length() >= 4) {
    ttl = TRI_ObjectToDouble(args[3]);
  }

  if (ttl <= 0.0) {
    ttl = 30.0; // default ttl
  }

  // create a cursor
  TRI_general_cursor_result_t* cursorResult = TRI_CreateResultGeneralCursor(json);

  if (cursorResult == nullptr) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    TRI_V8_EXCEPTION_MEMORY();
  }
  
  TRI_general_cursor_t* cursor = TRI_CreateGeneralCursor(vocbase, cursorResult, doCount, batchSize, ttl, nullptr);

  if (cursor == nullptr) {
    TRI_FreeCursorResult(cursorResult);
    TRI_V8_EXCEPTION_MEMORY();
  }

  TRI_WrapGeneralCursor(args, cursor);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a general cursor
////////////////////////////////////////////////////////////////////////////////

static void JS_DisposeGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("dispose()");
  }

  bool found = TRI_DropGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (found) {
    TRI_V8_RETURN_TRUE();
  }
  else {
    TRI_V8_RETURN_FALSE();
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the id of a general cursor
////////////////////////////////////////////////////////////////////////////////

static void JS_IdGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("id()");
  }

  TRI_voc_tick_t id = TRI_IdGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (id != 0) {
    TRI_V8_RETURN(V8TickId(isolate, id));
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of results
////////////////////////////////////////////////////////////////////////////////

static void JS_CountGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("count()");
  }

  size_t length = TRI_CountGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  TRI_V8_RETURN(v8::Number::New(isolate, (double) length));
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the next result from the general cursor
////////////////////////////////////////////////////////////////////////////////

static void JS_NextGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("next()");
  }

  v8::Handle<v8::Value> value;
  TRI_general_cursor_t* cursor;

  cursor = (TRI_general_cursor_t*) TRI_UseGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (cursor != 0) {
    bool result = false;

    TRI_LockGeneralCursor(cursor);

    if (cursor->_length == 0) {
      TRI_UnlockGeneralCursor(cursor);
      TRI_ReleaseGeneralCursor(cursor);

      TRI_V8_RETURN_UNDEFINED();
    }

    // exceptions must be caught in the following part because we hold an exclusive
    // lock that might otherwise not be freed
    v8::TryCatch tryCatch;

    try {
      TRI_general_cursor_row_t row = cursor->next(cursor);

      if (row == 0) {
        value = v8::Undefined(isolate);
      }
      else {
        value = TRI_ObjectJson(isolate, (TRI_json_t*) row);
        result = true;
      }
    }
    catch (...) {
    }

    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    if (result && ! tryCatch.HasCaught()) {
      TRI_V8_RETURN(value);
    }

    if (tryCatch.HasCaught()) {
      if (tryCatch.CanContinue()) {
        TRI_V8_LOG_THROW_EXCEPTION(tryCatch);
      }
      else {
        TRI_GET_GLOBALS();
        v8g->_canceled = true;
        TRI_V8_RETURN_UNDEFINED();
      }
    }
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief persist the general cursor for usage in subsequent requests
////////////////////////////////////////////////////////////////////////////////

static void JS_PersistGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("persist()");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  TRI_PersistGeneralCursor(vocbase, UnwrapGeneralCursor(args.Holder()));
  TRI_V8_RETURN_TRUE();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return all following rows from the cursor in one go
///
/// This function constructs multiple rows at once and should be preferred over
/// hasNext()...next() when iterating over bigger result sets
////////////////////////////////////////////////////////////////////////////////

static void JS_ToArrayGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("toArray()");
  }

  v8::Handle<v8::Array> rows = v8::Array::New(isolate);
  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (cursor != nullptr) {
    bool result = false;

    TRI_LockGeneralCursor(cursor);

    // exceptions must be caught in the following part because we hold an exclusive
    // lock that might otherwise not be freed
    v8::TryCatch tryCatch;

    try {
      uint32_t max = (uint32_t) cursor->getBatchSize(cursor);

      for (uint32_t i = 0; i < max; ++i) {
        TRI_general_cursor_row_t row = cursor->next(cursor);
        if (row == nullptr) {
          break;
        }
        rows->Set(v8::Number::New(isolate, i), TRI_ObjectJson(isolate, (TRI_json_t*) row));
      }

      result = true;
    }
    catch (...) {
    }

    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    if (result && ! tryCatch.HasCaught()) {
      TRI_V8_RETURN(rows);
    }

    if (tryCatch.HasCaught()) {
      if (tryCatch.CanContinue()) {
        TRI_V8_LOG_THROW_EXCEPTION(tryCatch);
      }
      else {
        TRI_GET_GLOBALS();
        v8g->_canceled = true;
        TRI_V8_RETURN_UNDEFINED();
      }
    }
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief alias for toArray()
/// @deprecated
////////////////////////////////////////////////////////////////////////////////

static void JS_GetRowsGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  return JS_ToArrayGeneralCursor(args);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return max number of results per transfer for cursor
////////////////////////////////////////////////////////////////////////////////

static void JS_GetBatchSizeGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("getBatchSize()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (cursor != nullptr) {
    TRI_LockGeneralCursor(cursor);
    uint32_t max = cursor->getBatchSize(cursor);
    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    TRI_V8_RETURN(v8::Number::New(isolate, (double) max));
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return extra data for cursor
////////////////////////////////////////////////////////////////////////////////

static void JS_GetExtraGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("getExtra()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (cursor != nullptr) {
    TRI_LockGeneralCursor(cursor);
    TRI_json_t* extra = cursor->getExtra(cursor);

    if (extra != nullptr && extra->_type == TRI_JSON_ARRAY) {
      TRI_UnlockGeneralCursor(cursor);
      TRI_ReleaseGeneralCursor(cursor);

      TRI_V8_RETURN(TRI_ObjectJson(isolate, extra));
    }

    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    TRI_V8_RETURN_UNDEFINED();
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return if count flag was set for cursor
////////////////////////////////////////////////////////////////////////////////

static void JS_HasCountGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("hasCount()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (cursor != nullptr) {
    TRI_LockGeneralCursor(cursor);
    bool hasCount = cursor->hasCount(cursor);
    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    if (hasCount) {
      TRI_V8_RETURN_TRUE();
    }
    else {
      TRI_V8_RETURN_FALSE();
    }
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if the cursor is exhausted
////////////////////////////////////////////////////////////////////////////////

static void JS_HasNextGeneralCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 0) {
    TRI_V8_EXCEPTION_USAGE("hasNext()");
  }

  TRI_general_cursor_t* cursor = TRI_UseGeneralCursor(UnwrapGeneralCursor(args.Holder()));

  if (cursor != nullptr) {
    TRI_LockGeneralCursor(cursor);
    bool hasNext = cursor->hasNext(cursor);
    TRI_UnlockGeneralCursor(cursor);
    TRI_ReleaseGeneralCursor(cursor);

    if (hasNext) {
      TRI_V8_RETURN_TRUE();
    }
    else {
      TRI_V8_RETURN_FALSE();
    }
  }

  TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a (persistent) cursor by its id
////////////////////////////////////////////////////////////////////////////////

static void JS_Cursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE("CURSOR(<cursor-identifier>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // get the id
  v8::Handle<v8::Value> idArg = args[0]->ToString();

  if (! idArg->IsString()) {
    TRI_V8_TYPE_ERROR("expecting a string for <cursor-identifier>)");
  }

  const string idString = TRI_ObjectToString(idArg);
  uint64_t id = TRI_UInt64String(idString.c_str());

  TRI_general_cursor_t* cursor = TRI_FindGeneralCursor(vocbase, (TRI_voc_tick_t) id);

  if (cursor == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_CURSOR_NOT_FOUND);
  }

  TRI_WrapGeneralCursor(args, cursor);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief delete a (persistent) cursor by its id
////////////////////////////////////////////////////////////////////////////////

static void JS_DeleteCursor (const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);


  if (args.Length() != 1) {
    TRI_V8_EXCEPTION_USAGE("DELETE_CURSOR(<cursor-identifier>)");
  }

  TRI_vocbase_t* vocbase = GetContextVocBase(isolate);

  if (vocbase == nullptr) {
    TRI_V8_EXCEPTION(TRI_ERROR_ARANGO_DATABASE_NOT_FOUND);
  }

  // get the id
  v8::Handle<v8::Value> idArg = args[0]->ToString();

  if (! idArg->IsString()) {
    TRI_V8_TYPE_ERROR("expecting a string for <cursor-identifier>)");
  }

  const string idString = TRI_ObjectToString(idArg);
  uint64_t id = TRI_UInt64String(idString.c_str());

  bool found = TRI_RemoveGeneralCursor(vocbase, id);

  if (found) {
    TRI_V8_RETURN_TRUE();
  }
  else {
    TRI_V8_RETURN_FALSE();
  }
}


  // .............................................................................
  // generate the general cursor template
  // .............................................................................
void TRI_InitV8cursor (v8::Handle<v8::Context> context,
                       TRI_v8_global_t* v8g){
  v8::Handle<v8::ObjectTemplate> rt;
  v8::Handle<v8::FunctionTemplate> ft;
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  ft = v8::FunctionTemplate::New(isolate);
  ft->SetClassName(TRI_V8_SYMBOL("ArangoCursor"));

  rt = ft->InstanceTemplate();
  rt->SetInternalFieldCount(2);

  TRI_AddMethodVocbase(isolate, rt, "count", JS_CountGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "dispose", JS_DisposeGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "getBatchSize", JS_GetBatchSizeGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "getExtra", JS_GetExtraGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "getRows", JS_GetRowsGeneralCursor, true); // DEPRECATED, use toArray
  TRI_AddMethodVocbase(isolate, rt, "hasCount", JS_HasCountGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "hasNext", JS_HasNextGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "id", JS_IdGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "next", JS_NextGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "persist", JS_PersistGeneralCursor);
  TRI_AddMethodVocbase(isolate, rt, "toArray", JS_ToArrayGeneralCursor);

  v8g->GeneralCursorTempl.Reset(isolate, rt);
  TRI_AddGlobalFunctionVocbase(isolate, context, "ArangoCursor", ft->GetFunction());

  // cursor functions. not intended to be used by end users
  TRI_AddGlobalFunctionVocbase(isolate, context, "CURSOR", JS_Cursor, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "CREATE_CURSOR", JS_CreateCursor, true);
  TRI_AddGlobalFunctionVocbase(isolate, context, "DELETE_CURSOR", JS_DeleteCursor, true);
}
