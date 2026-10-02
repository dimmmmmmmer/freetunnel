# FreeTunnel

[![Codacy Badge](https://app.codacy.com/project/badge/Grade/7080586146744b0095656b8eb9c51fff?branch=main)](https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Codacy coverage](https://app.codacy.com/project/badge/Coverage/7080586146744b0095656b8eb9c51fff?branch=main)](https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_coverage)
[![latest version](https://img.shields.io/github/v/release/dimmmmmmmer/freetunnel)](https://github.com/dimmmmmmmer/freetunnel/releases)
[![Tests](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml/badge.svg)](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml)
[![Security](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml/badge.svg)](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml)
[![Apache-2.0 License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)

<!-- markdownlint-disable-next-line MD033 -->
<img src="assets/logo.svg" width="96" align="right" alt="FreeTunnel logo"/>

**FreeTunnel** — a free, open-source desktop VPN client with a modern Qt
interface, built on the [TrustTunnel](https://github.com/TrustTunnel/TrustTunnelClient)
core. It wraps TrustTunnel in a lightweight GUI for Windows, macOS and Linux:
connect with one click, manage configs, split tunnelling, kill switch, system
tray and global hotkeys.

This page is for people installing and using FreeTunnel. To build it or work on
it, see [CONTRIBUTING.md](CONTRIBUTING.md).

## Contents

- [Download](#download)
- [Install and first launch](#install-and-first-launch)
- [Quick start](#quick-start)
- [System tray and closing the window](#system-tray-and-closing-the-window)
- [Importing a configuration](#importing-a-configuration)
- [Split tunnelling](#split-tunnelling)
- [Kill switch](#kill-switch)
- [Settings](#settings)
- [Global hotkeys](#global-hotkeys)
- [External control](#external-control) (Stream Deck, scripts, links)
- [Updates](#updates)
- [Building from source](#building-from-source)
- [Security](#security)
- [License](#license)

## Download

Get the build for your platform from
[**Releases**](https://github.com/dimmmmmmmer/freetunnel/releases/latest):

| Platform | File |
| --- | --- |
| **Windows 10/11** | `freetunnel-windows-x86_64-Setup.exe` |
| **macOS** (Apple Silicon + Intel) | `freetunnel-macos-universal.dmg` |
| **Linux** (Debian/Ubuntu/Pop!_OS) | `freetunnel-linux-x86_64.deb` |
| **Linux** (portable, most distros with glibc 2.31 or newer: Ubuntu 20.04+, Debian 11+, RHEL 9, Fedora, Arch…) | `freetunnel-x86_64.AppImage` |

## Install and first launch

Builds are **unsigned** (no code-signing certificates), so the OS may warn on
first launch.

### Windows

If SmartScreen warns: **More info** → **Run anyway**.

### macOS

Right-click the app → **Open**. Or clear the quarantine flag:

```sh
xattr -dr com.apple.quarantine /Applications/FreeTunnel.app
```

### Linux

The .deb:

```sh
sudo apt install ./freetunnel-linux-x86_64.deb
```

The AppImage:

```sh
chmod +x freetunnel-x86_64.AppImage && ./freetunnel-x86_64.AppImage
```

### Administrator prompt

The VPN requires elevated privileges:

| Platform | What you see |
| --- | --- |
| Windows | A UAC prompt |
| macOS, Linux | A prompt for an admin password, the first time you connect in a session |

## Quick start

1. On the **Configs** tab (＋), create a config or import one (see
   [Importing a configuration](#importing-a-configuration)).
2. On the home screen, pick a config and click the **logo** to connect /
   disconnect.
3. In **Settings**: auto-connect on startup, kill switch, theme, language,
   hotkeys, and update checks (see [Settings](#settings)).

## System tray and closing the window

The **system tray** (the menu bar on macOS) can connect or disconnect, switch
configs, show the window and quit.

Closing the window keeps FreeTunnel and the VPN running:

| Platform | Closing the window |
| --- | --- |
| Windows, Linux | ✕ minimizes the window to the taskbar. |
| macOS | The red button (or ⌘W) hides it. The menu-bar icon or the Dock brings it back. |
| A Linux desktop with no system tray, such as GNOME without an AppIndicator extension | ✕ quits, since nothing would be left to bring the window back from. |

To quit, choose **Quit** in the tray menu (or press ⌘Q on macOS).

## Importing a configuration

- **Configs → ＋**: create a new TOML, import from file, or paste a `tt://` link
  from the clipboard.
- **⌘V or Ctrl+V** on the **Configs** page pastes a `tt://` link the same way.
- **A `tt://` link** opened from a browser or another app imports too.

For a link, pasted or opened, FreeTunnel names the server the link would add and
asks before adding it.

Passwords stay in the OS credential store: Keychain on macOS, Credential Manager
on Windows, libsecret on Linux.

## Split tunnelling

The **Split tunnelling** tab decides what goes through the tunnel. Rules are
addresses (a domain, an IP, a subnet) or programs, and both obey the **Mode**
switch at the top of the page:

| Mode | What is listed |
| --- | --- |
| *Bypass VPN* | goes around the tunnel |
| *Through VPN* | is the only thing inside it |

Under *Through VPN* with no rule that can be used, FreeTunnel keeps the full
tunnel and the page says so, rather than send everything around it.

### Changes while connected

Every change on this page, turning split tunnelling on or off included, reaches
a tunnel that is already up without reconnecting it:

- A program rule applies from the program's next connection.
- After a change to the addresses or the mode, connections that were open start
  again under the new rules.

### Program rules

Add a program in any of three ways:

- pick it from the list of what is installed;
- drag its icon onto the page;
- pick the file yourself.

What to expect:

- Nothing extra is installed for this: no driver, no system extension, no
  permission dialog.
- A rule added while the VPN is up applies to the next connection rather than
  the next session. Which program a connection belongs to is worked out by
  asking the operating system who owns the socket.
- On Windows, a rule for an app that updates itself the way Discord and Slack
  do, from a folder named after each version, names the app and keeps matching
  it after its updates.

### Profiles

Rules are grouped into profiles, addresses and programs both, and a config can
be tied to one. A set for work and a set for everything else can then be
switched between without retyping either.

Switching to another config while connected reconnects, with the new config's
profile.

### Excluded routes

**Settings → Excluded routes** lists subnets that stay outside the tunnel for
every config.

- They are added to the routes a config excludes itself, not used in their
  place. A config made in FreeTunnel or imported from a link already keeps
  local networks and multicast outside the tunnel, and emptying the list in
  Settings does not change that.
- Unlike the rules above, a change to this list while connected reconnects.
- A route of every address (`0.0.0.0/0`, `::/0`) is refused here and on the
  Split tunnelling page, since it would take all of that traffic out of the
  tunnel.

## Kill switch

Turn it on in **Settings → Security**. It is for a connection that is not up:
while FreeTunnel connects, or brings back a connection that dropped, connections
that would have gone out over the open network are refused instead. What split
tunnelling sends around the tunnel still goes.

A first connect that keeps failing keeps trying, blocked, until it gets through
or you press **Disconnect**. The status reads "Connecting…" ("Waiting for
network…" while there is none), and the window says why it is failing.

### When it applies

The block belongs to the running VPN session, so it is not a firewall of its
own:

| Situation | The block |
| --- | --- |
| FreeTunnel is connecting, or bringing back a connection that dropped | On |
| You change anything on the **Split tunnelling** page | Stays on: the session stays up, and the block with it ([how changes apply](#changes-while-connected)) |
| While connected, you switch configs, or change the excluded routes, the kill switch itself or, on Windows, **Let the VPN config open ports** | Lifts for the moment it takes to build a session anew |
| The server refuses the login or its certificate, and FreeTunnel starts over | Lifts for a moment, as above |
| The config names its server by a domain name rather than an IP address, and that name cannot be looked up | None: there is no session at all |
| The VPN is off, or an error has stopped it | None: nothing is blocked |

### Windows: ports a config opens

FreeTunnel ignores the ports a config file names to pass the kill switch
(`killswitch_allow_ports`) unless **Let the VPN config open ports** is on. That
setting is under the kill switch in Settings.

- Use it, for example, to reach this computer over Remote Desktop while the kill
  switch is on.
- It is one setting for every config, including any you import later, so turn
  it on only if you trust every config you use.

## Settings

| Section | What is there |
| --- | --- |
| **General** | Language (English, Русский), Theme (System, Light, Dark), Launch at system startup, Connect on startup |
| **Security** | [Kill switch](#kill-switch); on Windows, [Let the VPN config open ports](#windows-ports-a-config-opens) |
| **Excluded routes** | Subnets that stay outside the tunnel for every config ([details](#excluded-routes)) |
| **Hotkeys** | Enable, then Toggle VPN, Connect and Disconnect ([details](#global-hotkeys)) |
| **Logging** | Enable logging (the app and the VPN core share one log file); Verbose logs (full VPN core detail, for debugging) |
| **Maintenance** | Check for updates ([details](#updates)), and the version you run |

## Global hotkeys

Set hotkeys for toggle, connect and disconnect in **Settings → Hotkeys**. They
are off until you turn them on there.

- A hotkey needs Ctrl, Alt or Meta (⌘, ⌥ or ⌃ on macOS), unless it is one of
  F1–F12.
- Hotkeys work while the window is minimized or hidden.
- On Linux they need an X11 (Xorg) session: under Wayland the setting is
  unavailable.

## External control

Three commands control the VPN from outside the window:

- `freetunnel://toggle`
- `freetunnel://connect`
- `freetunnel://disconnect`

`tt://…` links import a config; see
[Importing a configuration](#importing-a-configuration).

### From a Stream Deck button or a script

Run FreeTunnel with the command as its argument. A running FreeTunnel acts on
it at once and leaves the window alone, as with the hotkeys; otherwise
FreeTunnel starts and acts on it.

Windows (or wherever you installed it):

```bat
"C:\Program Files\FreeTunnel\FreeTunnel.exe" freetunnel://toggle
```

macOS:

```sh
/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel freetunnel://toggle
```

Linux, .deb:

```sh
freetunnel freetunnel://toggle
```

Linux, AppImage: the AppImage's path followed by the command.

```sh
/path/to/freetunnel-x86_64.AppImage freetunnel://toggle
```

### Opened as a link

The same URL works opened as a link too: from a browser, a document, `open` on
macOS or `xdg-open`. But any web page can open a link, so:

- a link that would turn the VPN off brings up the window and asks first;
- connecting is not asked about.

See [DEEP_LINK.md](DEEP_LINK.md#freetunnel-control-links-separate).

### One FreeTunnel per user

Each user runs one FreeTunnel. Launching it again, with a command or without,
hands over to the copy already running.

If that copy cannot be reached, as when the keyring that holds its launch key is
locked, the new launch closes without acting on the command rather than start a
second FreeTunnel. Run it again once the keyring is unlocked.

## Updates

FreeTunnel looks for a newer release shortly after it starts, and
**Settings → Maintenance → Check for updates** looks again.

Updates are verified with SHA-256 manifests and Ed25519 signatures before
install. How, and what that guards against: [SECURITY.md](SECURITY.md).

## Building from source

See [CONTRIBUTING.md](CONTRIBUTING.md) for local build instructions, tests,
translations, and Codacy setup.

Builds are fully automated in GitHub Actions. The client links against the C++
core [`TrustTunnel/TrustTunnelClient`](https://github.com/TrustTunnel/TrustTunnelClient).

| Workflow | What it does |
| --- | --- |
| [`.github/workflows/build.yml`](.github/workflows/build.yml) | Builds for Windows, macOS and Linux, with HTTP/3 (QUIC) enabled (`DISABLE_HTTP3=OFF`) |
| [`.github/workflows/tests.yml`](.github/workflows/tests.yml) | Unit tests |
| [`.github/workflows/security.yml`](.github/workflows/security.yml) | Security checks |

Releases are published automatically on `v*` tags, once Tests and Security have
passed on the tagged commit.

## Security

[SECURITY.md](SECURITY.md) describes FreeTunnel's security model and known
limitations, and how to report a vulnerability privately.

## License

[Apache-2.0](LICENSE)
