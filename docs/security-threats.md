# Security — threat model and IPC

Companion to [SECURITY.md](../SECURITY.md): single-instance control, deep links,
and threat summary.

## Single-instance control (deep links)

A second launch forwards commands (`freetunnel://toggle`, `tt://…` import) to the
running instance via a local socket (`QLocalServer`) protected by:

- `UserAccessOption` (Windows) and peer-uid verification (macOS/Linux) — other
  OS users cannot connect; the second instance also verifies the listener's
  owner before sending the token, so a squatted socket name can't harvest it
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

What it **cannot** do is reach root through us: the elevated helper does not act
on paths the GUI names. The connect command must carry an inline config (a file
path is refused), the core's log path is chosen by the helper itself and is not
part of the protocol at all — the GUI receives log lines over IPC and keeps the
durable copy — and the elevated argv is derived from the running executable
rather than from the environment.

That last part is the one worth spelling out, because it was wrong once. On Linux
an AppImage build has to re-exec the `.AppImage` file rather than the executable
inside its FUSE mount, since root cannot read that user-private mount. Naming the
file from `$APPIMAGE` and validating it against `$APPDIR` is not validation at
all — an attacker who can set the GUI's environment sets both sides, and `$APPDIR`
only had to be a path *prefix* of the executable, so `APPDIR=/usr` passed for an
ordinary `/usr/bin/FreeTunnel` install and `$APPIMAGE` was then run as root. The
answer now comes from the kernel: `runningAppImagePath()` resolves
`/proc/self/exe`, finds the FUSE mount containing it in `/proc/self/mountinfo`,
and takes that mount's backing file. When the kernel does not name a regular file
there, elevation falls back to the running executable rather than guessing — a
prompt that is about to run something as root gets a definite answer or none.

Mitigations already in place: no remote attack surface for control IPC, tokens
rotate each session, helper binds to loopback only.

### Helper IPC: mutual authentication

The helper listens on a random loopback port, but only once the elevation prompt
has been answered — seconds to a minute after the GUI starts trying to connect.
Any local process can bind a port in that window, so the handshake is mutual and
neither side puts the token on the wire: the GUI opens with a nonce, the helper
answers with an HMAC over it (keyed by the one-time token) plus a nonce of its
own, and only then does the GUI send its own proof and, after that, the config.
A peer that cannot prove it holds the token never receives the config TOML —
which carries the VPN password — and cannot report a tunnel that does not exist.
Pre-authentication connections are capped and time-limited so they cannot
exhaust the root process.

### Server probes leave the tunnel by design

The Configs page shows a latency figure per server. Those probes bind to the
physical interface (`IP_BOUND_IF` / `IPV6_UNICAST_IF` / source bind), so they go
around the tunnel **even while connected** — otherwise they would measure the
tunnel rather than the server. The consequence is that refreshing that page
reveals the full list of configured endpoints, including servers never connected
to, to the local network and the ISP. Traffic that is not a probe is unaffected.

### Update manifests are bound to their release

The update check trusts the GitHub Application Programming Interface (API)
response for *which* release exists. Everything it names is then checked: asset
Uniform Resource Locators (URLs) must sit under this repository's release
download path for the advertised tag, `SHA256SUMS.txt` must carry a valid Ed25519
signature from the compiled-in key, and the installer's SHA-256 must appear in
that manifest.

None of that used to establish *which version* the signature belonged to. The
signed material is the manifest, and neither it nor the asset names carried a
version — `freetunnel-linux-x86_64.deb` is the same name in every release. So an
attacker able to forge the API response (a compromised transport to
`api.github.com`, not a passive network observer) could present an **older, real**
release: its tag, its assets, its manifest and its genuine signature. Every check
passed, because everything was authentic — just stale. `isVersionNewer()` kept it
above the version already installed, so it could not roll a user backwards; the
harm was pinning them short of the newest build, and so short of a fix they were
waiting for.

The manifest now opens with a `version=X` line, inside the bytes the signature
covers, and the updater refuses a manifest whose version is not the one being
offered. Two details are load-bearing:

- **Written as `#version=X`.** The `#` is for the people who verify the file by
  hand: `sha256sum -c` calls any line it cannot parse "improperly formatted",
  which reads like tampering and fails outright under `--strict`. It tolerates
  comments. The absence of a space keeps the line a single field, and the manifest
  parser in every already-shipped client skips lines with fewer than two fields —
  so old clients neither trip over it nor mistake it for an asset name.
- **A missing version is accepted.** Releases published before this existed have
  no such line. Refusing them would strand exactly the clients this protects on
  the build they already have, which is worse than the replay it prevents — a
  deployed client cannot be taught to enforce anything by changing the server
  side. The check therefore tightens by itself as pre-migration releases age out
  of being the newest thing on offer.

## Deep links (`tt://`)

Config import links use TrustTunnel's type-length-value (TLV) / base64url format.
Passwords from links go directly to the credential store, not on-disk TOML.

## File access from QML

`safeReadUserTextFile()` only reads regular files under the user's home, temp,
downloads, documents, or desktop directories; symlinks are rejected.

## Threat summary

| Threat | Mitigation |
| --- | --- |
| Remote man-in-the-middle (MITM) on update | SHA256 manifest + Ed25519 signature, bound to the release version |
| Malicious `tt://` link | TLV parser limits; cred store separation |
| Other local user | Socket access-control list (ACL) + loopback-only helper |
| Same-user malware | Documented limitation; OS credential APIs |
| TOML injection | `tomlEsc()` strips control chars |
| Unsigned installer | User warnings; in-app hash verify before install |
