////////////////////////////////////////////////////////////////////////////////
/// @brief generic skip list implementation
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, ArangoDB GmbH, Cologne, Germany
/// @author Copyright 2013-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_BASICS_C_SKIP__LIST_H
#define ARANGODB_BASICS_C_SKIP__LIST_H 1

#include "Basics/Common.h"

// We will probably never see more than 2^48 documents in a skip list
#define TRI_SKIPLIST_MAX_HEIGHT 48

// -----------------------------------------------------------------------------
// --SECTION--                                                         SKIP LIST
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief type of a skiplist node
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_skiplist_node_s {
    struct TRI_skiplist_node_s** next;
    struct TRI_skiplist_node_s* prev;
    void *doc;
    int height;
} TRI_skiplist_node_t;

////////////////////////////////////////////////////////////////////////////////
/// @brief two possibilities for comparison, see below
////////////////////////////////////////////////////////////////////////////////

typedef enum {
  TRI_CMP_PREORDER,
  TRI_CMP_TOTORDER
}
TRI_cmp_type_e;

////////////////////////////////////////////////////////////////////////////////
/// @brief type of a function pointer to a comparison function for a skiplist.
///
/// The last argument is called preorder. If true then the comparison
/// function must implement a preorder (reflexive and transitive).
/// If preorder is false, then a proper total order must be
/// implemented by the comparison function. The proper total order
/// must refine the preorder in the sense that a < b in the proper order
/// implies a <= b in the preorder.
/// The first argument is a data pointer which may contain arbitrary
/// infrastructure needed for the comparison. See cmpdata field in the
/// skiplist type.
/// The cmp_key_elm variant compares a key with an element using the preorder.
/// The first argument is a data pointer as above, the second is a pointer
/// to the key and the third is a pointer to an element.
////////////////////////////////////////////////////////////////////////////////

typedef int (*TRI_skiplist_cmp_elm_elm_t)(void*, void*, void*, TRI_cmp_type_e);
typedef int (*TRI_skiplist_cmp_key_elm_t)(void*, void*, void*);

////////////////////////////////////////////////////////////////////////////////
/// @brief Type of a pointer to a function that is called whenever a
/// document is removed from a skiplist.
////////////////////////////////////////////////////////////////////////////////

typedef void (*TRI_skiplist_free_func_t)(void*);

////////////////////////////////////////////////////////////////////////////////
/// @brief type of a skiplist
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_skiplist_s {
    TRI_skiplist_node_t* start;
    TRI_skiplist_node_t* end;
    TRI_skiplist_cmp_elm_elm_t cmp_elm_elm;
    TRI_skiplist_cmp_key_elm_t cmp_key_elm;
    void* cmpdata;   // will be the first argument
    TRI_skiplist_free_func_t free;
    bool unique;     // indicates whether multiple entries that
                     // are equal in the preorder are allowed in
    uint64_t nrUsed;
    size_t _memoryUsed;
} TRI_skiplist_t;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new skiplist
///
/// Returns NULL if allocation fails and a pointer to the skiplist
/// otherwise.
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_t* TRI_InitSkipList (TRI_skiplist_cmp_elm_elm_t cmp_elm_elm,
                                  TRI_skiplist_cmp_key_elm_t cmp_key_elm,
                                  void *cmpdata,
                                  TRI_skiplist_free_func_t freefunc,
                                  bool unique);

////////////////////////////////////////////////////////////////////////////////
/// @brief frees a skiplist and all its documents
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeSkipList (TRI_skiplist_t* sl);

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief return the start node
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListStartNode (TRI_skiplist_t* sl);

////////////////////////////////////////////////////////////////////////////////
/// @brief return the end node
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListEndNode (TRI_skiplist_t* sl);

////////////////////////////////////////////////////////////////////////////////
/// @brief return the successor node or NULL if last node
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListNextNode (TRI_skiplist_node_t* node);

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a new document into a skiplist
///
/// Comparison is done using proper order comparison. If the skiplist
/// is unique then no two documents that compare equal in the preorder
/// can be inserted. Returns 0 if all is well, -1 if allocation failed
/// and -2 if the unique constraint would have been violated by the
/// insert. In the latter two cases nothing is inserted.
////////////////////////////////////////////////////////////////////////////////

int TRI_SkipListInsert (TRI_skiplist_t *sl, void *doc);

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a skiplist
///
/// Comparison is done using proper order comparison. Returns 0 if all
/// is well and -1 if the document was not found. In the latter two
/// cases nothing is inserted.
////////////////////////////////////////////////////////////////////////////////

int TRI_SkipListRemove (TRI_skiplist_t *sl, void *doc);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the number of entries in the skiplist.
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_SkipListGetNrUsed (TRI_skiplist_t const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the memory used by the index
////////////////////////////////////////////////////////////////////////////////

size_t TRI_SkipListMemoryUsage (TRI_skiplist_t const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up doc in the skiplist using the proper order
/// comparison.
///
/// Only comparisons using the proper order are done using cmp_elm_elm.
/// Returns NULL if doc is not in the skiplist.
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListLookup (TRI_skiplist_t *sl, void *doc);

////////////////////////////////////////////////////////////////////////////////
/// @brief finds the last document that is less to doc in the preorder
/// comparison or the start node if none is.
///
/// Only comparisons using the preorder are done using cmp_elm_elm.
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListLeftLookup (TRI_skiplist_t *sl, void *doc);

////////////////////////////////////////////////////////////////////////////////
/// @brief finds the last document that is less or equal to doc in
/// the preorder comparison or the start node if none is.
///
/// Only comparisons using the preorder are done using cmp_elm_elm.
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListRightLookup (TRI_skiplist_t *sl, void *doc);

////////////////////////////////////////////////////////////////////////////////
/// @brief finds the last document whose key is less to key in the preorder
/// comparison or the start node if none is.
///
/// Only comparisons using the preorder are done using cmp_key_elm.
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListLeftKeyLookup (TRI_skiplist_t *sl, void *key);

////////////////////////////////////////////////////////////////////////////////
/// @brief finds the last document that is less or equal to doc in
/// the preorder comparison or the start node if none is.
///
/// Only comparisons using the preorder are done using cmp_key_elm.
////////////////////////////////////////////////////////////////////////////////

TRI_skiplist_node_t* TRI_SkipListRightKeyLookup (TRI_skiplist_t *sl, void *key);

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
