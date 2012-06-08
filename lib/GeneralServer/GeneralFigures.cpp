////////////////////////////////////////////////////////////////////////////////
/// @brief general server figures
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
/// @author Copyright 2009-2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "GeneralFigures.h"

namespace triagens {
  namespace rest {
    namespace GeneralFigures {
      basics::RoundRobinFigures<1, 61, GeneralServerStatistics> FiguresSecond;
      basics::RoundRobinFigures<60, 61, GeneralServerStatistics> FiguresMinute;
      basics::RoundRobinFigures<60 * 60, 25, GeneralServerStatistics> FiguresHour;
      basics::RoundRobinFigures<24 * 60 * 60, 31, GeneralServerStatistics> FiguresDay;
    }
  }
}
