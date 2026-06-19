# FreeTunnel

<img src="assets/logo.svg" width="96" align="right" alt="FreeTunnel logo"/>

A Qt GUI client for [TrustTunnel](https://github.com/TrustTunnel/TrustTunnelClient) on Linux, macOS, and Windows.

## Installation

Download a build for your platform from
[**Releases**](https://github.com/dimmmmmmmer/freetunnel/releases/latest):

| Platform | File |
| --- | --- |
| **Windows 10/11** | `freetunnel-windows-x86_64-Setup.exe` |
| **macOS** (Apple Silicon + Intel) | `freetunnel-macos-universal.dmg` |
| **Linux** (Debian/Ubuntu/Pop!_OS) | `freetunnel-linux-x86_64.deb` |

Builds are **unsigned** (no code-signing certificates), so the OS may warn on first launch:

- **macOS**: right-click the app → **Open** (or
  `xattr -dr com.apple.quarantine /Applications/FreeTunnel.app`).
- **Windows**: SmartScreen → **More info** → **Run anyway**.
- **Linux (.deb)**: `sudo apt install ./freetunnel-linux-x86_64.deb`

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
- Official TrustTunnel format: `tt://?<base64url>` (same as QR codes and mobile
  clients).

## External control

- **Deep links**: `freetunnel://toggle`, `freetunnel://connect`,
  `freetunnel://disconnect`, plus `tt://…` for import. The app is
  single-instance — launching again with a link forwards the command to the
  running window.
- **Global hotkeys** — configured in Settings (toggle / connect / disconnect);
  work even when the window is minimized.
- **System tray** — quick actions; closing the window hides to tray instead of
  quitting.

## Compatibility

- **Windows**: 10/11 (x64).
- **macOS**: 11 Big Sur or newer — **universal** (Apple Silicon and Intel in one `.dmg`).
- **Linux**: Debian/Ubuntu/Pop!_OS amd64 (`.deb` bundles its own Qt).

## For developers

See [CONTRIBUTING.md](CONTRIBUTING.md) for local build instructions, tests, and
translations. Builds are fully automated in GitHub Actions
([`.github/workflows/build.yml`](.github/workflows/build.yml)): the client links
against the C++ core [`TrustTunnel/TrustTunnelClient`](https://github.com/TrustTunnel/TrustTunnelClient).
HTTP/3 (QUIC) builds are available via
[`.github/workflows/build-http3.yml`](.github/workflows/build-http3.yml). Unit tests —
[`.github/workflows/tests.yml`](.github/workflows/tests.yml). Releases are
published automatically on `v*` tags.

## License

See [LICENSE](LICENSE).
