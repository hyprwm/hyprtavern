# Permissions

This document defines Hyprtavern permission names and authorization semantics. Hyprwire is the transport used by Hyprtavern; these permissions are not general Hyprwire permissions.

## Permission names

A permission name is a lowercase ASCII string containing at least two dot-separated segments. Each segment MUST match `[a-z][a-z0-9_-]*`. A name MUST be no longer than 255 bytes and contain no more than 16 segments. Implementations MUST reject invalid names and MUST NOT case-fold or otherwise normalize them.

The segment `all` is reserved for the end of a name. A grant ending in `.all` grants every permission below its parent at any current or future depth. Matching is segment-aware: `example.settings.all` grants `example.settings.theme.dark`, but grants neither `example.setting.theme` nor `example.settings`.

The `core` namespace is reserved by Hyprtavern. A `core.*` name not listed below MUST be rejected. Syntactically valid names outside `core` are vendor permissions and MUST be accepted.

## Authorization

`core.unobtainium.tavernkeep` identifies Hyprtavern's internal authority and MUST NOT be externally requestable. `core.unobtainium.unsandboxed` is a derived host-status permission: clients MAY request it to check their eligibility, but it MUST NOT prompt, be persisted, or appear in explicit grants. Every other standard permission and every valid vendor permission is an ordinary, externally requestable permission.

By default, a verified unsandboxed host process with the same effective UID as Hyprtavern bypasses ordinary permission checks. This is a full ordinary-permission bypass, not a default grant limited to environment access. Sandboxed, differently owned, or unverified processes do not receive the bypass. Hosts MAY disable this policy, in which case host processes require explicit ordinary grants as well.

Neither ordinary bypass nor a catch-all grant confers `core.unobtainium.tavernkeep` or `core.unobtainium.unsandboxed`. Internal authority is established by the tavern. `core.unobtainium.unsandboxed` is satisfied only by positively verified same-user, unsandboxed host status, independently of the ordinary-bypass setting. A request for it MUST return `granted_by_policy` for such a host and `denied` otherwise, regardless of requested persistence. Missing or ambiguous sandbox information MUST NOT be treated as proof of host status.

A prompt for a standard permission SHOULD present its localized standard description. A prompt for a vendor permission MUST use the same grant and denial flow, identify the requesting application, and display the exact permission name. Prompt implementations MUST treat vendor names as opaque; they MUST NOT infer trust from a namespace or silently broaden a requested grant. A terminal `.all` request must make its recursive subtree effect clear.

## Admission and grant lifetime

Authorization of routed service connections is admission-only. The tavern MUST check the current object-level requirements and every requested protocol's requirements before issuing either endpoint. It MUST issue the full requested scope or fail; `connect` MUST NOT implicitly prompt. Providers MUST allowlist that scope before handling the handshake or any client input, including rejecting attempts to bind unadvertised protocols.

The scope is immutable for the connection. Revocation, expiration, context destruction, broker disconnection, and later grants affect future admissions, not existing service sockets. Expanding a scope requires a new connection. Principal requery is informational, not reauthorization. Application protocols MAY define their own additional authorization, and broker management requests still require authorization when processed.

A connection-principal token MUST retain an admission-time snapshot independently of the originating client and permission context until redemption or expiry. An unexpired, unused token redeemed by its intended recipient MUST remain usable after the originating client disconnects or destroys its context. Initial redemption returns that admission snapshot. The redeemed principal object retains its admitted identity and authority independently of the consumed token's expiry; later requery of current policy, display metadata, and grants MAY fail if live state is unavailable. Neither PID reuse nor a replacement client connection may retarget the admitted identity.

Session grants last until the permission context is destroyed or its client connection closes, whichever happens first. They do not automatically authorize other connections from the same application. Persistent grants are saved under a restoration token and its stable attested owner. Core `permission_result.grant_mode` MUST report the actual lifetime of explicit grants; permanent success MUST NOT be reported before the grant is durably stored. Policy results are not explicit or persistent grants.

The union of a context's session and permanent explicit grants MUST contain at most 512 distinct names. Requests that would exceed this limit MUST return `permission_result` with `failed` without adding or persisting a grant. A context MUST have at most 512 pending requests, and further requests MUST return `failed` as well. Restoration that would exceed the limit MUST make the context unavailable, without partial restoration or truncation. Explicit grant snapshots MUST contain each name at most once.

Enumeration and revocation of saved grants are reserved for a separate permission-management protocol; the core v1 protocol does not provide those operations.

## Identity and persistence

An application's identity is the pair of its attested identity kind and identifier. A PID, self-asserted display name, or sandbox-local executable path MUST NOT substitute for it. Persistent app-scoped state requires a stable attested identity. Routed sockets are created by the tavern; providers MUST use its attested principal metadata rather than those sockets' peer credentials to identify the original application.

The `host_executable` identity kind MUST only describe an attested executable of a positively verified unsandboxed host process with the same effective UID as the tavern. It attests `core.unobtainium.unsandboxed` at admission independently of ordinary permission bypass, including when `permission_policy` is `enforced`. Missing or ambiguous sandbox information MUST NOT be reported as `host_executable`.

A persistence token selects an independent saved permission context; possession alone does not authorize restoration. The tavern MUST verify the original stable owner before restoring grants or display metadata. Unknown tokens MUST NOT be adopted as new token values: the tavern generates a fresh token when it can durably create a saved context.

The KV store persists values in plaintext, without a password or lock/unlock state. Initialization MUST NOT depend on a display or user interaction. Encryption at rest, if desired, is provided externally, such as by disk encryption. Filesystem and sandbox isolation MUST prevent sandboxed applications from accessing the backing files directly. App and tavern namespace checks remain mandatory; unrestricted same-user processes are outside this isolation boundary.

While the store is initializing or unavailable, session grants and policy decisions remain usable, but unverified saved grants MUST NOT be restored. The tavern MUST retain a supplied token as an unverified restoration hint and retry when the store becomes available. An empty `persistence_token` event indicates no verified saved context yet and MUST NOT overwrite the client's previous durable token. A nonempty token may be emitted only after ownership verification and durable restoration-state creation, as applicable. Unknown or unstable identities remain session-only.

Clients storing restoration tokens in app-scoped KV values MUST read any previously saved token before replacing it. A failed or unavailable lookup MUST NOT be treated as `value_missing`.

## Trusted providers and permission prompts

Barmaids are started by the tavern and authenticated through private startup channels. The tavern MUST reserve the barmaid, KV-store, and permission-authentication protocol names for their corresponding authenticated providers. Exclusivity is not provider authentication. An arbitrary registration, provider-supplied property, or ordinary-permission bypass MUST NOT establish a trusted infrastructure role. The barmaid control protocol MUST NOT be exposed on application-facing connections.

Permission prompts MUST distinguish attested identity from self-asserted display text. The requesting context, typed identity, permission name, and offered lifetime MUST be frozen when the prompt is requested. Destroying the context MUST cancel pending prompts; stale or mismatched responses MUST NOT create grants. `accepted_once` means a session grant, not a single service operation. Persistent consent still requires a successful durable write by the tavern.

Implementations SHOULD bound outstanding requests and rate-limit prompts per attested principal, rather than only per connection, so reconnecting does not reset prompt throttling. Resource or prompt unavailability MUST produce a defined failure rather than an indefinitely pending request.

## Standard catalog

### `core.unobtainium.tavernkeep`

Internal Hyprtavern authority. This permission is not requestable.

### `core.unobtainium.unsandboxed`

Verified same-user, unsandboxed host status. This permission is derived by the tavern and cannot be obtained through user consent, stored grants, catch-all matching, or ordinary permission bypass. Disabling ordinary bypass does not remove it from a verified host. It is required to set or unset any Tavern environment variable, including before a desktop environment has initialized permission-prompt UI.

All environment mutation is host-only: variables other than `DISPLAY` and `WAYLAND_DISPLAY`, such as `XDG_RUNTIME_DIR` and `LD_PRELOAD`, can also redirect or compromise trusted barmaids. No ordinary grant authorizes environment updates. The tavern MUST validate the complete request and check host status before applying or forwarding any changes. A valid but unauthorized request MUST return `update_tavern_environment_result` with `denied`, without changing the environment or disconnecting the client; malformed requests remain protocol errors. Results MUST be correlated in request order on each manager.

### `core.settings.basic.all`

Manage all basic, non-sensitive settings, including future permissions added below this subtree.

### `core.settings.basic.wallpaper`

Manage wallpapers.

### `core.settings.sensitive.manage_secrets`

View and modify all secrets in the key-value store.

### `core.settings.sensitive.manage_permissions`

View and modify application permission grants.

### `core.monitoring.sensitive.all_bus_objects`

View every object on the Hyprtavern bus, including objects to which the client cannot connect.
