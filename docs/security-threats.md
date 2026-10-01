# Security — threat model and IPC

Companion to [SECURITY.md](../SECURITY.md): single-instance control, deep links,
and threat summary.

## Single-instance control (deep links)

A second launch forwards commands (`freetunnel://toggle`, `tt://…` import) to the
running instance via a local socket (`QLocalServer`) protected by:

- A socket name of each user's own, created owner-only (`UserAccessOption`),
  and a check of the peer's user on both ends (uid on macOS/Linux, SID on
  Windows) — other OS users cannot connect; the second instance also verifies
  the listener's owner before sending the token, so a squatted socket name
  can't harvest it
- Per-session random token (stored in the OS credential store when available),
  removed when the instance quits
- Constant-time token comparison
- 64 KB message cap

A second launch that finds the running instance but cannot hand it the command
(the token unreadable, or no answer in time) exits rather than starting a second
instance beside it, and a starting instance clears the socket name only when
nothing listens on it, as after a crash. Builds up to 1.2.2 listened on one name
shared by all users; a second launch still forwards there, after its own name,
so an older instance left running through an update is found rather than
duplicated.

A launch gives way only to a listener that is provably this user's: one that
passed the peer check at a socket file of this user's own (macOS/Linux: at the
name itself, not through a link, which `connect()` would follow to any socket of
this user's, such as the session bus), or, when the connection does not go
through at all, such a socket file. What another account can still do with the
name differs by system:

- **Linux** puts the socket in `$XDG_RUNTIME_DIR` (`/run/user/<uid>`), a 0700
  directory of the user's own, which every systemd or elogind session has, so
  no other account can create this user's name or a link at it. A launch also
  tries `/tmp/FreeTunnelInstance-<uid>` (an instance started without
  `$XDG_RUNTIME_DIR`) and the shared `/tmp/FreeTunnelInstance`; another account
  can hold those, but a launch never gives way to a listener there that lets no
  one in, and one that answers fails the peer check, so all it costs is the
  time to find that out. Without `$XDG_RUNTIME_DIR`, the user's own name is the
  one in `/tmp`, and another account can create it first. The launch still
  starts, but cannot take the name (the sticky `/tmp` keeps it from renaming its
  socket over another account's file), so it runs without the single-instance
  listener: while the other account holds the name, every further launch and
  link starts another copy.
- **macOS** keeps the socket in the user's own temporary directory (`$TMPDIR`),
  which no other account can enter, the shared name included. A socket there
  turns a connection away when its backlog is full just as it does when nothing
  listens on it, so a refusal is taken for a stale name only when no running
  FreeTunnel holds the lock beside it (`<name>.lock`); a busy one is asked again
  for a second and then given way to, never cleared or taken over.
- **Windows** has one pipe namespace for every session, and the user's SID in
  the name is no secret, so another account can create this user's pipe first.
  It cannot keep FreeTunnel from starting: a pipe that does not let the launch
  in, or stays busy for the five seconds Qt waits for it, is no instance of
  ours, since FreeTunnel's listener keeps fifty instances of its pipe waiting
  for callers. But while the other account holds the pipe, FreeTunnel's own
  listener may not get the name, or not every connection made to it, so as on
  Linux without `$XDG_RUNTIME_DIR`, further launches and links can start
  another copy, after waiting out those five seconds when the pipe is kept
  busy. A starting FreeTunnel does not wait on the pipe a second time to see
  whether it is stale, since a pipe goes away with its last handle.

On every system the token and the command go only to a listener that passed
these checks, so a name another account holds never receives them.

### Links opened from a web page

`freetunnel://` and `tt://` are registered with the system, so any web page can
open one. The browser's prompt about opening FreeTunnel was the only gate, and
the user can tell it to stop asking. So a link cannot turn the VPN off by
itself: `freetunnel://disconnect`, or `toggle` while the tunnel is up or coming
up, brings up the window and asks (so does a `disconnect` that would keep
"Connect on startup" from connecting). The question is answered by a click
only: the page decides when it appears, and the window comes forward to take
the keyboard, so a Return the user is already holding must not be the answer.
Nor is a click that lands in the first moment, as one already on its way when
the window came forward would. The kill switch is no defence here, since a
disconnect the user asks for removes it with the session. Connecting is not
asked about. Every `tt://` import is, answered the same way, since the config it
adds is the page's choice.

A command run directly — the executable started with the URL as an argument, as
a Stream Deck button or a script does — still acts without asking; that is the
point of it. The two are told apart by `--url-handler`, which the Windows
installer's registrations and the Linux `.desktop` files put before the URL,
where nothing in the URL can remove it; on macOS links arrive as Apple events
and are always treated as links. A scheme registration made without the flag
(by hand, by a desktop-integration tool that drops arguments, or by
AppImageLauncher or Gear Lever from an AppImage older than the flag) turns links
back into commands.

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

Nor does the helper act on paths the GUI names, whether in the protocol or
inside the config (see below). Over IPC, the connect command must carry an inline
config (a file path is refused), and the core's log path is chosen by the helper
itself and is not part of the protocol at all — the GUI receives log lines over
IPC and keeps the durable copy. And the elevated argv is derived from the running
executable rather than from the environment.

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

The config is the other way in. The core reads more of it than FreeTunnel ever
writes, and an imported file keeps every key the editor does not know, so that
saving it loses nothing. Most of those keys only shape the tunnel. A few would
have the core, running as root, act on something the file names: a directory to
keep TLS sessions in, where the core deletes and writes files; ports the Windows
kill switch lets through; the name to give the tunnel interface; and, on Linux,
an existing interface to attach to and a network namespace to set ours up in.
The helper clears those keys before the core sees the config, whatever the GUI
sent, and refuses a config whose listener is a SOCKS proxy rather than a tunnel
interface. Routes, DNS, the endpoint and its certificate pass through untouched:
they are what a config is for.

The kill-switch ports are the one key the user can hand back to the config, since
reaching the machine from outside (Remote Desktop, say) with the kill switch on
needs them. A Windows-only setting, "Let the VPN config open ports", off by
default, tells the helper to keep them; the other keys stay cleared either way.
It travels to the helper as the kill switch itself does, so a process able to
send it could as well turn the kill switch off: it gives same-user malware
nothing it did not have. It is one setting for every config, though, not a
choice made for one: while it is on, every config the user connects with,
including one imported or swapped in later, again chooses which ports bypass
the kill switch. Turning it on is trusting every config in use.

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

### The kill switch lives in the VPN session

The kill switch is a flag on the VPN core's session, not a firewall of
FreeTunnel's own. While the session is up and not connected (connecting, or
recovering a connection that dropped) the core refuses the connections meant
for the tunnel rather than let them out over the open network; what the
split-tunnelling rules send around the tunnel still goes. Its tunnel device
keeps the routes, and on Windows the filters that block untunnelled traffic are
installed with that device and removed with it. So what the block covers is
exactly how long one session lasts.

A session therefore must not be rebuilt for something it can take while it
runs. The address rules and the mode are handed to the running session (the
core's own `vpn_update_exclusions()`, reached through FreeTunnel's third vendor
patch) in one update per edit, and before the program rules change, so that no
connection decided by the new program rules is completed under the old mode.
With the kill switch on, a first connect that fails stays in the core's
recovery loop inside its session instead of ending it after a few attempts, so
it no longer passes through FreeTunnel's own backoff, which took the session
down and built another every round.

What is left, and is accepted:

- **Building a session anew** lifts the block for the time it takes: switching
  configs, editing the excluded routes, the kill switch itself or (on Windows)
  whether a config may open ports in it while connected (all are read when a
  session is built), and starting over after an error the core treats as
  final, such as a refused login or certificate.
- **No session, no block.** The VPN being off, an error that stops it, and a
  config whose server is a domain name that cannot be resolved (the core needs
  its address before a session can start) all leave traffic unblocked.
- **The helper quits with the GUI**, and the block with the session it holds,
  so that an elevated process never outlives the app that started it.
- **An instant per edit.** Program rules are decided as a connection arrives
  and the mode when the core completes it, and the two cannot change together.
  An edit that turns "Through VPN" on and lists a program in the same step, as
  adding the first program with no addresses listed does, can let a connection
  that program starts in that instant leave the tunnel. So can taking the last
  program out of "Through VPN" while a session is still being built, in the
  moment before the new session is handed the change. And so can two edits
  that land before the core has taken the first: it keeps only the latest
  update, which carries the whole of the rules and the mode, so the session
  ends up routed by the second edit, but a connection the first edit's program
  rules decided in between is completed under the mode from before it.

### Server probes go around the tunnel where the system lets them

The Configs page shows a latency figure per server. Those probes are meant to
measure the server rather than the tunnel, so on macOS and Windows each one is
bound to the physical interface (`IP_BOUND_IF` / `IPV6_BOUND_IF`, `IP_UNICAST_IF` /
`IPV6_UNICAST_IF`) and goes around the tunnel **even while connected**. On Linux
the GUI, which is not root, can only bind the probe's source address, and the
tunnel routes by destination, taking no notice of that: while connected, probes
there go through the tunnel. While connected, the server in use is probed with a
plain socket on every platform, because the core already routes its address
around the tunnel.

The consequence is that refreshing that page reveals the full list of configured
endpoints, including servers never connected to, to the local network and the
ISP: always while disconnected, and while connected too on macOS and Windows. On
Linux while connected, it is the server you are connected to that sees the probes
to the others, inside the tunnel. Traffic that is not a probe is unaffected.

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
signature from the compiled-in key, the manifest must name the version being
offered, and the installer's SHA-256 must appear in that manifest. Only a version
newer than the installed one is offered at all.

What is left depends on what an attacker can impersonate. Without a certificate
the machine trusts, an attacker on the network can at most block the check.
With one:

- **Forging the API response alone** (a TLS-intercepting proxy the machine
  trusts, or a compromised certificate authority, used against
  `api.github.com`). The asset URLs must point at the advertised tag's download
  path, and the real `github.com` serves only that release's real assets there.
  So the attacker chooses which genuine release is on offer — any one newer than
  the installed build, not necessarily the newest — or claims there is none.
  That can hold a user back, short of a fix they are waiting for. It cannot
  install anything the release job did not publish, nor move a user backwards.
  This is inherent in asking the API which release is newest: no check on the
  client tells a withheld release from one that does not exist.
- **Impersonating `github.com` as well** (the same capability, used against the
  download host and the host it redirects to). Now any bytes can be served under
  any path, so the tag in the URL binds nothing, and the asset names carry no
  version — `freetunnel-linux-x86_64.deb` is the same name in every release. What
  is left is the signed manifest. Since 1.1.8 the release job writes a
  `#version=X` line into it, inside the bytes the signature covers, and the
  updater refuses a manifest whose version is not the one being offered. A
  manifest signed for one release cannot be served as another's, and this
  attacker too is left choosing among genuine releases newer than the installed
  one.

Releases before 1.1.8 have no version line. FreeTunnel 1.1.8 to 1.2.2 accepted a
manifest without one, so that those releases stayed installable, and that undid
the binding against the second attacker: any pre-1.1.8 release, offered under a
forged higher tag, passed every check — a rollback onto a build with known holes.
The updater now refuses a manifest that names no version. No client running that
check can be stranded by it, because it is only ever offered a newer release, and
every release since 1.1.8 has the line. Clients from before this change, 1.2.2
and earlier, keep accepting such manifests until they update.

The line is written as `#version=X`, and the spelling is load-bearing. The `#`
is for the people who verify the file by hand: `sha256sum -c` calls any line it
cannot parse "improperly formatted", which reads like tampering and fails
outright under `--strict`. It tolerates comments. The absence of a space keeps
the line a single field, and the manifest parser in every already-shipped client
skips lines with fewer than two fields — so old clients neither trip over it nor
mistake it for an asset name.

## Deep links (`tt://`)

Config import links use TrustTunnel's type-length-value (TLV) / base64url format.
Passwords from links go directly to the credential store, not on-disk TOML.

## File access from QML

`safeReadUserTextFile()` only reads regular files under the user's home, temp,
downloads, documents, or desktop directories; symlinks are rejected.

## Threat summary

| Threat | Mitigation |
| --- | --- |
| Remote man-in-the-middle (MITM) on update | SHA256 manifest + Ed25519 signature naming its release version; at worst a withheld update, never an unsigned or older build |
| Malicious `tt://` link | TLV parser limits; cred store separation; every import asks, answered by a click |
| Web page opens `freetunnel://disconnect` or `toggle` | Asks in the window, answered by a click once the question has been up a moment, before turning the VPN off |
| Other local user | A single-instance socket name of each user's own (Linux: in `$XDG_RUNTIME_DIR`), owner-only, peer checked at both ends, so the token and commands never reach another account; loopback-only helper; AppImage unpacked for root in a directory only root can write. Not covered: an AppImage started with `--appimage-extract-and-run`, which its own runtime first unpacks as the user in `/tmp` (use the .deb); on Windows, and on Linux without `$XDG_RUNTIME_DIR`, another account taking the user's socket name first, which makes later launches start further copies |
| Same-user malware | Documented limitation; OS credential APIs; the helper reads only a token file and deletes nothing, whoever starts it. Can reach root through the user-owned `.AppImage`, not through the .deb |
| TOML injection | `tomlEsc()` strips control chars |
| Config keys that point the elevated core at a path, interface or port | Helper clears the five such keys (session-cache folder, kill-switch ports, interface name, attach, namespace; the ports kept only if the user turns that on, for every config); tunnel listener only; an upstream bump whose core reads a new key fails `verify_upstream_patch.sh` |
| Operator learns which app opened a flow | Program name kept out of the core decision, local log only |
| Unsigned installer | User warnings; in-app hash verify before install |
| Traffic leaving outside the tunnel while it is not connected | Kill switch, held by one session through rule edits and failed first connects; remaining gaps listed above |
