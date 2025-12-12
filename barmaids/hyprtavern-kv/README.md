## hyprtavern-kv

A key-value store implementing the core `hp_hyprtavern_kv_store_v1` protocol
for the tavern.

This is a required component of the bus, but can be rewritten, as long
as it implements the protocol and the proper launch method with
passing a wire fd.

## cmdline

`hyprtavern-kv --fd [int]`
