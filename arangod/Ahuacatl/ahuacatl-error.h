////////////////////////////////////////////////////////////////////////////////
/// @brief Ahuacatl, errors
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

#ifndef TRIAGENS_AHUACATL_AHUACATL_ERROR_H
#define TRIAGENS_AHUACATL_AHUACATL_ERROR_H 1

#include "Basics/Common.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief query error structure
///
/// This struct is used to hold information about errors that happen during
/// query execution. The data will be passed to the end user.
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_aql_error_s {
  int _code;
  char* _message;
  char* _data;
  const char* _file;
  int _line;
}
TRI_aql_error_t;

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief get the error code registered last
////////////////////////////////////////////////////////////////////////////////

int TRI_GetErrorCodeAql (const TRI_aql_error_t* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief get the error string registered last
////////////////////////////////////////////////////////////////////////////////

char* TRI_GetErrorMessageAql (const TRI_aql_error_t* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief initialize an error structure
////////////////////////////////////////////////////////////////////////////////

void TRI_InitErrorAql (TRI_aql_error_t* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy an error structure, not freeing the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyErrorAql (TRI_aql_error_t* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief free an error structure
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeErrorAql (TRI_aql_error_t* const);

////////////////////////////////////////////////////////////////////////////////
/// @brief get a format query error message
////////////////////////////////////////////////////////////////////////////////

char* TRI_GetContextErrorAql (const char* const, 
                              const size_t, 
                              const size_t, 
                              const size_t);

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
