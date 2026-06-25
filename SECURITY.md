# Security

This document describes FreeTunnel's security model, known limitations, and how
to report vulnerabilities. See also [docs/security-threats.md](docs/security-threats.md)
for IPC, deep links, and the threat summary table.

## Reporting issues

Please report security bugs privately via GitHub Security Advisories on
[dimmmmmmmer/freetunnel](https://github.com/dimmmmmmmer/freetunnel/security/advisories/new)
rather than opening a public issue.

## Architecture

FreeTunnel splits privileges:

| Component | Privilege level | Role |
| --- | --- | --- |
| GUI (`FreeTunnel`) | Normal user | Qt Modeling Language (QML) UI, settings, update checks |
| Helper (`--helper`) | Elevated (User Account Control / sudo / pkexec) | VPN tunnel via TrustTunnel core |

The GUI talks to the helper over **loopback TCP** (Transmission Control Protocol,
`127.0.0.1`) with a one-time random token and a 64 kilobyte (KB) read cap.

## Credential storage

Virtual private network (VPN) passwords are **not stored in user-editable TOML**
(Tom's Obvious Minimal Language) config files under normal operation:

| Platform | Store |
| --- | --- |
| macOS | Keychain (`com.freetunnel.app`) |
| Windows | Credential Manager |
| Linux | libsecret / Secret Service (`secret-tool`) |

On Linux, if no Secret Service is available (no GNOME Keyring / KWallet bridge),
the app **refuses to save new passwords** and shows a warning in Settings. Existing
passwords saved in an older plaintext fallback (0600 files) can still be loaded
until migrated.

Temporary `.connect-*.toml` files (password injected for the helper) are removed on
disconnect, quit, and swept at startup.

## Updates

The in-app updater requires:

1. `SHA256SUMS.txt` on the GitHub Release
2. Ed25519 signature (`SHA256SUMS.txt.sig`) — verified by the in-app updater
3. SHA-256 (Secure Hash Algorithm 256-bit) match of the downloaded installer against the manifest

The release job signs `SHA256SUMS.txt` with the `ED25519_SIGNING_KEY` GitHub
secret (OpenSSL Ed25519 Privacy-Enhanced Mail (PEM) private key, **not** OpenPGP/GPG).
Public key: `include/core/ReleaseSigning.h`.

Release binaries are **not code-signed** (no Apple Developer ID / Authenticode).
Users should approve first launch manually when the OS prompts (see README).
