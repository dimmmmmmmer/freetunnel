# Security threat model

What FreeTunnel defends against, how, and what is left open. Each section takes
one asset or trust boundary and lists **threat → mitigation → residual risk**;
the reasoning behind the design comes last in the section. For contributors and
reviewers of security-relevant code. The short version for users, and how to
report a vulnerability, is in [SECURITY.md](../SECURITY.md).

## Contents

- [Threat summary](#threat-summary)
- [Processes running as the same user](#processes-running-as-the-same-user)
- [Privileged helper and its IPC](#privileged-helper-and-its-ipc)
- [AppImage elevation on Linux](#appimage-elevation-on-linux)
- [Config files reaching root](#config-files-reaching-root)
- [Updates and signatures](#updates-and-signatures)
- [Single-instance channel](#single-instance-channel)
- [Deep links and control links](#deep-links-and-control-links)
- [Credential storage](#credential-storage)
- [Files read from the UI](#files-read-from-the-ui)
- [Logs](#logs)
- [Network: kill switch, probes and per-app rules](#network-kill-switch-probes-and-per-app-rules)
- [CI and release](#ci-and-release)

## Threat summary

| Threat | Mitigation | Details |
| --- | --- | --- |
| Remote man-in-the-middle (MITM) on update | SHA256 manifest + Ed25519 signature naming its release version; at worst a withheld update, never an unsigned or older build | [Updates](#updates-and-signatures) |
| Malicious `tt://` link | TLV parser limits; cred store separation; every import asks, answered by a click | [Deep links](#a-web-page-adds-a-config-with-a-tt-link) |
| Web page opens `freetunnel://disconnect` or `toggle` | Asks in the window, answered by a click once the question has been up a moment, before turning the VPN off | [Deep links](#a-web-page-turns-the-vpn-off) |
| Other local user, at the single-instance socket | A socket name of each user's own (Linux: in `$XDG_RUNTIME_DIR`), owner-only, peer checked at both ends, so the token and commands never reach another account. Not covered: on Windows, and on Linux without `$XDG_RUNTIME_DIR`, another account taking the user's socket name first, which makes later launches start further copies | [Single instance](#single-instance-channel) |
| Other local user, at the helper | Loopback-only helper; AppImage unpacked for root in a directory only root can write. Not covered: an AppImage started with `--appimage-extract-and-run`, which its own runtime first unpacks as the user in `/tmp` (use the .deb) | [Helper](#privileged-helper-and-its-ipc), [AppImage](#appimage-elevation-on-linux) |
| Same-user malware | Documented limitation; OS credential APIs; the helper reads only a token file and deletes nothing, whoever starts it. Can reach root through the user-owned `.AppImage`, not through the .deb | [Same user](#processes-running-as-the-same-user) |
| TOML injection | `tomlEsc()` strips control chars | [Config files](#toml-injection) |
| Config keys that point the elevated core at a path, interface or port | Helper clears the five such keys (session-cache folder, kill-switch ports, interface name, attach, namespace; the ports kept only if the user turns that on, for every config); tunnel listener only; an upstream bump whose core reads a new key fails `verify_upstream_patch.sh` | [Config files](#config-files-reaching-root) |
| Operator learns which app opened a flow | Program name kept out of the core decision, local log only | [Per-app rules](#per-application-rules) |
| Unsigned installer | User warnings; in-app hash verify before install | [CI and release](#ci-and-release) |
| Traffic leaving outside the tunnel while it is not connected | Kill switch, held by one session through rule edits and failed first connects; remaining gaps listed under the kill switch | [Kill switch](#kill-switch) |

## Processes running as the same user

This is a known limitation, typical for desktop apps without a system daemon.

Any process running as the **same OS user** can:

- read the instance-auth credential (when stored in Secret Service /
  Keychain);
- connect to the local control socket if it obtains the token;
- read the helper inter-process communication (IPC) token file during VPN
  connect;
- toggle the VPN or import configs.

It **cannot**:

- read Keychain/Secret Service entries without OS APIs available to that user
  anyway;
- turn the elevated helper to ends of its own: however it is started, the
  helper does no more than the GUI could ask of it (see
  [the helper](#a-same-user-process-starts-the-helper-itself));
- reach root through the .deb, whose files only root can change: we know of no
  way left for it to do so through us. The AppImage is the exception (see
  [AppImage](#same-user-malware-replaces-the-appimage)).

Mitigations already in place: no remote attack surface for control IPC, tokens
rotate each session, helper binds to loopback only.

## Privileged helper and its IPC

The helper (`--helper`) runs elevated and runs the VPN tunnel via the
TrustTunnel core. The GUI talks to it over loopback TCP;
[SECURITY.md](../SECURITY.md#how-the-app-is-split) has the overview.

### A local process poses as the helper

- **Threat:** the helper listens on a random loopback port, but only once the
  elevation prompt has been answered, seconds to a minute after the GUI starts
  trying to connect. Any local process can bind a port in that window.
- **Mitigation:** the handshake is mutual and neither side puts the token on
  the wire:
  1. The GUI opens with a nonce.
  2. The helper answers with an HMAC over it (keyed by the one-time token) plus
     a nonce of its own.
  3. Only then does the GUI send its own proof and, after that, the config.

  A peer that cannot prove it holds the token never receives the config TOML,
  which carries the VPN password, and cannot report a tunnel that does not
  exist.
- **Residual risk:** a process running as the same user can read the token file
  during VPN connect ([same user](#processes-running-as-the-same-user)).

### Strangers hold the root process open

- **Threat:** connections that never authenticate tie up the root process.
- **Mitigation:** Pre-authentication connections are capped and time-limited
  so they cannot exhaust the root process.

### A same-user process starts the helper itself

- **Threat:** a process running as the user can start the genuine helper binary
  itself, with arguments of its choosing, behind an administrator prompt that
  is the real one. Whatever the helper does with those arguments, it does as
  root or Administrator once the user approves.
- **Mitigation:** the helper's protections hold whoever starts it. From its
  command line it takes a port and the path of the token file, and:
  - that file it only reads, and only if it could be one: named `.fthelper-…`,
    a regular file rather than a link to another (on Linux and macOS not a
    second hard link either), of at most 128 bytes;
  - it deletes nothing. The GUI removes its own token file, as the user, and at
    its next start any that a GUI which ended before the helper answered left
    behind;
  - the elevated argv is derived from the running executable rather than from
    the environment ([AppImage](#appimage-elevation-on-linux)).
- **Residual risk:** on Windows a folder in the path can still be made to lead
  to another file without the name showing it, so there the helper may read a
  file that small which the user could not. It serves only as the key for the
  handshake, and what leaves the helper is a hash over a nonce, which gives
  nothing away unless the file's contents can be guessed.

### Paths named to the helper

- **Threat:** a path in the protocol or the config makes the elevated helper act
  on a file the caller chose.
- **Mitigation:** the helper does not act on paths the GUI names, whether in the
  protocol or inside the config ([config files](#config-files-reaching-root)):
  - over IPC, the connect command must carry an inline config; a file path is
    refused;
  - the core's log path is chosen by the helper itself and is not part of the
    protocol at all ([logs](#logs)).

### An elevated process outlives the app

- **Mitigation:** the helper quits with the GUI, so that an elevated process
  never outlives the app that started it.
- **Residual risk:** the kill switch's block goes with the session the helper
  holds ([kill switch](#kill-switch)).

### Why it works this way

The helper used to read whatever token file it was named and then delete it, so
a prompt the user approved could delete any file as root or Administrator. Its
rules have to hold whoever starts it, because a same-user process can start the
genuine binary behind the real prompt.

## AppImage elevation on Linux

An AppImage build has to re-exec the `.AppImage` file as root rather than the
executable inside its FUSE mount, since root cannot read that user-private
mount. Root does not mount the AppImage; it unpacks it, with the runtime's
extract-and-run, which also works where there is no FUSE.

### Root runs a path the environment chose

- **Threat:** an attacker who can set the GUI's environment chooses what the
  administrator prompt runs as root.
- **Mitigation:** the answer comes from the kernel. `runningAppImagePath()`:
  1. resolves `/proc/self/exe`;
  2. finds the FUSE mount containing it in `/proc/self/mountinfo`;
  3. takes that mount's backing file.

  An AppImage started without FUSE (`--appimage-extract-and-run`) has no mount.
  The runtime unpacks it into `appimage_extracted_<MD5 of the AppImage>` and
  waits as the app's parent, so the answer is `/proc/<parent>/exe`, accepted
  only when that file's MD5 is the one in the directory's name.

  When the kernel does not name a regular file either way, elevation falls back
  to the running executable rather than guessing.

### Same-user malware replaces the `.AppImage`

- **Threat:** the file the administrator prompt runs is the `.AppImage` itself,
  and it belongs to the user. Anything running as that user can replace it, and
  the next time the user authorizes a connection, root runs the replacement.
- **Mitigation:** none is possible for the AppImage; it cannot be otherwise.
  With the .deb, whose files only root can change, we know of no way left for
  same-user malware to reach root through us.
- **Residual risk:** the AppImage is in the position of any program a user runs
  with sudo out of their own home directory. Where that matters, use the .deb.

### Other users prepare root's unpack directory

- **Threat:** pkexec does not pass `TMPDIR` on, so root unpacked under a fixed
  name in the shared `/tmp`, which another account could prepare in advance.
- **Mitigation:** the elevated command is now a short `/bin/sh` script that:
  - gives the runtime a directory root has just made for this run with
    `mktemp -d` (a new name every time, mode 0700), and removes it when the
    helper exits;
  - receives the AppImage and the helper's arguments as `"$@"`; they are never
    part of its text;
  - sets its own `PATH` before it runs anything, and the helper inherits it:
    under sudo without `secure_path` it would otherwise be the user's.
- **Residual risk:** where `/tmp` is mounted `noexec`, the runtime cannot run
  what it unpacked there, and the AppImage cannot connect. The .deb is the way
  on such a system.

### AppImages started with `--appimage-extract-and-run`

- **Threat:** before FreeTunnel runs at all, the AppImage's own runtime unpacks
  it, as the user, into the shared `/tmp`. Other accounts on the machine can
  interfere with that copy, and so with what runs as the user. An AppImage is
  started this way where FUSE is missing, for one, and autostart and the
  restart after an update keep doing it for a copy started that way.
- **Mitigation:** nothing FreeTunnel does can help there, since it has not
  started yet. The script above protects what root unpacks, and with it an
  AppImage started normally, through FUSE, but not this copy.
- **Residual risk:** on a machine shared with other accounts and without FUSE,
  use the .deb, or set `TMPDIR` to a directory only the user can write to for
  the whole login session:
  - setting it only in the shell the AppImage is started from does not reach
    autostart, which starts it from the session's environment;
  - the restart after an update inherits it from the running copy.

### Why it works this way

- **The environment is not validation.** Naming the file from `$APPIMAGE` and
  validating it against `$APPDIR`, as an earlier version did, is not validation
  at all: an attacker who can set the GUI's environment sets both sides. And
  `$APPDIR` only had to be a path *prefix* of the executable, so `APPDIR=/usr`
  passed for an ordinary `/usr/bin/FreeTunnel` install and `$APPIMAGE` was then
  run as root. A prompt that is about to run something as root gets a definite
  answer or none.
- **Why `/tmp`.** `/run` would have been the obvious place, but Debian and
  Ubuntu mount it `noexec`, and the runtime executes what it unpacks. A fixed
  directory on disk would keep a copy of the app after every shutdown that
  caught the helper running, where `/tmp` is emptied at boot.
- **What `noexec` `/tmp` costs.** It already stopped an AppImage started
  normally. One started with `--appimage-extract-and-run` and a `TMPDIR` that
  allows execution used to get by, because root ran the copy the user had
  unpacked there; now that it knows its `.AppImage` and root unpacks that afresh
  in `/tmp`, it no longer does.

## Config files reaching root

The core reads more of a config than FreeTunnel ever writes, and an imported
file keeps every key the editor does not know, so that saving it loses nothing.
Most of those keys only shape the tunnel.

### Keys that point the elevated core at a path, interface or port

- **Threat:** a few keys would have the core, running as root, act on something
  the file names:
  - a directory to keep TLS sessions in, where the core deletes and writes
    files;
  - ports the Windows kill switch lets through;
  - the name to give the tunnel interface;
  - on Linux, an existing interface to attach to;
  - on Linux, a network namespace to set ours up in.
- **Mitigation:** the helper clears those keys before the core sees the config,
  whatever the GUI sent (`clearKeysRootMustNotTakeFromAConfig()` in
  `src/vpn/qt_trusttunnel_client.cpp`). It refuses a config whose listener is a
  SOCKS proxy rather than a tunnel interface. An upstream bump whose core reads
  a new key fails `verify_upstream_patch.sh`
  ([CI and release](#ci-and-release)).
- **Residual risk:** the kill-switch ports, if the user hands them back (next
  section).

Routes, DNS, the endpoint and its certificate pass through untouched: they are
what a config is for.

### The "Let the VPN config open ports" setting (Windows)

The kill-switch ports are the one key the user can hand back to the config,
since reaching the machine from outside (Remote Desktop, say) with the kill
switch on needs them.

- **Threat:** while the setting is on, every config the user connects with,
  including one imported or swapped in later, again chooses which ports bypass
  the kill switch.
- **Mitigation:**
  - it is Windows-only and off by default (Settings → Security);
  - it tells the helper to keep the ports only; the other keys stay cleared
    either way;
  - it travels to the helper as the kill switch itself does, so a process able
    to send it could as well turn the kill switch off: it gives same-user
    malware nothing it did not have.
- **Residual risk:** it is one setting for every config, not a choice made for
  one. Turning it on is trusting every config in use.

### TOML injection

- **Threat:** a field value breaks out of its quoted string in a TOML file
  FreeTunnel writes.
- **Mitigation:** `tomlEsc()` (`src/core/ConfigToml.cpp`) escapes backslashes
  and quotes and strips control chars.

## Updates and signatures

The update check trusts the GitHub Application Programming Interface (API)
response for *which* release exists. Everything it names is then checked:

- asset Uniform Resource Locators (URLs) must sit under this repository's
  release download path for the advertised tag;
- `SHA256SUMS.txt` must carry a valid Ed25519 signature from the compiled-in
  key;
- the manifest must name the version being offered;
- the installer's SHA-256 must appear in that manifest.

Only a version newer than the installed one is offered at all.

What is left depends on what an attacker can impersonate:

| Attacker | Can | Cannot |
| --- | --- | --- |
| On the network, without a certificate the machine trusts | At most block the check | Anything more |
| Forging the API response | Choose which genuine release newer than the installed build is on offer, or claim there is none | Install anything the release job did not publish, or move a user backwards |
| Impersonating `github.com` as well | The same choice among genuine newer releases | Serve a manifest signed for one release as another's, so not move a user backwards either (clients 1.2.2 and earlier: see [rollback](#rollback-onto-an-older-build)) |

### The API response is forged

- **Threat:** an attacker forges the API response alone, with a
  TLS-intercepting proxy the machine trusts or a compromised certificate
  authority, used against `api.github.com`.
- **Mitigation:** the asset URLs must point at the advertised tag's download
  path, and the real `github.com` serves only that release's real assets there.
- **Residual risk:** the attacker chooses which genuine release is on offer
  (any one newer than the installed build, not necessarily the newest) or
  claims there is none. That can hold a user back, short of a fix they are
  waiting for, but cannot install anything the release job did not publish,
  nor move a user backwards.

### `github.com` is impersonated as well

- **Threat:** an attacker uses the same capability against the download host
  and the host it redirects to as well. Now any bytes can be served under any
  path, so the tag in the URL binds nothing, and the asset names carry no
  version: `freetunnel-linux-x86_64.deb` is the same name in every release.
  What is left is the signed manifest.
- **Mitigation:** since 1.1.8 the release job writes a `#version=X` line into
  the manifest, inside the bytes the signature covers. The updater refuses a
  manifest whose version is not the one being offered, and now also one that
  names no version. A manifest signed for one release cannot be served as
  another's.
- **Residual risk:** this attacker too is left choosing among genuine releases
  newer than the installed one. Clients 1.2.2 and earlier are open to a rollback
  ([next section](#rollback-onto-an-older-build)).

### Rollback onto an older build

- **Threat:** a genuine older release, offered under a forged higher tag.
- **Mitigation:** only a newer version is offered, the manifest must name the
  version being offered, and a manifest that names no version is refused.
- **Residual risk:** releases before 1.1.8 have no version line. FreeTunnel
  1.1.8 to 1.2.2 accepted a manifest without one, so that those releases stayed
  installable, and that undid the binding against an attacker impersonating
  `github.com`: any pre-1.1.8 release, offered under a forged higher tag, passed
  every check, a rollback onto a build with known holes. Clients from before the
  updater refused such manifests, 1.2.2 and earlier, keep accepting them until
  they update.

### Why it works this way

- **Trusting the API is inherent** in asking it which release is newest: no
  check on the client tells a withheld release from one that does not exist.
- **Refusing an unversioned manifest strands no one.** No client running that
  check can be stranded by it, because it is only ever offered a newer release,
  and every release since 1.1.8 has the line.
- **The line is written as `#version=X`, and the spelling is load-bearing.**
  - The `#` is for the people who verify the file by hand: `sha256sum -c` calls
    any line it cannot parse "improperly formatted", which reads like tampering
    and fails outright under `--strict`. It tolerates comments.
  - The absence of a space keeps the line a single field, and the manifest
    parser in every already-shipped client skips lines with fewer than two
    fields, so old clients neither trip over it nor mistake it for an asset
    name.

## Single-instance channel

A second launch forwards commands (`freetunnel://toggle`, `tt://…` import) to
the running instance via a local socket (`QLocalServer`).

### Another account reaches the control socket

- **Threat:** another OS user connects to the socket, or holds its name to
  collect the token and the commands.
- **Mitigation:**
  - a socket name of each user's own, created owner-only (`UserAccessOption`);
  - a check of the peer's user on both ends (uid on macOS/Linux, SID on
    Windows), so other OS users cannot connect;
  - the second instance verifies the listener's owner before sending the token,
    so a squatted socket name can't harvest it;
  - a per-session random token (stored in the OS credential store when
    available), removed when the instance quits;
  - constant-time token comparison;
  - a 64 KB message cap;
  - a launch gives way only to a listener that is provably this user's: one that
    passed the peer check at a socket file of this user's own (macOS/Linux: at
    the name itself, not through a link, which `connect()` would follow to any
    socket of this user's, such as the session bus), or, when the connection
    does not go through at all, such a socket file.

  On every system the token and the command go only to a listener that passed
  these checks, so a name another account holds never receives them.
- **Residual risk:** on Windows, and on Linux without `$XDG_RUNTIME_DIR`,
  another account can create the user's name first; further launches and links
  then start another copy. Per system:

| System | Where the socket is | Can another account take the user's name first? | If it does |
| --- | --- | --- | --- |
| Linux with `$XDG_RUNTIME_DIR` | `$XDG_RUNTIME_DIR` (`/run/user/<uid>`), a 0700 directory of the user's own | No, nor make a link at it | — |
| Linux without it | `/tmp/FreeTunnelInstance-<uid>`, in the shared, sticky `/tmp` | Yes | The launch runs without the single-instance listener; every further launch and link starts another copy |
| macOS | The user's own temporary directory (`$TMPDIR`) | No, the shared name included | — |
| Windows | One pipe namespace for every session | Yes: the user's SID in the name is no secret | FreeTunnel still starts; further launches and links can start another copy |

#### Linux

- Every systemd or elogind session has `$XDG_RUNTIME_DIR`.
- A launch also tries `/tmp/FreeTunnelInstance-<uid>` (an instance started
  without `$XDG_RUNTIME_DIR`) and the shared `/tmp/FreeTunnelInstance`. Another
  account can hold those, but a launch never gives way to a listener there that
  lets no one in, and one that answers fails the peer check, so all it costs is
  the time to find that out.
- Without `$XDG_RUNTIME_DIR`, the user's own name is the one in `/tmp`, and
  another account can create it first. The launch still starts, but cannot take
  the name (the sticky `/tmp` keeps it from renaming its socket over another
  account's file), so it runs without the single-instance listener. While the
  other account holds the name, every further launch and link starts another
  copy.

#### macOS

- No other account can enter `$TMPDIR`.
- A socket there turns a connection away when its backlog is full just as it
  does when nothing listens on it. So a refusal is taken for a stale name only
  when no running FreeTunnel holds the lock beside it (`<name>.lock`).
- A busy one is asked again for a second and then given way to, never cleared
  or taken over.

#### Windows

- Another account can create this user's pipe first, but it cannot keep
  FreeTunnel from starting: a pipe that does not let the launch in, or stays
  busy for the five seconds Qt waits for it, is no instance of ours, since
  FreeTunnel's listener keeps fifty instances of its pipe waiting for callers.
- While the other account holds the pipe, FreeTunnel's own listener may not get
  the name, or not every connection made to it. So, as on Linux without
  `$XDG_RUNTIME_DIR`, further launches and links can start another copy, after
  waiting out those five seconds when the pipe is kept busy.
- A starting FreeTunnel does not wait on the pipe a second time to see whether
  it is stale, since a pipe goes away with its last handle.

### How a second launch behaves

- A second launch that finds the running instance but cannot hand it the
  command (the token unreadable, or no answer in time) exits rather than
  starting a second instance beside it.
- A starting instance clears the socket name only when nothing listens on it,
  as after a crash.
- Builds up to 1.2.2 listened on one name shared by all users. A second launch
  still forwards there, after its own name, so an older instance left running
  through an update is found rather than duplicated.

## Deep links and control links

`freetunnel://` and `tt://` are registered with the system, so any web page can
open one. The browser's prompt about opening FreeTunnel was the only gate, and
the user can tell it to stop asking.

### A web page turns the VPN off

- **Threat:** a page opens `freetunnel://disconnect`, or `toggle` while the
  tunnel is up or coming up. The kill switch is no defence here, since a
  disconnect the user asks for removes it with the session.
- **Mitigation:** a link cannot turn the VPN off by itself. It brings up the
  window and asks; so does a `disconnect` that would keep "Connect on startup"
  from connecting. The question is answered by a click only (not Return), and
  not by a click that lands in the first moment. Connecting is not asked about.
- **Residual risk:** a scheme registration made without `--url-handler` turns
  links back into commands, which act without asking (see
  [commands and links](#commands-run-directly-and-links)).

### A web page adds a config with a `tt://` link

- **Threat:** the config a `tt://` import adds is the page's choice.
- **Mitigation:**
  - every `tt://` import asks, answered the same way: by a click;
  - config import links use TrustTunnel's type-length-value (TLV) / base64url
    format, read with TLV parser limits;
  - passwords from links go directly to the credential store, not on-disk TOML;
  - the helper clears the config keys that would point root at a path, port or
    interface ([config files](#config-files-reaching-root)).

### Commands run directly and links

- A command run directly (the executable started with the URL as an argument,
  as a Stream Deck button or a script does) still acts without asking; that is
  the point of it.
- The two are told apart by `--url-handler`, which the Windows installer's
  registrations and the Linux `.desktop` files put before the URL, where nothing
  in the URL can remove it.
- On macOS links arrive as Apple events and are always treated as links.
- **Residual risk:** a scheme registration made without the flag turns links
  back into commands: one made by hand, by a desktop-integration tool that drops
  arguments, or by AppImageLauncher or Gear Lever from an AppImage older than
  the flag.

### Why it works this way

The question is answered by a click only because the page decides when it
appears, and the window comes forward to take the keyboard, so a Return the
user is already holding must not be the answer. Nor is a click that lands in the
first moment, as one already on its way when the window came forward would.

## Credential storage

Where each platform keeps passwords, and what happens on Linux without a Secret
Service, is in [SECURITY.md](../SECURITY.md#credential-storage).

- **Threat:** passwords read from config files on disk.
- **Mitigation:** passwords live in the OS credential store; passwords from
  links go directly there; the helper's config is built in memory for each
  connect ([SECURITY.md](../SECURITY.md#during-connect)).
- **Residual risk:**
  - password files older builds wrote stay on disk until the app next reads
    each one with secure storage available
    ([SECURITY.md](../SECURITY.md#password-files-from-older-builds));
  - a process running as the same user can read the instance-auth credential
    (when stored in Secret Service / Keychain), but not Keychain/Secret Service
    entries without OS APIs available to that user anyway
    ([same user](#processes-running-as-the-same-user)).

## Files read from the UI

- **Threat:** a path handed over from the QML UI leads the GUI to read a file it
  should not.
- **Mitigation:** `safeReadUserTextFile()` only reads regular files under the
  user's home, temp, downloads, documents, or desktop directories;
  symlinks are rejected.

## Logs

- **Threat:** a caller-supplied log path would be a write primitive for root:
  whoever names the path chooses where root writes.
- **Mitigation:** the core's log path is chosen by the helper itself and is not
  part of the protocol at all. The GUI receives log lines over IPC and keeps the
  durable copy.
- **Residual risk:** the local log names the program for connections a
  per-application rule matched, and in verbose mode for every connection. It is
  the same log the Logs page shows and the same file a bug report attaches, so a
  log shared with someone else describes what was running.

## Network: kill switch, probes and per-app rules

### Kill switch

The kill switch is a flag on the VPN core's session, not a firewall of
FreeTunnel's own.

- **Threat:** traffic leaving outside the tunnel while it is not connected.
- **Mitigation:**
  - while the session is up and not connected (connecting, or recovering a
    connection that dropped), the core refuses the connections meant for the
    tunnel rather than let them out over the open network; what the
    split-tunnelling rules send around the tunnel still goes;
  - its tunnel device keeps the routes, and on Windows the filters that block
    untunnelled traffic are installed with that device and removed with it, so
    what the block covers is exactly how long one session lasts;
  - the address rules and the mode are handed to the running session (the
    core's own `vpn_update_exclusions()`, reached through FreeTunnel's third
    vendor patch) in one update per edit, and before the program rules change,
    so that no connection decided by the new program rules is completed under
    the old mode;
  - with the kill switch on, a first connect that fails stays in the core's
    recovery loop inside its session instead of ending it after a few attempts,
    so it no longer passes through FreeTunnel's own backoff, which took the
    session down and built another every round.
- **Residual risk**, accepted:
  - **Building a session anew** lifts the block for the time it takes:
    switching configs, editing the excluded routes, the kill switch itself or
    (on Windows) whether a config may open ports in it while connected (all are
    read when a session is built), and starting over after an error the core
    treats as final, such as a refused login or certificate.
  - **No session, no block.** The VPN being off, an error that stops it, and a
    config whose server is a domain name that cannot be resolved (the core needs
    its address before a session can start) all leave traffic unblocked.
  - **The helper quits with the GUI**, and the block with the session it holds,
    so that an elevated process never outlives the app that started it.
  - **An instant per edit.** Program rules are decided as a connection arrives
    and the mode when the core completes it, and the two cannot change together:
    - an edit that turns "Through VPN" on and lists a program in the same step,
      as adding the first program with no addresses listed does, can let a
      connection that program starts in that instant leave the tunnel;
    - so can taking the last program out of "Through VPN" while a session is
      still being built, in the moment before the new session is handed the
      change;
    - and so can two edits that land before the core has taken the first: it
      keeps only the latest update, which carries the whole of the rules and the
      mode, so the session ends up routed by the second edit, but a connection
      the first edit's program rules decided in between is completed under the
      mode from before it.

**Why:** the block covers exactly how long one session lasts, so a session must
not be rebuilt for something it can take while it runs. That is why edits go to
the running session and a failed first connect stays inside it.

### Server latency probes

The Configs page shows a latency figure per server. Those probes are meant to
measure the server rather than the tunnel, so every probe is bound to the
physical interface as far as the platform allows, connected or not
(`startPingProbe()` in `src/app/BackendConfig.cpp`, the binding in
`src/core/NetBind.cpp`). The one exception, on every platform: while connected,
the server in use is probed with a plain socket, because the core already
routes its address around the tunnel.

| Platform | How a probe is bound | While connected |
| --- | --- | --- |
| macOS | To the physical interface: `IP_BOUND_IF` / `IPV6_BOUND_IF` | Probes go around the tunnel |
| Windows | To the physical interface: `IP_UNICAST_IF` / `IPV6_UNICAST_IF` | Probes go around the tunnel |
| Linux | Only to the physical interface's source address: the GUI is not root | The tunnel routes by destination, taking no notice of the source address: probes go through the tunnel |

While disconnected there is no tunnel, and every probe goes out directly. When
no physical interface is found, or binding it fails, a probe goes out on a
plain socket and follows the routing table.

- **Residual risk:** refreshing that page reveals the full list of configured
  endpoints, including servers never connected to, to the local network and the
  ISP: always while disconnected, and while connected too on macOS and Windows.
  On Linux while connected, it is the server you are connected to that sees the
  probes to the others, inside the tunnel. Traffic that is not a probe is
  unaffected.

### Per-application rules

A split-tunnel rule that names a program has to be answered per connection:
which process owns the local socket this packet came from. The elevated helper
puts that question to the operating system:

| Platform | Source |
| --- | --- |
| Windows | `GetExtendedTcpTable` / `GetExtendedUdpTable` |
| macOS | `proc_pidinfo` / `proc_pidfdinfo` |
| Linux | The `inet_diag` netlink tables (or `/proc/net`) plus the descriptor lists of the named processes |

The helper can therefore see processes belonging to other OS users. Nothing is
installed to do it: no driver, no system extension, no entitlement.

- **Threat:** the VPN operator learns which application opened every
  connection, the one thing this app exists not to tell anyone.
- **Mitigation:** the program's name is deliberately **not** handed back to the
  core with the routing decision, because the core forwards that field into the
  CONNECT request it sends the VPN endpoint. The name goes to the local log
  instead.
- **Residual risk:** the local log names the program ([logs](#logs)).

## CI and release

### Releases the release job did not publish

- **Threat:** a release asset or manifest that the release job did not publish.
- **Mitigation:** the release job signs `SHA256SUMS.txt` with the
  `ED25519_SIGNING_KEY` secret, and every shipped binary checks it against the
  compiled-in public key (`include/core/ReleaseSigning.h`). The tag build fails
  rather than publish an unsigned release. Details in
  [SECURITY.md](../SECURITY.md#release-signing) and under
  [updates](#updates-and-signatures).

### An upstream core that reads new config keys

- **Threat:** an upstream core bump starts reading a new config key, which
  would reach root without anyone having looked at it.
- **Mitigation:** an upstream bump whose core reads a new key fails
  `scripts/verify_upstream_patch.sh` until someone has read what the core does
  with it, cleared it in the helper if it points root at something, and listed
  it in the script. The security workflow (`.github/workflows/security.yml`)
  runs that check; see [CONTRIBUTING.md](../CONTRIBUTING.md).

### Unsigned installers

- **Threat:** an unsigned installer.
- **Mitigation:** user warnings; in-app hash verify before install.
- **Residual risk:** release binaries are not code-signed, so the OS prompts on
  first launch and the user approves it manually
  ([SECURITY.md](../SECURITY.md#release-signing)).
