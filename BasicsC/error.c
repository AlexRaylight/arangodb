////////////////////////////////////////////////////////////////////////////////
/// @brief error handling
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

#ifndef TRI_GCC_THREAD_LOCAL_STORAGE
#ifdef TRI_HAVE_POSIX_THREADS
#include <pthread.h>
#endif
#endif

#include "BasicsC/strings.h"
#include "BasicsC/vector.h"
#include "BasicsC/associative.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                     private types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ErrorHandling
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief error number and system error
////////////////////////////////////////////////////////////////////////////////

typedef struct tri_error_s {
  int _number;
  int _sys;
}
tri_error_t;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ErrorHandling
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief already initialised
////////////////////////////////////////////////////////////////////////////////

static bool Initialised = false;

////////////////////////////////////////////////////////////////////////////////
/// @brief holds the last error
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_GCC_THREAD_LOCAL_STORAGE
static __thread tri_error_t ErrorNumber;
#elif defined(TRI_WIN32_THREAD_LOCAL_STORAGE)
static __declspec(thread) tri_error_t ErrorNumber;
#elif defined(TRI_HAVE_POSIX_THREADS)
static pthread_key_t ErrorKey;
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief the error messages
////////////////////////////////////////////////////////////////////////////////

static TRI_associative_pointer_t ErrorMessages;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ErrorHandling
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief cleanup
////////////////////////////////////////////////////////////////////////////////

#ifndef TRI_GCC_THREAD_LOCAL_STORAGE
#ifdef TRI_HAVE_POSIX_THREADS

static void CleanupError (void* ptr) {
  TRI_Free(ptr);
}

#endif
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief Hash function used to hash error codes (no real hash, uses the error 
/// code only)
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashErrorCode (TRI_associative_pointer_t* array, 
                               void const* key) {
  return (uint64_t) *((int*) key);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Hash function used to hash errors messages (not used)
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashError (TRI_associative_pointer_t* array, 
                           void const* element) {

  TRI_error_t* entry = (TRI_error_t*) element;
  return (uint64_t) entry->_code;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Comparison function used to determine error equality
////////////////////////////////////////////////////////////////////////////////

static bool EqualError (TRI_associative_pointer_t* array, 
                        void const* key, 
                        void const* element) {
  TRI_error_t* entry = (TRI_error_t*) element;

  return *((int*) key) == entry->_code;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ErrorHandling
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the last error
////////////////////////////////////////////////////////////////////////////////

#if defined(TRI_GCC_THREAD_LOCAL_STORAGE) || defined(TRI_WIN32_THREAD_LOCAL_STORAGE)

int TRI_errno () {
  return ErrorNumber._number;
}

#elif defined(TRI_HAVE_POSIX_THREADS)

int TRI_errno () {
  tri_error_t* eptr;

  eptr = pthread_getspecific(ErrorKey);

  if (eptr == NULL) {
    return 0;
  }
  else {
    return eptr->_number;
  }
}

#else
#error no TLS
#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the last error as string
////////////////////////////////////////////////////////////////////////////////

char const* TRI_last_error () {
  int err;
  int sys;
  TRI_error_t* entry;

#if defined(TRI_GCC_THREAD_LOCAL_STORAGE) || defined(TRI_WIN32_THREAD_LOCAL_STORAGE)

  err = ErrorNumber._number;
  sys = ErrorNumber._sys;

#elif defined(TRI_HAVE_POSIX_THREADS)

  tri_error_t* eptr;

  eptr = pthread_getspecific(ErrorKey);

  if (eptr == NULL) {
    err = 0;
    sys = 0;
  }
  else {
    err = eptr->_number;
    sys = eptr->_sys;
  }

#else
#error no TLS
#endif

  if (err == TRI_ERROR_SYS_ERROR) {
    return strerror(sys);
  }

  entry = (TRI_error_t*) 
    TRI_LookupByKeyAssociativePointer(&ErrorMessages, (void const*) &err); 
  if (!entry) {
    return "general error";
  }
  return entry->_message;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief sets the last error
////////////////////////////////////////////////////////////////////////////////

int TRI_set_errno (int error) {
#if defined(TRI_GCC_THREAD_LOCAL_STORAGE) || defined(TRI_WIN32_THREAD_LOCAL_STORAGE)

  ErrorNumber._number = error;

  if (error == TRI_ERROR_SYS_ERROR) {
    ErrorNumber._sys = errno;
  }
  else {
    ErrorNumber._sys = 0;
  }

#elif defined(TRI_HAVE_POSIX_THREADS)

  tri_error_t* eptr;
  int copyErrno = errno;

  eptr = pthread_getspecific(ErrorKey);

  if (eptr == NULL) {
    eptr = TRI_Allocate(sizeof(tri_error_t));
    pthread_setspecific(ErrorKey, eptr);
  }

  eptr->_number = error;

  if (error == TRI_ERROR_SYS_ERROR) {
    eptr->_sys = copyErrno;
  }
  else {
    eptr->_sys = 0;
  }

#else
#error no TLS
#endif

  return error;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief defines an error string
////////////////////////////////////////////////////////////////////////////////

void TRI_set_errno_string (int error, char const* msg) {
  TRI_error_t* entry;
  
  if (TRI_LookupByKeyAssociativePointer(&ErrorMessages, (void const*) &error)) {
    // logic error, error number is redeclared
    printf("Error: duplicate declaration of error code %i in %s:%i\n", 
           error, 
           __FILE__, 
           __LINE__);
    exit(EXIT_FAILURE);
  }

  entry = (TRI_error_t*) TRI_Allocate(sizeof(TRI_error_t));
  if (entry) {
    entry->_code = error;
    entry->_message = TRI_DuplicateString(msg);
    TRI_InsertKeyAssociativePointer(&ErrorMessages,
                                    &error, 
                                    entry, 
                                    false);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                            MODULE
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ErrorHandling
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises the error messages
////////////////////////////////////////////////////////////////////////////////

void TRI_InitialiseError () {
  if (Initialised) {
    return;
  }

  TRI_InitAssociativePointer(&ErrorMessages,
                             HashErrorCode,
                             HashError,
                             EqualError,
                             0);

  TRI_set_errno_string(0, "no error");
  TRI_set_errno_string(1, "failed");
  TRI_set_errno_string(2, "system error");
  TRI_set_errno_string(3, "illegal number");
  TRI_set_errno_string(4, "numeric overflow");
  TRI_set_errno_string(5, "illegal option");
  TRI_set_errno_string(6, "dead process identifier");
  TRI_set_errno_string(7, "unlocked file");

#if defined(TRI_GCC_THREAD_LOCAL_STORAGE) || defined(TRI_WIN32_THREAD_LOCAL_STORAGE)
  ErrorNumber._number = 0;
  ErrorNumber._sys = 0;
#elif defined(TRI_HAVE_POSIX_THREADS)
  pthread_key_create(&ErrorKey, CleanupError);
#else
#error no TLS
#endif

  Initialised = true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief shuts down the error messages
////////////////////////////////////////////////////////////////////////////////

void TRI_ShutdownError () {
  size_t i;

  if (! Initialised) {
    return;
  }

  for (i = 0; i < ErrorMessages._nrAlloc; i++) {
    TRI_error_t* entry = ErrorMessages._table[i];
    if (entry) {
      if (entry->_message) {
        TRI_Free(entry->_message);
      }
      TRI_Free(entry);
    }
  }

  TRI_DestroyAssociativePointer(&ErrorMessages);

  Initialised = false;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
