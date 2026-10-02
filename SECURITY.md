# Security

How to report a vulnerability in FreeTunnel, which versions get fixes, and what
the app does to protect your passwords, your traffic and your machine. For users
and security researchers. The full threat model, with what each protection
still leaves open, is in [docs/security-threats.md](docs/security-threats.md).

## Reporting a vulnerability

Please report security bugs privately via GitHub Security Advisories on
[dimmmmmmmer/freetunnel](https://github.com/dimmmmmmmer/freetunnel/security/advisories/new)
rather than opening a public issue.

## Supported versions

So far every fix, security fixes included, has shipped in a new release cut
from `main`, and no older version has had one backported: use the latest
release. The in-app updater offers newer releases, and
[CHANGELOG.md](CHANGELOG.md) lists what each one fixed under **Security**.

## What protects you

One line per protection; the links lead to the details here and to the threat
model.

| Area | Protection | Details |
| --- | --- | --- |
| VPN passwords | Kept in the OS credential store, not in config files | [Credential storage](#credential-storage), [threat model](docs/security-threats.md#credential-storage) |
| Connecting | The helper's config is built in memory and sent over an authenticated loopback channel | [During connect](#during-connect) |
| Privileged helper | Only the helper runs elevated; it listens on loopback only and both sides prove a one-time token | [How the app is split](#how-the-app-is-split), [threat model](docs/security-threats.md#privileged-helper-and-its-ipc) |
| Configs reaching root | The helper clears config keys that would point the elevated core at a path, port or interface | [What the helper takes from a config](#what-the-helper-takes-from-a-config), [threat model](docs/security-threats.md#config-files-reaching-root) |
| AppImage on Linux | What root runs comes from the kernel, not the environment, and root unpacks it in a fresh directory only root can write | [Threat model](docs/security-threats.md#appimage-elevation-on-linux) |
| Updates | Ed25519-signed `SHA256SUMS.txt` that names its release; installer hash checked; only newer versions offered | [Updates](#updates), [threat model](docs/security-threats.md#updates-and-signatures) |
| Single instance | A socket of each user's own, owner-only, peer checked at both ends, with a per-session random token | [Threat model](docs/security-threats.md#single-instance-channel) |
| Links from web pages | A link that would turn the VPN off, and every `tt://` import, asks first and takes only a click as the answer | [Threat model](docs/security-threats.md#deep-links-and-control-links) |
| Kill switch | Held by one VPN session through rule edits and failed first connects | [Threat model](docs/security-threats.md#kill-switch) |
| Privacy from the VPN operator | The name of the program that opened a connection is not handed to the VPN core, which would send it to the server; it goes to the local log only | [Threat model](docs/security-threats.md#per-application-rules) |
| Files opened from the UI | Only regular files in your home, temp, downloads, documents or desktop folders; symlinks rejected | [Threat model](docs/security-threats.md#files-read-from-the-ui) |
| Release binaries | Not code-signed: approve the first launch when the OS asks | [Release signing](#release-signing) |

## How the app is split

FreeTunnel splits privileges between two processes:

| Component | Privilege level | Role |
| --- | --- | --- |
| GUI (`FreeTunnel`) | Normal user | Qt Modeling Language (QML) UI, settings, update checks |
| Helper (`--helper`) | Elevated | Virtual private network (VPN) tunnel via TrustTunnel core |

How the helper is elevated:

| Platform | Elevation |
| --- | --- |
| Windows | User Account Control |
| macOS | An administrator prompt through `osascript` |
| Linux | pkexec, or sudo where pkexec cannot run |

### The GUI–helper channel

- The GUI talks to the helper over **loopback** (local-only) TCP on the IPv4
  loopback address `127.0.0.1`.
- Both sides prove knowledge of a one-time random token over the other side's
  nonce before anything sensitive is sent, so the token itself never travels
  over the socket.
- Each protocol line is capped at 512 kilobytes (KB): connect payloads carry a
  full config, certificates included.
- The separate single-instance socket, over which an already-running copy
  accepts control commands, caps a message at 64 KB.

### What the helper takes from a config

The helper does not take the config it receives at its word. An imported config
keeps keys FreeTunnel never writes, and a few of them would have the elevated
VPN core act on something the file names:

- a directory to keep TLS sessions in;
- ports to let through the kill switch;
- a name for the tunnel interface;
- an existing interface to attach to;
- a network namespace.

The helper clears those before the core sees the config, and accepts only a
tunnel (TUN) listener, not a SOCKS proxy. See
[docs/security-threats.md](docs/security-threats.md#config-files-reaching-root).

The kill-switch ports are the one exception a user can make. On Windows, the
setting "Let the VPN config open ports" (Settings → Security), off by default,
has the helper keep them, and the config then decides which ports bypass the
kill switch. It applies to every config, one imported later included, so it is
for someone who trusts every config they use.

## Credential storage

VPN passwords are **not stored in user-editable TOML** config files under normal
operation. They go to the OS credential store:

| Platform | Store |
| --- | --- |
| macOS | Keychain (`com.freetunnel.app`) |
| Windows | Credential Manager |
| Linux | Secret Service through libsecret (GNOME Keyring, KWallet); `secret-tool` only in builds without libsecret |

### Linux without a Secret Service

When no Secret Service is available (no GNOME Keyring or KWallet bridge):

- The app **refuses to save new passwords** and shows a warning in Settings.
- The one secret it still writes is the single-instance token, which lets a
  second launch hand a link to the running app. It goes to an owner-only
  (0600) file, `instance-auth`, until a start finds a store to keep it in.
- The app deletes that file when it quits.

### Password files from older builds

- Older builds wrote password files (0600, under `credentials/`).
- Each one is **moved into the OS store** the first time the app reads it with
  secure storage available: to connect, edit, share or export that config.
- Until then they stay on disk; nothing sweeps them at startup.

### During connect

- The GUI builds the helper config **in memory** and sends it over
  authenticated loopback IPC (`configToml`).
- Legacy `.connect-*.toml` temp files from older builds are swept at startup.

## Updates

The in-app updater requires all of these:

1. `SHA256SUMS.txt` on the GitHub Release.
2. An Ed25519 signature of it, `SHA256SUMS.txt.sig`, verified by the in-app
   updater.
3. A SHA-256 match of the downloaded installer against the published checksum
   list (`SHA256SUMS.txt`).
4. A `#version=` line inside the signed bytes naming the release the manifest
   belongs to, equal to the version being offered. A manifest without one is
   refused; every release since 1.1.8 has it.
5. Asset URLs under this repository's release download path for the release
   being offered.

Only a version newer than the installed one is offered at all.

### What an attacker on the update path can still do

An attacker who can tamper with the update traffic can:

- withhold updates;
- offer a genuine release that is newer than the installed one but not the
  newest.

Never an unsigned build, nor one older than the installed one. Even this takes
impersonating GitHub over TLS (a certificate the machine trusts), not merely
watching the network.

### Why the version line matters

Without the `#version=` line, an authentic manifest from another release could
be served as this one's, including one from before 1.1.8, offered under a higher
version number to roll a user back.

FreeTunnel 1.2.2 and earlier accepted a manifest without the version line
(before 1.1.8 they did not read it at all). That left them open to this rollback
from an attacker who could impersonate `github.com` itself. See
[docs/security-threats.md](docs/security-threats.md#updates-and-signatures).

## Release signing

- The release job signs `SHA256SUMS.txt` with the `ED25519_SIGNING_KEY` GitHub
  Actions secret: an OpenSSL Ed25519 PEM private key, **not** OpenPGP / GPG.
- Public key: `include/core/ReleaseSigning.h`.
- Release binaries are **not code-signed** (no Apple Developer ID /
  Authenticode). Users should approve first launch manually when the OS prompts
  (see the [README](README.md)).

## Terms used here

| Term | Meaning |
| --- | --- |
| Ed25519 | Edwards-curve digital signature |
| GNOME Keyring | Linux desktop secrets daemon |
| GPG | GNU Privacy Guard |
| IPC | Inter-process communication |
| IPv4 | Internet Protocol version 4 |
| KB | Kilobytes |
| KWallet | KDE wallet |
| Loopback | Local-only network address |
| OpenPGP | Open Pretty Good Privacy |
| OpenSSL | Open-source TLS and cryptography toolkit |
| PEM | Privacy-Enhanced Mail |
| QML | Qt Modeling Language |
| Secret Service | D-Bus secrets API |
| SHA-256 | Secure Hash Algorithm 256-bit |
| TCP | Transmission Control Protocol |
| TOML | Tom's Obvious Minimal Language |
| TUN | Tunnel network interface |
| URL | Uniform Resource Locator |
| VPN | Virtual private network |
