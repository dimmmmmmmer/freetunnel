# FreeTunnel for Android (preview)

Android VPN client using the official, checksum-pinned **TrustTunnel 1.1.7**
adapter. This preview is submitted to the original
[dimmmmmmmer/freetunnel](https://github.com/dimmmmmmmer/freetunnel) project.
Official Android release publishing and signing are controlled by that project;
a fork's release name or certificate is not an official release or an update
compatibility contract.

## Features

- Confirmed import of TOML, `tt://` and TrustTunnel QR-page links. Import only
  adds a profile; it never starts VPN or replaces another profile implicitly.
  Links of format versions 0 to 2 are read; TrustTunnel 1.3 writes version 2.
  Subscriptions are not followed yet: a subscription-only link is refused, and
  from other links and files only the server's own settings are imported,
  without the subscription address. Servers that need an auth key are refused
  until the app moves to TrustTunnel core 1.3.
- TOML, Android adapter and native configuration checks before saving.
  The editor hides credentials by default and prevents screen capture.
- AES-GCM encrypted profiles with Android Keystore. Read failures preserve
  ciphertext and prevent overwriting inaccessible storage. Backup/device transfer
  are excluded; the existing storage format and alias remain compatible.
- A private VPN-consent activity and Quick Settings tile. Connection state is
  process-local, so a stopped process cannot leave a persisted Connected state.
- Kill switch enabled by default. For protection while the process or TUN is
  absent, enable Android **Always-on VPN** and **Block connections without VPN**.
  Android controls crash restart; neither START_STICKY nor Always-on is a watchdog.
- Replacement TUN established before releasing the previous one. Unsupported
  IPv6 is blocked; supported IPv6 imported from links uses global `2000::/3`.
  VPN is unmetered on Android 10+; fatal errors show a separate notification.
- Russian and English UI. TCP ping measures reachability of the first endpoint
  address, not VPN throughput or tunnel latency.

## Split tunnelling

The UI, tile and service use one routing plan.

| Mode | Rules | Behavior |
| --- | --- | --- |
| Bypass VPN | Apps and/or destinations | Selected traffic bypasses VPN |
| Through VPN | Apps only | Native general mode plus Android app allowlist |
| Through VPN | Destinations only | Native selective mode without app filtering |
| Through VPN | Empty / all selected apps removed | Full VPN with warning |
| Through VPN | Apps and destinations | Full VPN with warning |

The adapter cannot express the union of Android app and destination rules: those
filters intersect. Full VPN protects both selections explicitly; use one rule
type for selective routing. Imported modes/exclusions are preserved when split
settings are disabled, and changing modes does not reverse imported exclusions.
Destination `/0` rules and whole-family bypass exclusions are rejected; default
routes remain valid in `included_routes`. Domains support IDN and whitespace,
commas or semicolons as separators.

## Build and signing

Requires JDK 17, Android SDK 35, Build Tools 35.0.0, Bash, curl and Python 3.
From the repository root:

```sh
scripts/prepare-trusttunnel-android.sh
./android/gradlew -p android --no-daemon :app:testDebugUnitTest :app:lintDebug :app:assembleDebug :app:assembleRelease
```

The public AAR download needs no GitHub PAT. Its SHA-256 is checked during
preparation and every build:
`5398df61f74610d1d63819059bb596560feb51882e9ca78609df32fc4c7840f7`.
Gradle 8.11.1 wrapper and distribution checksums are pinned too.

Debug APK: `android/app/build/outputs/apk/debug/app-debug.apk` (debug signing).
Gradle builds release APKs unsigned, and never sees a keystore:
`scripts/sign-android-release.sh` signs them with Build Tools 35.0.0 alone
(`apksigner`, with `zipalign` and `aapt2` to check the result), in CI and
locally. For a signed release APK on your own machine, supply
`ANDROID_KEYSTORE_PATH`, `ANDROID_KEYSTORE_PASSWORD` and `ANDROID_KEY_PASSWORD`
(and `ANDROID_KEY_ALIAS` if the key is not `freetunnel`) through the
environment, then run:

```sh
scripts/build-android-release.sh
```

The version is set once, as `appVersion` in `android/app/build.gradle`
(`X.Y.Z`); `versionCode` is derived from it (`X*10000 + Y*100 + Z`). Official
releases are tagged `android-X.Y.Z`, and the tag must equal `appVersion`. The
preview is `0.9.0`.

The script checks the required variables, runs the release tests and lint with
Gradle's environment cleared of them, builds the APK unsigned, and signs it
through `sign-android-release.sh`, which checks its alignment, package, version
and signature and that it is signed with the keystore's own certificate. The
signed APK is `android/app/build/outputs/apk/release/app-release.apk`. No AAB
is built: the project publishes to no app store. Never commit keystores or
credentials. The official project supplies its own signing identity;
compatibility with fork-signed builds is not required. Android itself enforces
signing identity on installed updates.

## Releasing

Official releases are tagged `android-X.Y.Z` and published by
[android-release.yml](../.github/workflows/android-release.yml). They are never
marked GitHub's latest release, which stays the desktop app's.

### One-time setup

The release keystore is the app's identity: Android installs an update only if
it is signed with the same key. If the keystore or its password is lost,
installed apps can never be updated again. Run these from the repository root,
in one shell, so that `KS` names the keystore throughout.

1. Generate the keystore outside the repository. keytool asks for a password;
   a PKCS12 keystore has one password, for the store and the key alike. The
   file is PKCS12 whatever its extension.

   ```sh
   KS=~/freetunnel-android-signing/freetunnel-release.jks
   mkdir -p -m 700 "$(dirname "$KS")"
   keytool -genkeypair -storetype PKCS12 -keystore "$KS" -alias freetunnel \
     -keyalg RSA -keysize 4096 -validity 10000 -dname 'CN=FreeTunnel, O=FreeTunnel'
   ```

2. Back up the keystore file and its password in two separate places, for
   example a password manager and an encrypted offline drive. Never commit
   it.
3. The `android-release` environment, which only `android-*` tags may deploy
   to, has existed since 2026-10-23. These commands create it again if it is
   ever deleted. Do that before the next tag: a workflow that names an
   environment that does not exist creates it, with no protection at all.

   ```sh
   echo '{"deployment_branch_policy": {"protected_branches": false, "custom_branch_policies": true}}' |
     gh api -X PUT repos/dimmmmmmmer/freetunnel/environments/android-release --input -
   gh api -X POST repos/dimmmmmmmer/freetunnel/environments/android-release/deployment-branch-policies \
     -f name='android-*' -f type=tag
   gh api repos/dimmmmmmmer/freetunnel/environments/android-release/deployment-branch-policies \
     --jq '.branch_policies[] | .name + " " + .type'
   ```

   The last command must print `android-* tag` and nothing else.

   The policy keeps the secrets from workflows run from a branch, but not from
   a run from an `android-*` tag, which runs the workflows of the commit the
   tag names. So only someone who may release can push such a tag: the
   repository's "Release tags" ruleset lets only admins create, move or delete
   `v*` and `android-*` tags. For a second look at every release, add a
   required reviewer under Settings → Environments → android-release. Each
   release then waits for that approval twice: once for the secrets check at
   the start, and once for the signing after CI.
4. Put the keystore and its password in the environment. `gh secret set` asks
   for each password, so neither lands in the shell history; give the same
   password twice.

   ```sh
   base64 < "$KS" | tr -d '\n' | gh secret set ANDROID_KEYSTORE_BASE64 --env android-release -R dimmmmmmmer/freetunnel
   gh secret set ANDROID_KEYSTORE_PASSWORD --env android-release -R dimmmmmmmer/freetunnel
   gh secret set ANDROID_KEY_PASSWORD --env android-release -R dimmmmmmmer/freetunnel
   ```

   The key's alias, `freetunnel`, is set in the workflow, not as a secret. The
   manifest is signed with `ED25519_SIGNING_KEY`, the desktop's repository
   secret, which is set already.
5. Pin the certificate in `android/release-certificate.sha256` and commit that
   file in a pull request. A release whose APK is signed by any other
   certificate stops before publishing.

   ```sh
   cer="$(mktemp)"
   keytool -exportcert -keystore "$KS" -alias freetunnel -file "$cer"
   sha256sum "$cer" | cut -d' ' -f1 > android/release-certificate.sha256
   keytool -printcert -file "$cer" | sed -n 's/^[[:space:]]*SHA256: //p' | tr -d : | tr A-F a-f
   rm "$cer"
   ```

   On macOS without `sha256sum`, use `shasum -a 256`. The fourth command
   prints the same value another way; check that it equals the file's. It is what `apksigner verify --print-certs` reports as the APK's
   certificate SHA-256 digest. Export to a file rather than piping keytool's
   output: with its output piped, keytool shows the password as you type it.

### Each release

1. In a pull request, bump `appVersion` in `android/app/build.gradle` (X at
   least 1, Y and Z at most 99), and give `android/CHANGELOG.md` a
   `## X.Y.Z` section that says what changed, for users: it becomes the
   release notes, down to the next heading.
2. Merge it once CI is green.
3. Tag the merged commit on an up-to-date `main` and push the tag:

   ```sh
   git switch main && git pull --ff-only
   git tag -a android-X.Y.Z -m "FreeTunnel for Android X.Y.Z"
   git push origin android-X.Y.Z
   ```

If a release stops before publishing, nothing is out yet. A missing or wrong
secret: fix it and use "Re-run failed jobs". Anything that needs a code change:
merge the fix, delete the tag (only an admin can, because of the ruleset) with
`git push origin :refs/tags/android-X.Y.Z` and `git tag -d android-X.Y.Z`, and
tag `main` again. Once a version is published, its files never change: release
the next version instead.

Before `android-1.0.0`: the `## Unreleased` section of `android/CHANGELOG.md`
still holds the preview's notes, partly written for reviewers ("No fork signing
identity or stable-release claim is part of this PR"), and a stray line that
reads only "preview", left from a split heading. Everything from `## 1.0.0` down to the next
heading is published, so rewrite all of it in the 1.0.0 release pull request.

The workflow checks the tag, that its commit is on `main`, that the version is
not published yet, the pin, the notes and that the secrets are set. It runs the
whole Android CI on the tagged commit, builds the APK unsigned, then signs it
in a job that runs only Build Tools, and checks its alignment, package, version
and signer against the pin. It writes `SHA256SUMS-android.txt`, signs it,
checks that signature against `include/core/ReleaseSigning.h`, and publishes
**FreeTunnel for Android X.Y.Z**. If any step fails or a secret is missing,
nothing is published. A version that is published already is never published
again: release a new one.

## Device checks

The automated device suite is currently validated on **API 35** (arm64 locally,
x86_64 in CI); older supported Android versions have not been validated by this
suite. Run on a dedicated emulator:

```sh
./android/gradlew -p android :app:installDebug :app:installDebugAndroidTest
adb shell appops set org.freetunnel.android ACTIVATE_VPN allow
adb shell pm grant org.freetunnel.android android.permission.POST_NOTIFICATIONS
./android/gradlew -p android :app:connectedDebugAndroidTest
```

[Android CI](../.github/workflows/android.yml) runs JVM tests, lint, builds and
emulator tests without lint baselines. Device tests exercise the real JNI adapter,
TUN replacement/failure cleanup, encrypted storage and exported-entry boundaries.
Their lifecycle TCP sink is not a working tunnel endpoint. Historical manual
acceptance used an API-35 emulator, `adb reverse` to a local macOS TrustTunnel
endpoint and TCP/HTTP2 only: [QA.md](QA.md). It does not establish physical-device,
remote-server, DNS or HTTP/3 coverage.

## License

FreeTunnel and TrustTunnel are Apache-2.0. See [LICENSE](../LICENSE) and
[TrustTunnelClient v1.1.7](https://github.com/TrustTunnel/TrustTunnelClient/tree/v1.1.7).

### Failure handling and legacy rules

With kill switch enabled, a fatal native disconnect or a failed start after TUN
establishment retains the interface as a blocking guard. The notification offers
Reconnect and Disconnect; only an explicit stop, permission revocation or service
destruction releases the guard. Per-app exclusions and configured bypass routes
remain outside its scope. Use Android lockdown for protection while this app's
process is absent; lockdown also blocks apps excluded from the VPN.

Native configuration checks run on a worker through a private isolated service.
Each validation process exits after unbinding, so checks cannot change the active
VPN's process-global native options or retain the adapter's temporary native copy
of credentials. Legacy unsupported saved destination rules are removed during
preparation and a notice appears on the routing screen. New rules remain strict.

### Authenticated Android updates

Settings offers **Check for updates**. Optional launch checks are disabled by
default and can be turned off independently. The client reads the original
repository's paginated Releases API, accepts only stable `android-X.Y.Z` tags newer
than its installed version, and pins all three asset URLs to that repository and
tag. Desktop tags, drafts, prereleases, foreign URLs and incomplete assets are
ignored. The desktop updater's `/releases/latest` endpoint is never used.

An Android release must provide `freetunnel-android-X.Y.Z.apk`,
`SHA256SUMS-android.txt`, and the raw 64-byte Ed25519 signature
`SHA256SUMS-android.txt.sig`. The manifest's first line must be `#version=X.Y.Z`,
followed by an unambiguous SHA-256 entry for the APK. The public verification key
is taken at build time from `include/core/ReleaseSigning.h`; no private signing
key is shipped. Verification uses Bouncy Castle's lightweight Ed25519 implementation
on all supported Android versions, including API 26.

Downloads stay in the app's private cache. Signature, version binding, APK hash,
package name, APK version and increasing version code are checked before enabling
Install. The hash is rechecked before writing a PackageInstaller session. Android
asks for the unknown-app-source permission where needed and requires installation
confirmation, then enforces APK signer compatibility. Any failed verification
refuses installation. Network failures and cancellation can be retried in Settings.

Releases are published by the repository's release workflow, which never marks
an Android release GitHub's `latest`; see [Releasing](#releasing). This client
does not publish releases or configure secrets.
