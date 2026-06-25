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
| Helper (`--helper`) | Elevated (User Account Control on Windows / sudo on Unix / pkexec on Linux) | Virtual private network (VPN) tunnel via TrustTunnel core |

The GUI talks to the helper over **loopback** (local-only) Transmission Control
Protocol (TCP) on the Internet Protocol version 4 (IPv4) loopback address
`127.0.0.1` with a one-time random token and a 64 kilobyte (KB) read cap.

## Credential storage

Virtual private network (VPN) passwords are **not stored in user-editable
Tom's Obvious Minimal Language (TOML)** config files under normal operation:

| Platform | Store |
| --- | --- |
| macOS | Keychain (`com.freetunnel.app`) |
| Windows | Credential Manager |
| Linux | libsecret / Secret Service (`secret-tool`) |

On Linux, if no Secret Service (D-Bus secrets API) is available (no GNOME
Keyring (Linux desktop secrets daemon) or KWallet (KDE wallet) bridge),
the app **refuses to save new passwords** and shows a warning in Settings.
Legacy plaintext files (0600 credential files, `instance-auth`) are **migrated
into the OS store automatically** when secure storage becomes available.

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

The release job signs `SHA256SUMS.txt` with the `ED25519_SIGNING_KEY` GitHub
Actions secret (OpenSSL (open-source TLS and cryptography toolkit) Ed25519
Privacy-Enhanced Mail (PEM) private key, **not** Open Pretty Good Privacy
(OpenPGP) / GNU Privacy Guard (GPG)).
Public key: `include/core/ReleaseSigning.h`.

Release binaries are **not code-signed** (no Apple Developer ID / Authenticode).
Users should approve first launch manually when the OS prompts (see README).
