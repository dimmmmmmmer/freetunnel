# Android preview changes

## Unreleased

- Android TV: FreeTunnel appears on the TV launcher with a banner, and works with a remote. The arrows reach every control, in dialogs too, and a ring shows which one has the focus. When a page updates, the focus and the scroll position stay where they were. Home opens on Connect, or on Reconnect while traffic is blocked, and Back goes to Home first. Pages keep clear of the screen's edges, and the app picker lists apps made only for TVs. Where a TV has no file picker, no screen for allowing VPN connections or no Always-on VPN, the app says so instead of closing or offering it.
- Only the official build updates itself. A build that isn't signed with the release certificate says it doesn't come from the official channel and never checks for updates, and a downloaded update must be signed with that same certificate.
- Installing an update while the VPN is on asks first, because Android turns the VPN off while it replaces the app. FreeTunnel then reconnects by itself after the update, unless you disconnected in the meantime. Android's Always-on start no longer restarts a connection that is already running.
- Cancelling Android's install dialog keeps the verified download, so Install update works again without downloading it again, and if the dialog got lost, Install update starts over. The downloaded file is deleted after the update.
- A download can be cancelled, and a failed update says whether the network, GitHub's rate limit, the security check or Android's installer stopped it. The update status names the version, and Settings keeps its scroll position when you come back to the app.
- In the light theme, the text in the configuration editor and the app picker was dark on their dark dialogs and hard to read, and the picker's check boxes were faint. They now use the dark theme's colours. In the dark theme, the editor's hints were faint and are now lighter.
- The configuration editor asks the keyboard not to go full screen, which some keyboards do in landscape, and not to show its own copy of the TOML if it does anyway. The keyboard draws that copy itself, without the password mask and outside the editor's screen-capture protection, where the TOML and its password could appear in plain text. It also asks the keyboard not to learn from what is typed there, such as the password.
- Import the TOML file a TrustTunnel endpoint exports. It holds the server's settings with no `[endpoint]` table, and the app refused it as missing one. It is now imported as a link with the same settings would be, by the same rules: one that lacks the server's host name, addresses, user name or password is refused, as such a link is. It is named after the server's `name`, or, when it has none, as other TOML files are: after the file, or "From clipboard" when pasted. This includes an export pasted into the editor, which keeps the name typed there.
- Ignore non-certificate PEM data in imported certificates. A certificate from a link or a TOML file, the editor's included, keeps only its certificates; anything else in it, such as a private key, is not saved, and the import says so. A certificate with no certificate in it is refused.
- Reconnect by itself after the connection is lost with the kill switch on, instead of blocking traffic until you tap Reconnect. This includes a first connection that never succeeded, such as Always-on starting at boot before there is a network. Attempts run on the blocking interface FreeTunnel already holds, after 1 s, doubling to 30 s, and again from 1 s when a network appears. They never wake a sleeping phone. An alert says that traffic is blocked, with Reconnect and Disconnect, and the home screen offers Reconnect too. If a server accepts the connection and drops it within 10 s, the delays keep growing and the alert comes back without sound.
- Import links from TrustTunnel endpoint 1.3. It marks every link as format version 2, and the app refused all of them as an unsupported version.
- Subscriptions are not supported yet. A link or TOML file with only a subscription is refused and says so. From one that also has the server's settings, those settings are imported and the confirmation says the server won't update itself. The subscription address, which holds the VPN password, is not kept; the rest of a TOML file is kept as written. This includes TOML pasted into the editor, and a subscription address saved by an earlier version is no longer passed to the TrustTunnel core.
- Refuse links and TOML files, the editor's included, for servers that need a TrustTunnel auth key, with a message saying a newer TrustTunnel core is needed, instead of failing to connect without a reason.

- Add authenticated official Android update discovery, Ed25519/version/hash verification, private downloads and PackageInstaller consent; launch checks are optional.

- Retain a blocking TUN after fatal native disconnect and post-establishment start failures when kill switch is enabled; add reconnect/disconnect notification actions.
- Isolate native validation in a disposable private process and move UI validation off the main thread.
- Fix permission-revocation cleanup, preserve state after rejected replacement, and reuse one network collector per service lifetime.
- Migrate unsupported legacy destination rules, show imported rules, explain per-app lockdown, and require unlock for secure locked tiles.
- Pin Android CI to Ubuntu 24.04, serialize superseded runs and limit push builds to main; verify release APK signer identity.
 preview

The preview includes import-only external links, private VPN consent, shared
split-routing policy, process-local connection state, default kill switch,
non-destructive encrypted storage, TOML/adapter/native validation, service
lifecycle handling, IPv6 protection and upstream network collection. The official
TrustTunnel 1.1.7 adapter and build tooling are pinned by checksums.

Review follow-up rejects `/0` destination rules and whole-family exclusions,
restores global IPv6 link-import routes, and serializes ordinary TOML tables for
adapter compatibility with escaped credentials and PEM certificates.

Release signing remains parameterized by environment variables or CI secrets.
Official release publishing, versioning and signing are managed by the original
project. No fork signing identity or stable-release claim is part of this PR.
