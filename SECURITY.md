# Security

This document describes FreeTunnel's security model, known limitations, and how
to report vulnerabilities. See also [docs/security-threats.md](docs/security-threats.md)
for inter-process communication (IPC), deep links, and the threat summary table.

## Reporting issues

Please report security bugs privately via GitHub Security Advisories on
[dimmmmmmmer/freetunnel](https://github.com/dimmmmmmmer/freetunnel/security/advisories/new)
rather than opening a public issue.

## Architecture

FreeTunnel splits privileges:

| Component | Privilege level | Role |
| --- | --- | --- |
| GUI (`FreeTunnel`) | Normal user | Qt Modeling Language (QML) UI, settings, update checks |
| Helper (`--helper`) | Elevated (User Account Control on Windows / an administrator prompt through `osascript` on macOS / pkexec on Linux, or sudo where pkexec cannot run) | Virtual private network (VPN) tunnel via TrustTunnel core |

The GUI talks to the helper over **loopback** (local-only) Transmission Control
Protocol (TCP) on the Internet Protocol version 4 (IPv4) loopback address
`127.0.0.1`. Both sides prove knowledge of a one-time random token over the
other side's nonce before anything sensitive is sent, so the token itself never
travels over the socket, and each protocol line is capped at 512 kilobytes (KB)
— connect payloads carry a full config, certificates included. The separate
single-instance socket, over which an already-running copy accepts control
commands, caps a message at 64 KB.

The helper does not take the config it receives at its word. An imported config
keeps keys FreeTunnel never writes, and a few of them would have the elevated VPN
core act on something the file names: a directory to keep TLS sessions in, ports
to let through the kill switch, a name for the tunnel interface, an existing
interface to attach to, a network namespace. The helper clears those before the
core sees the config, and accepts only a tunnel (TUN) listener, not a SOCKS
proxy (see [docs/security-threats.md](docs/security-threats.md)). The kill-switch
ports are the one exception a user can make: on Windows, the setting "Let the VPN
config open ports", off by default, has the helper keep them, and the config then
decides which ports bypass the kill switch. It applies to every config, one
imported later included, so it is for someone who trusts every config they use.

## Credential storage

Virtual private network (VPN) passwords are **not stored in user-editable
Tom's Obvious Minimal Language (TOML)** config files under normal operation:

| Platform | Store |
| --- | --- |
| macOS | Keychain (`com.freetunnel.app`) |
| Windows | Credential Manager |
| Linux | Secret Service through libsecret (GNOME Keyring, KWallet); `secret-tool` only in builds without libsecret |

On Linux, if no Secret Service (D-Bus secrets API) is available (no GNOME
Keyring (Linux desktop secrets daemon) or KWallet (KDE wallet) bridge),
the app **refuses to save new passwords** and shows a warning in Settings.
The one secret it still writes then is the single-instance token, which lets a
second launch hand a link to the running app: it goes to an owner-only (0600)
file, `instance-auth`, until a start finds a store to keep it in, and the app
deletes that file when it quits.
Password files older builds wrote (0600, under `credentials/`) are **moved into
the OS store** the first time the app reads each one with secure storage
available — to connect, edit, share or export that config. Until then they stay
on disk; nothing sweeps them at startup.

During connect, the GUI builds the helper config **in memory** and sends it over
authenticated loopback IPC (`configToml`). Legacy `.connect-*.toml` temp files from
older builds are swept at startup.

## Updates

The in-app updater requires:

1. `SHA256SUMS.txt` on the GitHub Release
2. Ed25519 (Edwards-curve digital signature) signature (`SHA256SUMS.txt.sig`) —
   verified by the in-app updater
3. Secure Hash Algorithm 256-bit (SHA-256) match of the downloaded installer
   against the published checksum list (`SHA256SUMS.txt`)
4. A `#version=` line inside the signed bytes naming the release the manifest
   belongs to, equal to the version being offered. A manifest without one is
   refused; every release since 1.1.8 has it. Without this an authentic manifest
   from another release could be served as this one's — including one from
   before 1.1.8, offered under a higher version number to roll a user back

Asset Uniform Resource Locators (URLs) must also sit under this repository's
release download path for the release being offered, and only a version newer
than the installed one is offered at all. What these leave to an attacker who can
tamper with the update traffic is to withhold updates, or to offer a genuine
release that is newer than the installed one but not the newest; never an
unsigned build, nor one older than the installed one. That takes impersonating
GitHub over TLS (a certificate the machine trusts), not merely watching the
network. FreeTunnel 1.2.2 and earlier accepted a manifest without the version
line (before 1.1.8 they did not read it at all), which left them open to the
rollback above from an attacker who could impersonate `github.com` itself. See
[docs/security-threats.md](docs/security-threats.md).

The release job signs `SHA256SUMS.txt` with the `ED25519_SIGNING_KEY` GitHub
Actions secret (OpenSSL (open-source TLS and cryptography toolkit) Ed25519
Privacy-Enhanced Mail (PEM) private key, **not** Open Pretty Good Privacy
(OpenPGP) / GNU Privacy Guard (GPG)).
Public key: `include/core/ReleaseSigning.h`.

Release binaries are **not code-signed** (no Apple Developer ID / Authenticode).
Users should approve first launch manually when the OS prompts (see README).
