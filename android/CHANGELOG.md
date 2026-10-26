# Android preview changes

## Unreleased

- Only the official build updates itself. A build that isn't signed with the release certificate says it doesn't come from the official channel and never checks for updates, and a downloaded update must be signed with that same certificate.
- Installing an update while the VPN is on asks first, because Android turns the VPN off while it replaces the app. FreeTunnel then reconnects by itself after the update, unless you disconnected in the meantime. Android's Always-on start no longer restarts a connection that is already running.
- Cancelling Android's install dialog keeps the verified download, so Install update works again without downloading it again, and if the dialog got lost, Install update starts over. The downloaded file is deleted after the update.
- A download can be cancelled, and a failed update says whether the network, GitHub's rate limit, the security check or Android's installer stopped it. The update status names the version, and Settings keeps its scroll position when you come back to the app.
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
