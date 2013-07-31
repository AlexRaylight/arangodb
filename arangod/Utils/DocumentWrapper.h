////////////////////////////////////////////////////////////////////////////////
/// @brief document wrapper utility functions
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
/// @author Jan Steemann
/// @author Copyright 2012-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_UTILS_DOCUMENT_WRAPPER_H
#define TRIAGENS_UTILS_DOCUMENT_WRAPPER_H 1

#include "BasicsC/json.h"
#include "Basics/JsonHelper.h"
#include "Logger/Logger.h"
#include "VocBase/document-collection.h"
#include "VocBase/primary-collection.h"
      
using namespace triagens::basics;

namespace triagens {
  namespace arango {


// -----------------------------------------------------------------------------
// --SECTION--                                              class DocumentHelper
// -----------------------------------------------------------------------------

    class DocumentWrapper {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

      private:

        DocumentWrapper (const DocumentWrapper&);
        DocumentWrapper& operator= (const DocumentWrapper&);
        
      public:

        DocumentWrapper (TRI_doc_mptr_t const* document, 
                         TRI_primary_collection_t* primary) :
          _json(0) {

          TRI_shaped_json_t shapedJson;

          TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->_data);
    
          // get document as json
          _json = TRI_JsonShapedJson(primary->_shaper, &shapedJson);
          _zone = primary->_shaper->_memoryZone;
        }

        ~DocumentWrapper () {
          if (_json) {
            TRI_FreeJson(_zone, _json);
          }
        }

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief returns true for arrays
////////////////////////////////////////////////////////////////////////////////
        
        bool isArrayDocument () {
          return JsonHelper::isArray(_json);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief returns then value of a boolean attribute or a default value
////////////////////////////////////////////////////////////////////////////////

        bool getBooleanValue (string const& name, bool defaultValue) {
          return JsonHelper::getBooleanValue(_json, name.c_str(), defaultValue);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief returns then value of a numeric attribute or a default value
////////////////////////////////////////////////////////////////////////////////

        template<typename T> T getNumericValue (string const& name, T defaultValue) {
          return JsonHelper::getNumericValue<T>(_json, name.c_str(), defaultValue);
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief returns then value of a string attribute or a default value
////////////////////////////////////////////////////////////////////////////////

        string getStringValue (string const& name, string const& defaultValue) {
          return JsonHelper::getStringValue(_json, name.c_str(), defaultValue);
        }
        
////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////
        
// -----------------------------------------------------------------------------
// --SECTION--                                                           private 
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

    private:
      
      TRI_json_t* _json;

      TRI_memory_zone_t* _zone;
      
////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

    };
  }
}

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
