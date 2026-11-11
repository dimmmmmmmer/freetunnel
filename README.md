<!-- markdownlint-disable MD033 MD041 -->
<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/readme-title-dark.svg">
    <img src="docs/images/readme-title-light.svg" width="336" height="76" alt="FreeTunnel">
  </picture>
</p>

<p align="center">
  <a href="https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&amp;utm_medium=referral&amp;utm_content=&amp;utm_campaign=Badge_grade"><img src="https://app.codacy.com/project/badge/Grade/7080586146744b0095656b8eb9c51fff?branch=main" alt="Codacy Badge"></a>
  <a href="https://app.codacy.com/gh/dimmmmmmmer/freetunnel/dashboard?utm_source=gh&amp;utm_medium=referral&amp;utm_content=&amp;utm_campaign=Badge_coverage"><img src="https://app.codacy.com/project/badge/Coverage/7080586146744b0095656b8eb9c51fff?branch=main" alt="Codacy coverage"></a>
  <a href="https://github.com/dimmmmmmmer/freetunnel/releases"><img src="https://img.shields.io/github/v/release/dimmmmmmmer/freetunnel" alt="latest version"></a>
  <a href="https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml"><img src="https://github.com/dimmmmmmmer/freetunnel/actions/workflows/tests.yml/badge.svg" alt="Tests"></a>
  <a href="https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml"><img src="https://github.com/dimmmmmmmer/freetunnel/actions/workflows/security.yml/badge.svg" alt="Security"></a>
  <a href="https://www.apache.org/licenses/LICENSE-2.0"><img src="https://img.shields.io/badge/License-Apache_2.0-blue.svg" alt="Apache-2.0 License"></a>
</p>
<!-- markdownlint-enable MD033 MD041 -->

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
- **Subscriptions:** keep server settings up to date, with confirmation before downloading from a new source.
- **Split tunnelling:** choose which apps and sites use the VPN, with a ready-made profile for Russia.
- **Geo lists:** ready-made site and IP lists or your own HTTPS list; choose VPN or direct access, with manual rules taking priority.
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
