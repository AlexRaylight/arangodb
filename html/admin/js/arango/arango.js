/*jslint indent: 2, nomen: true, maxlen: 100, sloppy: true, vars: true, white: true, plusplus: true */
/*global require, exports, Backbone, window, arangoCollection, $, arangoHelper, data */
/*global arrayContainer:true, SliderInstance:true, DomObjects:true */

arangoHelper = {
  CollectionTypes: {},
  systemAttributes: function () {
    return {
      '_id' : true,
      '_rev' : true,
      '_key' : true,
      '_bidirectional' : true,
      '_vertices' : true,
      '_from' : true,
      '_to' : true,
      '$id' : true
    };
  },
  arangoNotification: function (message) {
    $.gritter.add({
      title: "Notification:",
      text: message,
      time: 800
    });
  },
  arangoError: function (message) {
    $.gritter.add({
      title: "Error:",
      text: message,
      sticky: true
    });
  },
  getRandomToken: function () {
    return Math.round(new Date().getTime());
  },

  isSystemAttribute: function (val) {
    var a = this.systemAttributes();
    return a[val];
  },

  isSystemCollection: function (val) {
    //return val && val.name && val.name.substr(0, 1) === '_';
    return val.substr(0, 1) === '_';
  },

  collectionApiType: function (identifier) {
    if (this.CollectionTypes[identifier] === undefined) {
      this.CollectionTypes[identifier] = window.arangoDocumentStore
      .getCollectionInfo(identifier).type;
    }

    if (this.CollectionTypes[identifier] === 3) {
      return "edge";
    }
    return "document";
  },

  collectionType: function (val) {
    if (! val || val.name === '') {
      return "-";
    }
    var type;
    if (val.type === 2) {
      type = "document";
    }
    else if (val.type === 3) {
      type = "edge";
    }
    else {
      type = "unknown";
    }

    if (val.name.substr(0, 1) === '_') {
      type += " (system)";
    }

    return type;
  },

  FormatJSON: function (oData, sIndent) {
    var self = this;
    var sHTML, iCount;
    if (sIndent === undefined) {
      sIndent = "";
    }
    var sIndentStyle = " ";
    var sDataType = arangoHelper.RealTypeOf(oData);

    if (sDataType === "array") {
      if (oData.length === 0) {
        return "[]";
      }
      sHTML = "[";
    } else {
      iCount = 0;
      $.each(oData, function() {
        iCount++;
        return;
      });
      if (iCount === 0) { // object is empty
        return "{}";
      }
      sHTML = "{";
    }

    iCount = 0;
    $.each(oData, function(sKey, vValue) {
      if (iCount > 0) {
        sHTML += ",";
      }
      if (sDataType === "array") {
        sHTML += ("\n" + sIndent + sIndentStyle);
      } else {
        sHTML += ("\n" + sIndent + sIndentStyle + JSON.stringify(sKey) + ": ");
      }

      // display relevant data type
      switch (arangoHelper.RealTypeOf(vValue)) {
        case "array":
          case "object":
          sHTML += self.FormatJSON(vValue, (sIndent + sIndentStyle));
        break;
        case "boolean":
          case "number":
          sHTML += vValue.toString();
        break;
        case "null":
          sHTML += "null";
        break;
        case "string":
          sHTML += "\"" + vValue.replace(/\\/g, "\\\\").replace(/"/g, "\\\"") + "\"";
        break;
        default:
          sHTML += ("TYPEOF: " + typeof vValue);
      }
      // loop
      iCount++;
    });

    // close object
    if (sDataType === "array") {
      sHTML += ("\n" + sIndent + "]");
    } else {
      sHTML += ("\n" + sIndent + "}");
    }

    // return
    return sHTML;
  },

  RealTypeOf: function (v) {
    if (typeof v === "object") {
      if (v === null) {
        return "null";
      }
      var array = [];
      if (v.constructor === array.constructor) {
        return "array";
      }
      var date = new Date();
      if (v.constructor === date.constructor) {
        return "date";
      }
      var regexp = new RegExp();
      if (v.constructor === regexp.constructor) {
        return "regex";
      }
      return "object";
    }
    return typeof v;
  }






};
