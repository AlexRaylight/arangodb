////////////////////////////////////////////////////////////////////////////////
/// @brief json helper functions
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

#include "Basics/JsonHelper.h"

#include "BasicsC/string-buffer.h"

using namespace triagens::basics;

// -----------------------------------------------------------------------------
// --SECTION--                                                  class JsonHelper
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                             public static methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a JSON object from a list of strings
////////////////////////////////////////////////////////////////////////////////

TRI_json_t* JsonHelper::stringList (TRI_memory_zone_t* zone,
                                    std::vector<std::string> const& values) {
  TRI_json_t* json = TRI_CreateList2Json(zone, values.size());

  if (json == 0) {
    return 0;
  }

  for (size_t i = 0, n = values.size(); i < n; ++i) {
    TRI_json_t* v = TRI_CreateString2CopyJson(zone, values[i].c_str(), values[i].size());
    if (v != 0) {
      TRI_PushBack3ListJson(zone, json, v);
    }
  }

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a list of strings from a JSON (sub-) object
////////////////////////////////////////////////////////////////////////////////

std::vector<std::string> JsonHelper::stringList (TRI_json_t const* json) {
  std::vector<std::string> result;

  if (isList(json)) {
    for (size_t i = 0, n = json->_value._objects._length; i < n; ++i) {
      TRI_json_t const* v = (TRI_json_t const*) TRI_AtVector(&json->_value._objects, i);

      if (isString(v)) {
        result.push_back(std::string(v->_value._string.data, v->_value._string.length - 1));
      }
    }
  }

  return result;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create JSON from string
////////////////////////////////////////////////////////////////////////////////
        
TRI_json_t* JsonHelper::fromString (std::string const& data) {
  TRI_json_t* json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, data.c_str());

  return json;
}
      
////////////////////////////////////////////////////////////////////////////////
/// @brief stringify json
////////////////////////////////////////////////////////////////////////////////
        
std::string JsonHelper::toString (TRI_json_t const* json) {
  TRI_string_buffer_t buffer;

  TRI_InitStringBuffer(&buffer, TRI_UNKNOWN_MEM_ZONE);
  int res = TRI_StringifyJson(&buffer, json);

  if (res != TRI_ERROR_NO_ERROR) {
    return "";
  }

  string out(TRI_BeginStringBuffer(&buffer), TRI_LengthStringBuffer(&buffer));
  TRI_DestroyStringBuffer(&buffer);

  return out;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns an array sub-element
////////////////////////////////////////////////////////////////////////////////
        
TRI_json_t* JsonHelper::getArrayElement (TRI_json_t const* json, 
                                         const char* name) {
  if (! isArray(json)) {
    return 0;
  }

  return TRI_LookupArrayJson(json, name);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a string element, or a default it is does not exist
////////////////////////////////////////////////////////////////////////////////
        
std::string JsonHelper::getStringValue (TRI_json_t const* json, 
                                        const std::string& defaultValue) {
  if (isString(json)) {
    return string(json->_value._string.data, json->_value._string.length - 1);
  }
  return defaultValue;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a string sub-element, or a default it is does not exist
////////////////////////////////////////////////////////////////////////////////
        
std::string JsonHelper::getStringValue (TRI_json_t const* json, 
                                        const char* name, 
                                        const std::string& defaultValue) {
  TRI_json_t const* sub = getArrayElement(json, name);

  if (isString(sub)) {
    return string(sub->_value._string.data, sub->_value._string.length - 1);
  }
  return defaultValue;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a boolean sub-element, or a default it is does not exist
////////////////////////////////////////////////////////////////////////////////
        
bool JsonHelper::getBooleanValue (TRI_json_t const* json, 
                                  const char* name, 
                                  bool defaultValue) {
  TRI_json_t const* sub = getArrayElement(json, name);

  if (isBoolean(sub)) {
    return sub->_value._boolean;
  }

  return defaultValue;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////
        
// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
