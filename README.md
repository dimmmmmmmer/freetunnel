<!-- markdownlint-disable-next-line MD033 -->
# <img src="assets/logo.svg" width="48" alt=""/> FreeTunnel

[![Codacy Badge](https://app.codacy.com/project/badge/Grade/7080586146744b0095656b8eb9c51fff?branch=main)](https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Codacy coverage](https://app.codacy.com/project/badge/Coverage/7080586146744b0095656b8eb9c51fff?branch=main)](https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_coverage)
[![latest version](https://img.shields.io/github/v/release/dimmmmmmmer/freetunnel)](https://github.com/dimmmmmmmer/freetunnel/releases)
[![Tests](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml/badge.svg)](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml)
[![Security](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml/badge.svg)](https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml)
[![Apache-2.0 License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://www.apache.org/licenses/LICENSE-2.0)

A free, open-source VPN app for Windows, macOS, Linux, Android and Android TV,
using [TrustTunnel](https://github.com/TrustTunnel/TrustTunnelClient).
Import your server configuration and connect with one button.

## Download

Get the app from [**Releases**](https://github.com/dimmmmmmmer/freetunnel/releases).

| Device | Download |
| --- | --- |
| Windows 10/11 | [freetunnel-windows-x86_64-Setup.exe](https://github.com/dimmmmmmmer/freetunnel/releases/latest/download/freetunnel-windows-x86_64-Setup.exe) |
| Mac — Apple Silicon or Intel, macOS 14.4+ | [freetunnel-macos-universal.dmg](https://github.com/dimmmmmmmer/freetunnel/releases/latest/download/freetunnel-macos-universal.dmg) |
| Linux — Ubuntu 22.04+, Debian 12+ | [freetunnel-linux-x86_64.deb](https://github.com/dimmmmmmmer/freetunnel/releases/latest/download/freetunnel-linux-x86_64.deb) |
| Other Linux distributions with glibc 2.35+ | [freetunnel-x86_64.AppImage](https://github.com/dimmmmmmmer/freetunnel/releases/latest/download/freetunnel-x86_64.AppImage) |
| Android phone, tablet or TV — Android 8.0+ | [Download APK](https://github.com/dimmmmmmmer/freetunnel/releases/download/android-1.2.4/freetunnel-android-1.2.4.apk) |

See [what changed](CHANGELOG.md) for release notes.

## Features

- **Connect with one click:** save multiple server configurations and switch between them.
- **Easy import:** add `tt://` links or TOML files; send several at once from your phone to Android TV.
- **Split tunnelling:** choose which apps and sites use the VPN, with a ready-made profile for Russia.
- **Kill switch:** block traffic if the VPN connection drops.
- **Desktop shortcuts:** control the VPN from the system tray, global hotkeys or Stream Deck.
- **Made for your device:** remote-friendly TV controls, light and dark themes, English and Russian, and built-in update checks.

## Get connected

1. Install FreeTunnel on your device.
2. Add the `tt://` link or TOML configuration supplied by your VPN provider.
3. Press **Connect**.

On Android TV, choose **Receive from phone**, scan the QR code with your phone
on the same Wi-Fi network, send your links or files and confirm **Add** on the TV.

FreeTunnel is a client: it does not include a VPN server or a subscription.

## Installation tips

- **Windows:** if SmartScreen warns, choose **More info → Run anyway**.
- **macOS:** desktop builds are unsigned. If macOS blocks the app, see the
  [installation instructions](USER_GUIDE.md#macos).
- **Linux:** open the `.deb` with your package installer, or make the AppImage
  executable before opening it.
- **Android / TV:** allow your browser or file manager to install apps when asked.

## Help

[User guide](USER_GUIDE.md) · [Report a problem](https://github.com/dimmmmmmmer/freetunnel/issues)
· [What's new](CHANGELOG.md) · [Build from source](CONTRIBUTING.md)

FreeTunnel is licensed under [Apache 2.0](LICENSE).
See [security reporting](SECURITY.md) and [third-party notices](THIRD_PARTY_NOTICES.md).
