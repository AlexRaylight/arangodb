////////////////////////////////////////////////////////////////////////////////
/// @brief AQL item block
///
/// @file 
///
/// DISCLAIMER
///
/// Copyright 2010-2014 triagens GmbH, Cologne, Germany
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_AQL_AQL_ITEM_BLOCK_H
#define ARANGODB_AQL_AQL_ITEM_BLOCK_H 1

#include "Basics/Common.h"
#include "Aql/AqlValue.h"
#include "Aql/Types.h"
#include "VocBase/document-collection.h"

namespace triagens {
  namespace aql {

// -----------------------------------------------------------------------------
// --SECTION--                                                      AqlItemBlock
// -----------------------------------------------------------------------------

// an <AqlItemBlock> is a <nrItems>x<nrRegs> vector of <AqlValue>s (not
// pointers). The size of an <AqlItemBlock> is the number of items.
// Entries in a given column (i.e. all the values of a given register
// for all items in the block) have the same type and belong to the
// same collection (if they are of type SHAPED). The document collection
// for a particular column is accessed via <getDocumentCollection>,
// and the entire array of document collections is accessed by
// <getDocumentCollections>. There is no access to an entire item, only
// access to particular registers of an item (via getValue).
//
// An AqlItemBlock is responsible to explicitly destroy all the
// <AqlValue>s it contains at destruction time. It is however allowed
// that multiple of the <AqlValue>s in it are pointing to identical
// structures, and thus must be destroyed only once for all identical
// copies. Furthermore, when parts of an AqlItemBlock are handed on
// to another AqlItemBlock, then the <AqlValue>s inside must be copied
// (deep copy) to make the blocks independent.

    class AqlItemBlock {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief create the block
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock (size_t nrItems, 
                      RegisterId nrRegs);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the block
////////////////////////////////////////////////////////////////////////////////

        ~AqlItemBlock ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief getValue, get the value of a register
////////////////////////////////////////////////////////////////////////////////

        AqlValue getValue (size_t index, RegisterId varNr) const {
          TRI_ASSERT(_data.capacity() > index * _nrRegs + varNr);
          return _data[index * _nrRegs + varNr];
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief setValue, set the current value of a register
////////////////////////////////////////////////////////////////////////////////

      void setValue (size_t index, RegisterId varNr, AqlValue value) {
        TRI_ASSERT(_data.capacity() > index * _nrRegs + varNr);
        TRI_ASSERT(_data.at(index * _nrRegs + varNr).isEmpty());

        // First update the reference count, if this fails, the value is empty
        auto it = _valueCount.find(value);
        if (it == _valueCount.end()) {
          _valueCount.insert(make_pair(value, 1));
        }
        else {
          it->second++;
        }

        _data[index * _nrRegs + varNr] = value;
      }

////////////////////////////////////////////////////////////////////////////////
/// @brief eraseValue, erase the current value of a register not freeing it
/// this is used if the value is stolen and later released from elsewhere
////////////////////////////////////////////////////////////////////////////////

        void eraseValue (size_t index, RegisterId varNr) {
          auto it = _valueCount.find(_data[index * _nrRegs + varNr]);
          _data[index * _nrRegs + varNr].erase();
          if (it != _valueCount.end()) {
            if (--it->second == 0) {
              _valueCount.erase(it);
            }
          }
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief eraseValue, erase the current value of a register not freeing it
/// this is used if the value is stolen and later released from elsewhere
////////////////////////////////////////////////////////////////////////////////

        void eraseAll () {
          size_t const n = _data.size();
          for (size_t i = 0; i < n; ++i) {
            _data[i].erase();
          }
          _valueCount.clear();
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief valueCount
/// this is used if the value is stolen and later released from elsewhere
////////////////////////////////////////////////////////////////////////////////

        uint32_t valueCount (AqlValue v) {
          auto it = _valueCount.find(v);
          if (it == _valueCount.end()) {
            return 0;
          }
          else {
            return it->second;
          }
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getDocumentCollection
////////////////////////////////////////////////////////////////////////////////

        inline TRI_document_collection_t const* getDocumentCollection (RegisterId varNr) const {
          return _docColls[varNr];
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief setDocumentCollection, set the current value of a variable or attribute
////////////////////////////////////////////////////////////////////////////////

        inline void setDocumentCollection (RegisterId varNr, TRI_document_collection_t const* docColl) {
          _docColls[varNr] = docColl;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _nrRegs
////////////////////////////////////////////////////////////////////////////////

        inline RegisterId getNrRegs () const {
          return _nrRegs;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _nrItems
////////////////////////////////////////////////////////////////////////////////

        inline size_t size () const {
          return _nrItems;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _data
////////////////////////////////////////////////////////////////////////////////

        inline vector<AqlValue>& getData () {
          return _data;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief getter for _docColls
////////////////////////////////////////////////////////////////////////////////

        vector<TRI_document_collection_t const*>& getDocumentCollections () {
          return _docColls;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief shrink the block to the specified number of rows
////////////////////////////////////////////////////////////////////////////////

        void shrink (size_t nrItems);

////////////////////////////////////////////////////////////////////////////////
/// @brief slice/clone, this does a deep copy of all entries
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* slice (size_t from, 
                             size_t to) const;

////////////////////////////////////////////////////////////////////////////////
/// @brief slice/clone chosen rows for a subset, this does a deep copy
/// of all entries
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* slice (std::vector<size_t>& chosen, 
                             size_t from, 
                             size_t to) const;

////////////////////////////////////////////////////////////////////////////////
/// @brief steal for a subset, this does not copy the entries, rather,
/// it remembers which it has taken. This is stored in the
/// this AqlItemBlock. It is highly recommended to delete it right
/// after this operation, because it is unclear, when the values
/// to which our AqlValues point will vanish.
////////////////////////////////////////////////////////////////////////////////

        AqlItemBlock* steal (vector<size_t>& chosen, size_t from, size_t to);

////////////////////////////////////////////////////////////////////////////////
/// @brief concatenate multiple blocks, note that the new block now owns all
/// AqlValue pointers in the old blocks, therefore, the latter are all
/// set to nullptr, just to be sure.
////////////////////////////////////////////////////////////////////////////////

        static AqlItemBlock* concatenate (std::vector<AqlItemBlock*> const& blocks);

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief _data, the actual data as a single vector of dimensions _nrItems
/// times _nrRegs
////////////////////////////////////////////////////////////////////////////////

        std::vector<AqlValue> _data;

////////////////////////////////////////////////////////////////////////////////
/// @brief _docColls, for every column a possible collection, which contains
/// all AqlValues of type SHAPED in this column.
////////////////////////////////////////////////////////////////////////////////

        std::vector<TRI_document_collection_t const*> _docColls;

////////////////////////////////////////////////////////////////////////////////
/// @brief _valueCount, since we have to allow for identical AqlValues
/// in an AqlItemBlock, this map keeps track over which AqlValues we
/// have in this AqlItemBlock and how often.
/// setValue above puts values in the map and increases the count if they 
/// are already there, eraseValue decreases the count. One can ask the
/// count with valueCount.
////////////////////////////////////////////////////////////////////////////////

        std::unordered_map<AqlValue, uint32_t> _valueCount;

////////////////////////////////////////////////////////////////////////////////
/// @brief _nrItems, number of rows
////////////////////////////////////////////////////////////////////////////////

        size_t     _nrItems;

////////////////////////////////////////////////////////////////////////////////
/// @brief _nrRegs, number of rows
////////////////////////////////////////////////////////////////////////////////

        RegisterId _nrRegs;

    };

  }  // namespace triagens::aql
}  // namespace triagens

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

