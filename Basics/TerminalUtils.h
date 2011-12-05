////////////////////////////////////////////////////////////////////////////////
/// @brief collections of terminal functions
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
/// @author Copyright 2011, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_JUTLAND_BASICS_TERMINAL_UTILS_H
#define TRIAGENS_JUTLAND_BASICS_TERMINAL_UTILS_H 1

#include <Basics/Common.h>


namespace triagens {
  namespace basics {

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief collection of terminal functions
    ////////////////////////////////////////////////////////////////////////////////

    namespace TerminalUtils {

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief default value for number of columns of a terminal
      ////////////////////////////////////////////////////////////////////////////////

      static int const DEFAULT_COLUMNS = 80;

      ////////////////////////////////////////////////////////////////////////////////
      /// @brief returns the columns
      ////////////////////////////////////////////////////////////////////////////////

      int columnsWidth ();
    }
  }
}

#endif

