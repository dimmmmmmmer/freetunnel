# Security — threat model and IPC

Companion to [SECURITY.md](../SECURITY.md): single-instance control, deep links,
and threat summary.

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
- Read the helper inter-process communication (IPC) token file during VPN connect

This is typical for desktop apps without a system daemon. Malware running as the
same user can toggle VPN or import configs; it **cannot** read Keychain/Secret
Service entries without OS APIs available to that user anyway.

Mitigations already in place: no remote attack surface for control IPC, tokens
rotate each session, helper binds to loopback only.

## Deep links (`tt://`)

Config import links use TrustTunnel's type-length-value (TLV) / base64url format.
Passwords from links go directly to the credential store, not on-disk TOML.

## File access from QML

`safeReadUserTextFile()` only reads regular files under the user's home, temp,
downloads, documents, or desktop directories; symlinks are rejected.

## Threat summary

| Threat | Mitigation |
| --- | --- |
| Remote man-in-the-middle (MITM) on update | SHA256 manifest + Ed25519 signature |
| Malicious `tt://` link | TLV parser limits; cred store separation |
| Other local user | Socket access-control list (ACL) + loopback-only helper |
| Same-user malware | Documented limitation; OS credential APIs |
| TOML injection | `tomlEsc()` strips control chars |
| Unsigned installer | User warnings; in-app hash verify before install |
