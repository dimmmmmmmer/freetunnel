# Android changes

## 1.3.0

- **Updated to stable TrustTunnel 1.3.0.** Choose a TLS profile for each server in the configuration editor; imported authentication keys and TLS choices remain intact.
- **Bypass VPN and Through VPN keep separate saved lists** of domains, IPs and applications in every split profile.
- **Select several apps in one popup on phones and TVs.** Apply saves the selection together; Cancel leaves the rules unchanged.
- **Single-label wildcard rules such as `*.ru` and `*.su` are accepted.**
- **New installations start with Russian sites and Russian IP addresses bypassing VPN.** Custom geo lists are added in a dialog.
- **Known hostnames remain visible in geo-routing logs**, including IPv6 connections.
- **VPN recovery preserves bypassed DNS and direct connections.** Shared-address routing and malformed native configurations are handled correctly.
- **Background geo refresh preserves saved routing choices**, including whether split tunnelling is enabled.
- **Releases contain one APK**, with the version in its filename.

## 1.2.5

- Added geo lists, subscriptions and easier bulk configuration imports.
- Fixed routing when selected apps and destinations are used together, so unselected traffic stays outside the VPN.
- Improved phone and TV controls, routing settings and readable, exportable logs.
- The connection logo turns green when connected; new installations start in dark mode.
- Updated the VPN core to stable TrustTunnel 1.1.12 and refreshed dependencies.

## 1.2.4

- FreeTunnel for Android phones, tablets and TVs: import your configuration and connect.
- Send multiple links or files from your phone to the TV, with clear delivery and error messages.
- Improved remote navigation, the TV launcher icon and Russian labels.
- Fixed saving split-tunnelling settings. Updated the VPN core to stable TrustTunnel 1.1.11.

Requires Android 8.0 or newer. The same APK works on phones and Android TV.
