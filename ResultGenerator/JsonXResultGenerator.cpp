////////////////////////////////////////////////////////////////////////////////
/// @brief json (extended) result generator
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
/// @author Achim Brandt
/// @author Copyright 2008-2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "JsonXResultGenerator.h"

#include <Basics/Logger.h>
#include <Basics/StringBuffer.h>
#include <Basics/StringUtils.h>
#include <Basics/VariantObject.h>

using namespace triagens::basics;

namespace triagens {
  namespace rest {

    // -----------------------------------------------------------------------------
    // static public methods
    // -----------------------------------------------------------------------------

    void JsonXResultGenerator::initialise () {
      JsonResultGenerator::initialise(RESULT_GENERATOR_JSONX);
    }

    // -----------------------------------------------------------------------------
    // ResultGenerator methods
    // -----------------------------------------------------------------------------

    void JsonXResultGenerator::generateAtom (StringBuffer& output, int64_t value) const {
      output.appendInteger(value);
      output.appendChar('L');
    }


    void JsonXResultGenerator::generateAtom (StringBuffer& output, uint32_t value) const {
      output.appendChar('U');
      output.appendInteger(value);
    }

    void JsonXResultGenerator::generateAtom (StringBuffer& output, uint64_t value) const {
      output.appendChar('U');
      output.appendInteger(value);
      output.appendChar('L');
    }
  }
}
