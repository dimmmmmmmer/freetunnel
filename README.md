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
| **Linux** (portable, any distro) | `freetunnel-linux-x86_64.AppImage` |

Builds are **unsigned** (no code-signing certificates), so the OS may warn on first launch:

- **macOS**: right-click the app → **Open** (or
  `xattr -dr com.apple.quarantine /Applications/FreeTunnel.app`).
- **Windows**: SmartScreen → **More info** → **Run anyway**.
- **Linux (.deb)**: `sudo apt install ./freetunnel-linux-x86_64.deb`
- **Linux (AppImage)**: `chmod +x freetunnel-linux-x86_64.AppImage && ./freetunnel-linux-x86_64.AppImage`

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
  link from the clipboard.

## Split tunnelling

The **Split tunnelling** tab decides what goes through the tunnel. Rules are
either addresses — a domain, an IP, a subnet — or programs, and both obey the
**Mode** switch at the top of the page: under *Bypass VPN* what is listed goes
around the tunnel, under *Through VPN* it is the only thing inside it.

Add a program from the list of what is installed, by dragging its icon onto the
page, or by picking the file yourself. Which program a connection belongs to is
worked out by asking the operating system who owns the socket, so a rule added
while the VPN is up applies to the next connection rather than the next session.
Nothing extra is installed for this: no driver, no system extension, no
permission dialog.

Rules are grouped into profiles — addresses and programs both — and a config can
be tied to one, so a set for work and a set for everything else can be switched
between without retyping either.

## External control

- **Deep links**: `freetunnel://toggle`, `freetunnel://connect`,
  `freetunnel://disconnect`, plus `tt://…` for import. The app is
  single-instance — launching again with a link forwards the command to the
  running window.
- **Global hotkeys** — configured in Settings (toggle / connect / disconnect);
  work even when the window is minimized.
- **System tray** — quick actions; closing the window hides to tray instead of
  quitting.

## For developers

See [CONTRIBUTING.md](CONTRIBUTING.md) for local build instructions, tests,
translations, and Codacy setup. Builds are fully automated in GitHub Actions
([`.github/workflows/build.yml`](.github/workflows/build.yml)): the client links
against the C++ core [`TrustTunnel/TrustTunnelClient`](https://github.com/TrustTunnel/TrustTunnelClient).
HTTP/3 (QUIC) builds are enabled in CI (`DISABLE_HTTP3=OFF`).
Unit tests — [`.github/workflows/tests.yml`](.github/workflows/tests.yml). Security checks —
[`.github/workflows/security.yml`](.github/workflows/security.yml) and [SECURITY.md](SECURITY.md).
Releases are published automatically on `v*` tags.

## License

[Apache-2.0](LICENSE)
