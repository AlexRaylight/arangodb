////////////////////////////////////////////////////////////////////////////////
/// @brief force symbols into programm
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2011 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2009-2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_JUTLAND_BASICS_INITIALISE_H
#define TRIAGENS_JUTLAND_BASICS_INITIALISE_H 1

#include <Basics/Common.h>

namespace triagens {

  ////////////////////////////////////////////////////////////////////////////////
  /// @brief the namespace containing the basic classes and functions
  ////////////////////////////////////////////////////////////////////////////////

  namespace basics {

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief initialise function
    ////////////////////////////////////////////////////////////////////////////////

    extern void InitialiseBasics ();

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief shutdown function
    ////////////////////////////////////////////////////////////////////////////////

    extern void ShutdownBasics ();
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise
////////////////////////////////////////////////////////////////////////////////

#define TRIAGENS_BASICS_INITIALISE              \
  do {                                          \
    triagens::basics::InitialiseBasics();       \
  } while (0)

////////////////////////////////////////////////////////////////////////////////
/// @brief shutdown
////////////////////////////////////////////////////////////////////////////////

#define TRIAGENS_BASICS_SHUTDOWN                \
  do {                                          \
    triagens::basics::ShutdownBasics();         \
  } while (0)

#endif
