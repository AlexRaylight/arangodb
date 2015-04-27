////////////////////////////////////////////////////////////////////////////////
/// @brief Aql, item block manager
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
/// @author Jan Steemann
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_ITEM_BLOCK_MANAGER_H
#define ARANGODB_AQL_ITEM_BLOCK_MANAGER_H 1

#include "Basics/Common.h"
#include "Aql/types.h"

namespace triagens {
  namespace aql {

    class AqlItemBlock;

// -----------------------------------------------------------------------------
// --SECTION--                                         class AqlItemBlockManager
// -----------------------------------------------------------------------------

    class AqlItemBlockManager {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief create the manager
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlockManager ();

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the manager
////////////////////////////////////////////////////////////////////////////////

        ~AqlItemBlockManager ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief request a block with the specified size
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* requestBlock (size_t, 
                                    RegisterId);

////////////////////////////////////////////////////////////////////////////////
/// @brief return a block to the manager
////////////////////////////////////////////////////////////////////////////////

        void returnBlock (AqlItemBlock*&);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief last block handed back to the manager
/// this is the block that may be recycled
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* _last;

    };

  }
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
