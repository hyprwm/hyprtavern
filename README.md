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
- Already implemented: You do not need GLib, Systemd, or any other dep. You need hyprwire
  and that's it.
- System-agnostic: Runs on systemd, systemd-less, BSD, etc.
