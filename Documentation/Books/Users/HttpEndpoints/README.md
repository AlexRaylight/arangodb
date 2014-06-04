<a name="http_interface_for_endpoints"></a>
# HTTP Interface for Endpoints

@NAVIGATE_HttpEndpoint
@EMBEDTOC{HttpEndpointTOC}

<a name="endpoints"></a>
### Endpoints

The ArangoDB server can listen for incoming requests on multiple *endpoints*.

The endpoints are normally specified either in ArangoDB's configuration file or on
the command-line, using the @ref CommandLineArangoEndpoint "--server.endpoint" option.
The default endpoint for ArangoDB is `tcp://127.0.0.1:8529` or `tcp://localhost:8529`.

The number of endpoints can also be changed at runtime using the API described
below. Each endpoint can optionally be restricted to a specific list of databases
only, thus allowing the usage of different port numbers for different databases.  

This may be useful in multi-tenant setups. 
A multi-endpoint setup may also be useful to turn on encrypted communication for
just specific databases.

The HTTP interface provides operations to add new endpoints at runtime, and
optionally restrict them for use with specific databases. The interface also can
be used to update existing endpoints or remove them at runtime.

Please note that all endpoint management operations can only be accessed via
the default database (`_system`) and none of the other databases.

<a name="managing_endpoints_via_http"></a>
## Managing Endpoints via HTTP

@anchor HttpEndpointPost
@copydetails JSF_post_api_endpoint

@CLEARPAGE
@anchor HttpEndpointDelete
@copydetails JSF_delete_api_endpoint

@CLEARPAGE
@anchor HttpEndpointGet
@copydetails JSF_get_api_endpoint
