////////////////////////////////////////////////////////////////////////////////
/// @brief Ahuacatl, query context
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
/// @author Jan Steemann
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Ahuacatl/ahuacatl-context.h"

#include "BasicsC/hashes.h"
#include "BasicsC/json.h"
#include "BasicsC/logging.h"
#include "BasicsC/tri-strings.h"

#include "Ahuacatl/ahuacatl-access-optimiser.h"
#include "Ahuacatl/ahuacatl-ast-node.h"
#include "Ahuacatl/ahuacatl-bind-parameter.h"
#include "Ahuacatl/ahuacatl-collections.h"
#include "Ahuacatl/ahuacatl-explain.h"
#include "Ahuacatl/ahuacatl-optimiser.h"
#include "Ahuacatl/ahuacatl-parser-functions.h"
#include "Ahuacatl/ahuacatl-scope.h"
#include "Ahuacatl/ahuacatl-statementlist.h"
#include "Ahuacatl/ahuacatl-statement-dump.h"
#include "Ahuacatl/ahuacatl-variable.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                    private macros
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut macro for signalling out of memory
////////////////////////////////////////////////////////////////////////////////

#define ABORT_OOM                                                \
  TRI_SetErrorContextAql(                                        \
    __FILE__, __LINE__, context, TRI_ERROR_OUT_OF_MEMORY, NULL); \
  return NULL;

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief free all collection memory
////////////////////////////////////////////////////////////////////////////////

static void FreeCollections (TRI_aql_context_t* const context) {
  size_t i = context->_collections._length;

  while (i--) {
    TRI_aql_collection_t* collection = (TRI_aql_collection_t*) context->_collections._buffer[i];

    if (collection != NULL) {
      TRI_FreeCollectionAql(collection);
    }
  }
  TRI_DestroyVectorPointer(&context->_collections);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free all strings
////////////////////////////////////////////////////////////////////////////////

static void FreeStrings (TRI_aql_context_t* const context) {
  size_t i = context->_memory._strings._length;

  while (i--) {
    void* string = context->_memory._strings._buffer[i];

    if (string) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, context->_memory._strings._buffer[i]);
    }
  }
  TRI_DestroyVectorPointer(&context->_memory._strings);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free all nodes
////////////////////////////////////////////////////////////////////////////////

static void FreeNodes (TRI_aql_context_t* const context) {
  size_t i = context->_memory._nodes._length;

  while (i--) {
    TRI_aql_node_t* node = (TRI_aql_node_t*) context->_memory._nodes._buffer[i];

    if (node == NULL) {
      continue;
    }

    TRI_DestroyVectorPointer(&node->_members);

    if (node->_type == TRI_AQL_NODE_COLLECTION) {
      // free attached collection hint
      TRI_aql_collection_hint_t* hint = (TRI_aql_collection_hint_t*) (TRI_AQL_NODE_DATA(node));

      if (hint != NULL) {
        TRI_FreeCollectionHintAql(hint);
      }
    }
    else if (node->_type == TRI_AQL_NODE_FOR) {
      // free attached for hint
      TRI_aql_for_hint_t* hint = (TRI_aql_for_hint_t*) (TRI_AQL_NODE_DATA(node));

      if (hint != NULL) {
        TRI_FreeForHintScopeAql(hint);
      }
    }

    // free node itself
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, node);
  }

  TRI_DestroyVectorPointer(&context->_memory._nodes);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief process options from the context
////////////////////////////////////////////////////////////////////////////////
  
static void ProcessOptions (TRI_aql_context_t* context) {
  TRI_json_t* value;

  // default values
  context->_fullCount = false;
  
  value = TRI_GetOptionContextAql(context, "fullCount");

  if (value != NULL && value->_type == TRI_JSON_BOOLEAN) {
    context->_fullCount = value->_value._boolean;
  }
}

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create and initialize a context
////////////////////////////////////////////////////////////////////////////////

TRI_aql_context_t* TRI_CreateContextAql (TRI_vocbase_t* vocbase,
                                         const char* const query,
                                         const size_t queryLength,
                                         bool isCoordinator,
                                         TRI_json_t* userOptions) {
  TRI_aql_context_t* context;
  int res;

  TRI_ASSERT(vocbase != NULL);
  TRI_ASSERT(query != NULL);

  LOG_TRACE("creating context");

  context = (TRI_aql_context_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_context_t), false);

  if (context == NULL) {
    return NULL;
  }

  context->_type            = TRI_AQL_QUERY_READ;
  context->_vocbase         = vocbase;
  context->_userOptions     = userOptions;
  context->_writeCollection = NULL;

  context->_variableIndex = 0;
  context->_scopeIndex    = 0;
  context->_subQueries    = 0;

  // actual bind parameter values
  res = TRI_InitAssociativePointer(&context->_parameters._values,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   &TRI_HashStringKeyAssociativePointer,
                                   &TRI_HashBindParameterAql,
                                   &TRI_EqualBindParameterAql,
                                   0);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, context);

    return NULL;
  }

  // bind parameter names used in the query
  res = TRI_InitAssociativePointer(&context->_parameters._names,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   &TRI_HashStringKeyAssociativePointer,
                                   &TRI_HashStringKeyAssociativePointer,
                                   &TRI_EqualStringKeyAssociativePointer,
                                   0);

  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativePointer(&context->_parameters._values);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, context);

    return NULL;
  }

  // collections
  res = TRI_InitAssociativePointer(&context->_collectionNames,
                                   TRI_UNKNOWN_MEM_ZONE,
                                   &TRI_HashStringKeyAssociativePointer,
                                   &TRI_HashStringKeyAssociativePointer,
                                   &TRI_EqualStringKeyAssociativePointer,
                                   0);
  
  if (res != TRI_ERROR_NO_ERROR) {
    TRI_DestroyAssociativePointer(&context->_parameters._names);
    TRI_DestroyAssociativePointer(&context->_parameters._values);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, context);

    return NULL;
  }

  TRI_InitVectorPointer2(&context->_memory._nodes, TRI_UNKNOWN_MEM_ZONE, 16);
  TRI_InitVectorPointer2(&context->_memory._strings, TRI_UNKNOWN_MEM_ZONE, 16);
  TRI_InitVectorPointer(&context->_collections, TRI_UNKNOWN_MEM_ZONE);

  TRI_InitErrorAql(&context->_error);

  context->_parser     = NULL;
  context->_statements = NULL;
  context->_query      = query;
  
  TRI_InitScopesAql(context);
 
  context->_parser = TRI_CreateParserAql(context->_query, queryLength);

  if (context->_parser == NULL) {
    // could not create the parser
    TRI_FreeContextAql(context);
    return NULL;
  }

  if (! TRI_InitParserAql(context)) {
    // could not initialise the lexer
    TRI_FreeContextAql(context);

    return NULL;
  }

  context->_statements = TRI_CreateStatementListAql();

  if (context->_statements == NULL) {
    // could not create statement list
    TRI_FreeContextAql(context);

    return NULL;
  }
  
  ProcessOptions(context);

  context->_isCoordinator = isCoordinator;

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a context
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeContextAql (TRI_aql_context_t* const context) {
  TRI_ASSERT(context != NULL);

  LOG_TRACE("freeing context");

  // release all scopes
  TRI_FreeScopesAql(context);

  FreeStrings(context);
  FreeNodes(context);

  // free parameter names hash
  TRI_DestroyAssociativePointer(&context->_parameters._names);

  // free collection names
  TRI_DestroyAssociativePointer(&context->_collectionNames);

  FreeCollections(context);

  // free parameter values
  TRI_FreeBindParametersAql(context);
  TRI_DestroyAssociativePointer(&context->_parameters._values);

  // free parser/lexer
  TRI_FreeParserAql(context->_parser);

  // free statement list
  TRI_FreeStatementListAql(context->_statements);

  // free error struct
  TRI_DestroyErrorAql(&context->_error);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, context);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parse & validate the query string
////////////////////////////////////////////////////////////////////////////////

bool TRI_ValidateQueryContextAql (TRI_aql_context_t* const context) {
  if (context->_parser->_length == 0) {
    // query is empty, no need to parse it
    TRI_SetErrorContextAql(__FILE__, __LINE__, context, TRI_ERROR_QUERY_EMPTY, NULL);
    return false;
  }

  // parse the query
  if (! TRI_ParseAql(context)) {
    // lexing/parsing failed
    return false;
  }

  if (context->_error._code) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add bind parameters to the query context
////////////////////////////////////////////////////////////////////////////////

bool TRI_BindQueryContextAql (TRI_aql_context_t* const context,
                              const TRI_json_t* const parameters) {

  // add the bind parameters
  if (! TRI_AddParameterValuesAql(context, parameters)) {
    // adding parameters failed
    return false;
  }

  // validate the bind parameters used/passed
  if (! TRI_ValidateBindParametersAql(context)) {
    // invalid bind parameters
    return false;
  }

  // inject the bind parameter values into the query AST
  if (! TRI_InjectBindParametersAql(context)) {
    // bind parameter injection failed
    return false;
  }

  if (context->_error._code) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief perform some AST optimisations
////////////////////////////////////////////////////////////////////////////////

bool TRI_OptimiseQueryContextAql (TRI_aql_context_t* const context) {

  // do some basic optimisations in the AST
  if (! TRI_OptimiseAql(context)) {
    // constant folding failed
    return false;
  }

  if (context->_error._code) {
    return false;
  }

  TRI_CompactStatementListAql(context->_statements);
  TRI_PulloutStatementListAql(context->_statements);

  // TRI_DumpStatementsAql(context->_statements);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief set up all collections used in the query
////////////////////////////////////////////////////////////////////////////////

bool TRI_SetupCollectionsContextAql (TRI_aql_context_t* const context) {
  // mark all used collections as being used
  if (! TRI_SetupCollectionsAql(context)) {
    return false;
  }

  if (context->_error._code) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a node
////////////////////////////////////////////////////////////////////////////////

bool TRI_RegisterNodeContextAql (TRI_aql_context_t* const context,
                                 void* const node) {
  TRI_ASSERT(context != NULL);
  TRI_ASSERT(node != NULL);

  TRI_PushBackVectorPointer(&context->_memory._nodes, node);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a string
////////////////////////////////////////////////////////////////////////////////

char* TRI_RegisterStringAql (TRI_aql_context_t* const context,
                             const char* const value,
                             const size_t length,
                             const bool deescape) {
  char* copy;

  if (value == NULL) {
    ABORT_OOM
  }

  if (deescape && length > 0) {
    size_t outLength;

    copy = TRI_UnescapeUtf8StringZ(TRI_UNKNOWN_MEM_ZONE, value, length, &outLength);
  }
  else {
    copy = TRI_DuplicateString2Z(TRI_UNKNOWN_MEM_ZONE, value, length);
  }

  if (copy == NULL) {
    ABORT_OOM
  }

  if (TRI_PushBackVectorPointer(&context->_memory._strings, copy) != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, copy);
    ABORT_OOM
  }

  return copy;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a combined string
////////////////////////////////////////////////////////////////////////////////

char* TRI_RegisterString2Aql (TRI_aql_context_t* const context,
                              const char* const s1,
                              const char* const s2) {
  char* copy;
  
  copy = TRI_Concatenate2StringZ(TRI_UNKNOWN_MEM_ZONE, s1, s2);
  
  if (copy == NULL) {
    ABORT_OOM
  }

  if (TRI_PushBackVectorPointer(&context->_memory._strings, copy) != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, copy);
    ABORT_OOM
  }

  return copy;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a combined string
////////////////////////////////////////////////////////////////////////////////

char* TRI_RegisterString3Aql (TRI_aql_context_t* const context,
                              const char* const s1,
                              const char* const s2,
                              const char* const s3) {
  char* copy;
  
  copy = TRI_Concatenate3StringZ(TRI_UNKNOWN_MEM_ZONE, s1, s2, s3);
  
  if (copy == NULL) {
    ABORT_OOM
  }

  if (TRI_PushBackVectorPointer(&context->_memory._strings, copy) != TRI_ERROR_NO_ERROR) {
    TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, copy);
    ABORT_OOM
  }

  return copy;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register an error
////////////////////////////////////////////////////////////////////////////////

void TRI_SetErrorContextAql (const char* file,
                             int line,
                             TRI_aql_context_t* const context,
                             const int code,
                             const char* const data) {

  TRI_ASSERT(context != NULL);
  TRI_ASSERT(code > 0);

  if (context->_error._code == 0) {
    // do not overwrite previous error
    TRI_set_errno(code);
    context->_error._code = code;
    context->_error._message = (char*) TRI_last_error();
    context->_error._file = file;
    context->_error._line = line;

    if (data) {
      context->_error._data = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, data);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the value of an option variable
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* TRI_GetOptionContextAql (TRI_aql_context_t* const context,
                              const char* name) {
  if (context->_userOptions == NULL || context->_userOptions->_type != TRI_JSON_ARRAY) {
    return NULL; 
  }

  return TRI_LookupArrayJson(context->_userOptions, name);
}

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
