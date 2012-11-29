Edge {#GlossaryEdge}
====================

@GE{Edge}: Edges in ArangoDB are special documents. In addition to the
internal attributes `_key`, `_id` and `_rev`, they have two attributes
`_from` and `_to`, which contain document handles, namely the
start-point and the end-point of the edge.
The values of `_from` and `_to` are immutable once saved.
