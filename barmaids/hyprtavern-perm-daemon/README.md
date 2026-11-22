## hyprtavern-perm-daemon

A permission daemon implementing the core `hp_hyprtavern_permission_authentication_v1` protocol
for the tavern.

Prompts show the tavern-attested identity of the requesting app first. Names supplied by
the app itself are shown as such, and never as markup. Prompts are available whenever a
display can be reached, which follows the environment forwarded by the tavern.

This is a required component of the bus, but can be rewritten, as long
as it implements the protocol and the launch method below.

## cmdline

`hyprtavern-perm-daemon --control-fd [int]`

- `--control-fd`: the private control channel. The barmaid is the hyprwire server, serving
  `hp_hyprtavern_barmaid_v1` and `hp_hyprtavern_permission_authentication_v1` to the tavern.
