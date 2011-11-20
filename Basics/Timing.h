////////////////////////////////////////////////////////////////////////////////
/// @brief class used for timing purposes
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
/// @author Copyright 2005-2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_BASICS_TIMING_H
#define TRIAGENS_BASICS_TIMING_H 1

#include <Basics/Common.h>

namespace triagens {
  namespace basics {

    ////////////////////////////////////////////////////////////////////////////////
    /// @ingroup Logging
    /// @brief used for timing tasks
    ///
    /// In order to get timings for a task, you must create an instance of Timing
    /// at the beginning of the task and use passed to get the microseconds since
    /// the beginning. You can use resetPassed to get the microseconds and reset
    /// the timer to a new beginning.
    ////////////////////////////////////////////////////////////////////////////////

    class Timing {
      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief timing types
        ////////////////////////////////////////////////////////////////////////////////

        enum TimingType {
          TI_DEFAULT = 0,
          TI_WALLCLOCK = 1,
          TI_RUSAGE_USER = 2,
          TI_RUSAGE_SYSTEM = 3,
          TI_RUSAGE_BOTH = 4,
          TI_UNKNOWN
        };

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief constructs a new instance
        ////////////////////////////////////////////////////////////////////////////////

        explicit
        Timing (TimingType type = TI_DEFAULT);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief copy constructor
        ////////////////////////////////////////////////////////////////////////////////

        Timing (const Timing& copy);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief assignment
        ////////////////////////////////////////////////////////////////////////////////

        Timing& operator= (const Timing& copy);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief destructor
        ////////////////////////////////////////////////////////////////////////////////

        ~Timing ();

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief microseconds passed
        ///
        /// Returns the number of microseconds passed since creation or last reset.
        ////////////////////////////////////////////////////////////////////////////////

        uint64_t time ();

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief microseconds passed and reset
        ///
        /// Returns the number of microseconds passed since creation or last reset
        /// and resets the timer.
        ////////////////////////////////////////////////////////////////////////////////

        uint64_t resetTime ();

      private:
        struct TimingImpl* impl;
    };
  }
}

#endif
