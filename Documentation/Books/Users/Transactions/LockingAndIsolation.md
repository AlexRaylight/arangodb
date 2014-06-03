<a name="locking_and_isolation"></a>
# Locking and Isolation

All collections specified in the `collections` attribute are locked in the
requested mode (read or write) at transaction start. Locking of multiple collections
is performed in alphabetical order.
When a transaction commits or rolls back, all locks are released in reverse order.
The locking order is deterministic to avoid deadlocks.

While locks are held, modifications by other transactions to the collections 
participating in the transaction are prevented.
A transaction will thus see a consistent view of the participating collections' data.

Additionally, a transaction will not be interrupted or interleaved with any other 
ongoing operations on the same collection. This means each transaction will run in
isolation. A transaction should never see uncommitted or rolled back modifications by
other transactions. Additionally, reads inside a transaction are repeatable.

Note that the above is true only for all collections that are declared in the 
`collections` attribute of the transaction.

There might be situations when declaring all collections a priori is not possible,
for example, because further collections are determined by a dynamic AQL query 
inside the transaction.
In this case, it would be impossible to know beforehand which collection to lock, and
thus it is legal to not declare collections that will be accessed in the transaction in
read-only mode. Accessing a non-declared collection in read-only mode during a 
transaction will add the collection to the transaction lazily, and fetch data 
from the collection as usual. However, as the collection ie added lazily, there is no 
isolation from other concurrent operations or transactions. Reads from such
collections are potentially non-repeatable.

Example:

    db._executeTransaction({
      collections: { 
        read: "users"
      },
      action: function () {
        // execute an AQL query that traverses a graph starting at a "users" vertex. 
        // it is yet unknown into which other collections the query will traverse 
        db._createStatement({ 
          query: "FOR t IN TRAVERSAL(users, connections, "users/1234", "any", { }) RETURN t"
        }).execute().toArray().forEach(function (d) {
          // ...
        });
      }
    });


This automatic lazy addition of collections to a transaction also introduces the 
possibility of deadlocks. Deadlocks may occur if there are concurrent transactions 
that try to acquire locks on the same collections lazily.

To recover from a deadlock state, ArangoDB will give up waiting for a collection
after a configurable amount of time. The wait time can be specified per transaction 
using the optional`lockTimeout`attribute. If no value is specified, some default
value will be applied.

If ArangoDB was waited at least `lockTimeout` seconds during lock acquisition, it
will give up and rollback the transaction. Note that the `lockTimeout` is used per
lock acquisition in a transaction, and not just once per transaction. There will be 
at least as many lock acquisition attempts as there are collections used in the 
transaction. The total lock wait time may thus be much higher than the value of
`lockTimeout`.


To avoid both deadlocks and non-repeatable reads, all collections used in a 
transaction should always be specified if known in advance.

