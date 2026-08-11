## hyprtavern

Let your apps meet and chat with each other.

A modern, simple and consistent session bus for IPC discovery.

> [!IMPORTANT]
> This project is still in early development. I'm working on adding docs and improving the protocol, but
> it's not set in stone yet.

## Why not D-Bus?

D-Bus is old. Bad. Poorly designed, poorly documented, slow, annoying.

Key advantages of hyprtavern over D-Bus:
- Consistent wire protocol: hyprtavern runs on hyprwire. It's a fast and consistent
  wire protocol, which means you cannot send random garbage over the wire like you can
  with D-Bus.
- Simplified API: D-Bus acquired a ton of garbage over the decades. We provide a simple
  wire protocol and bus API.
- Basic security built-in: Basic permissions and permission groups baked-in.
- No framework dependency: Hyprtavern does not require GLib or systemd. Its core IPC dependency is Hyprwire.
- System-agnostic: Runs on systemd, systemd-less, BSD, etc.

## Security model

Hyprtavern is a per-user session bus, not a boundary between unrestricted processes running as the same user. By default, a client that is positively identified as an unsandboxed, same-user host process bypasses ordinary permissions. Applications must still request permissions and handle denial because this policy is configurable.

Sandboxed clients do not receive that bypass. On Linux, Flatpak applications are identified from the original peer PID through `/proc/<pid>/root/.flatpak-info` and use their Flatpak application ID as their stable identity. Other confined or ambiguous clients are treated conservatively: permissions are enforced, and persistent app-scoped state is disabled when no stable identity is available. Flatpak detection is Linux-specific; BSD uses its platform process credentials and falls back to permission enforcement when identity cannot be established confidently.

Routed peer sockets do not provide the original application's credentials because they are created by the tavern. Hyprtavern therefore attaches a short-lived, one-time principal token to every routed connection. Providers redeem that token to obtain the core-attested application identifier and effective permissions.

The built-in KV store keeps app values under this attested identifier. Its directory and database are private to the user, and updates use a checked temporary-file, `fsync`, and atomic-rename sequence. Hyprtavern does not attempt to defend secrets from an unrestricted process running as the same user.

## Build dependencies

The complete build currently uses CMake and pkg-config with Hyprwire, Hyprwire protocols, Hyprutils, UUID, Glaze, OpenSSL, Hyprtoolkit, Pixman, libdrm, xkbcommon, Cairo/Pango, and the graphics/backend dependencies pulled in by Hyprtoolkit. Generated protocol bindings are written to the build directory, so out-of-source and read-only-source builds are supported.

## Non-goals

Hyprtavern intentionally does not aim for D-Bus feature parity. In particular, the design keeps discovery in the tavern while application traffic uses direct scoped Hyprwire connections; it does not add generic message routing, object-path semantics, activation, or introspection solely for compatibility.
