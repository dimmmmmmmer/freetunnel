# FreeTunnel

A free, open-source VPN app for Windows, macOS, Linux, Android and Android TV,
using [TrustTunnel](https://github.com/TrustTunnel/TrustTunnelClient).
Import your server configuration and connect with one button.

## Download

Get the app from [**Releases**](https://github.com/dimmmmmmmer/freetunnel/releases).

| Device | Download |
| --- | --- |
| Windows 10/11 | `freetunnel-windows-x86_64-Setup.exe` |
| Mac — Apple Silicon or Intel, macOS 14.4+ | `freetunnel-macos-universal.dmg` |
| Linux — Ubuntu 22.04+, Debian 12+ | `freetunnel-linux-x86_64.deb` |
| Other Linux distributions with glibc 2.35+ | `freetunnel-x86_64.AppImage` |
| Android phone, tablet or TV — Android 8.0+ | `freetunnel-android-1.2.4.apk` |

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
