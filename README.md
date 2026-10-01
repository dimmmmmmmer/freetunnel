# FreeTunnel

[![Codacy Badge](https://app.codacy.com/project/badge/Grade/7080586146744b0095656b8eb9c51fff?branch=main)](https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Codacy coverage](https://app.codacy.com/project/badge/Coverage/7080586146744b0095656b8eb9c51fff?branch=main)](https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_coverage)
[![latest version](https://img.shields.io/github/v/release/dimmmmmmmer/freetunnel)](https://github.com/dimmmmmmmer/freetunnel/releases)
[![Tests](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml/badge.svg)](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml)
[![Security](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml/badge.svg)](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml)
[![Apache-2.0 License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)

<!-- markdownlint-disable-next-line MD033 -->
<img src="assets/logo.svg" width="96" align="right" alt="FreeTunnel logo"/>

**FreeTunnel** — a free, open-source desktop VPN client with a modern Qt interface,
built on the [TrustTunnel](https://github.com/TrustTunnel/TrustTunnelClient) core.

## About

FreeTunnel wraps TrustTunnel in a lightweight GUI: connect with one click, manage
configs, split tunneling, kill switch, system tray, and global hotkeys. Passwords
stay in the OS credential store (Keychain / Credential Manager / libsecret).

Updates are verified with SHA-256 manifests and Ed25519 signatures before install.

## Installation

Download a build for your platform from
[**Releases**](https://github.com/dimmmmmmmer/freetunnel/releases/latest):

| Platform | File |
| --- | --- |
| **Windows 10/11** | `freetunnel-windows-x86_64-Setup.exe` |
| **macOS** (Apple Silicon + Intel) | `freetunnel-macos-universal.dmg` |
| **Linux** (Debian/Ubuntu/Pop!_OS) | `freetunnel-linux-x86_64.deb` |
| **Linux** (portable, most distros with glibc 2.31 or newer: Ubuntu 20.04+, Debian 11+, RHEL 9, Fedora, Arch…) | `freetunnel-x86_64.AppImage` |

Builds are **unsigned** (no code-signing certificates), so the OS may warn on first launch:

- **macOS**: right-click the app → **Open** (or
  `xattr -dr com.apple.quarantine /Applications/FreeTunnel.app`).
- **Windows**: SmartScreen → **More info** → **Run anyway**.
- **Linux (.deb)**: `sudo apt install ./freetunnel-linux-x86_64.deb`
- **Linux (AppImage)**: `chmod +x freetunnel-x86_64.AppImage && ./freetunnel-x86_64.AppImage`

VPN requires elevated privileges: Windows shows UAC; on Linux/macOS you enter an
admin password the first time you connect in a session.

## Quick start

1. On the **Configs** tab (＋), create a config or import one (see below).
2. On the home screen, pick a config and click the **logo** to connect /
   disconnect.
3. In **Settings**: auto-connect on startup, kill switch, theme, language,
   hotkeys, and update checks.

## Importing a configuration

- **Configs → ＋** — create a new TOML, import from file, or paste a `tt://`
  link from the clipboard (⌘V or Ctrl+V on the **Configs** page does the same).
- **A `tt://` link** opened from a browser or another app imports too.
  FreeTunnel names the server the link would add and asks before adding it, as
  it does for a pasted link.

## Split tunnelling

The **Split tunnelling** tab decides what goes through the tunnel. Rules are
either addresses — a domain, an IP, a subnet — or programs, and both obey the
**Mode** switch at the top of the page: under *Bypass VPN* what is listed goes
around the tunnel, under *Through VPN* it is the only thing inside it. Under
*Through VPN* with no rule that can be used, FreeTunnel keeps the full tunnel
and the page says so, rather than send everything around it.

Every change on this page, turning split tunnelling on or off included, reaches
a tunnel that is already up without reconnecting it. A program rule applies from
the program's next connection; after a change to the addresses or the mode,
connections that were open start again under the new rules.

Add a program from the list of what is installed, by dragging its icon onto the
page, or by picking the file yourself. Which program a connection belongs to is
worked out by asking the operating system who owns the socket, so a rule added
while the VPN is up applies to the next connection rather than the next session.
Nothing extra is installed for this: no driver, no system extension, no
permission dialog. On Windows, a rule for an app that updates itself the way
Discord and Slack do, from a folder named after each version, names the app and
keeps matching it after its updates.

Rules are grouped into profiles — addresses and programs both — and a config can
be tied to one, so a set for work and a set for everything else can be switched
between without retyping either. Switching to another config while connected
reconnects, with the new config's profile.

**Settings → Excluded routes** lists subnets that stay outside the tunnel for
every config. They are added to the routes a config excludes itself, not used in
their place: a config made in FreeTunnel or imported from a link already keeps
local networks and multicast outside the tunnel, and emptying the list in
Settings does not change that. Unlike the rules above, a change to this list
while connected reconnects. A route of every address (`0.0.0.0/0`, `::/0`) is
refused here and on the Split tunnelling page, since it would take all of that
traffic out of the tunnel.

## Kill switch

The kill switch in **Settings → Security** is for a connection that is not up:
while FreeTunnel connects, or brings back a connection that dropped, connections
that would have gone out over the open network are refused instead. What split
tunnelling sends around the tunnel still goes. A first connect that keeps
failing keeps trying, blocked, until it gets through or you press
**Disconnect**; the status reads "Connecting…" ("Waiting for network…" while
there is none), and the window says why it is failing.

The block belongs to the running VPN session, so it is not a firewall of its
own. Nothing is blocked while the VPN is off or after an error has stopped it.
The session stays up, and the block with it, through any change on the
**Split tunnelling** page: a program rule applies from the program's next
connection, and a domain, address or mode change restarts the connections that
are open. The block lifts for the moment it takes to build a session anew,
which happens when you switch configs while connected, or change the excluded
routes, the kill switch itself or, on Windows, **Let the VPN config open ports**;
and when the server refuses the login or its certificate and FreeTunnel starts
over. A config that names its server by a domain name rather than an IP address
gets no session at all, and so no block, while that name cannot be looked up.

**Windows: ports a config opens.** A config file can name ports that pass the
kill switch (`killswitch_allow_ports`), for example to reach this computer over
Remote Desktop while the kill switch is on. FreeTunnel ignores them unless **Let
the VPN config open ports** is on, under the kill switch in Settings. That
setting is one for every config, including any you import later, so turn it on
only if you trust every config you use.

## External control

- **Commands and links**: `freetunnel://toggle`, `freetunnel://connect`,
  `freetunnel://disconnect`, plus `tt://…` for import. Each user runs one
  FreeTunnel: launching it again, with a command or without, hands over to the
  copy already running. If that copy cannot be reached, as when the keyring
  that holds its launch key is locked, the new launch closes without acting on
  the command rather than start a second FreeTunnel; run it again once the
  keyring is unlocked.
  - **From a Stream Deck button or a script**, run FreeTunnel with the command
    as its argument. A running FreeTunnel acts on it at once and leaves the
    window alone, as with the hotkeys; otherwise FreeTunnel starts and acts on it:
    - Windows: `"C:\Program Files\FreeTunnel\FreeTunnel.exe" freetunnel://toggle`
      (or wherever you installed it)
    - macOS: `/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel freetunnel://toggle`
    - Linux: `freetunnel freetunnel://toggle` (.deb), or the AppImage's path
      followed by `freetunnel://toggle`
  - **Opened as a link** — from a browser, a document, `open` on macOS,
    `xdg-open` — the same URL works too, but any web page can open a link, so
    one that would turn the VPN off brings up the window and asks first.
    Connecting is not asked about. See [DEEP_LINK.md](DEEP_LINK.md#freetunnel-control-links-separate).
- **Global hotkeys** for toggle, connect and disconnect, in **Settings →
  Hotkeys**. They are off until you turn them on there. A hotkey needs Ctrl, Alt
  or Meta (⌘, ⌥ or ⌃ on macOS), unless it is one of F1–F12, and works while the
  window is minimized or hidden. On Linux they need an X11 (Xorg) session: under
  Wayland the setting is unavailable.
- **System tray** (the menu bar on macOS): connect or disconnect, switch
  configs, show the window, quit.
- **Closing the window** keeps FreeTunnel and the VPN running. On Windows and
  Linux ✕ minimizes the window to the taskbar; on macOS the red button (or ⌘W)
  hides it, and the menu-bar icon or the Dock brings it back. To quit, choose
  **Quit** in the tray menu (or press ⌘Q on macOS). On a Linux desktop with no
  system tray, such as GNOME without an AppIndicator extension, ✕ quits, since
  nothing would be left to bring the window back from.

## For developers

See [CONTRIBUTING.md](CONTRIBUTING.md) for local build instructions, tests,
translations, and Codacy setup. Builds are fully automated in GitHub Actions
([`.github/workflows/build.yml`](.github/workflows/build.yml)): the client links
against the C++ core [`TrustTunnel/TrustTunnelClient`](https://github.com/TrustTunnel/TrustTunnelClient).
HTTP/3 (QUIC) builds are enabled in CI (`DISABLE_HTTP3=OFF`).
Unit tests — [`.github/workflows/tests.yml`](.github/workflows/tests.yml). Security checks —
[`.github/workflows/security.yml`](.github/workflows/security.yml) and [SECURITY.md](SECURITY.md).
Releases are published automatically on `v*` tags, once Tests and Security have
passed on the tagged commit.

## License

[Apache-2.0](LICENSE)
