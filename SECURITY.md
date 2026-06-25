# Security

This document describes FreeTunnel's security model, known limitations, and how
to report vulnerabilities.

## Reporting issues

Please report security bugs privately via GitHub Security Advisories on
[dimmmmmmmer/freetunnel](https://github.com/dimmmmmmmer/freetunnel/security/advisories/new)
rather than opening a public issue.

## Architecture

FreeTunnel splits privileges:

| Component | Privilege level | Role |
| --- | --- | --- |
| GUI (`FreeTunnel`) | Normal user | QML UI, settings, update checks |
| Helper (`--helper`) | Elevated (UAC/sudo/pkexec) | VPN tunnel via TrustTunnel core |

The GUI talks to the helper over **loopback TCP** (`127.0.0.1`) with a
one-time random token and a 64 KB read cap.

## Credential storage

VPN passwords are **never** stored in user-editable TOML files:

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
3. SHA-256 match of the downloaded installer against the manifest

The release job signs `SHA256SUMS.txt` with the `ED25519_SIGNING_KEY` GitHub
secret (OpenSSL Ed25519 PEM, **not** OpenPGP/GPG). Public key:
`include/core/ReleaseSigning.h`.

Release binaries are **not code-signed** (no Apple Developer ID / Authenticode).
Users must approve first launch manually (README).

## Single-instance control (deep links)

A second launch forwards commands (`freetunnel://toggle`, `tt://…` import) to the
running instance via a local socket (`QLocalServer`) protected by:

- `UserAccessOption` — other OS users cannot connect
- Per-session random token (stored in the OS credential store when available)
- Constant-time token comparison
- 64 KB message cap

### Known limitation: same-user local processes

Any process running as the **same OS user** can:

- Read the instance-auth credential (when stored in Secret Service / Keychain)
- Connect to the local control socket if it obtains the token
- Read the helper IPC token file during VPN connect

This is typical for desktop apps without a system daemon. Malware running as the
same user can toggle VPN or import configs; it **cannot** read Keychain/Secret
Service entries without OS APIs available to that user anyway.

Mitigations already in place: no remote attack surface for control IPC, tokens
rotate each session, helper binds to loopback only.

## Deep links (`tt://`)

Config import links use TrustTunnel's TLV/base64url format. Passwords from links
go directly to the credential store, not on-disk TOML.

## File access from QML

`safeReadUserTextFile()` only reads regular files under the user's home, temp,
downloads, documents, or desktop directories; symlinks are rejected.

## Threat summary

| Threat | Mitigation |
| --- | --- |
| Remote MITM on update | SHA256 manifest + Ed25519 signature |
| Malicious `tt://` link | TLV parser limits; cred store separation |
| Other local user | Socket ACL + loopback-only helper |
| Same-user malware | Documented limitation; OS credential APIs |
| TOML injection | `tomlEsc()` strips control chars |
| Unsigned installer | User warnings; in-app hash verify before install |
