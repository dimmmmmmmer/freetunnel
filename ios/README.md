# FreeTunnel for iOS

Native SwiftUI app and a separate Network Extension, targeting iOS 17+.
Development branch: not released and not yet validated on a physical iPhone.

The scope includes server management, batch link/file import and export,
subscription refresh, manual domain/IP routing, preset/custom geo lists,
automatic list refresh, connection controls, kill switch, logs, and Russian/English UI.
Desktop tray/hotkeys and consumer per-app split tunnelling are not iOS features.
Apple only supports per-app VPN for managed apps on MDM-enrolled devices.

Configuration secrets belong in a shared Keychain access group; the system VPN
profile stores only a Keychain item identifier. Non-secret preferences and list
caches belong in the App Group. Both targets must use the same identifiers and
signing team. Never put credentials in UserDefaults, logs, or build artifacts.

The core must be built from the pinned stable source with the existing geo-priority
patch. An unpatched official binary is not an interchangeable replacement: it
would silently lose the promised precedence of manual routing rules.

Before distribution: finish unsigned simulator/device builds and tests, acquire
an Apple Developer organization membership, configure bundle/App Group/Keychain
identifiers and profiles, validate real-device networking and reconnect/kill-switch
behaviour, then submit an external TestFlight build for Apple's review.

## Build without distribution credentials

Use a Mac with Xcode 26.3, CMake, Ninja, Rustup, Python, Conan 2.31.1 and XcodeGen.
The source pin and geo-priority patch are shared with `android/core-source.json`.

```sh
bash ios/scripts/build-core.sh
xcodegen generate --spec ios/project.yml
xcodebuild -project ios/FreeTunnel.xcodeproj -scheme FreeTunnel \
  -sdk iphonesimulator -configuration Debug CODE_SIGNING_ALLOWED=NO ARCHS=arm64 build
swift test --package-path ios/FreeTunnelCore
```

The generated project is disposable; `project.yml` is its source. The iOS workflow
builds both device and simulator targets without signing and renders the real UI
in a simulator. Debug previews use sample data and disable connection; release
builds do not accept the preview flag.

For a signed build, override `FT_APP_ID`, `FT_APP_GROUP`, `FT_KEYCHAIN_GROUP`, and
`DEVELOPMENT_TEAM` with identifiers provisioned in your Apple account. The app and
extension need Network Extension, App Groups, and shared Keychain capabilities.
Also update the background-refresh identifier in the app and permitted identifiers
if changing the bundle prefix. Archive the FreeTunnel scheme and distribute via
Xcode Organizer/App Store Connect. Review export-compliance answers for the actual
VPN cryptography; the project does not falsely declare that encryption is absent.

TestFlight signing, upload and invitations are deliberately not automated before
an Apple team and provisioning profiles are available. No Apple credentials are
stored in this repository.

## Before calling the port ready

- Both unsigned platform builds and the core tests must pass.
- Verify real-device connect/disconnect, locked-screen operation, Wi-Fi/cellular
  handover, DNS, IPv6, reconnect, on-demand and kill-switch behaviour.
- Verify manual-rule precedence over geo domains/IPs against a real endpoint.
- Check Keychain sharing and accessibility after first unlock, and fail closed
  when access is denied. Simulator preview mode does not validate these features.
- Review imported configurations with custom certificates and TLS verification
  disabled; test subscription authentication, redirects and credential rotation.
- Check background refresh on device; iOS does not promise exact hourly execution.
- Review accessibility, Dynamic Type, VoiceOver, iPhone/iPad layouts and translations.
- Include core dependencies' notices in the distribution, review the privacy manifest,
  and obtain Apple's beta approval before inviting external TestFlight users.
