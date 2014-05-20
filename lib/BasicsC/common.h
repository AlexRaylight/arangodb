////////////////////////////////////////////////////////////////////////////////
/// @brief High-Performance Database Framework made by triagens
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
/// @author Dr. Frank Celler
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_BASICS_C_COMMON_H
#define TRIAGENS_BASICS_C_COMMON_H 1

// -----------------------------------------------------------------------------
// --SECTION--                                             configuration options
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Configuration
/// @{
////////////////////////////////////////////////////////////////////////////////

#define TRI_WITHIN_COMMON 1
#include "BasicsC/operating-system.h"
#ifdef _WIN32
#include "BasicsC/local-configuration-win.h"
#else
#include "BasicsC/local-configuration.h"
#endif
#include "BasicsC/application-exit.h"

#include "build.h"

#ifdef _DEBUG
#define TRI_VERSION_FULL TRI_VERSION " [" TRI_PLATFORM "-DEBUG]"
#else
#define TRI_VERSION_FULL TRI_VERSION " [" TRI_PLATFORM "]"
#endif

#undef TRI_WITHIN_COMMON

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    C header files
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Configuration
/// @{
////////////////////////////////////////////////////////////////////////////////

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef TRI_HAVE_PROCESS_H
#include <process.h>
#endif

#ifdef TRI_HAVE_SIGNAL_H
#include <signal.h>
#endif

#ifdef TRI_HAVE_STDBOOL_H
#include <stdbool.h>
#endif

#ifdef TRI_HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef TRI_HAVE_STRINGS_H
#include <strings.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>

#ifdef TRI_HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

#ifdef TRI_HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

// .............................................................................
// The problem we have for visual studio is that if we include WinSock2.h here
// it may conflict later in some other source file. The conflict arises when
// windows.h is included BEFORE WinSock2.h -- this is a visual studio issue. For
// now be VERY careful to ensure that if you need windows.h, then you include
// this file AFTER common.h.
// .............................................................................

#ifdef TRI_HAVE_WINSOCK2_H
#include <WinSock2.h>
typedef long suseconds_t;
#endif

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                            basic triAGENS headers
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Configuration
/// @{
////////////////////////////////////////////////////////////////////////////////

#define TRI_WITHIN_COMMON 1
#include "BasicsC/voc-errors.h"
#include "BasicsC/error.h"
#include "BasicsC/debugging.h"
#include "BasicsC/memory.h"
#include "BasicsC/mimetypes.h"
#include "BasicsC/structures.h"
#undef TRI_WITHIN_COMMON

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                              basic compiler stuff
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Configuration
/// @{
////////////////////////////////////////////////////////////////////////////////

#define TRI_WITHIN_COMMON 1
#include "BasicsC/system-compiler.h"
#include "BasicsC/system-functions.h"
#undef TRI_WITHIN_COMMON

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 low level helpers
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Configuration
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief const cast for C
////////////////////////////////////////////////////////////////////////////////

static inline void* CONST_CAST (void const* ptr) {
  union { void* p; void const* c; } cnv;

  cnv.c = ptr;
  return cnv.p;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief incrementing a uint64_t modulo a number with wraparound
////////////////////////////////////////////////////////////////////////////////

static inline uint64_t TRI_IncModU64(uint64_t i, uint64_t len) {
  // Note that the dummy variable gives the compiler a (good) chance to
  // use a conditional move instruction instead of a branch. This actually
  // works on modern gcc.
  uint64_t dummy;
  dummy = (++i) - len;
  return i < len ? i : dummy;
}

static inline uint64_t TRI_DecModU64(uint64_t i, uint64_t len) {
  if ((i--) != 0) {
    return i;
  }
  return len-1;
}

// The following two possibilities are equivalent, but seem to produce 
// a branch instruction in the assembler code rather than a conditional move:

#if 0
static inline uint64_t TRI_IncModU64(uint64_t i, uint64_t len) {
  if ((++i) == len) {
    return 0;
  }
  return i;
}
#endif

#if 0
static inline uint64_t TRI_IncModU64(uint64_t i, uint64_t len) {
  return (++i) == len ? 0 : i;
}
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief mark a pointer by setting its least significant bit
////////////////////////////////////////////////////////////////////////////////

static inline void* TRI_MarkPointer (void* p) {
  return (void*) ((uintptr_t) p | (uintptr_t) 1);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether or not a pointer is marked in its least significant bit
////////////////////////////////////////////////////////////////////////////////

static inline bool TRI_IsPointerMarked (void* p) {
  return ((uintptr_t) p | (uintptr_t) 1) != 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief strip the mark of a pointer
////////////////////////////////////////////////////////////////////////////////

static inline void* TRI_StripMarkPointer (void const* p) {
  return (void*) ((uintptr_t) p & ~((uintptr_t)1));
}


////////////////////////////////////////////////////////////////////////////////
/// @brief a wrapper for assert()
///
/// This wrapper maps TRI_ASSERT_MAINTAINER() to (void) 0 for non-maintainers. 
/// It maps TRI_ASSERT_MAINTAINER() to assert() when TRI_ENABLE_MAINTAINER_MODE 
/// is set.
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_MAINTAINER_MODE

#define TRI_ASSERT_MAINTAINER(what) assert(what)

#else

#ifdef __cplusplus

// TODO: change back when releasing
//#define TRI_ASSERT_MAINTAINER(what) (static_cast<void> (0))
#define TRI_ASSERT_MAINTAINER(what) assert(what)

#else

// TODO: change back when releasing
//#define TRI_ASSERT_MAINTAINER(what) ((void) (0))
#define TRI_ASSERT_MAINTAINER(what) assert(what)

#endif

#endif

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
