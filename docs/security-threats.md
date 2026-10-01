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

What it **cannot** do is turn the elevated helper to ends of its own — and that
has to hold whoever starts the helper, because such a process can start the
genuine binary itself, with arguments of its choosing, behind an administrator
prompt that is the real one. From its command line the helper takes a port and
the path of the token file, and that file it only reads, and only if it could be
one: named `.fthelper-…`, a regular file rather than a link to another (on Linux
and macOS not a second hard link either), of at most 128 bytes. It deletes
nothing; the GUI removes its own token file, as the user, and at its next start
any that a GUI which ended before the helper answered left behind. It used to
read whatever it was named and then delete it, so a prompt the user approved
could delete any file as root or Administrator. On Windows a folder in the path can
still be made to lead to another file without the name showing it, so there the
helper may read a file that small which the user could not; it serves only as
the key for the handshake, and what leaves the helper is a hash over a nonce,
which gives nothing away unless the file's contents can be guessed.

Over IPC, the connect command must carry an inline config (a file path is
refused), and the core's log path is chosen by the helper itself and is not part
of the protocol at all — the GUI receives log lines over IPC and keeps the
durable copy. And the elevated argv is derived from the running executable rather
than from the environment.

That last part is the one worth spelling out, because it was wrong once. On Linux
an AppImage build has to re-exec the `.AppImage` file rather than the executable
inside its FUSE mount, since root cannot read that user-private mount. Naming the
file from `$APPIMAGE` and validating it against `$APPDIR` is not validation at
all — an attacker who can set the GUI's environment sets both sides, and `$APPDIR`
only had to be a path *prefix* of the executable, so `APPDIR=/usr` passed for an
ordinary `/usr/bin/FreeTunnel` install and `$APPIMAGE` was then run as root. The
answer now comes from the kernel: `runningAppImagePath()` resolves
`/proc/self/exe`, finds the FUSE mount containing it in `/proc/self/mountinfo`,
and takes that mount's backing file. An AppImage started without FUSE
(`--appimage-extract-and-run`) has no mount: the runtime unpacks it into
`appimage_extracted_<MD5 of the AppImage>` and waits as the app's parent, so the
answer is `/proc/<parent>/exe`, accepted only when that file's MD5 is the one in
the directory's name. When the kernel does not name a regular file either way,
elevation falls back to the running executable rather than guessing — a prompt
that is about to run something as root gets a definite answer or none.

With the .deb, whose files only root can change, we know of no way left for
same-user malware to reach root through us: however it is started, the helper
does no more than the GUI could ask of it. The AppImage is the exception, and
cannot be otherwise: the file the administrator prompt runs is the `.AppImage`
itself, and it belongs to the user. Anything running as that user can replace
it, and the next time the user authorizes a connection, root runs the
replacement — the position of any program a user runs with sudo out of their own
home directory. Where that matters, use the .deb.

Root does not mount the AppImage; it unpacks it, with the runtime's
extract-and-run, which also works where there is no FUSE. Where it unpacked to
was a way in for **other** users of the machine: pkexec does not pass `TMPDIR`
on, so root unpacked under a fixed name in the shared `/tmp`, which another
account could prepare in advance. The elevated command is now a short `/bin/sh`
script that gives the runtime a directory root has just made for this run with
`mktemp -d` — a new name every time, mode 0700 — and removes it when the helper
exits. The AppImage and the helper's arguments reach the script as `"$@"` and
are never part of its text. `/run` would have been the obvious place, but Debian
and Ubuntu mount it `noexec`, and the runtime executes what it unpacks; a fixed
directory on disk would keep a copy of the app after every shutdown that caught
the helper running, where `/tmp` is emptied at boot. The script sets its own
`PATH` before it runs anything, and the helper inherits it: under sudo without
`secure_path` it would otherwise be the user's.

That protects what root unpacks, and with it an AppImage started normally,
through FUSE. It does not protect an AppImage started with
`--appimage-extract-and-run`, as where FUSE is missing, which autostart and the
restart after an update keep doing for a copy started that way. Before
FreeTunnel runs at all, the AppImage's own runtime unpacks it, as the user, into
the shared `/tmp`, and other accounts on the machine can interfere with that
copy, and so with what runs as the user. Nothing FreeTunnel does can help
there, since it has not started yet. On a machine shared with other accounts
and without FUSE, use the .deb, or set `TMPDIR` to a directory only the user can
write to for the whole login session. Setting it only in the shell the AppImage
is started from does not reach autostart, which starts it from the session's
environment; the restart after an update inherits it from the running copy.

The price is `/tmp` itself: where it is mounted `noexec`, the runtime cannot run
what it unpacked there, and the AppImage cannot connect. That was already so for
an AppImage started normally. One started with `--appimage-extract-and-run` and a
`TMPDIR` that allows execution used to get by, because root ran the copy the user
had unpacked there; now that it knows its `.AppImage` and root unpacks that
afresh in `/tmp`, it no longer does. The .deb is the way on such a system.

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

### Per-application rules read the system's socket tables

A split-tunnel rule that names a program has to be answered per connection: which
process owns the local socket this packet came from. That question is put to the
operating system — `GetExtendedTcpTable` / `GetExtendedUdpTable` on Windows,
`proc_pidinfo`/`proc_pidfdinfo` on macOS, the `inet_diag` netlink tables (or
`/proc/net`) plus the descriptor lists of the named processes on Linux — from the
elevated helper, which can therefore see processes belonging to other OS users.
Nothing is installed to do it: no driver, no system extension, no entitlement.

The program's name is deliberately **not** handed back to the core with the routing
decision. The core forwards that field into the CONNECT request it sends the VPN
endpoint, which would tell the operator which application opened every connection —
the one thing this app exists not to tell anyone. It goes to the local log instead.

That local log does name the program for connections a rule matched, and in verbose
mode for every connection. It is the same log the Logs page shows and the same file
a bug report attaches, so a log shared with someone else describes what was running.

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
| Other local user | Socket access-control list (ACL) + loopback-only helper; AppImage unpacked for root in a directory only root can write. Not covered: an AppImage started with `--appimage-extract-and-run`, which its own runtime first unpacks as the user in `/tmp` (use the .deb) |
| Same-user malware | Documented limitation; OS credential APIs; the helper reads only a token file and deletes nothing, whoever starts it. Can reach root through the user-owned `.AppImage`, not through the .deb |
| TOML injection | `tomlEsc()` strips control chars |
| Operator learns which app opened a flow | Program name kept out of the core decision, local log only |
| Unsigned installer | User warnings; in-app hash verify before install |
