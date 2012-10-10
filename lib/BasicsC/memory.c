////////////////////////////////////////////////////////////////////////////////
/// @brief basic memory management
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "BasicsC/common.h"

#include "BasicsC/logging.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Memory
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief core memory zone, allocation will never fail
////////////////////////////////////////////////////////////////////////////////

static TRI_memory_zone_t TriCoreMemZone;

////////////////////////////////////////////////////////////////////////////////
/// @brief unknown memory zone
////////////////////////////////////////////////////////////////////////////////

static TRI_memory_zone_t TriUnknownMemZone;

////////////////////////////////////////////////////////////////////////////////
/// @brief memory reserve for core memory zone
////////////////////////////////////////////////////////////////////////////////

static void* CoreReserve;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Memory
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief core memory zone, allocation will never fail
////////////////////////////////////////////////////////////////////////////////

TRI_memory_zone_t* TRI_CORE_MEM_ZONE = &TriCoreMemZone;

////////////////////////////////////////////////////////////////////////////////
/// @brief unknown memory zone
////////////////////////////////////////////////////////////////////////////////

#ifndef TRI_ENABLE_ZONE_DEBUG
TRI_memory_zone_t* TRI_UNKNOWN_MEM_ZONE = &TriUnknownMemZone;
#endif

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Memory
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief generates an error message
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_ZONE_DEBUG
TRI_memory_zone_t* TRI_UnknownMemZoneZ (char const* file, int line) {
/*  printf("MEMORY ZONE: using unknown memory zone at (%s,%d)\n",
         file,
         line);
*/
  return &TriUnknownMemZone;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief basic memory management for allocate
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_ZONE_DEBUG
void* TRI_AllocateZ (TRI_memory_zone_t* zone, uint64_t n, bool set, char const* file, int line) {
#else
void* TRI_Allocate (TRI_memory_zone_t* zone, uint64_t n, bool set) {
#endif
  char* m;

#ifdef TRI_ENABLE_ZONE_DEBUG
  m = malloc((size_t) n + sizeof(intptr_t));
#else
  m = malloc((size_t) n);
#endif

  if (m == NULL) {
    if (zone->_failable) {
      return NULL;
    }

    if (CoreReserve == NULL) {
      printf("FATAL: failed to allocate memory in zone '%d', giving up!", zone->_zid);
      exit(EXIT_FAILURE);
    }

    free(CoreReserve);
    CoreReserve = NULL;

    LOG_FATAL("failed to allocate memory in zone '%d' of size '%ld', retrying!",
              (int) zone->_zid,
              (unsigned long) n);

#ifdef TRI_ENABLE_ZONE_DEBUG
    return TRI_AllocateZ(zone, n, set, file, line);
#else
    return TRI_Allocate(zone, n, set);
#endif
  }
#ifdef TRI_ENABLE_ZONE_DEBUG
  else if (set) {
    memset(m, 0, (size_t) n + sizeof(intptr_t));
  }
  else {
    memset(m, 0xA5, (size_t) n + sizeof(intptr_t));
  }
#else
  else if (set) {
    memset(m, 0, (size_t) n);
  }
#endif

#ifdef TRI_ENABLE_ZONE_DEBUG
  * (intptr_t*) m = zone->_zid;
  m += sizeof(intptr_t);
#endif

  return m;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief basic memory management for reallocate
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_ZONE_DEBUG
void* TRI_ReallocateZ (TRI_memory_zone_t* zone, void* m, uint64_t n, char const* file, int line) {
#else
void* TRI_Reallocate (TRI_memory_zone_t* zone, void* m, uint64_t n) {
#endif
  char* p;

  if (m == NULL) {
#ifdef TRI_ENABLE_ZONE_DEBUG
    return TRI_AllocateZ(zone, n, false, file, line);
#else
    return TRI_Allocate(zone, n, false);
#endif
  }

  p = (char*) m;

#ifdef TRI_ENABLE_ZONE_DEBUG
  p -= sizeof(intptr_t);

  if (* (intptr_t*) p != zone->_zid) {
    printf("MEMORY ZONE: mismatch in TRI_Reallocate(%s,%d), old '%d', new '%d'\n",
           file,
           line,
           (int) * (intptr_t*) p,
           (int) zone->_zid);
  }

  p = realloc(p, (size_t) n + sizeof(intptr_t));
#else
  p = realloc(p, (size_t) n);
#endif

  if (p == NULL) {
    if (zone->_failable) {
      return NULL;
    }

    if (CoreReserve == NULL) {
      printf("FATAL: failed to allocate memory in zone '%d', giving up!", zone->_zid);
      exit(EXIT_FAILURE);
    }

    free(CoreReserve);
    CoreReserve = NULL;

    LOG_FATAL("failed to allocate memory in zone '%d' of size '%ld', retrying!",
              (int) zone->_zid,
              (unsigned long) n);

#ifdef TRI_ENABLE_ZONE_DEBUG
    return TRI_ReallocateZ(zone, m, n, file, line);
#else
    return TRI_Reallocate(zone, m, n);
#endif
  }

#ifdef TRI_ENABLE_ZONE_DEBUG
  p += sizeof(intptr_t);
#endif

  return p;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief basic memory management for deallocate
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_ZONE_DEBUG
void TRI_FreeZ (TRI_memory_zone_t* zone, void* m, char const* file, int line) {
#else
void TRI_Free (TRI_memory_zone_t* zone, void* m) {
#endif
  char* p;

  p = (char*) m;

#ifdef TRI_ENABLE_ZONE_DEBUG
  p -= sizeof(intptr_t);

  if (* (intptr_t*) p != zone->_zid) {
    printf("MEMORY ZONE: mismatch in TRI_Free(%s,%d), old '%d', new '%d'\n",
           file,
           line,
           (int) * (intptr_t*) p,
           (int) zone->_zid);
  }
#endif

  free(p);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialize memory subsystem
////////////////////////////////////////////////////////////////////////////////

void TRI_InitialiseMemory () {
  static bool initialised = false;
  static size_t const reserveSize = 1024 * 1024 * 10;

  if (! initialised) {
    TriCoreMemZone._zid = 0;
    TriCoreMemZone._failed = false;
    TriCoreMemZone._failable = false;

    TriUnknownMemZone._zid = 1;
    TriUnknownMemZone._failed = false;
    TriUnknownMemZone._failable = false;

    CoreReserve = malloc(reserveSize);

    if (CoreReserve == NULL) {
      fprintf(stderr, "FATAL: cannot allocate initial core reserve of size %ld, giving up!\n",
              (unsigned long) reserveSize);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
