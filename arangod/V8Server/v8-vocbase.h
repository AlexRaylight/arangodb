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

#ifndef TRIAGENS_V8SERVER_V8_VOCBASE_H
#define TRIAGENS_V8SERVER_V8_VOCBASE_H 1

#include "Basics/Common.h"

#include "V8/v8-globals.h"
#include "ShapedJson/shaped-json.h"
#include "VocBase/document-collection.h"

struct TRI_server_s;
struct TRI_vocbase_s;

namespace triagens {
  namespace arango {
    class CollectionNameResolver;
    class JSLoader;
  }
}

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief parse vertex handle from a v8 value (string | object)
////////////////////////////////////////////////////////////////////////////////

int TRI_ParseVertex (triagens::arango::CollectionNameResolver const*,
                     TRI_voc_cid_t&,
                     TRI_voc_key_t&,
                     v8::Handle<v8::Value> const,
                     bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up a index identifier
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_LookupIndexByHandle (triagens::arango::CollectionNameResolver const*,
                                      TRI_vocbase_col_t const*,
                                      v8::Handle<v8::Value>,
                                      bool,
                                      v8::Handle<v8::Object>*);

////////////////////////////////////////////////////////////////////////////////
/// @brief wraps a TRI_shaped_json_t
////////////////////////////////////////////////////////////////////////////////

template<class T>
v8::Handle<v8::Value> TRI_WrapShapedJson (T&,
                                          TRI_voc_cid_t,
                                          TRI_doc_mptr_t const*,
                                          TRI_barrier_t*,
                                          bool&);

////////////////////////////////////////////////////////////////////////////////
/// @brief return the private WRP_VOCBASE_COL_TYPE value
////////////////////////////////////////////////////////////////////////////////

int32_t TRI_GetVocBaseColType ();

////////////////////////////////////////////////////////////////////////////////
/// @brief run version check
////////////////////////////////////////////////////////////////////////////////

bool TRI_V8RunVersionCheck (void*,
                            triagens::arango::JSLoader*,
                            v8::Handle<v8::Context>);

////////////////////////////////////////////////////////////////////////////////
/// @brief run upgrade check
////////////////////////////////////////////////////////////////////////////////

int TRI_V8RunUpgradeCheck (void* vocbase, 
                           triagens::arango::JSLoader* startupLoader,
                           v8::Handle<v8::Context> context);

////////////////////////////////////////////////////////////////////////////////
/// @brief initialize foxx
////////////////////////////////////////////////////////////////////////////////

void TRI_V8InitialiseFoxx (void*, 
                           v8::Handle<v8::Context>);

////////////////////////////////////////////////////////////////////////////////
/// @brief reloads routing
////////////////////////////////////////////////////////////////////////////////

void TRI_V8ReloadRouting (v8::Handle<v8::Context>);

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a TRI_vocbase_t global context
////////////////////////////////////////////////////////////////////////////////

void TRI_InitV8VocBridge (v8::Handle<v8::Context>,
                          struct TRI_server_s*,
                          struct TRI_vocbase_s*,
                          triagens::arango::JSLoader*,
                          const size_t);

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
