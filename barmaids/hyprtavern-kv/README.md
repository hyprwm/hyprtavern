## hyprtavern-kv

A key-value store implementing the core `hp_hyprtavern_kv_store_v1` protocol
for the tavern.

Values are stored in plaintext at `$XDG_DATA_HOME/hyprtavern/kv.json`, in a directory
private to the user. Every update is committed durably before it is acknowledged. If
you want encryption at rest, use disk encryption.

This is a required component of the bus, but can be rewritten, as long
as it implements the protocol and the launch method below.

## cmdline

`hyprtavern-kv --control-fd [int] --tavern-fd [int]`

- `--control-fd`: the private control channel. The barmaid is the hyprwire server, serving
  `hp_hyprtavern_barmaid_v1` and `hp_hyprtavern_kv_store_v1` to the tavern.
- `--tavern-fd`: a client connection to the tavern's bus, used to expose the store to apps.
