# FreeTunnel — Project Audit

Date: 2026-06-16 · Version: 1.0.0 · Scope: the Qt/QML client in this repository.
The VPN core lives in `TrustTunnel/TrustTunnelClient` and is out of scope except
where the integration matters.

## Summary

The client is in good shape for a 1.0.0. It is a Qt 6 **Quick/QML** application
on the C++ TrustTunnel core, builds and ships **unsigned** binaries for Linux,
macOS (universal arm64 + x86_64) and Windows via CI, publishes releases on tags,
and has a fast unit-test job. The Widgets UI and assorted dead code from the
original fork have been removed; versioning is a single source of truth (1.0.0).

The remaining risks are inherent rather than accidental: **unsigned
distribution**, **fragility of the upstream integration** (a source patch + a
pinned core SHA that must be re-verified on every bump), and a few **platform
limitations** of optional features (global hotkeys, tray, per-app routing).

## Architecture

- `main.cpp` — `QApplication` + `QQmlApplicationEngine`; single-instance via
  `QLocalServer`, deep-link/`QFileOpenEvent` routing, tray-friendly lifecycle.
  With `--helper` it runs the headless **privileged helper** instead.
- **Privilege model**: the GUI stays user-level; the VPN core runs in a
  separate elevated helper process (`VpnHelperClient` ↔ `runVpnHelper`, token-
  authed localhost JSON). Elevated once per session (osascript/pkexec/runas).
- `qml/Main.qml` — the entire UI (home, configs, split-tunnel, settings, logs,
  create-config sub-screen) bound to a single `backend` object.
- `include|src/app/Backend.*` — the QML bridge; wraps `VpnHelperClient`
  (VPN, via the elevated helper), `AppSettings`/`ConfigStore` (persistence),
  `UpdateChecker`, `QHotkey`, and the `ControlCommand` parser.
- `include|src/app/MacWindow.mm` — tiny AppKit shim for the unified
  (transparent) macOS title bar.
- `include|src/core/*` — small, mostly core-independent units (deep-link codec,
  config import, settings, control-command parsing) — the unit-tested surface.
- `include|src/vpn/qt_trusttunnel_client.*` — the Qt wrapper over the C++ core.

## Findings

| # | Area | Severity | Finding | Recommendation |
| --- | --- | --- | --- | --- |
| 1 | Distribution | Medium | Binaries are unsigned → Gatekeeper/SmartScreen friction; macOS is ad-hoc signed only. | Documented for users; add Developer ID / Windows signing when certs exist (CI hooks ready). |
| 2 | Integration fragility | Medium | Live telemetry relies on patching upstream `client.{h,cpp}` in CI (`scripts/patch_core_wrapper.py`); deep-link/`tunnel_stats` assume a specific core API. | Pinned `UPSTREAM_REF` mitigates. On bump: re-run the patch, rebuild, re-check `VpnCallbacks`. Better: upstream a PR exposing the handler. |
| 3 | Dependency drift | Medium | `dns-libs` is re-pinned in CI because the upstream bootstrap exports the *latest* tag. | Current pin handles it; pin `native_libs_common` too if it ever drifts. |
| 4 | Feature scope | Low (by design) | Split tunneling is **domain/IP/subnet-based**, with both **bypass** (general) and **only-these-via-VPN** (selective) modes and named **profiles** (functional). **Per-app** routing is not offered — the core exposes domain `exclusions` + routes + `VpnMode`, not an app-level API. | Revisit only if/when the core gains app-level routing. |
| 12 | Autostart | Low | "Launch at startup" writes a LaunchAgent/registry/.desktop entry pointing at the running binary — so it only works once the app is installed to a stable location (not run from a mounted .dmg). | Documented; consider a check that warns when launched from a volume. |
| 5 | Hotkeys (Linux) | Low | Global hotkeys use QHotkey, which is **X11-only** on Linux — no Wayland support. | Acceptable; document. Hotkeys are optional and empty by default. |
| 6 | Tray (Linux) | Low | The Qt.labs.platform tray needs a StatusNotifier/AppIndicator host; absent on some minimal desktops. | App still works without a tray; window remains usable. |
| 7 | Updater | Low | The in-app updater works because the repo is **public** (GitHub Releases API, unauthenticated). | Keep the repo public, or ship a token-less alternative if it ever goes private. |
| 8 | Credentials | Low (by design) | Deep links and TOML configs carry cleartext username/password. | Deep-link URIs are not logged; configs live in the per-user config dir. |
| 9 | Security (elevation) | Low (OK) | Elevation uses `osascript`/`pkexec` with escaped args on the app's own path (not user input). | No action; keep inputs non-user-controlled. |
| 10 | Housekeeping | Low | `removeConfig` drops a config from the list but leaves a self-created `.toml` on disk; a few QML `MouseArea`s anchor inside a layout (benign UB warning). | Cosmetic; tidy opportunistically. |
| 11 | i18n | Low (OK) | Full EN/RU: English source strings, bundled Russian `.qm`, live language switch. New strings must be wrapped in `qsTr`/`tr` and `freetunnel_ru.ts` re-released. | Run `lupdate`/`lrelease` when adding strings. |

## What is solid

- **Single-source versioning** (1.0.0) across CMake, the app, and the installer.
- **Universal macOS** `.dmg` (arm64 + x86_64) and a **portable Linux AppImage**
  built on glibc 2.35 (ubuntu-22.04) with bundled libstdc++.
- **Public repo ⇒ free CI**: full Mac/Win/Linux matrix on PRs and tags;
  release-on-tag publishes artifacts; cheap Linux unit-test job on every push.
- **Reproducible core** via pinned `UPSTREAM_REF`; conan + ccache caching.
- **Clean codebase**: the Qt Widgets layer and dead helpers/assets from the
  fork were removed; the QML UI binds to one well-scoped `Backend`. Every UI
  control is wired to real behaviour (no decorative toggles/buttons).
- **Functional depth**: deep-link control, global hotkeys, tray (with an
  inline config switcher), in-app updater, split-tunnel mode + profiles,
  kill switch, per-config ping, autostart, clipboard/file import,
  host:port config validation — all backed.
- **Localised**: full EN/RU with a live language switch.
- **Tested surface**: 6 Qt-only unit suites — deep-link codec, config import,
  app settings round-trip, config store, control-command parsing, and
  config-TOML round-trip.

## Not covered by automated tests

Runtime/E2E behaviour (actual tunnel establishment, elevation, the native
macOS title bar, OS URL-scheme dispatch) requires a TUN device + the core and is
verified manually per release. The core itself is upstream's responsibility.

## Suggested next steps (priority order)

1. Code signing once certificates exist (finding 1).
2. Upstream the telemetry handler to drop the source patch (finding 2).
3. Wire `UpdateChecker` results into a richer "what's new" view (optional).
4. Opportunistic cleanup of the layout `MouseArea` warnings (finding 10).
