////////////////////////////////////////////////////////////////////////////////
/// @brief replication dump functions
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

#ifndef ARANGODB_VOC_BASE_REPLICATION__DUMP_H
#define ARANGODB_VOC_BASE_REPLICATION__DUMP_H 1

#include "Basics/Common.h"

#include "BasicsC/associative.h"
#include "ShapedJson/shaped-json.h"
#include "VocBase/replication-common.h"
#include "VocBase/voc-types.h"

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

struct TRI_shape_s;
struct TRI_string_buffer_s;
struct TRI_vocbase_col_s;
struct TRI_vocbase_s;

// -----------------------------------------------------------------------------
// --SECTION--                                                REPLICATION LOGGER
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief replication dump container
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_replication_dump_s {
  struct TRI_string_buffer_s*  _buffer;
  TRI_voc_tick_t               _lastFoundTick;
  TRI_shape_sid_t              _lastSid;
  struct TRI_shape_s const*    _lastShape;
  struct TRI_vocbase_s*        _vocbase;
  TRI_associative_pointer_t    _collectionNames;
  bool                         _failed;
  bool                         _hasMore;
  bool                         _bufferFull;
}
TRI_replication_dump_t;

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from a single collection
////////////////////////////////////////////////////////////////////////////////

int TRI_DumpCollectionReplication (TRI_replication_dump_t*,
                                   struct TRI_vocbase_col_s*,
                                   TRI_voc_tick_t,
                                   TRI_voc_tick_t,
                                   uint64_t,
                                   bool,
                                   bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief dump data from the replication log
////////////////////////////////////////////////////////////////////////////////

int TRI_DumpLogReplication (struct TRI_vocbase_s*,
                            TRI_replication_dump_t*,
                            TRI_voc_tick_t,
                            TRI_voc_tick_t,
                            uint64_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a replication dump container
////////////////////////////////////////////////////////////////////////////////

int TRI_InitDumpReplication (TRI_replication_dump_t*,
                             struct TRI_vocbase_s*,
                             size_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a replication dump container
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyDumpReplication (TRI_replication_dump_t*);

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
