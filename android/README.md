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
Without signing variables, release builds produce an unsigned APK. For a signed
release, supply `ANDROID_KEYSTORE_PATH`, `ANDROID_KEYSTORE_PASSWORD`,
`ANDROID_KEY_ALIAS` and `ANDROID_KEY_PASSWORD` through the environment or CI
secrets, then run:

```sh
scripts/build-android-release.sh
```

The version is set once, as `appVersion` in `android/app/build.gradle`
(`X.Y.Z`); `versionCode` is derived from it (`X*10000 + Y*100 + Z`). Official
releases are tagged `android-X.Y.Z`, and the tag must equal `appVersion`. The
preview is `0.9.0`.

The script checks the required variables, runs release tests/lint, builds APK/AAB
and verifies their signatures. Never commit keystores or credentials. The official
project supplies its own signing identity; compatibility with fork-signed builds
is not required. Android itself enforces signing identity on installed updates.

## Releasing

Official releases are tagged `android-X.Y.Z` and published by
[android-release.yml](../.github/workflows/android-release.yml). They are never
marked GitHub's latest release, which stays the desktop app's.

### One-time setup

The release keystore is the app's identity: Android installs an update only if
it is signed with the same key. If the keystore or its password is lost,
installed apps can never be updated again.

1. Generate the keystore outside the repository. keytool asks for a password;
   a PKCS12 keystore has one password, for the store and the key alike.

   ```sh
   keytool -genkeypair -storetype PKCS12 -keystore freetunnel-release.p12 \
     -alias freetunnel -keyalg RSA -keysize 4096 -validity 10000 \
     -dname "CN=FreeTunnel"
   ```

2. Back up `freetunnel-release.p12` and its password in two separate places,
   for example a password manager and an encrypted offline drive. Never commit
   it.
3. Add the four secrets. `gh secret set` asks for each value, so none of them
   lands in the shell history:

   ```sh
   base64 -w0 freetunnel-release.p12 | gh secret set ANDROID_KEYSTORE_BASE64 --repo dimmmmmmmer/freetunnel
   gh secret set ANDROID_KEYSTORE_PASSWORD --repo dimmmmmmmer/freetunnel
   gh secret set ANDROID_KEY_ALIAS --repo dimmmmmmmer/freetunnel     # freetunnel
   gh secret set ANDROID_KEY_PASSWORD --repo dimmmmmmmer/freetunnel  # the same password
   ```

   The manifest is signed with `ED25519_SIGNING_KEY`, the desktop's, which is
   set already.
4. Pin the certificate in `android/release-certificate.sha256` and commit that
   file in a pull request. A release whose APK is signed by any other
   certificate stops before publishing.

   ```sh
   keytool -exportcert -keystore freetunnel-release.p12 -alias freetunnel -file release.cer
   sha256sum release.cer | cut -d' ' -f1 > android/release-certificate.sha256
   keytool -printcert -file release.cer | sed -n 's/^[[:space:]]*SHA256: //p' | tr -d : | tr A-F a-f
   rm release.cer
   ```

   The third command prints the same value another way; check that it equals
   the file's. It is what `apksigner verify --print-certs` reports as the APK's
   certificate SHA-256 digest. Export to a file rather than piping keytool:
   with its output piped, keytool shows the password as you type it.

### Each release

1. In a pull request, bump `appVersion` in `android/app/build.gradle` (X at
   least 1, Y and Z at most 99), and rename `## Unreleased` in
   `android/CHANGELOG.md` to `## X.Y.Z`. That section becomes the release
   notes, so write it for users.
2. Merge it once CI is green.
3. Tag the merged commit on `main` and push the tag:

   ```sh
   git tag -a android-X.Y.Z -m "FreeTunnel for Android X.Y.Z"
   git push origin android-X.Y.Z
   ```

The workflow checks the tag, the pin and the notes, runs the whole Android CI
on the tagged commit, then builds and signs the APK and checks its signer
against the pin. It writes `SHA256SUMS-android.txt`, signs it, checks that
signature against `include/core/ReleaseSigning.h`, and publishes
**FreeTunnel for Android X.Y.Z**. If any step fails or a secret is missing,
nothing is published.

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
