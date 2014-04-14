Naming Conventions in ArangoDB {#NamingConventions}
===================================================

@NAVIGATE_NamingConventions
@EMBEDTOC{NamingConventionsTOC}

The following naming conventions should be followed by users when creating
databases, collections, and documents in ArangoDB.

Database Names {#DatabaseNames}
===============================

ArangoDB will always start up with a default database, named `_system`.
Users can create additional databases in ArangoDB, provided the database
names conform to the following constraints:

- Database names must only consist of the letters `a` to `z` (both lower and
  upper case allowed), the numbers `0` to `9`, and the underscore (`_`) or 
  dash (`-`) symbols
  This also means that any non-ASCII database names are not allowed
- Database names must always start with a letter. Database names starting 
  with an underscore are considered to be system databases, and users should 
  not create or delete those
- The maximum allowed length of a database name is 64 bytes
- Database names are case-sensitive

Collection Names {#CollectionNames}
===================================

Users can pick names for their collections as desired, provided the following
naming constraints are not violated:

- Collection names must only consist of the letters `a` to `z` (both in lower 
  and upper case), the numbers `0` to `9`, and the underscore (`_`) or dash (`-`)
  symbols. This also means that any non-ASCII collection names are not allowed
- User-defined collection names must always start with a letter. System collection
  names must start with an underscore. 
  All collection names starting with an underscore are considered to be system 
  collections that are for ArangoDB's internal use only. System collection names 
  should not be used by end users for their own collections
- The maximum allowed length of a collection name is 64 bytes
- Collection names are case-sensitive

Document Keys {#DocumentKeys}
=============================

Users can define their own keys for documents they save. The document key will
be saved along with a document in the `_key` attribute. Users can pick key
values as required, provided that the values conform to the following
restrictions:
- The key must be at least 1 byte and at most 254 bytes long. Empty keys are 
  disallowed when specified (though it may be valid to completely omit the
  `_key` attribute from a document)
- It must consist of the letters a-z (lower or upper case), the digits 0-9,
  the underscore (_), dash (-), or colon (:) characters only
	- Any other characters, especially multi-byte sequences, whitespace or
	  punctuation characters cannot be used inside key values
- The key must be unique within the collection it is used

Keys are case-sensitive, i.e. `myKey` and `MyKEY` are considered to be
different keys.

Specifying a document key is optional when creating new documents. If no
document key is specified by the user, ArangoDB will create the document key
itself as each document is required to have a key.

There are no guarantees about the format and pattern of auto-generated document
keys other than the above restrictions. Clients should therefore treat
auto-generated document keys as opaque values and not rely on their format.

Attribute Names {#AttributeNames}
=================================

Users can pick attribute names for document attributes as desired, provided the
following attribute naming constraints are not violated:
- Attribute names starting with an underscore are considered to be system
  attributes for ArangoDB's internal use. Such attribute names are already used
  by ArangoDB for special purposes, e.g. `_id` is used to contain a document's
  handle, `_key` is used to contain a document's user-defined key, and `_rev` is
  used to contain the document's revision number. In edge collections, the
  `_from` and `_to` attributes are used to reference other documents.

  More system attributes may be added in the future without further notice so
  end users should not use attribute names starting with an underscore for their
  own attributes.

- Attribute names should not start with the at-mark (`\@`). The at-mark
  at the start of attribute names is reserved in ArangoDB for future use cases.
- Theoretically, attribute names can include punctuation and special characters
  as desired, provided the name is a valid UTF-8 string.  For maximum
  portability, special characters should be avoided though.  For example,
  attribute names may contain the dot symbol, but the dot has a special meaning
  in Javascript and also in AQL, so when using such attribute names in one of
  these languages, the attribute name would need to be quoted by the end
  user. This will work but requires more work so it might be better to use
  attribute names which don't require any quoting/escaping in all languages
  used. This includes languages used by the client (e.g. Ruby, PHP) if the
  attributes are mapped to object members there.
- ArangoDB does not enforce a length limit for attribute names. However, long
  attribute names may use more memory in result sets etc. Therefore the use
  of long attribute names is discouraged.
- As ArangoDB saves document attribute names separate from the actual document
  attribute value data, the combined length of all attribute names for a
  document must fit into an ArangoDB shape structure. The maximum combined names
  length is variable and depends on the number and data types of attributes
  used.
- Attribute names are case-sensitive.

- Attributes with empty names (the empty string) and attributes with names that
  start with an underscore and don't have a special meaning (system attributes)
  are removed from the document when saving it. 

  When the document is later requested, it will be returned without these 
  attributes. For example, if this document is saved

      { "a" : 1, "" : 2, "_test" : 3, "b": 4 }

  and later requested, it will be returned like this:
      
      { "a" : 1, "b": 4 }

@BNAVIGATE_NamingConventions
