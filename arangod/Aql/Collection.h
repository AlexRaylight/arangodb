////////////////////////////////////////////////////////////////////////////////
/// @brief Aql, collection
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
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_COLLECTION_H
#define ARANGODB_AQL_COLLECTION_H 1

#include "Basics/Common.h"
#include "VocBase/document-collection.h"
#include "VocBase/transaction.h"
#include "VocBase/vocbase.h"

namespace triagens {
  namespace aql {

// -----------------------------------------------------------------------------
// --SECTION--                                                 struct Collection
// -----------------------------------------------------------------------------

    struct Collection {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      Collection& operator= (Collection const&) = delete;
      Collection (Collection const&) = delete;
      Collection () = delete;
      
      Collection (std::string const& name,
                  struct TRI_vocbase_s* vocbase,
                  TRI_transaction_type_e accessType) 
        : name(name),
          vocbase(vocbase),
          collection(nullptr),
          accessType(accessType) {
      }
      
      ~Collection () {
      }

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief get the collection id
////////////////////////////////////////////////////////////////////////////////

      inline TRI_voc_cid_t cid () const {
        TRI_ASSERT(collection != nullptr);
        return collection->_cid;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief get the pointer to the document collection
////////////////////////////////////////////////////////////////////////////////
        
      inline TRI_document_collection_t* documentCollection () const {
        TRI_ASSERT(collection != nullptr);
        TRI_ASSERT(collection->_collection != nullptr);

        return collection->_collection;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief count the LOCAL number of documents in the collection
/// TODO: must be adjusted for clusters
////////////////////////////////////////////////////////////////////////////////

      size_t count () {
        if (numDocuments == UNINITIALIZED) {
          auto document = documentCollection();
          // cache the result
          numDocuments = static_cast<int64_t>(document->size(document));
        }

        return static_cast<size_t>(numDocuments);
      }

// -----------------------------------------------------------------------------
// --SECTION--                                                  public variables
// -----------------------------------------------------------------------------

      std::string const       name;
      TRI_vocbase_t*          vocbase;
      TRI_vocbase_col_t*      collection;
      TRI_transaction_type_e  accessType;
      int64_t                 numDocuments = UNINITIALIZED;

      static int64_t const    UNINITIALIZED = -1;
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
