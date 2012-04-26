////////////////////////////////////////////////////////////////////////////////
/// @brief V8 utility functions
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

#include "v8-conv.h"

#include "Basics/StringUtils.h"
#include "BasicsC/conversions.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/strings.h"
#include "ShapedJson/shaped-json.h"
#include "VocBase/vocbase.h"

#include "V8/v8-json.h"
#include "V8/v8-utils.h"

using namespace std;
using namespace triagens::basics;

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

static bool FillShapeValueJson (TRI_shaper_t* shaper,
                                TRI_shape_value_t* dst,
                                v8::Handle<v8::Value> json,
                                set<int>& seenHashes,
                                vector< v8::Handle<v8::Object> >& seenObjects);

static v8::Handle<v8::Value> JsonShapeData (TRI_shaper_t* shaper,
                                            TRI_shape_t const* shape,
                                            char const* data,
                                            size_t size);

// -----------------------------------------------------------------------------
// --SECTION--                                              CONVERSION FUNCTIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Conversions
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a null into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueNull (TRI_shaper_t* shaper, TRI_shape_value_t* dst) {
  dst->_type = TRI_SHAPE_NULL;
  dst->_sid = shaper->_sidNull;
  dst->_fixedSized = true;
  dst->_size = 0;
  dst->_value = 0;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a boolean into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueBoolean (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::Boolean> json) {
  TRI_shape_boolean_t* ptr;

  dst->_type = TRI_SHAPE_BOOLEAN;
  dst->_sid = shaper->_sidBoolean;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_boolean_t);
  dst->_value = (char*)(ptr = (TRI_shape_boolean_t*) TRI_Allocate(shaper->_memoryZone, dst->_size));

  if (dst->_value == NULL) {
    return false;
  }

  *ptr = json->Value() ? 1 : 0;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a boolean into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueBoolean (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::BooleanObject> json) {
  TRI_shape_boolean_t* ptr;

  dst->_type = TRI_SHAPE_BOOLEAN;
  dst->_sid = shaper->_sidBoolean;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_boolean_t);
  dst->_value = (char*)(ptr = (TRI_shape_boolean_t*) TRI_Allocate(shaper->_memoryZone, dst->_size));

  if (dst->_value == NULL) {
    return false;
  }

  *ptr = json->BooleanValue() ? 1 : 0;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a number into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueNumber (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::Number> json) {
  TRI_shape_number_t* ptr;

  dst->_type = TRI_SHAPE_NUMBER;
  dst->_sid = shaper->_sidNumber;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_number_t);
  dst->_value = (char*)(ptr = (TRI_shape_number_t*) TRI_Allocate(shaper->_memoryZone, dst->_size));

  if (dst->_value == NULL) {
    return false;
  }

  *ptr = json->Value();

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a number into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueNumber (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::NumberObject> json) {
  TRI_shape_number_t* ptr;

  dst->_type = TRI_SHAPE_NUMBER;
  dst->_sid = shaper->_sidNumber;
  dst->_fixedSized = true;
  dst->_size = sizeof(TRI_shape_number_t);
  dst->_value = (char*)(ptr = (TRI_shape_number_t*) TRI_Allocate(shaper->_memoryZone, dst->_size));

  if (dst->_value == NULL) {
    return false;
  }

  *ptr = json->NumberValue();

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a string into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueString (TRI_shaper_t* shaper, TRI_shape_value_t* dst, v8::Handle<v8::String> json) {
  char* ptr;

  v8::String::Utf8Value str(json);

  if (*str == 0) {
    dst->_type = TRI_SHAPE_SHORT_STRING;
    dst->_sid = shaper->_sidShortString;
    dst->_fixedSized = true;
    dst->_size = sizeof(TRI_shape_length_short_string_t) + TRI_SHAPE_SHORT_STRING_CUT;
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == NULL) {
      return false;
    }

    * ((TRI_shape_length_short_string_t*) ptr) = 1;
    * (ptr + sizeof(TRI_shape_length_short_string_t)) = '\0';
  }
  else if (str.length() < TRI_SHAPE_SHORT_STRING_CUT) { // includes '\0'
    size_t size = str.length() + 1;

    dst->_type = TRI_SHAPE_SHORT_STRING;
    dst->_sid = shaper->_sidShortString;
    dst->_fixedSized = true;
    dst->_size = sizeof(TRI_shape_length_short_string_t) + TRI_SHAPE_SHORT_STRING_CUT;
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == NULL) {
      return false;
    }

    * ((TRI_shape_length_short_string_t*) ptr) = size;

    memcpy(ptr + sizeof(TRI_shape_length_short_string_t), *str, size);
  }
  else {
    size_t size = str.length() + 1;

    dst->_type = TRI_SHAPE_LONG_STRING;
    dst->_sid = shaper->_sidLongString;
    dst->_fixedSized = false;
    dst->_size = sizeof(TRI_shape_length_long_string_t) + size;
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == NULL) {
      return false;
    }

    * ((TRI_shape_length_long_string_t*) ptr) = size;

    memcpy(ptr + sizeof(TRI_shape_length_long_string_t), *str, size);
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json list into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueList (TRI_shaper_t* shaper,
                                TRI_shape_value_t* dst,
                                v8::Handle<v8::Array> json,
                                set<int>& seenHashes,
                                vector< v8::Handle<v8::Object> >& seenObjects) {
  size_t i;
  size_t n;
  size_t total;

  TRI_shape_value_t* values;
  TRI_shape_value_t* p;
  TRI_shape_value_t* e;

  bool hs;
  bool hl;

  TRI_shape_sid_t s;
  TRI_shape_sid_t l;

  TRI_shape_sid_t* sids;
  TRI_shape_size_t* offsets;
  TRI_shape_size_t offset;

  TRI_shape_t const* found;

  char* ptr;

  // check for special case "empty list"
  n = json->Length();

  if (n == 0) {
    dst->_type = TRI_SHAPE_LIST;
    dst->_sid = shaper->_sidList;

    dst->_fixedSized = false;
    dst->_size = sizeof(TRI_shape_length_list_t);
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == NULL) {
      return false;
    }

    * (TRI_shape_length_list_t*) ptr = 0;

    return true;
  }

  // convert into TRI_shape_value_t array
  p = (values = (TRI_shape_value_t*) TRI_Allocate(shaper->_memoryZone, sizeof(TRI_shape_value_t) * n));

  if (p == NULL) {
    return false;
  }

  memset(values, 0, sizeof(TRI_shape_value_t) * n);

  total = 0;
  e = values + n;

  for (i = 0;  i < n;  ++i, ++p) {
    v8::Local<v8::Value> el = json->Get(i);
    bool ok = FillShapeValueJson(shaper, p, el, seenHashes, seenObjects);

    if (! ok) {
      for (e = p, p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      return false;
    }

    total += p->_size;
  }

  // check if this list is homoegenous
  hs = true;
  hl = true;

  s = values[0]._sid;
  l = values[0]._size;
  p = values;

  for (;  p < e;  ++p) {
    if (p->_sid != s) {
      hs = false;
      break;
    }

    if (p->_size != l) {
      hl = false;
    }
  }

  // homogeneous sized
  if (hs && hl) {
    TRI_homogeneous_sized_list_shape_t* shape;

    shape = (TRI_homogeneous_sized_list_shape_t*) TRI_Allocate(shaper->_memoryZone, sizeof(TRI_homogeneous_sized_list_shape_t));

    if (shape == NULL) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);

      return false;
    }

    shape->base._size = sizeof(TRI_homogeneous_sized_list_shape_t);
    shape->base._type = TRI_SHAPE_HOMOGENEOUS_SIZED_LIST;
    shape->base._dataSize = TRI_SHAPE_SIZE_VARIABLE;
    shape->_sidEntry = s;
    shape->_sizeEntry = l;

    found = shaper->findShape(shaper, &shape->base);

    if (found == 0) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      TRI_Free(shaper->_memoryZone, shape);

      return false;
    }

    dst->_type = found->_type;
    dst->_sid = found->_sid;

    dst->_fixedSized = false;
    dst->_size = sizeof(TRI_shape_length_list_t) + total;
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == NULL) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      TRI_Free(shaper->_memoryZone, shape);

      return false;
    }

    // copy sub-objects into data space
    * (TRI_shape_length_list_t*) ptr = n;
    ptr += sizeof(TRI_shape_length_list_t);

    for (p = values;  p < e;  ++p) {
      memcpy(ptr, p->_value, p->_size);
      ptr += p->_size;
    }
  }

  // homogeneous
  else if (hs) {
    TRI_homogeneous_list_shape_t* shape;

    shape = (TRI_homogeneous_list_shape_t*) TRI_Allocate(shaper->_memoryZone, sizeof(TRI_homogeneous_list_shape_t));

    if (shape == NULL) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);

      return false;
    }

    shape->base._size = sizeof(TRI_homogeneous_list_shape_t);
    shape->base._type = TRI_SHAPE_HOMOGENEOUS_LIST;
    shape->base._dataSize = TRI_SHAPE_SIZE_VARIABLE;
    shape->_sidEntry = s;

    found = shaper->findShape(shaper, &shape->base);

    if (found == 0) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      TRI_Free(shaper->_memoryZone, shape);

      return false;
    }

    dst->_type = found->_type;
    dst->_sid = found->_sid;

    offset = sizeof(TRI_shape_length_list_t) + (n + 1) * sizeof(TRI_shape_size_t);

    dst->_fixedSized = false;
    dst->_size = offset + total;
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == 0) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      TRI_Free(shaper->_memoryZone, shape);

      return false;
    }

    // copy sub-objects into data space
    * (TRI_shape_length_list_t*) ptr = n;
    ptr += sizeof(TRI_shape_length_list_t);

    offsets = (TRI_shape_size_t*) ptr;
    ptr += (n + 1) * sizeof(TRI_shape_size_t);

    for (p = values;  p < e;  ++p) {
      *offsets++ = offset;
      offset += p->_size;

      memcpy(ptr, p->_value, p->_size);
      ptr += p->_size;
    }

    *offsets = offset;
  }

  // in-homogeneous
  else {
    dst->_type = TRI_SHAPE_LIST;
    dst->_sid = shaper->_sidList;

    offset =
      sizeof(TRI_shape_length_list_t)
      + n * sizeof(TRI_shape_sid_t)
      + (n + 1) * sizeof(TRI_shape_size_t);

    dst->_fixedSized = false;
    dst->_size = offset + total;
    dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

    if (dst->_value == NULL) {
      for (p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      return false;
    }

    // copy sub-objects into data space
    * (TRI_shape_length_list_t*) ptr = n;
    ptr += sizeof(TRI_shape_length_list_t);

    sids = (TRI_shape_sid_t*) ptr;
    ptr += n * sizeof(TRI_shape_sid_t);

    offsets = (TRI_shape_size_t*) ptr;
    ptr += (n + 1) * sizeof(TRI_shape_size_t);

    for (p = values;  p < e;  ++p) {
      *sids++ = p->_sid;

      *offsets++ = offset;
      offset += p->_size;

      memcpy(ptr, p->_value, p->_size);
      ptr += p->_size;
    }

    *offsets = offset;
  }

  // free TRI_shape_value_t array
  for (p = values;  p < e;  ++p) {
    if (p->_value != 0) {
      TRI_Free(shaper->_memoryZone, p->_value);
    }
  }

  TRI_Free(shaper->_memoryZone, values);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json array into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueArray (TRI_shaper_t* shaper,
                                 TRI_shape_value_t* dst,
                                 v8::Handle<v8::Object> json,
                                 set<int>& seenHashes,
                                 vector< v8::Handle<v8::Object> >& seenObjects) {
  size_t n;
  size_t i;
  size_t total;

  size_t f;
  size_t v;

  TRI_shape_value_t* values;
  TRI_shape_value_t* p;
  TRI_shape_value_t* e;

  TRI_array_shape_t* a;

  TRI_shape_sid_t* sids;
  TRI_shape_aid_t* aids;
  TRI_shape_size_t* offsetsF;
  TRI_shape_size_t* offsetsV;
  TRI_shape_size_t offset;

  TRI_shape_t const* found;

  char* ptr;

  // number of attributes
  v8::Handle<v8::Array> names = json->GetOwnPropertyNames();
  n = names->Length();

  // convert into TRI_shape_value_t array
  p = (values = (TRI_shape_value_t*) TRI_Allocate(shaper->_memoryZone, n * sizeof(TRI_shape_value_t)));

  if (p == NULL) {
    return false;
  }

  memset(values, 0, n * sizeof(TRI_shape_value_t));

  total = 0;
  f = 0;
  v = 0;

  for (i = 0;  i < n;  ++i, ++p) {
    v8::Handle<v8::Value> key = names->Get(i);
    v8::Handle<v8::Value> val = json->Get(key);
    bool ok;

    // first find an identifier for the name
    v8::String::Utf8Value keyStr(key);

    if (*keyStr == 0) {
      --p;
      continue;
    }

    if ((*keyStr)[0] == '_') {
      --p;
      continue;
    }

    p->_aid = shaper->findAttributeName(shaper, *keyStr);

    // convert value
    if (p->_aid == 0) {
      ok = false;
    }
    else {
      ok = FillShapeValueJson(shaper, p, val, seenHashes, seenObjects);
    }

    if (! ok) {
      for (e = p, p = values;  p < e;  ++p) {
        if (p->_value != 0) {
          TRI_Free(shaper->_memoryZone, p->_value);
        }
      }

      TRI_Free(shaper->_memoryZone, values);
      return false;
    }

    total += p->_size;

    // count fixed and variable sized values
    if (p->_fixedSized) {
      ++f;
    }
    else {
      ++v;
    }
  }

  n = f + v;

  // add variable offset table size
  total += (v + 1) * sizeof(TRI_shape_size_t);

  // now sort the shape entries
  TRI_SortShapeValues(values, n);

#ifdef DEBUG_JSON_SHAPER
  printf("shape values\n------------\ntotal: %u, fixed: %u, variable: %u\n",
         (unsigned int) n,
         (unsigned int) f,
         (unsigned int) v);
  PrintShapeValues(values, n);
  printf("\n");
#endif

  // generate shape structure
  i =
    sizeof(TRI_array_shape_t)
    + n * sizeof(TRI_shape_sid_t)
    + n * sizeof(TRI_shape_aid_t)
    + (f + 1) * sizeof(TRI_shape_size_t);

  a = (TRI_array_shape_t*) (ptr = (char*) TRI_Allocate(shaper->_memoryZone, i));

  if (ptr == NULL) {
    for (p = values;  p < e;  ++p) {
      if (p->_value != NULL) {
        TRI_Free(shaper->_memoryZone, p->_value);
      }
    }

    TRI_Free(shaper->_memoryZone, values);

    return false;
  }

  memset(ptr, 0, i);

  a->base._type = TRI_SHAPE_ARRAY;
  a->base._size = i;
  a->base._dataSize = (v == 0) ? total : TRI_SHAPE_SIZE_VARIABLE;

  a->_fixedEntries = f;
  a->_variableEntries = v;

  ptr += sizeof(TRI_array_shape_t);

  // array of shape identifiers
  sids = (TRI_shape_sid_t*) ptr;
  ptr += n * sizeof(TRI_shape_sid_t);

  // array of attribute identifiers
  aids = (TRI_shape_aid_t*) ptr;
  ptr += n * sizeof(TRI_shape_aid_t);

  // array of offsets for fixed part (within the shape)
  offset = (v + 1) * sizeof(TRI_shape_size_t);
  offsetsF = (TRI_shape_size_t*) ptr;

  // fill destination (except sid)
  dst->_type = TRI_SHAPE_ARRAY;

  dst->_fixedSized = true;
  dst->_size = total;
  dst->_value = (ptr = (char*) TRI_Allocate(shaper->_memoryZone, dst->_size));

  if (ptr == NULL) {
    for (p = values;  p < e;  ++p) {
      if (p->_value != NULL) {
        TRI_Free(shaper->_memoryZone, p->_value);
      }
    }

    TRI_Free(shaper->_memoryZone, values);
    TRI_Free(shaper->_memoryZone, a);

    return false;
  }

  // array of offsets for variable part (within the value)
  offsetsV = (TRI_shape_size_t*) ptr;
  ptr += (v + 1) * sizeof(TRI_shape_size_t);

  // and fill in attributes
  e = values + n;

  for (p = values;  p < e;  ++p) {
    *aids++ = p->_aid;
    *sids++ = p->_sid;

    memcpy(ptr, p->_value, p->_size);
    ptr += p->_size;

    dst->_fixedSized &= p->_fixedSized;

    if (p->_fixedSized) {
      *offsetsF++ = offset;
      offset += p->_size;
      *offsetsF = offset;
    }
    else {
      *offsetsV++ = offset;
      offset += p->_size;
      *offsetsV = offset;
    }
  }

  // free TRI_shape_value_t array
  for (p = values;  p < e;  ++p) {
    if (p->_value != 0) {
      TRI_Free(shaper->_memoryZone, p->_value);
    }
  }

  TRI_Free(shaper->_memoryZone, values);

  // lookup this shape
  found = shaper->findShape(shaper, &a->base);

  if (found == 0) {
    TRI_Free(shaper->_memoryZone, a);
    return false;
  }

  // and finally add the sid
  dst->_sid = found->_sid;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a json object into TRI_shape_value_t
////////////////////////////////////////////////////////////////////////////////

static bool FillShapeValueJson (TRI_shaper_t* shaper,
                                TRI_shape_value_t* dst,
                                v8::Handle<v8::Value> json,
                                set<int>& seenHashes,
                                vector< v8::Handle<v8::Object> >& seenObjects) {
  if (json->IsObject()) {
    v8::Handle<v8::Object> o = json->ToObject();
    int hash = o->GetIdentityHash();

    if (seenHashes.find(hash) != seenHashes.end()) {
      LOG_TRACE("found hash %d", hash);

      for (vector< v8::Handle<v8::Object> >::iterator i = seenObjects.begin();  i != seenObjects.end();  ++i) {
        if (json->StrictEquals(*i)) {
          LOG_TRACE("found duplicate for hash %d", hash);
          return FillShapeValueNull(shaper, dst);
        }
      }

      seenObjects.push_back(o);
    }
    else {
      seenHashes.insert(hash);
      seenObjects.push_back(o);
    }
  }

  if (json->IsNull()) {
    return FillShapeValueNull(shaper, dst);
  }

  if (json->IsBoolean()) {
    return FillShapeValueBoolean(shaper, dst, json->ToBoolean());
  }

  if (json->IsBooleanObject()) {
    v8::Handle<v8::BooleanObject> bo = v8::Handle<v8::BooleanObject>::Cast(json);
    return FillShapeValueBoolean(shaper, dst, bo);
  }

  if (json->IsNumber()) {
    return FillShapeValueNumber(shaper, dst, json->ToNumber());
  }

  if (json->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(json);
    return FillShapeValueNumber(shaper, dst, no);
  }

  if (json->IsString()) {
    return FillShapeValueString(shaper, dst, json->ToString());
  }

  if (json->IsStringObject()) {
    v8::Handle<v8::StringObject> so = v8::Handle<v8::StringObject>::Cast(json);
    return FillShapeValueString(shaper, dst, so->StringValue());
  }

  if (json->IsArray()) {
    v8::Handle<v8::Array> array = v8::Handle<v8::Array>::Cast(json);
    return FillShapeValueList(shaper, dst, array, seenHashes, seenObjects);
  }

  if (json->IsObject()) {
    return FillShapeValueArray(shaper, dst, json->ToObject(), seenHashes, seenObjects);
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data null blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataNull (TRI_shaper_t* shaper,
                                                TRI_shape_t const* shape,
                                                char const* data,
                                                size_t size) {
  return v8::Null();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data boolean blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataBoolean (TRI_shaper_t* shaper,
                                                   TRI_shape_t const* shape,
                                                   char const* data,
                                                   size_t size) {
  bool v;

  v = (* (TRI_shape_boolean_t const*) data) != 0;

  return v ? v8::True() : v8::False();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data number blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataNumber (TRI_shaper_t* shaper,
                                                  TRI_shape_t const* shape,
                                                  char const* data,
                                                  size_t size) {
  TRI_shape_number_t v;

  v = * (TRI_shape_number_t const*) data;

  return v8::Number::New(v);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data short string blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataShortString (TRI_shaper_t* shaper,
                                                       TRI_shape_t const* shape,
                                                       char const* data,
                                                       size_t size) {
  TRI_shape_length_short_string_t l;

  l = * (TRI_shape_length_short_string_t const*) data;
  data += sizeof(TRI_shape_length_short_string_t);

  return v8::String::New(data, l - 1);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data long string blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataLongString (TRI_shaper_t* shaper,
                                                      TRI_shape_t const* shape,
                                                      char const* data,
                                                      size_t size) {
  TRI_shape_length_long_string_t l;

  l = * (TRI_shape_length_long_string_t const*) data;
  data += sizeof(TRI_shape_length_long_string_t);

  return v8::String::New(data, l - 1);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data array blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataArray (TRI_shaper_t* shaper,
                                                 TRI_shape_t const* shape,
                                                 char const* data,
                                                 size_t size) {
  TRI_array_shape_t const* s;
  TRI_shape_aid_t const* aids;
  TRI_shape_sid_t const* sids;
  TRI_shape_size_t const* offsetsF;
  TRI_shape_size_t const* offsetsV;
  TRI_shape_size_t f;
  TRI_shape_size_t i;
  TRI_shape_size_t n;
  TRI_shape_size_t v;
  char const* qtr;

  v8::Handle<v8::Object> array;

  s = (TRI_array_shape_t const*) shape;
  f = s->_fixedEntries;
  v = s->_variableEntries;
  n = f + v;

  qtr = (char const*) shape;
  array = v8::Object::New();

  qtr += sizeof(TRI_array_shape_t);

  sids = (TRI_shape_sid_t const*) qtr;
  qtr += n * sizeof(TRI_shape_sid_t);

  aids = (TRI_shape_aid_t const*) qtr;
  qtr += n * sizeof(TRI_shape_aid_t);

  offsetsF = (TRI_shape_size_t const*) qtr;

  for (i = 0;  i < f;  ++i, ++sids, ++aids, ++offsetsF) {
    TRI_shape_sid_t sid = *sids;
    TRI_shape_aid_t aid = *aids;
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    char const* name;
    v8::Handle<v8::Value> element;

    offset = *offsetsF;
    subshape = shaper->lookupShapeId(shaper, sid);
    name = shaper->lookupAttributeId(shaper, aid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    if (name == 0) {
      LOG_WARNING("cannot find attribute #%u", (unsigned int) aid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsetsF[1] - offset);

    array->Set(v8::String::New(name), element);
  }

  offsetsV = (TRI_shape_size_t const*) data;

  for (i = 0;  i < v;  ++i, ++sids, ++aids, ++offsetsV) {
    TRI_shape_sid_t sid = *sids;
    TRI_shape_aid_t aid = *aids;
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    char const* name;
    v8::Handle<v8::Value> element;

    offset = *offsetsV;
    subshape = shaper->lookupShapeId(shaper, sid);
    name = shaper->lookupAttributeId(shaper, aid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    if (name == 0) {
      LOG_WARNING("cannot find attribute #%u", (unsigned int) aid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsetsV[1] - offset);

    array->Set(v8::String::New(name), element);
  }

  return array;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data list blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataList (TRI_shaper_t* shaper,
                                                TRI_shape_t const* shape,
                                                char const* data,
                                                size_t size) {
  TRI_shape_length_list_t i;
  TRI_shape_length_list_t l;
  TRI_shape_sid_t const* sids;
  TRI_shape_size_t const* offsets;
  char const* ptr;
  v8::Handle<v8::Array> list;

  list = v8::Array::New();

  ptr = data;
  l = * (TRI_shape_length_list_t const*) ptr;

  ptr += sizeof(TRI_shape_length_list_t);
  sids = (TRI_shape_sid_t const*) ptr;

  ptr += l * sizeof(TRI_shape_sid_t);
  offsets = (TRI_shape_size_t const*) ptr;

  for (i = 0;  i < l;  ++i, ++sids, ++offsets) {
    TRI_shape_sid_t sid = *sids;
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    v8::Handle<v8::Value> element;

    offset = *offsets;
    subshape = shaper->lookupShapeId(shaper, sid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsets[1] - offset);

    list->Set(i, element);
  }

  return list;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data homogeneous list blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataHomogeneousList (TRI_shaper_t* shaper,
                                                           TRI_shape_t const* shape,
                                                           char const* data,
                                                           size_t size) {
  TRI_homogeneous_list_shape_t const* s;
  TRI_shape_length_list_t i;
  TRI_shape_length_list_t l;
  TRI_shape_sid_t sid;
  TRI_shape_size_t const* offsets;
  char const* ptr;
  v8::Handle<v8::Array> list;

  list = v8::Array::New();

  s = (TRI_homogeneous_list_shape_t const*) shape;
  sid = s->_sidEntry;

  ptr = data;
  l = * (TRI_shape_length_list_t const*) ptr;

  ptr += sizeof(TRI_shape_length_list_t);
  offsets = (TRI_shape_size_t const*) ptr;

  for (i = 0;  i < l;  ++i, ++offsets) {
    TRI_shape_size_t offset;
    TRI_shape_t const* subshape;
    v8::Handle<v8::Value> element;

    offset = *offsets;
    subshape = shaper->lookupShapeId(shaper, sid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, offsets[1] - offset);

    list->Set(i, element);
  }

  return list;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data homogeneous sized list blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeDataHomogeneousSizedList (TRI_shaper_t* shaper,
                                                                TRI_shape_t const* shape,
                                                                char const* data,
                                                                size_t size) {
  TRI_homogeneous_sized_list_shape_t const* s;
  TRI_shape_length_list_t i;
  TRI_shape_length_list_t l;
  TRI_shape_sid_t sid;
  TRI_shape_size_t length;
  TRI_shape_size_t offset;
  char const* ptr;
  v8::Handle<v8::Array> list;

  list = v8::Array::New();

  s = (TRI_homogeneous_sized_list_shape_t const*) shape;
  sid = s->_sidEntry;

  ptr = data;
  l = * (TRI_shape_length_list_t const*) ptr;

  length = s->_sizeEntry;
  offset = sizeof(TRI_shape_length_list_t);

  for (i = 0;  i < l;  ++i, offset += length) {
    TRI_shape_t const* subshape;
    v8::Handle<v8::Value> element;

    subshape = shaper->lookupShapeId(shaper, sid);

    if (subshape == 0) {
      LOG_WARNING("cannot find shape #%u", (unsigned int) sid);
      continue;
    }

    element = JsonShapeData(shaper, subshape, data + offset, length);

    list->Set(i, element);
  }

  return list;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a data blob into a json object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> JsonShapeData (TRI_shaper_t* shaper,
                                            TRI_shape_t const* shape,
                                            char const* data,
                                            size_t size) {
  if (shape == 0) {
    return v8::Null();
  }

  switch (shape->_type) {
    case TRI_SHAPE_NULL:
      return JsonShapeDataNull(shaper, shape, data, size);

    case TRI_SHAPE_BOOLEAN:
      return JsonShapeDataBoolean(shaper, shape, data, size);

    case TRI_SHAPE_NUMBER:
      return JsonShapeDataNumber(shaper, shape, data, size);

    case TRI_SHAPE_SHORT_STRING:
      return JsonShapeDataShortString(shaper, shape, data, size);

    case TRI_SHAPE_LONG_STRING:
      return JsonShapeDataLongString(shaper, shape, data, size);

    case TRI_SHAPE_ARRAY:
      return JsonShapeDataArray(shaper, shape, data, size);

    case TRI_SHAPE_LIST:
      return JsonShapeDataList(shaper, shape, data, size);

    case TRI_SHAPE_HOMOGENEOUS_LIST:
      return JsonShapeDataHomogeneousList(shaper, shape, data, size);

    case TRI_SHAPE_HOMOGENEOUS_SIZED_LIST:
      return JsonShapeDataHomogeneousSizedList(shaper, shape, data, size);
  }

  return v8::Null();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t NULL into a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectJsonNull (TRI_json_t const* json) {
  return v8::Null();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t BOOLEAN into a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectJsonBoolean (TRI_json_t const* json) {
  return json->_value._boolean ? v8::True() : v8::False();
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t NUMBER into a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectJsonNumber (TRI_json_t const* json) {
  return v8::Number::New(json->_value._number);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t NUMBER into a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectJsonString (TRI_json_t const* json) {
  return v8::String::New(json->_value._string.data);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t ARRAY into a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectJsonArray (TRI_json_t const* json) {
  v8::Handle<v8::Object> object = v8::Object::New();

  size_t n = json->_value._objects._length;

  for (size_t i = 0;  i < n;  i += 2) {
    TRI_json_t* key = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);

    if (key->_type != TRI_JSON_STRING) {
      continue;
    }

    TRI_json_t* j = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i + 1);
    v8::Handle<v8::Value> val = TRI_ObjectJson(j);

    object->Set(v8::String::New(key->_value._string.data), val);
  }

  return object;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t LIST into a V8 object
////////////////////////////////////////////////////////////////////////////////

static v8::Handle<v8::Value> ObjectJsonList (TRI_json_t const* json) {
  v8::Handle<v8::Array> object = v8::Array::New();

  size_t n = json->_value._objects._length;

  for (size_t i = 0;  i < n;  ++i) {
    TRI_json_t* j = (TRI_json_t*) TRI_AtVector(&json->_value._objects, i);
    v8::Handle<v8::Value> val = TRI_ObjectJson(j);

    object->Set(i, val);
  }

    return object;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Conversions
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief pushes the names of an associative char* array into a V8 array
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Array> TRI_ArrayAssociativePointer (const TRI_associative_pointer_t* const array) {
  v8::HandleScope scope;
  v8::Handle<v8::Array> result = v8::Array::New();

  uint32_t j = 0;
  uint32_t n = (uint32_t) array->_nrAlloc;
  for (uint32_t i = 0;  i < n;  ++i) {
    char* value = (char*) array->_table[i];

    if (value == 0) {
      continue;
    }

    result->Set(j++, v8::String::New(value));
  }

  return scope.Close(result);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts identifier into a object reference
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> TRI_ObjectReference (TRI_voc_cid_t cid, TRI_voc_did_t did) {
  v8::HandleScope scope;
  TRI_string_buffer_t buffer;

  TRI_InitStringBuffer(&buffer, TRI_CORE_MEM_ZONE);
  TRI_AppendUInt64StringBuffer(&buffer, cid);
  TRI_AppendCharStringBuffer(&buffer, TRI_DOCUMENT_HANDLE_SEPARATOR_CHR);
  TRI_AppendUInt64StringBuffer(&buffer, did);

  v8::Handle<v8::String> ref = v8::String::New(buffer._buffer);

  TRI_AnnihilateStringBuffer(&buffer);

  return scope.Close(ref);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extratcs identifiers from a object reference
////////////////////////////////////////////////////////////////////////////////

bool TRI_IdentifiersObjectReference (v8::Handle<v8::Value> value, TRI_voc_cid_t& cid, TRI_voc_did_t& did) {
  bool error;

  cid = 0;
  did = 0;

  if (value->IsNumber() || value->IsNumberObject()) {
    did = TRI_ObjectToDouble(value, error);
    return ! error;
  }

  string v = TRI_ObjectToString(value);

  vector<string> doc = StringUtils::split(v, TRI_DOCUMENT_HANDLE_SEPARATOR_STR);

  switch (doc.size()) {
    case 1:
      did = StringUtils::uint64(doc[1]);
      return did != 0;

    case 2:
      cid = StringUtils::uint64(doc[0]);
      did = StringUtils::uint64(doc[1]);
      return cid != 0 && did != 0;

    default:
      return false;
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_json_t into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> TRI_ObjectJson (TRI_json_t const* json) {
  v8::HandleScope scope;

  switch (json->_type) {
    case TRI_JSON_UNUSED:
      return scope.Close(v8::Undefined());

    case TRI_JSON_NULL:
      return scope.Close(ObjectJsonNull(json));

    case TRI_JSON_BOOLEAN:
      return scope.Close(ObjectJsonBoolean(json));

    case TRI_JSON_NUMBER:
      return scope.Close(ObjectJsonNumber(json));

    case TRI_JSON_STRING:
      return scope.Close(ObjectJsonString(json));

    case TRI_JSON_ARRAY:
      return scope.Close(ObjectJsonArray(json));

    case TRI_JSON_LIST:
      return scope.Close(ObjectJsonList(json));
  }

  return scope.Close(v8::Undefined());
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_shaped_json_t into a V8 object
////////////////////////////////////////////////////////////////////////////////

bool TRI_ObjectDocumentPointer (TRI_doc_collection_t* collection,
                                TRI_doc_mptr_t const* document,
                                void* storage) {
  TRI_df_marker_type_t type;
  TRI_doc_edge_marker_t* marker;
  TRI_shape_t const* shape;
  TRI_shaped_json_t const* shaped;
  TRI_shaper_t* shaper;
  TRI_v8_global_t* v8g;

  v8g = (TRI_v8_global_t*) v8::Isolate::GetCurrent()->GetData();
  shaper = collection->_shaper;
  shaped = &document->_document;
  shape = shaper->lookupShapeId(shaper, shaped->_sid);

  if (shape == 0) {
    LOG_WARNING("cannot find shape #%u", (unsigned int) shaped->_sid);
    return false;
  }

  v8::Handle<v8::Value> result = JsonShapeData(shaper,
                                               shape,
                                               shaped->_data.data,
                                               shaped->_data.length);

  if (result->IsObject()) {
    result->ToObject()->Set(v8g->DidKey, TRI_ObjectReference(collection->base._cid, document->_did));
    result->ToObject()->Set(v8g->RevKey, v8::Number::New(document->_rid));

    type = ((TRI_df_marker_t*) document->_data)->_type;

    if (type == TRI_DOC_MARKER_EDGE) {
      marker = (TRI_doc_edge_marker_t*) document->_data;

      result->ToObject()->Set(v8g->FromKey, TRI_ObjectReference(marker->_fromCid, marker->_fromDid));
      result->ToObject()->Set(v8g->ToKey, TRI_ObjectReference(marker->_toCid, marker->_toDid));
    }
  }

  * (v8::Handle<v8::Value>*) storage = result;

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief Convert a raw data document pointer into a master pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_MarkerMasterPointer (void const* data, TRI_doc_mptr_t* header) {
  TRI_doc_document_marker_t const* marker;
  size_t markerSize = sizeof(TRI_doc_document_marker_t);
  marker = (TRI_doc_document_marker_t const*) data;

  header->_did = marker->_did;
  header->_rid = marker->_rid;
  header->_fid = 0; // should be datafile->_fid, but we do not have this info here
  header->_deletion = 0;
  header->_data = data;
  header->_document._sid = marker->_shape;
  header->_document._data.length = marker->base._size - markerSize;
  header->_document._data.data = ((char*) marker) + markerSize;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a V8 object to a TRI_shaped_json_t
////////////////////////////////////////////////////////////////////////////////

TRI_shaped_json_t* TRI_ShapedJsonV8Object (v8::Handle<v8::Value> object, TRI_shaper_t* shaper) {
  TRI_shape_value_t dst;
  set<int> seenHashes;
  vector< v8::Handle<v8::Object> > seenObjects;
  bool ok = FillShapeValueJson(shaper, &dst, object, seenHashes, seenObjects);

  if (! ok) {
    return 0;
  }

  TRI_shaped_json_t* shaped = (TRI_shaped_json_t*) TRI_Allocate(shaper->_memoryZone, sizeof(TRI_shaped_json_t));

  if (shaped == NULL) {
    return NULL;
  }

  shaped->_sid = dst._sid;
  shaped->_data.length = dst._size;
  shaped->_data.data = dst._value;

  return shaped;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a string
////////////////////////////////////////////////////////////////////////////////

string TRI_ObjectToString (v8::Handle<v8::Value> value) {
  v8::String::Utf8Value utf8Value(value);

  if (*utf8Value == 0) {
    return "";
  }
  else {
    return string(*utf8Value, utf8Value.length());
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a character
////////////////////////////////////////////////////////////////////////////////

char TRI_ObjectToCharacter (v8::Handle<v8::Value> value, bool& error) {
  error = false;

  if (! value->IsString() && ! value->IsStringObject()) {
    error = true;
    return '\0';
  }

  v8::String::Utf8Value sep(value->ToString());

  if (*sep == 0 || sep.length() != 1) {
    error = true;
    return '\0';
  }

  return (*sep)[0];
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to an int64_t
////////////////////////////////////////////////////////////////////////////////

int64_t TRI_ObjectToInt64 (v8::Handle<v8::Value> value) {
  if (value->IsNumber()) {
    return (int64_t) value->ToNumber()->Value();
  }

  if (value->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(value);
    return (int64_t) no->NumberValue();
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a uint64_t
////////////////////////////////////////////////////////////////////////////////

uint64_t TRI_ObjectToUInt64 (v8::Handle<v8::Value> value) {
  if (value->IsNumber()) {
    return (uint64_t) value->ToNumber()->Value();
  }

  if (value->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(value);
    return (uint64_t) no->NumberValue();
  }

  return 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a double
////////////////////////////////////////////////////////////////////////////////

double TRI_ObjectToDouble (v8::Handle<v8::Value> value) {
  if (value->IsNumber()) {
    return value->ToNumber()->Value();
  }

  if (value->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(value);
    return no->NumberValue();
  }

  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a double
////////////////////////////////////////////////////////////////////////////////

double TRI_ObjectToDouble (v8::Handle<v8::Value> value, bool& error) {
  error = false;

  if (value->IsNumber()) {
    return value->ToNumber()->Value();
  }

  if (value->IsNumberObject()) {
    v8::Handle<v8::NumberObject> no = v8::Handle<v8::NumberObject>::Cast(value);
    return no->NumberValue();
  }

  error = true;

  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts an V8 object to a boolean
////////////////////////////////////////////////////////////////////////////////

bool TRI_ObjectToBoolean (v8::Handle<v8::Value> value) {
  if (value->IsBoolean()) {
    return value->ToBoolean()->Value();
  }
  else if (value->IsBooleanObject()) {
    v8::Handle<v8::BooleanObject> bo = v8::Handle<v8::BooleanObject>::Cast(value);
    return bo->BooleanValue();
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a TRI_shaped_json_t into a V8 object
////////////////////////////////////////////////////////////////////////////////

v8::Handle<v8::Value> TRI_JsonShapeData (TRI_shaper_t* shaper,
                                         TRI_shape_t const* shape,
                                         char const* data,
                                         size_t size) {
  return JsonShapeData(shaper, shape, data, size);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Conversions
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief initialises the V8 conversion module
////////////////////////////////////////////////////////////////////////////////

void TRI_InitV8Conversions (v8::Handle<v8::Context> context) {
  v8::HandleScope scope;

  // check the isolate
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  TRI_v8_global_t* v8g = (TRI_v8_global_t*) isolate->GetData();

  if (v8g == 0) {
    v8g = new TRI_v8_global_t;
    isolate->SetData(v8g);
  }

  // .............................................................................
  // keys
  // .............................................................................

  if (v8g->DidKey.IsEmpty()) {
    v8g->DidKey = v8::Persistent<v8::String>::New(v8::String::New("_id"));
  }

  if (v8g->FromKey.IsEmpty()) {
    v8g->FromKey = v8::Persistent<v8::String>::New(v8::String::New("_from"));
  }

  if (v8g->ToKey.IsEmpty()) {
    v8g->ToKey = v8::Persistent<v8::String>::New(v8::String::New("_to"));
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
