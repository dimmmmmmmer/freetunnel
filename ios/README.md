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
