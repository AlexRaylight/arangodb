////////////////////////////////////////////////////////////////////////////////
/// @brief Write-ahead log slots
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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_WAL_LOG_SLOTS_H
#define TRIAGENS_WAL_LOG_SLOTS_H 1

#include "Basics/Common.h"
#include "Basics/ConditionVariable.h"
#include "Basics/Mutex.h"
#include "Wal/Logfile.h"
#include "Wal/Slot.h"
#include "Wal/SyncRegion.h"

namespace triagens {
  namespace wal {

    class LogfileManager;

// -----------------------------------------------------------------------------
// --SECTION--                                               struct SlotInfoCopy
// -----------------------------------------------------------------------------
    
    struct SlotInfoCopy {
      explicit SlotInfoCopy (Slot const* slot) 
        : mem(slot->mem()),
          size(slot->size()),
          logfileId(slot->logfileId()),
          tick(slot->tick()),
          errorCode(TRI_ERROR_NO_ERROR) {
      }

      explicit SlotInfoCopy (int errorCode)
        : mem(nullptr),
          size(0),
          logfileId(0),
          tick(0),
          errorCode(errorCode) {
      }

      void const*           mem;
      uint32_t const        size;
      Logfile::IdType const logfileId;
      Slot::TickType const  tick;
      int const             errorCode;
    };

// -----------------------------------------------------------------------------
// --SECTION--                                                   struct SlotInfo
// -----------------------------------------------------------------------------

    struct SlotInfo {
      explicit SlotInfo (int errorCode) 
        : slot(nullptr),
          mem(nullptr),
          size(0),
          errorCode(errorCode) {
      }
      
      explicit SlotInfo (Slot* slot) 
        : slot(slot),
          mem(slot->mem()),
          size(slot->size()),
          errorCode(TRI_ERROR_NO_ERROR) {
      }

      SlotInfo () 
        : SlotInfo(TRI_ERROR_NO_ERROR) {
      }

      Slot*       slot;
      void const* mem;
      uint32_t    size;
      int         errorCode;
    };

// -----------------------------------------------------------------------------
// --SECTION--                                                       class Slots
// -----------------------------------------------------------------------------

    class Slots {

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

      public:
            
////////////////////////////////////////////////////////////////////////////////
/// @brief create the slots
////////////////////////////////////////////////////////////////////////////////

        Slots (LogfileManager*,
               size_t,
               Slot::TickType); 

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the slots
////////////////////////////////////////////////////////////////////////////////

        ~Slots ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief execute a flush operation
////////////////////////////////////////////////////////////////////////////////

        int flush (bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief return the last committed tick
////////////////////////////////////////////////////////////////////////////////

        Slot::TickType lastCommittedTick ();

////////////////////////////////////////////////////////////////////////////////
/// @brief return the next unused slot
////////////////////////////////////////////////////////////////////////////////

        SlotInfo nextUnused (uint32_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief return a used slot, allowing its synchronisation
////////////////////////////////////////////////////////////////////////////////

        void returnUsed (SlotInfo&,
                         bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief get the next synchronisable region
////////////////////////////////////////////////////////////////////////////////

        SyncRegion getSyncRegion ();

////////////////////////////////////////////////////////////////////////////////
/// @brief return a region to the freelist
////////////////////////////////////////////////////////////////////////////////

        void returnSyncRegion (SyncRegion const&);

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief write a header marker
////////////////////////////////////////////////////////////////////////////////

        int writeHeader (Slot*);

////////////////////////////////////////////////////////////////////////////////
/// @brief write a footer marker
////////////////////////////////////////////////////////////////////////////////

        int writeFooter (Slot*);

////////////////////////////////////////////////////////////////////////////////
/// @brief handout a region and advance the handout index
////////////////////////////////////////////////////////////////////////////////

        Slot::TickType handout ();        

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief the logfile manager
////////////////////////////////////////////////////////////////////////////////

        LogfileManager* _logfileManager;

////////////////////////////////////////////////////////////////////////////////
/// @brief condition variable for slots
////////////////////////////////////////////////////////////////////////////////

        basics::ConditionVariable _condition;

////////////////////////////////////////////////////////////////////////////////
/// @brief mutex protecting the slots interface
////////////////////////////////////////////////////////////////////////////////

        basics::Mutex _lock;

////////////////////////////////////////////////////////////////////////////////
/// @brief all slots
////////////////////////////////////////////////////////////////////////////////

        std::vector<Slot*> _slots;

////////////////////////////////////////////////////////////////////////////////
/// @brief the total number of slots
////////////////////////////////////////////////////////////////////////////////

        size_t const _numberOfSlots;

////////////////////////////////////////////////////////////////////////////////
/// @brief the number of currently free slots
////////////////////////////////////////////////////////////////////////////////

        size_t _freeSlots;

////////////////////////////////////////////////////////////////////////////////
/// @brief whether or not someone is waiting for a slot
////////////////////////////////////////////////////////////////////////////////

        uint32_t _waiting;

////////////////////////////////////////////////////////////////////////////////
/// @brief the index of the slot to hand out next
////////////////////////////////////////////////////////////////////////////////

        size_t _handoutIndex;

////////////////////////////////////////////////////////////////////////////////
/// @brief the index of the slot to recycle
////////////////////////////////////////////////////////////////////////////////

        size_t _recycleIndex;

////////////////////////////////////////////////////////////////////////////////
/// @brief the current logfile to write into
////////////////////////////////////////////////////////////////////////////////

        Logfile* _logfile;

////////////////////////////////////////////////////////////////////////////////
/// @brief last committed tick value
////////////////////////////////////////////////////////////////////////////////
    
        Slot::TickType _lastCommittedTick;

    };

  }
}

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
