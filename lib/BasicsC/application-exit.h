////////////////////////////////////////////////////////////////////////////////
/// @brief High-Performance Database Framework made by triagens
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triagens GmbH, Cologne, Germany
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
/// @author Dr. O
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_BASICSC_APPLICATION_EXIT_H
#define TRIAGENS_BASICSC_APPLICATION_EXIT_H 1

#ifndef TRI_WITHIN_COMMON
#error use <BasicsC/common.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// --SECTION--                                     Special Application Exit 
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Application Exit
/// @{
////////////////////////////////////////////////////////////////////////////////

typedef void (*TRI_ExitFunction_t) (int, void*);

extern TRI_ExitFunction_t TRI_EXIT_FUNCTION;

void TRI_Application_Exit_SetExit (TRI_ExitFunction_t);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif


#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
