Deprecated Features
-------------------

This file lists all features that have been deprecated in ArangoDB
or are known to become deprecated in a future version of ArangoDB.
Deprecated features will likely be removed in upcoming versions of
ArangoDB and shouldn't be used if possible.

## 2.5

* Foxx: method `controller.collection()` has been removed entirely. Please use `appContext.collection()` instead.
* Foxx: method `FoxxRepository.modelPrototype` has been removed entirely. Please use `FoxxRepository.model` instead.
* Foxx: the signature of `Model.extend()` has changed. `Model.extend({}, {attributes: {}})` does not work anymore. Please use `Model.extend({schema: {}})` instead.
* Foxx: the signature of method `requestContext.bodyParam()` has changed. `requestContext.bodyParam(paramName, description, Model)` does not work anymore. Please use `requestContext.bodyParam(paramName, options)` instead.
* Foxx: the signature of method `requestContext.queryParam()` has changed. `requestContext.queryParam({type: "string"})` does not work anymore. Please use `requestContext.queryParam({type: joi.string()})` instead.
* Foxx: the signature of method `requestContext.pathParam()` has changed. `requestContext.pathParam({type: "string"})` does not work anymore. Please use `requestContext.pathParam({type: joi.string()})` instead.
* Foxx: method `Model#toJSONSchema(id)` is deprecated, it will raise a warning if you use it. Please use `Foxx.toJSONSchema(id, model)` instead.
* Graphs: The modules `org/arangodb/graph` and `org/arangodb/graph-blueprint` have been removed entirely. Please use module `org/arangodb/general-graph` instead.
* General-Graph: In the module `org/arangodb/general-graph` the functions `_undirectedRelation` and `_directedRelation` are no longer available. Both functions have been unified to `_relation`.
* HTTP API: The api `_api/graph` has been removed entirely. Please use the general graph api `_api/gharial` instead.


## 2.6
* Foxx: method `Model#toJSONSchema(id)` has been removed entirely. Please use `Foxx.toJSONSchema(id, model)` instead.
