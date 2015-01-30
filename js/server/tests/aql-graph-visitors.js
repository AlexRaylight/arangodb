/*jshint strict: false, sub: true, maxlen: 500 */
/*global require, assertEqual, AQL_EXECUTE */

////////////////////////////////////////////////////////////////////////////////
/// @brief tests for query language, graph functions
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var jsunity = require("jsunity");
var db = require("org/arangodb").db;

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite for EDGES() function
////////////////////////////////////////////////////////////////////////////////

function ahuacatlGraphVisitorsSuite () {
  var vertex = null;
  var edge   = null;
  var aqlfunctions = require("org/arangodb/aql/functions");

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      db._drop("UnitTestsAhuacatlVertex");
      db._drop("UnitTestsAhuacatlEdge");

      vertex = db._create("UnitTestsAhuacatlVertex");
      edge = db._createEdgeCollection("UnitTestsAhuacatlEdge");

      /* vertices: root node */ 
      vertex.save({ _key: "world", name: "World", type: "root" });

      /* vertices: continents */
      vertex.save({ _key: "continent-africa", name: "Africa", type: "continent" });
      vertex.save({ _key: "continent-asia", name: "Asia", type: "continent" });
      vertex.save({ _key: "continent-australia", name: "Australia", type: "continent" });
      vertex.save({ _key: "continent-europe", name: "Europe", type: "continent" });
      vertex.save({ _key: "continent-north-america", name: "North America", type: "continent" });
      vertex.save({ _key: "continent-south-america", name: "South America", type: "continent" });

      /* vertices: countries */
      vertex.save({ _key: "country-afghanistan", name: "Afghanistan", type: "country", code: "AFG" });
      vertex.save({ _key: "country-albania", name: "Albania", type: "country", code: "ALB" });
      vertex.save({ _key: "country-algeria", name: "Algeria", type: "country", code: "DZA" });
      vertex.save({ _key: "country-andorra", name: "Andorra", type: "country", code: "AND" });
      vertex.save({ _key: "country-angola", name: "Angola", type: "country", code: "AGO" });
      vertex.save({ _key: "country-antigua-and-barbuda", name: "Antigua and Barbuda", type: "country", code: "ATG" });
      vertex.save({ _key: "country-argentina", name: "Argentina", type: "country", code: "ARG" });
      vertex.save({ _key: "country-australia", name: "Australia", type: "country", code: "AUS" });
      vertex.save({ _key: "country-austria", name: "Austria", type: "country", code: "AUT" });
      vertex.save({ _key: "country-bahamas", name: "Bahamas", type: "country", code: "BHS" });
      vertex.save({ _key: "country-bahrain", name: "Bahrain", type: "country", code: "BHR" });
      vertex.save({ _key: "country-bangladesh", name: "Bangladesh", type: "country", code: "BGD" });
      vertex.save({ _key: "country-barbados", name: "Barbados", type: "country", code: "BRB" });
      vertex.save({ _key: "country-belgium", name: "Belgium", type: "country", code: "BEL" });
      vertex.save({ _key: "country-bhutan", name: "Bhutan", type: "country", code: "BTN" });
      vertex.save({ _key: "country-bolivia", name: "Bolivia", type: "country", code: "BOL" });
      vertex.save({ _key: "country-bosnia-and-herzegovina", name: "Bosnia and Herzegovina", type: "country", code: "BIH" });
      vertex.save({ _key: "country-botswana", name: "Botswana", type: "country", code: "BWA" });
      vertex.save({ _key: "country-brazil", name: "Brazil", type: "country", code: "BRA" });
      vertex.save({ _key: "country-brunei", name: "Brunei", type: "country", code: "BRN" });
      vertex.save({ _key: "country-bulgaria", name: "Bulgaria", type: "country", code: "BGR" });
      vertex.save({ _key: "country-burkina-faso", name: "Burkina Faso", type: "country", code: "BFA" });
      vertex.save({ _key: "country-burundi", name: "Burundi", type: "country", code: "BDI" });
      vertex.save({ _key: "country-cambodia", name: "Cambodia", type: "country", code: "KHM" });
      vertex.save({ _key: "country-cameroon", name: "Cameroon", type: "country", code: "CMR" });
      vertex.save({ _key: "country-canada", name: "Canada", type: "country", code: "CAN" });
      vertex.save({ _key: "country-chad", name: "Chad", type: "country", code: "TCD" });
      vertex.save({ _key: "country-chile", name: "Chile", type: "country", code: "CHL" });
      vertex.save({ _key: "country-colombia", name: "Colombia", type: "country", code: "COL" });
      vertex.save({ _key: "country-cote-d-ivoire", name: "Cote d'Ivoire", type: "country", code: "CIV" });
      vertex.save({ _key: "country-croatia", name: "Croatia", type: "country", code: "HRV" });
      vertex.save({ _key: "country-czech-republic", name: "Czech Republic", type: "country", code: "CZE" });
      vertex.save({ _key: "country-denmark", name: "Denmark", type: "country", code: "DNK" });
      vertex.save({ _key: "country-ecuador", name: "Ecuador", type: "country", code: "ECU" });
      vertex.save({ _key: "country-egypt", name: "Egypt", type: "country", code: "EGY" });
      vertex.save({ _key: "country-eritrea", name: "Eritrea", type: "country", code: "ERI" });
      vertex.save({ _key: "country-finland", name: "Finland", type: "country", code: "FIN" });
      vertex.save({ _key: "country-france", name: "France", type: "country", code: "FRA" });
      vertex.save({ _key: "country-germany", name: "Germany", type: "country", code: "DEU" });
      vertex.save({ _key: "country-people-s-republic-of-china", name: "People's Republic of China", type: "country", code: "CHN" });

      /* vertices: capitals */ 
      vertex.save({ _key: "capital-algiers", name: "Algiers", type: "capital" });
      vertex.save({ _key: "capital-andorra-la-vella", name: "Andorra la Vella", type: "capital" });
      vertex.save({ _key: "capital-asmara", name: "Asmara", type: "capital" });
      vertex.save({ _key: "capital-bandar-seri-begawan", name: "Bandar Seri Begawan", type: "capital" });
      vertex.save({ _key: "capital-beijing", name: "Beijing", type: "capital" });
      vertex.save({ _key: "capital-berlin", name: "Berlin", type: "capital" });
      vertex.save({ _key: "capital-bogota", name: "Bogota", type: "capital" });
      vertex.save({ _key: "capital-brasilia", name: "Brasilia", type: "capital" });
      vertex.save({ _key: "capital-bridgetown", name: "Bridgetown", type: "capital" });
      vertex.save({ _key: "capital-brussels", name: "Brussels", type: "capital" });
      vertex.save({ _key: "capital-buenos-aires", name: "Buenos Aires", type: "capital" });
      vertex.save({ _key: "capital-bujumbura", name: "Bujumbura", type: "capital" });
      vertex.save({ _key: "capital-cairo", name: "Cairo", type: "capital" });
      vertex.save({ _key: "capital-canberra", name: "Canberra", type: "capital" });
      vertex.save({ _key: "capital-copenhagen", name: "Copenhagen", type: "capital" });
      vertex.save({ _key: "capital-dhaka", name: "Dhaka", type: "capital" });
      vertex.save({ _key: "capital-gaborone", name: "Gaborone", type: "capital" });
      vertex.save({ _key: "capital-helsinki", name: "Helsinki", type: "capital" });
      vertex.save({ _key: "capital-kabul", name: "Kabul", type: "capital" });
      vertex.save({ _key: "capital-la-paz", name: "La Paz", type: "capital" });
      vertex.save({ _key: "capital-luanda", name: "Luanda", type: "capital" });
      vertex.save({ _key: "capital-manama", name: "Manama", type: "capital" });
      vertex.save({ _key: "capital-nassau", name: "Nassau", type: "capital" });
      vertex.save({ _key: "capital-n-djamena", name: "N'Djamena", type: "capital" });
      vertex.save({ _key: "capital-ottawa", name: "Ottawa", type: "capital" });
      vertex.save({ _key: "capital-ouagadougou", name: "Ouagadougou", type: "capital" });
      vertex.save({ _key: "capital-paris", name: "Paris", type: "capital" });
      vertex.save({ _key: "capital-phnom-penh", name: "Phnom Penh", type: "capital" });
      vertex.save({ _key: "capital-prague", name: "Prague", type: "capital" });
      vertex.save({ _key: "capital-quito", name: "Quito", type: "capital" });
      vertex.save({ _key: "capital-saint-john-s", name: "Saint John's", type: "capital" });
      vertex.save({ _key: "capital-santiago", name: "Santiago", type: "capital" });
      vertex.save({ _key: "capital-sarajevo", name: "Sarajevo", type: "capital" });
      vertex.save({ _key: "capital-sofia", name: "Sofia", type: "capital" });
      vertex.save({ _key: "capital-thimphu", name: "Thimphu", type: "capital" });
      vertex.save({ _key: "capital-tirana", name: "Tirana", type: "capital" });
      vertex.save({ _key: "capital-vienna", name: "Vienna", type: "capital" });
      vertex.save({ _key: "capital-yamoussoukro", name: "Yamoussoukro", type: "capital" });
      vertex.save({ _key: "capital-yaounde", name: "Yaounde", type: "capital" });
      vertex.save({ _key: "capital-zagreb", name: "Zagreb", type: "capital" });

      /* edges: continent -> world */ 
      edge.save("UnitTestsAhuacatlVertex/continent-africa", "UnitTestsAhuacatlVertex/world", { _key: "001", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/continent-asia", "UnitTestsAhuacatlVertex/world", { _key: "002", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/continent-australia", "UnitTestsAhuacatlVertex/world", { _key: "003", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/continent-europe", "UnitTestsAhuacatlVertex/world", { _key: "004", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/continent-north-america", "UnitTestsAhuacatlVertex/world", { _key: "005", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/continent-south-america", "UnitTestsAhuacatlVertex/world", { _key: "006", type: "is-in" });

      /* edges: country -> continent */ 
      edge.save("UnitTestsAhuacatlVertex/country-afghanistan", "UnitTestsAhuacatlVertex/continent-asia", { _key: "100", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-albania", "UnitTestsAhuacatlVertex/continent-europe", { _key: "101", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-algeria", "UnitTestsAhuacatlVertex/continent-africa", { _key: "102", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-andorra", "UnitTestsAhuacatlVertex/continent-europe", { _key: "103", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-angola", "UnitTestsAhuacatlVertex/continent-africa", { _key: "104", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-antigua-and-barbuda", "UnitTestsAhuacatlVertex/continent-north-america", { _key: "105", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-argentina", "UnitTestsAhuacatlVertex/continent-south-america", { _key: "106", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-australia", "UnitTestsAhuacatlVertex/continent-australia", { _key: "107", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-austria", "UnitTestsAhuacatlVertex/continent-europe", { _key: "108", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bahamas", "UnitTestsAhuacatlVertex/continent-north-america", { _key: "109", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bahrain", "UnitTestsAhuacatlVertex/continent-asia", { _key: "110", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bangladesh", "UnitTestsAhuacatlVertex/continent-asia", { _key: "111", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-barbados", "UnitTestsAhuacatlVertex/continent-north-america", { _key: "112", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-belgium", "UnitTestsAhuacatlVertex/continent-europe", { _key: "113", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bhutan", "UnitTestsAhuacatlVertex/continent-asia", { _key: "114", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bolivia", "UnitTestsAhuacatlVertex/continent-south-america", { _key: "115", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bosnia-and-herzegovina", "UnitTestsAhuacatlVertex/continent-europe", { _key: "116", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-botswana", "UnitTestsAhuacatlVertex/continent-africa", { _key: "117", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-brazil", "UnitTestsAhuacatlVertex/continent-south-america", { _key: "118", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-brunei", "UnitTestsAhuacatlVertex/continent-asia", { _key: "119", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-bulgaria", "UnitTestsAhuacatlVertex/continent-europe", { _key: "120", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-burkina-faso", "UnitTestsAhuacatlVertex/continent-africa", { _key: "121", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-burundi", "UnitTestsAhuacatlVertex/continent-africa", { _key: "122", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-cambodia", "UnitTestsAhuacatlVertex/continent-asia", { _key: "123", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-cameroon", "UnitTestsAhuacatlVertex/continent-africa", { _key: "124", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-canada", "UnitTestsAhuacatlVertex/continent-north-america", { _key: "125", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-chad", "UnitTestsAhuacatlVertex/continent-africa", { _key: "126", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-chile", "UnitTestsAhuacatlVertex/continent-south-america", { _key: "127", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-colombia", "UnitTestsAhuacatlVertex/continent-south-america", { _key: "128", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-cote-d-ivoire", "UnitTestsAhuacatlVertex/continent-africa", { _key: "129", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-croatia", "UnitTestsAhuacatlVertex/continent-europe", { _key: "130", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-czech-republic", "UnitTestsAhuacatlVertex/continent-europe", { _key: "131", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-denmark", "UnitTestsAhuacatlVertex/continent-europe", { _key: "132", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-ecuador", "UnitTestsAhuacatlVertex/continent-south-america", { _key: "133", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-egypt", "UnitTestsAhuacatlVertex/continent-africa", { _key: "134", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-eritrea", "UnitTestsAhuacatlVertex/continent-africa", { _key: "135", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-finland", "UnitTestsAhuacatlVertex/continent-europe", { _key: "136", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-france", "UnitTestsAhuacatlVertex/continent-europe", { _key: "137", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-germany", "UnitTestsAhuacatlVertex/continent-europe", { _key: "138", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/country-people-s-republic-of-china", "UnitTestsAhuacatlVertex/continent-asia", { _key: "139", type: "is-in" });

      /* edges: capital -> country */ 
      edge.save("UnitTestsAhuacatlVertex/capital-algiers", "UnitTestsAhuacatlVertex/country-algeria", { _key: "200", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-andorra-la-vella", "UnitTestsAhuacatlVertex/country-andorra", { _key: "201", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-asmara", "UnitTestsAhuacatlVertex/country-eritrea", { _key: "202", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-bandar-seri-begawan", "UnitTestsAhuacatlVertex/country-brunei", { _key: "203", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-beijing", "UnitTestsAhuacatlVertex/country-people-s-republic-of-china", { _key: "204", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-berlin", "UnitTestsAhuacatlVertex/country-germany", { _key: "205", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-bogota", "UnitTestsAhuacatlVertex/country-colombia", { _key: "206", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-brasilia", "UnitTestsAhuacatlVertex/country-brazil", { _key: "207", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-bridgetown", "UnitTestsAhuacatlVertex/country-barbados", { _key: "208", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-brussels", "UnitTestsAhuacatlVertex/country-belgium", { _key: "209", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-buenos-aires", "UnitTestsAhuacatlVertex/country-argentina", { _key: "210", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-bujumbura", "UnitTestsAhuacatlVertex/country-burundi", { _key: "211", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-cairo", "UnitTestsAhuacatlVertex/country-egypt", { _key: "212", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-canberra", "UnitTestsAhuacatlVertex/country-australia", { _key: "213", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-copenhagen", "UnitTestsAhuacatlVertex/country-denmark", { _key: "214", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-dhaka", "UnitTestsAhuacatlVertex/country-bangladesh", { _key: "215", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-gaborone", "UnitTestsAhuacatlVertex/country-botswana", { _key: "216", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-helsinki", "UnitTestsAhuacatlVertex/country-finland", { _key: "217", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-kabul", "UnitTestsAhuacatlVertex/country-afghanistan", { _key: "218", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-la-paz", "UnitTestsAhuacatlVertex/country-bolivia", { _key: "219", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-luanda", "UnitTestsAhuacatlVertex/country-angola", { _key: "220", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-manama", "UnitTestsAhuacatlVertex/country-bahrain", { _key: "221", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-nassau", "UnitTestsAhuacatlVertex/country-bahamas", { _key: "222", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-n-djamena", "UnitTestsAhuacatlVertex/country-chad", { _key: "223", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-ottawa", "UnitTestsAhuacatlVertex/country-canada", { _key: "224", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-ouagadougou", "UnitTestsAhuacatlVertex/country-burkina-faso", { _key: "225", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-paris", "UnitTestsAhuacatlVertex/country-france", { _key: "226", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-phnom-penh", "UnitTestsAhuacatlVertex/country-cambodia", { _key: "227", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-prague", "UnitTestsAhuacatlVertex/country-czech-republic", { _key: "228", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-quito", "UnitTestsAhuacatlVertex/country-ecuador", { _key: "229", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-saint-john-s", "UnitTestsAhuacatlVertex/country-antigua-and-barbuda", { _key: "230", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-santiago", "UnitTestsAhuacatlVertex/country-chile", { _key: "231", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-sarajevo", "UnitTestsAhuacatlVertex/country-bosnia-and-herzegovina", { _key: "232", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-sofia", "UnitTestsAhuacatlVertex/country-bulgaria", { _key: "233", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-thimphu", "UnitTestsAhuacatlVertex/country-bhutan", { _key: "234", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-tirana", "UnitTestsAhuacatlVertex/country-albania", { _key: "235", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-vienna", "UnitTestsAhuacatlVertex/country-austria", { _key: "236", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-yamoussoukro", "UnitTestsAhuacatlVertex/country-cote-d-ivoire", { _key: "237", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-yaounde", "UnitTestsAhuacatlVertex/country-cameroon", { _key: "238", type: "is-in" });
      edge.save("UnitTestsAhuacatlVertex/capital-zagreb", "UnitTestsAhuacatlVertex/country-croatia", { _key: "239", type: "is-in" });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      db._drop("UnitTestsAhuacatlVertex");
      db._drop("UnitTestsAhuacatlEdge");

      try {
        aqlfunctions.unregister("UnitTests::visitor");
      }
      catch (err1) {
      }
      
      try {
        aqlfunctions.unregister("UnitTests::filter");
      }
      catch (err2) {
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testStringifyVisitor : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex) {
        var indentation = Array(path.vertices.length).join("  ");
        var label       = "- " + vertex.name + " (" + vertex.type + ")";
        return indentation + label;
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, visitor : "UnitTests::visitor", visitorReturnsResults : true } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        "- World (root)", 
        "  - Africa (continent)", 
        "    - Algeria (country)", 
        "      - Algiers (capital)", 
        "    - Angola (country)", 
        "      - Luanda (capital)", 
        "    - Botswana (country)", 
        "      - Gaborone (capital)", 
        "    - Burkina Faso (country)", 
        "      - Ouagadougou (capital)", 
        "    - Burundi (country)", 
        "      - Bujumbura (capital)", 
        "    - Cameroon (country)", 
        "      - Yaounde (capital)", 
        "    - Chad (country)", 
        "      - N'Djamena (capital)", 
        "    - Cote d'Ivoire (country)", 
        "      - Yamoussoukro (capital)", 
        "    - Egypt (country)", 
        "      - Cairo (capital)", 
        "    - Eritrea (country)", 
        "      - Asmara (capital)", 
        "  - Asia (continent)", 
        "    - Afghanistan (country)", 
        "      - Kabul (capital)", 
        "    - Bahrain (country)", 
        "      - Manama (capital)", 
        "    - Bangladesh (country)", 
        "      - Dhaka (capital)", 
        "    - Bhutan (country)", 
        "      - Thimphu (capital)", 
        "    - Brunei (country)", 
        "      - Bandar Seri Begawan (capital)", 
        "    - Cambodia (country)", 
        "      - Phnom Penh (capital)", 
        "    - People's Republic of China (country)", 
        "      - Beijing (capital)", 
        "  - Australia (continent)", 
        "    - Australia (country)", 
        "      - Canberra (capital)", 
        "  - Europe (continent)", 
        "    - Albania (country)", 
        "      - Tirana (capital)", 
        "    - Andorra (country)", 
        "      - Andorra la Vella (capital)", 
        "    - Austria (country)", 
        "      - Vienna (capital)", 
        "    - Belgium (country)", 
        "      - Brussels (capital)", 
        "    - Bosnia and Herzegovina (country)", 
        "      - Sarajevo (capital)", 
        "    - Bulgaria (country)", 
        "      - Sofia (capital)", 
        "    - Croatia (country)", 
        "      - Zagreb (capital)", 
        "    - Czech Republic (country)", 
        "      - Prague (capital)", 
        "    - Denmark (country)", 
        "      - Copenhagen (capital)", 
        "    - Finland (country)", 
        "      - Helsinki (capital)", 
        "    - France (country)", 
        "      - Paris (capital)", 
        "    - Germany (country)", 
        "      - Berlin (capital)", 
        "  - North America (continent)", 
        "    - Antigua and Barbuda (country)", 
        "      - Saint John's (capital)", 
        "    - Bahamas (country)", 
        "      - Nassau (capital)", 
        "    - Barbados (country)", 
        "      - Bridgetown (capital)", 
        "    - Canada (country)", 
        "      - Ottawa (capital)", 
        "  - South America (continent)", 
        "    - Argentina (country)", 
        "      - Buenos Aires (capital)", 
        "    - Bolivia (country)", 
        "      - La Paz (capital)", 
        "    - Brazil (country)", 
        "      - Brasilia (capital)", 
        "    - Chile (country)", 
        "      - Santiago (capital)", 
        "    - Colombia (country)", 
        "      - Bogota (capital)", 
        "    - Ecuador (country)", 
        "      - Quito (capital)" 
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testStructuredVisitor : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex, path) {
        return {
          name: vertex.name,
          type: vertex.type,
          level: path.vertices.length
        };
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, visitor : "UnitTests::visitor", visitorReturnsResults : true } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        { 
          "name" : "World", 
          "type" : "root", 
          "level" : 1 
        }, 
        { 
          "name" : "Africa", 
          "type" : "continent", 
          "level" : 2 
        }, 
        { 
          "name" : "Algeria", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Algiers", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Angola", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Luanda", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Botswana", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Gaborone", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Burkina Faso", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Ouagadougou", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Burundi", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Bujumbura", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Cameroon", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Yaounde", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Chad", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "N'Djamena", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Cote d'Ivoire", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Yamoussoukro", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Egypt", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Cairo", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Eritrea", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Asmara", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Asia", 
          "type" : "continent", 
          "level" : 2 
        }, 
        { 
          "name" : "Afghanistan", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Kabul", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bahrain", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Manama", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bangladesh", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Dhaka", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bhutan", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Thimphu", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Brunei", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Bandar Seri Begawan", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Cambodia", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Phnom Penh", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "People's Republic of China", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Beijing", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Australia", 
          "type" : "continent", 
          "level" : 2 
        }, 
        { 
          "name" : "Australia", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Canberra", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Europe", 
          "type" : "continent", 
          "level" : 2 
        }, 
        { 
          "name" : "Albania", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Tirana", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Andorra", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Andorra la Vella", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Austria", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Vienna", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Belgium", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Brussels", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bosnia and Herzegovina", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Sarajevo", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bulgaria", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Sofia", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Croatia", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Zagreb", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Czech Republic", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Prague", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Denmark", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Copenhagen", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Finland", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Helsinki", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "France", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Paris", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Germany", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Berlin", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "North America", 
          "type" : "continent", 
          "level" : 2 
        }, 
        { 
          "name" : "Antigua and Barbuda", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Saint John's", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bahamas", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Nassau", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Barbados", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Bridgetown", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Canada", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Ottawa", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "South America", 
          "type" : "continent", 
          "level" : 2 
        }, 
        { 
          "name" : "Argentina", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Buenos Aires", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Bolivia", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "La Paz", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Brazil", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Brasilia", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Chile", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Santiago", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Colombia", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Bogota", 
          "type" : "capital", 
          "level" : 4 
        }, 
        { 
          "name" : "Ecuador", 
          "type" : "country", 
          "level" : 3 
        }, 
        { 
          "name" : "Quito", 
          "type" : "capital", 
          "level" : 4 
        } 
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testParentPrinterVisitor : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex, path) {
        var result = {
          name: vertex.name,
          type: vertex.type,
          level: path.vertices.length
        };
        if (path.vertices.length > 1) {
          result.parent = {
            name: path.vertices[path.vertices.length - 2].name,
            type: path.vertices[path.vertices.length - 2].type
          }
        }
        return result;
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, visitor : "UnitTests::visitor", visitorReturnsResults : true } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        { 
          "name" : "World", 
          "type" : "root", 
          "level" : 1 
        }, 
        { 
          "name" : "Africa", 
          "type" : "continent", 
          "level" : 2, 
          "parent" : { 
            "name" : "World", 
            "type" : "root" 
          } 
        }, 
        { 
          "name" : "Algeria", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Algiers", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Algeria", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Angola", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Luanda", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Angola", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Botswana", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Gaborone", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Botswana", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Burkina Faso", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Ouagadougou", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Burkina Faso", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Burundi", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Bujumbura", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Burundi", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Cameroon", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Yaounde", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Cameroon", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Chad", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "N'Djamena", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Chad", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Cote d'Ivoire", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Yamoussoukro", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Cote d'Ivoire", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Egypt", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Cairo", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Egypt", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Eritrea", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Africa", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Asmara", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Eritrea", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Asia", 
          "type" : "continent", 
          "level" : 2, 
          "parent" : { 
            "name" : "World", 
            "type" : "root" 
          } 
        }, 
        { 
          "name" : "Afghanistan", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Kabul", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Afghanistan", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bahrain", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Manama", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bahrain", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bangladesh", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Dhaka", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bangladesh", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bhutan", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Thimphu", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bhutan", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Brunei", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Bandar Seri Begawan", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Brunei", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Cambodia", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Phnom Penh", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Cambodia", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "People's Republic of China", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Asia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Beijing", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "People's Republic of China", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Australia", 
          "type" : "continent", 
          "level" : 2, 
          "parent" : { 
            "name" : "World", 
            "type" : "root" 
          } 
        }, 
        { 
          "name" : "Australia", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Australia", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Canberra", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Australia", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Europe", 
          "type" : "continent", 
          "level" : 2, 
          "parent" : { 
            "name" : "World", 
            "type" : "root" 
          } 
        }, 
        { 
          "name" : "Albania", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Tirana", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Albania", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Andorra", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Andorra la Vella", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Andorra", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Austria", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Vienna", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Austria", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Belgium", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Brussels", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Belgium", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bosnia and Herzegovina", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Sarajevo", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bosnia and Herzegovina", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bulgaria", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Sofia", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bulgaria", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Croatia", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Zagreb", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Croatia", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Czech Republic", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Prague", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Czech Republic", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Denmark", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Copenhagen", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Denmark", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Finland", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Helsinki", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Finland", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "France", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Paris", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "France", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Germany", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "Europe", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Berlin", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Germany", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "North America", 
          "type" : "continent", 
          "level" : 2, 
          "parent" : { 
            "name" : "World", 
            "type" : "root" 
          } 
        }, 
        { 
          "name" : "Antigua and Barbuda", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "North America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Saint John's", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Antigua and Barbuda", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bahamas", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "North America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Nassau", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bahamas", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Barbados", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "North America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Bridgetown", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Barbados", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Canada", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "North America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Ottawa", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Canada", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "South America", 
          "type" : "continent", 
          "level" : 2, 
          "parent" : { 
            "name" : "World", 
            "type" : "root" 
          } 
        }, 
        { 
          "name" : "Argentina", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "South America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Buenos Aires", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Argentina", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Bolivia", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "South America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "La Paz", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Bolivia", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Brazil", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "South America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Brasilia", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Brazil", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Chile", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "South America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Santiago", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Chile", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Colombia", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "South America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Bogota", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Colombia", 
            "type" : "country" 
          } 
        }, 
        { 
          "name" : "Ecuador", 
          "type" : "country", 
          "level" : 3, 
          "parent" : { 
            "name" : "South America", 
            "type" : "continent" 
          } 
        }, 
        { 
          "name" : "Quito", 
          "type" : "capital", 
          "level" : 4, 
          "parent" : { 
            "name" : "Ecuador", 
            "type" : "country" 
          } 
        } 
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testLeafNodeVisitorWithoutCorrectOrder : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex, path, connected) {
        if (connected && connected.length === 0) {
          return 1;
        }
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, visitor : "UnitTests::visitor", visitorReturnsResults : true } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        // intentionally empty
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testLeafNodeVisitor : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex, path, connected) {
        if (connected && connected.length === 0) {
          return vertex.name + " (" + vertex.type + ")";
        }
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, visitor : "UnitTests::visitor", visitorReturnsResults : true, order : "preorder-expander" } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        "Algiers (capital)", 
        "Luanda (capital)", 
        "Gaborone (capital)", 
        "Ouagadougou (capital)", 
        "Bujumbura (capital)", 
        "Yaounde (capital)", 
        "N'Djamena (capital)", 
        "Yamoussoukro (capital)", 
        "Cairo (capital)", 
        "Asmara (capital)", 
        "Kabul (capital)", 
        "Manama (capital)", 
        "Dhaka (capital)", 
        "Thimphu (capital)", 
        "Bandar Seri Begawan (capital)", 
        "Phnom Penh (capital)", 
        "Beijing (capital)", 
        "Canberra (capital)", 
        "Tirana (capital)", 
        "Andorra la Vella (capital)", 
        "Vienna (capital)", 
        "Brussels (capital)", 
        "Sarajevo (capital)", 
        "Sofia (capital)", 
        "Zagreb (capital)", 
        "Prague (capital)", 
        "Copenhagen (capital)", 
        "Helsinki (capital)", 
        "Paris (capital)", 
        "Berlin (capital)", 
        "Saint John's (capital)", 
        "Nassau (capital)", 
        "Bridgetown (capital)", 
        "Ottawa (capital)", 
        "Buenos Aires (capital)", 
        "La Paz (capital)", 
        "Brasilia (capital)", 
        "Santiago (capital)", 
        "Bogota (capital)", 
        "Quito (capital)" 
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testLeafNodeVisitorInPlace : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex, path, connected) {
        if (connected && connected.length === 0) {
          result.push(vertex.name + " (" + vertex.type + ")");
        }
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, visitor : "UnitTests::visitor", visitorReturnsResults : false, order : "preorder-expander" } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        "Algiers (capital)", 
        "Luanda (capital)", 
        "Gaborone (capital)", 
        "Ouagadougou (capital)", 
        "Bujumbura (capital)", 
        "Yaounde (capital)", 
        "N'Djamena (capital)", 
        "Yamoussoukro (capital)", 
        "Cairo (capital)", 
        "Asmara (capital)", 
        "Kabul (capital)", 
        "Manama (capital)", 
        "Dhaka (capital)", 
        "Thimphu (capital)", 
        "Bandar Seri Begawan (capital)", 
        "Phnom Penh (capital)", 
        "Beijing (capital)", 
        "Canberra (capital)", 
        "Tirana (capital)", 
        "Andorra la Vella (capital)", 
        "Vienna (capital)", 
        "Brussels (capital)", 
        "Sarajevo (capital)", 
        "Sofia (capital)", 
        "Zagreb (capital)", 
        "Prague (capital)", 
        "Copenhagen (capital)", 
        "Helsinki (capital)", 
        "Paris (capital)", 
        "Berlin (capital)", 
        "Saint John's (capital)", 
        "Nassau (capital)", 
        "Bridgetown (capital)", 
        "Ottawa (capital)", 
        "Buenos Aires (capital)", 
        "La Paz (capital)", 
        "Brasilia (capital)", 
        "Santiago (capital)", 
        "Bogota (capital)", 
        "Quito (capital)" 
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testCountingVisitorInPlace : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result) {
        if (result.length === 0) {
          result.push(0);
        }
        result[0]++;
      });

      var result = AQL_EXECUTE('LET params = { visitor : "UnitTests::visitor", visitorReturnsResults : false } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      assertEqual([ 87 ], result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom visitor
////////////////////////////////////////////////////////////////////////////////

    testTypeCountingVisitorInPlace : function () {
      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex) {
        if (result.length === 0) {
          result.push({ });
        }
        var vertexType = vertex.type;
        if (! result[0].hasOwnProperty(vertexType)) {
          result[0][vertexType] = 1;
        }
        else {
          result[0][vertexType]++;
        }
      });

      var result = AQL_EXECUTE('LET params = { visitor : "UnitTests::visitor", visitorReturnsResults : false } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');
        
      var expected = [
        { 
          "root" : 1,
          "continent" : 6,
          "country" : 40,
          "capital" : 40
        }
      ];

      assertEqual(expected, result.json);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief registers a custom filter
////////////////////////////////////////////////////////////////////////////////

    testFilter : function () {
      aqlfunctions.register("UnitTests::filter", function (config, vertex) {
        if (vertex.type === "country") {
          return "prune";
        }

        if (vertex.type === "continent") {
          if (vertex.name !== "Europe") {
            return [ "prune", "exclude" ];
          }
        }

        return "exclude";
      });

      aqlfunctions.register("UnitTests::visitor", function (config, result, vertex) {
        return vertex.name;
      });

      var result = AQL_EXECUTE('LET params = { _sort : true, filterVertices : "UnitTests::filter", visitor : "UnitTests::visitor", visitorReturnsResults : true } FOR result IN TRAVERSAL(UnitTestsAhuacatlVertex, UnitTestsAhuacatlEdge, "UnitTestsAhuacatlVertex/world", "inbound", params) RETURN result');

      var expected = [
        "Albania", 
        "Andorra", 
        "Austria", 
        "Belgium", 
        "Bosnia and Herzegovina", 
        "Bulgaria", 
        "Croatia", 
        "Czech Republic", 
        "Denmark", 
        "Finland", 
        "France", 
        "Germany" 
      ];

      assertEqual(expected, result.json);
    }

  };

}  

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(ahuacatlGraphVisitorsSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
