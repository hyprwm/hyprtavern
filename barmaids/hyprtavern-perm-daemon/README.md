## hyprtavern-perm-daemon

A permission daemon implementing the core `hp_hyprtavern_permission_authentication_v1` protocol
for the tavern.

This is a required component of the bus, but can be rewritten, as long
as it implements the protocol and the proper launch method with
passing a wire fd.

## cmdline

`hyprtavern-perm-daemon --fd [int]`
