////////////////////////////////////////////////////////////////////////////////
/// @brief Ahuacatl, parser nodes
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2012, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Ahuacatl/ahuacatl-ast-node.h"
#include "Ahuacatl/ahuacatl-collections.h"
#include "Ahuacatl/ahuacatl-functions.h"
#include "Ahuacatl/ahuacatl-parser-functions.h"
#include "Ahuacatl/ahuacatl-scope.h"
#include "Ahuacatl/ahuacatl-variable.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                    private macros
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Ahuacatl
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut macro for signalling out of memory
////////////////////////////////////////////////////////////////////////////////

#define ABORT_OOM                                                                \
  TRI_SetErrorContextAql(context, TRI_ERROR_OUT_OF_MEMORY, NULL);                \
  return NULL;

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut macro to create a new node or fail in case of OOM
////////////////////////////////////////////////////////////////////////////////

#define CREATE_NODE(type)                                                        \
  TRI_aql_node_t* node = (TRI_aql_node_t*)                                       \
    TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_node_t), false);           \
  if (!node) {                                                                   \
    ABORT_OOM                                                                    \
  }                                                                              \
                                                                                 \
  assert(context);                                                               \
  InitNode(context, node, type); 
  
////////////////////////////////////////////////////////////////////////////////
/// @brief add a sub node to a node
////////////////////////////////////////////////////////////////////////////////

#define ADD_MEMBER(member)                                                       \
  if (!member) {                                                                 \
    ABORT_OOM                                                                    \
  }                                                                              \
                                                                                 \
  TRI_PushBackVectorPointer(&node->_members, (void*) member);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Ahuacatl
/// @{
////////////////////////////////////////////////////////////////////////////////

static inline void InitNode (TRI_aql_context_t* const context,
                             TRI_aql_node_t* const node, 
                             const TRI_aql_node_type_e type) {
  node->_type = type;
 
  TRI_InitVectorPointer(&node->_members, TRI_UNKNOWN_MEM_ZONE);
  TRI_RegisterNodeContextAql(context, node);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST nop node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeNopAql (void) {
  TRI_aql_node_t* node = (TRI_aql_node_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_node_t), false);

  if (node == NULL) {
    return NULL;
  }
  
  node->_type = TRI_AQL_NODE_NOP;
 
  TRI_InitVectorPointer(&node->_members, TRI_UNKNOWN_MEM_ZONE);

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST return empty node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeReturnEmptyAql (void) {
  TRI_aql_node_t* node;
  TRI_aql_node_t* list;

  node = (TRI_aql_node_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_node_t), false);

  if (node == NULL) {
    return NULL;
  }
  
  node->_type = TRI_AQL_NODE_RETURN_EMPTY;

  list = (TRI_aql_node_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_node_t), false);
  
  if (list == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, list);

    return NULL;
  }

  list->_type = TRI_AQL_NODE_LIST;
  TRI_InitVectorPointer(&list->_members, TRI_UNKNOWN_MEM_ZONE);
 
  TRI_InitVectorPointer(&node->_members, TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBackVectorPointer(&node->_members, (void*) list);

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST scope start node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeScopeStartAql (TRI_aql_context_t* const context,
                                             void* data) {
  CREATE_NODE(TRI_AQL_NODE_SCOPE_START)
  node->_value._value._data = data; 

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST scope end node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeScopeEndAql (TRI_aql_context_t* const context,
                                           void* data) {
  CREATE_NODE(TRI_AQL_NODE_SCOPE_END)
  node->_value._value._data = data; 

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST for node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeForAql (TRI_aql_context_t* const context,
                                      const char* const name,
                                      const TRI_aql_node_t* const expression) {
  CREATE_NODE(TRI_AQL_NODE_FOR)
  node->_value._value._data = NULL; 

  if (!name) {
    ABORT_OOM
  }

  if (!TRI_IsValidVariableNameAql(name)) { 
    TRI_SetErrorContextAql(context, TRI_ERROR_QUERY_VARIABLE_NAME_INVALID, name); 
    return NULL;
  }
  
  {
    TRI_aql_node_t* variable = TRI_CreateNodeVariableAql(context, name, node);
    ADD_MEMBER(variable)
    ADD_MEMBER(expression)
  }
 
  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST let node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeLetAql (TRI_aql_context_t* const context,
                                      const char* const name,
                                      const TRI_aql_node_t* const expression) {
  CREATE_NODE(TRI_AQL_NODE_LET)
  
  if (!name) {
    ABORT_OOM
  }
  
  if (!TRI_IsValidVariableNameAql(name)) { 
    TRI_SetErrorContextAql(context, TRI_ERROR_QUERY_VARIABLE_NAME_INVALID, name); 
    return NULL;
  }
  
  {
    TRI_aql_node_t* variable = TRI_CreateNodeVariableAql(context, name, node);
    ADD_MEMBER(variable)
    ADD_MEMBER(expression)
  }
  
  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST filter node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeFilterAql (TRI_aql_context_t* const context,
                                         const TRI_aql_node_t* const expression) {
  CREATE_NODE(TRI_AQL_NODE_FILTER) 
  
  ADD_MEMBER(expression)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST return node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeReturnAql (TRI_aql_context_t* const context,
                                         const TRI_aql_node_t* const expression) {
  CREATE_NODE(TRI_AQL_NODE_RETURN) 
  
  ADD_MEMBER(expression)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST collect node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeCollectAql (TRI_aql_context_t* const context,
                                          const TRI_aql_node_t* const list,
                                          const char* const name) {
  CREATE_NODE(TRI_AQL_NODE_COLLECT)

  ADD_MEMBER(list)
  if (name) {
    TRI_aql_node_t* variable = TRI_CreateNodeVariableAql(context, name, node);
    ADD_MEMBER(variable)
  }

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST sort node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeSortAql (TRI_aql_context_t* const context,
                                       const TRI_aql_node_t* const list) {
  CREATE_NODE(TRI_AQL_NODE_SORT)
  
  ADD_MEMBER(list)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST sort element node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeSortElementAql (TRI_aql_context_t* const context,
                                              const TRI_aql_node_t* const expression, 
                                              const bool ascending) {
  CREATE_NODE(TRI_AQL_NODE_SORT_ELEMENT)

  ADD_MEMBER(expression)
  TRI_AQL_NODE_BOOL(node) = ascending;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST limit node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeLimitAql (TRI_aql_context_t* const context,
                                        const TRI_aql_node_t* const offset, 
                                        const TRI_aql_node_t* const count) {
  CREATE_NODE(TRI_AQL_NODE_LIMIT)

  ADD_MEMBER(offset)
  ADD_MEMBER(count)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST assign node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeAssignAql (TRI_aql_context_t* const context,
                                         const char* const name,
                                         const TRI_aql_node_t* const expression) {
  CREATE_NODE(TRI_AQL_NODE_ASSIGN)
 
  {
    TRI_aql_node_t* variable = TRI_CreateNodeVariableAql(context, name, node);
    ADD_MEMBER(variable)
    ADD_MEMBER(expression)
  }
  
  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST variable node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeVariableAql (TRI_aql_context_t* const context,
                                           const char* const name,
                                           TRI_aql_node_t* const definingNode) {
  CREATE_NODE(TRI_AQL_NODE_VARIABLE)
   
  if (!name) {
    ABORT_OOM
  }
  
  if (!TRI_AddVariableScopeAql(context, name, definingNode)) {
    // duplicate variable name 
    TRI_SetErrorContextAql(context, TRI_ERROR_QUERY_VARIABLE_REDECLARED, name); 
    return NULL;
  }
  
  TRI_AQL_NODE_STRING(node) = (char*) name;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST collection node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeCollectionAql (TRI_aql_context_t* const context,
                                             const char* const name) {
  CREATE_NODE(TRI_AQL_NODE_COLLECTION)

  if (!name) {
    ABORT_OOM
  }

  if (strlen(name) == 0) {
    TRI_SetErrorContextAql(context, TRI_ERROR_QUERY_COLLECTION_NOT_FOUND, name);
    return NULL;
  }

  {
    TRI_aql_node_t* nameNode = TRI_CreateNodeValueStringAql(context, name);
    ADD_MEMBER(nameNode)
  }
  
  if (!TRI_AddCollectionAql(context, name)) {
    return NULL;
  }

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST reference node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeReferenceAql (TRI_aql_context_t* const context,
                                            const char* const name) {
  CREATE_NODE(TRI_AQL_NODE_REFERENCE)

  if (!name) {
    ABORT_OOM
  }
  
  TRI_AQL_NODE_STRING(node) = (char*) name;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST attribute node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeAttributeAql (TRI_aql_context_t* const context,
                                            const char* const name) {
  CREATE_NODE(TRI_AQL_NODE_ATTRIBUTE)

  if (!name) {
    ABORT_OOM
  }
  
  TRI_AQL_NODE_STRING(node) = (char*) name;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST parameter node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeParameterAql (TRI_aql_context_t* const context,
                                            const char* const name) {
  CREATE_NODE(TRI_AQL_NODE_PARAMETER)

  if (!name) {
    ABORT_OOM
  }

  // save name of bind parameter for later
  TRI_InsertKeyAssociativePointer(&context->_parameters._names, name, (void*) name, true);

  TRI_AQL_NODE_STRING(node) = (char*) name;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST unary plus node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorUnaryPlusAql (TRI_aql_context_t* const context,
                                                    const TRI_aql_node_t* const operand) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_UNARY_PLUS)
 
  ADD_MEMBER(operand)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST unary minus node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorUnaryMinusAql (TRI_aql_context_t* const context,
                                                     const TRI_aql_node_t* const operand) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_UNARY_MINUS)
  
  ADD_MEMBER(operand)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST unary not node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorUnaryNotAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const operand) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_UNARY_NOT)
  
  ADD_MEMBER(operand)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary and node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryAndAql (TRI_aql_context_t* const context,
                                                    const TRI_aql_node_t* const lhs,
                                                    const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_AND)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary or node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryOrAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_OR)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary eq node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryEqAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_EQ)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary ne node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryNeAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_NE)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary lt node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryLtAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_LT)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary le node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryLeAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_LE)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary gt node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryGtAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_GT)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary ge node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryGeAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_GE)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary in node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryInAql (TRI_aql_context_t* const context,
                                                   const TRI_aql_node_t* const lhs,
                                                   const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_IN)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary plus node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryPlusAql (TRI_aql_context_t* const context,
                                                     const TRI_aql_node_t* const lhs,
                                                     const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_PLUS)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary minus node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryMinusAql (TRI_aql_context_t* const context,
                                                      const TRI_aql_node_t* const lhs,
                                                      const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_MINUS)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary times node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryTimesAql (TRI_aql_context_t* const context,
                                                      const TRI_aql_node_t* const lhs,
                                                      const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_TIMES)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary div node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryDivAql (TRI_aql_context_t* const context,
                                                    const TRI_aql_node_t* const lhs,
                                                    const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_DIV)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST binary mod node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorBinaryModAql (TRI_aql_context_t* const context,
                                                    const TRI_aql_node_t* const lhs,
                                                    const TRI_aql_node_t* const rhs) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_BINARY_MOD)
  
  ADD_MEMBER(lhs)
  ADD_MEMBER(rhs)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST ternary operator node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeOperatorTernaryAql (TRI_aql_context_t* const context,
                                                  const TRI_aql_node_t* const condition,
                                                  const TRI_aql_node_t* const truePart,
                                                  const TRI_aql_node_t* const falsePart) {
  CREATE_NODE(TRI_AQL_NODE_OPERATOR_TERNARY)
  
  ADD_MEMBER(condition)
  ADD_MEMBER(truePart)
  ADD_MEMBER(falsePart)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST subquery node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeSubqueryAql (TRI_aql_context_t* const context) {
  CREATE_NODE(TRI_AQL_NODE_SUBQUERY)
     
  {
    // add the temporary variable
    TRI_aql_node_t* variable = TRI_CreateNodeVariableAql(context, TRI_GetNameParseAql(context), node);
    ADD_MEMBER(variable)
  }
 
  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST attribute access node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeAttributeAccessAql (TRI_aql_context_t* const context, 
                                                  const TRI_aql_node_t* const accessed,
                                                  const char* const name) { 
  CREATE_NODE(TRI_AQL_NODE_ATTRIBUTE_ACCESS)

  if (!name) {
    ABORT_OOM
  }

  ADD_MEMBER(accessed)
  TRI_AQL_NODE_STRING(node) = (char*) name;  

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST index access node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeIndexedAql (TRI_aql_context_t* const context,
                                          const TRI_aql_node_t* const accessed, 
                                          const TRI_aql_node_t* const indexValue) {
  CREATE_NODE(TRI_AQL_NODE_INDEXED)

  ADD_MEMBER(accessed)
  ADD_MEMBER(indexValue)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST expand node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeExpandAql (TRI_aql_context_t* const context,
                                         const char* const varname,
                                         const TRI_aql_node_t* const expanded,
                                         const TRI_aql_node_t* const expansion) {
  CREATE_NODE(TRI_AQL_NODE_EXPAND)

  if (!varname) {
    ABORT_OOM
  }

  {
    // TODO: check if 3rd parameters' values are correct for these
    TRI_aql_node_t* variable1 = TRI_CreateNodeVariableAql(context, varname, node);
    TRI_aql_node_t* variable2 = TRI_CreateNodeVariableAql(context, TRI_GetNameParseAql(context), node);

    ADD_MEMBER(variable1)
    ADD_MEMBER(variable2)
    ADD_MEMBER(expanded)
    ADD_MEMBER(expansion)
  }

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST null value node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeValueNullAql (TRI_aql_context_t* const context) {
  CREATE_NODE(TRI_AQL_NODE_VALUE)

  TRI_AQL_NODE_TYPE(node) = TRI_AQL_TYPE_NULL;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST int value node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeValueIntAql (TRI_aql_context_t* const context,
                                           const int64_t value) { 
  CREATE_NODE(TRI_AQL_NODE_VALUE)

  TRI_AQL_NODE_TYPE(node) = TRI_AQL_TYPE_INT;
  TRI_AQL_NODE_INT(node) = value;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST double value node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeValueDoubleAql (TRI_aql_context_t* const context,
                                              const double value) { 
  CREATE_NODE(TRI_AQL_NODE_VALUE)

  TRI_AQL_NODE_TYPE(node) = TRI_AQL_TYPE_DOUBLE;
  TRI_AQL_NODE_DOUBLE(node) = value;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST bool value node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeValueBoolAql (TRI_aql_context_t* const context,
                                            const bool value) { 
  CREATE_NODE(TRI_AQL_NODE_VALUE)

  TRI_AQL_NODE_TYPE(node) = TRI_AQL_TYPE_BOOL;
  TRI_AQL_NODE_BOOL(node) = value;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST string value node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeValueStringAql (TRI_aql_context_t* const context,
                                              const char* const value) { 
  CREATE_NODE(TRI_AQL_NODE_VALUE)

  if (!value) {
    ABORT_OOM
  }
  
  TRI_AQL_NODE_TYPE(node) = TRI_AQL_TYPE_STRING;
  TRI_AQL_NODE_STRING(node) = (char*) value;

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST list node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeListAql (TRI_aql_context_t* const context) {
  CREATE_NODE(TRI_AQL_NODE_LIST)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST array node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeArrayAql (TRI_aql_context_t* const context) {
  CREATE_NODE(TRI_AQL_NODE_ARRAY)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST array element node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeArrayElementAql (TRI_aql_context_t* const context,
                                               const char* const name,
                                               const TRI_aql_node_t* const value) {
  CREATE_NODE(TRI_AQL_NODE_ARRAY_ELEMENT)

  if (!name) {
    ABORT_OOM
  }

  TRI_AQL_NODE_STRING(node) = (char*) name;
  ADD_MEMBER(value)

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create an AST function call node
////////////////////////////////////////////////////////////////////////////////

TRI_aql_node_t* TRI_CreateNodeFcallAql (TRI_aql_context_t* const context,
                                        const char* const name,
                                        const TRI_aql_node_t* const parameters) {
  CREATE_NODE(TRI_AQL_NODE_FCALL)

  if (!name) {
    ABORT_OOM
  }
  
  {
    TRI_aql_function_t* function;
    TRI_associative_pointer_t* functions;

    assert(context->_vocbase);
    functions = context->_vocbase->_functions;
    assert(functions);

    function = TRI_GetByExternalNameFunctionAql(functions, name);

    if (!function) {
      // function name is unknown
      TRI_SetErrorContextAql(context, TRI_ERROR_QUERY_FUNCTION_NAME_UNKNOWN, name);
      return NULL;
    }

    // validate function call arguments
    if (!TRI_ValidateArgsFunctionAql(context, function, parameters)) {
      return NULL;
    }

    ADD_MEMBER(parameters)
    TRI_AQL_NODE_DATA(node) = function;
  }

  return node;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add a value to the end of a list
////////////////////////////////////////////////////////////////////////////////

bool TRI_PushListAql (TRI_aql_context_t* const context, 
                      const TRI_aql_node_t* const value) {
  TRI_aql_node_t* node = TRI_PeekStackParseAql(context);

  assert(node);
 
  ADD_MEMBER(value)

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add a value to the end of an array
////////////////////////////////////////////////////////////////////////////////

bool TRI_PushArrayAql (TRI_aql_context_t* const context, 
                       const char* const name,
                       const TRI_aql_node_t* const value) {
  TRI_aql_node_t* node = TRI_PeekStackParseAql(context);
  TRI_aql_node_t* element;

  assert(node);
  
  element = TRI_CreateNodeArrayElementAql(context, name, value);
  if (!element) {
    ABORT_OOM
  }

  ADD_MEMBER(element)

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the boolean value of a node
////////////////////////////////////////////////////////////////////////////////

inline bool TRI_GetBooleanNodeValueAql (const TRI_aql_node_t* const node) {
  assert(node);
  assert(node->_type == TRI_AQL_NODE_VALUE);
    
  return TRI_AQL_NODE_BOOL(node);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get the numeric value of a node
////////////////////////////////////////////////////////////////////////////////

double TRI_GetNumericNodeValueAql (const TRI_aql_node_t* const node) {
  assert(node);
  assert(node->_type == TRI_AQL_NODE_VALUE);

  if (node->_value._type == TRI_AQL_TYPE_INT) {
    return (double) TRI_AQL_NODE_INT(node);
  }
  if (node->_value._type == TRI_AQL_TYPE_DOUBLE) {
    return TRI_AQL_NODE_DOUBLE(node);
  }
  if (node->_value._type == TRI_AQL_TYPE_BOOL) {
    return TRI_AQL_NODE_BOOL(node) ? 1.0 : 0.0;
  }

  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief reverse a relational operator
////////////////////////////////////////////////////////////////////////////////
  
TRI_aql_node_type_e TRI_ReverseOperatorRelationalAql (const TRI_aql_node_type_e source) {
  switch (source) {
    case TRI_AQL_NODE_OPERATOR_BINARY_LT:
      return TRI_AQL_NODE_OPERATOR_BINARY_GT;
    case TRI_AQL_NODE_OPERATOR_BINARY_LE:
      return TRI_AQL_NODE_OPERATOR_BINARY_GE;
    case TRI_AQL_NODE_OPERATOR_BINARY_GT:
      return TRI_AQL_NODE_OPERATOR_BINARY_LT;
    case TRI_AQL_NODE_OPERATOR_BINARY_GE:
      return TRI_AQL_NODE_OPERATOR_BINARY_LE;
    case TRI_AQL_NODE_OPERATOR_BINARY_NE:
    case TRI_AQL_NODE_OPERATOR_BINARY_EQ: 
      return source;
    case TRI_AQL_NODE_OPERATOR_BINARY_IN:
    default: {
      assert(false);
      return TRI_AQL_NODE_NOP;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @} 
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
