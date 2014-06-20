////////////////////////////////////////////////////////////////////////////////
/// @brief memory region to be synched
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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_WAL_SYNC_REGION_H
#define ARANGODB_WAL_SYNC_REGION_H 1

#include "Basics/Common.h"
#include "Wal/Logfile.h"

namespace triagens {
  namespace wal {
    class Slots;

    struct SyncRegion {
      SyncRegion ()
        : logfileId(0),
          mem(nullptr),
          size(0),
          firstSlotIndex(0),
          lastSlotIndex(0),
          waitForSync(false) {
      }

      ~SyncRegion () {
      }

      Logfile::IdType logfileId;
      char*           mem;
      uint32_t        size;
      size_t          firstSlotIndex;
      size_t          lastSlotIndex;
      bool            waitForSync;
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
