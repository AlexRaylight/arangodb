////////////////////////////////////////////////////////////////////////////////
/// @brief http request result
///
/// @file
///
/// DISCLAIMER
///
/// Copyright by triAGENS GmbH - All rights reserved.
///
/// The Programs (which include both the software and documentation)
/// contain proprietary information of triAGENS GmbH; they are
/// provided under a license agreement containing restrictions on use and
/// disclosure and are also protected by copyright, patent and other
/// intellectual and industrial property laws. Reverse engineering,
/// disassembly or decompilation of the Programs, except to the extent
/// required to obtain interoperability with other independently created
/// software or as specified by law, is prohibited.
///
/// The Programs are not intended for use in any nuclear, aviation, mass
/// transit, medical, or other inherently dangerous applications. It shall
/// be the licensee's responsibility to take all appropriate fail-safe,
/// backup, redundancy, and other measures to ensure the safe use of such
/// applications if the Programs are used for such purposes, and triAGENS
/// GmbH disclaims liability for any damages caused by such use of
/// the Programs.
///
/// This software is the confidential and proprietary information of
/// triAGENS GmbH. You shall not disclose such confidential and
/// proprietary information and shall use it only in accordance with the
/// terms of the license agreement you entered into with triAGENS GmbH.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Achim Brandt
/// @author Copyright 2008-2011, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "SimpleHttpResult.h"
#include "Basics/StringUtils.h"

using namespace triagens::basics;
using namespace std;

namespace triagens {
  namespace httpclient {

    ////////////////////////////////////////////////////////////////////////////////
    /// constructor and destructor
    ////////////////////////////////////////////////////////////////////////////////

    SimpleHttpResult::SimpleHttpResult () {
      clear();    
    }

    SimpleHttpResult::~SimpleHttpResult () {
    }

    ////////////////////////////////////////////////////////////////////////////////
    /// public methods
    ////////////////////////////////////////////////////////////////////////////////

    void SimpleHttpResult::clear () {
      _returnCode = 0;
      _returnMessage = "";
      _contentLength = 0;
      _chunked = false;
      _requestResultType = UNKNOWN;            
      _headerFields.clear();
      _resultBody.clear();      
    }
    
    stringstream& SimpleHttpResult::getBody () {
      return _resultBody;
    }

    string SimpleHttpResult::getResultTypeMessage () {
      switch (_requestResultType) {
        case (COMPLETE):
          return "No error.";
        case (COULD_NOT_CONNECT):
          return "Could not connect to server.";
        case (WRITE_ERROR):
          return "Error while writing to server.";
        case (READ_ERROR):
          return "Error while reading from server.";
      default:
        return "Unknown error.";
      }
    }

    void SimpleHttpResult::addHeaderField (std::string const& line) {
      string key;
      string value;
      
       size_t find = line.find(": ");
       
       if (find != string::npos) {
         key = line.substr(0, find);
         value = StringUtils::trim(line.substr(find + 2));
         addHeaderField(key, value);
         return;
       }      
      
       find = line.find(" ");
       if (find != string::npos) {
         key = line.substr(0, find);
         if (find + 1 < line.length()) {
           value = StringUtils::trim(line.substr(find + 1));
         }
         addHeaderField(key, value);
       }      
    }
    
    void SimpleHttpResult::addHeaderField (std::string const& key, std::string const& value) {
      string k = StringUtils::trim(StringUtils::tolower(key));

      if (k == "http/1.1" || k == "http/1.0") {
        if (value.length() > 2) {
          // we assume the status code is 3 chars long
          string code = value.substr(0, 3);
          setHttpReturnCode(atoi(code.c_str()));
          setHttpReturnMessage(value.substr(3 + 1));
        }
      }
      else if (k == "content-length") {
        setContentLength((size_t) StringUtils::int64(value.c_str()));
      }
      else if (k == "transfer-encoding") {
        if (StringUtils::tolower(value) == "chunked") {
          _chunked = true;
        }
      }
      
      _headerFields[k] = value;
    }

    const string SimpleHttpResult::getContentType (const bool partial) {
      map<string, string>::const_iterator find = _headerFields.find("content-type");
      if (find != _headerFields.end()) {
        // header found
        if (partial) {
          // return partial match before first semicolon
          size_t semicolon = find->second.find(";");
          if (semicolon != string::npos) {
            // partial match found
            return find->second.substr(0, semicolon);
          }
        }
        return find->second;   
      }

      return "";
    }
  }
}
