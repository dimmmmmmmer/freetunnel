# FreeTunnel — Project Audit

Date: 2026-06-13. Scope: the Qt client in this repository (the VPN core lives in
`TrustTunnel/TrustTunnelClient` and is out of scope except where integration
matters).

## Summary

The project is in good shape: it builds and ships unsigned binaries for Linux,
macOS (arm64 + Intel) and Windows via CI, publishes releases on tags, has live
traffic telemetry restored, official `tt://` deep-link import, and a fast unit
test job. The main risks are **maintainability** (one 2200-line file), a few
**dead/stale artifacts**, **unsigned distribution**, and **fragility of the
upstream integration** (a source patch + pinned SHA that must be re-verified on
every bump).

## Findings

| # | Area | Severity | Finding | Recommendation |
| --- | --- | --- | --- | --- |
| 1 | Maintainability | **High** | `src/ui/MainWindow.cpp` is ~2226 lines and owns UI, tray, elevation, deep-link import, updater wiring, relaunch, etc. | Split into focused units (tray controller, import service, updater controller). |
| 2 | Dead code | Medium | `src/vpn/helper_main.cpp` (114 lines) is not in the CMake target — a leftover separate-helper design. | Remove, or wire it up if a privileged helper is intended. |
| 3 | Build hygiene | Medium | `CMakeLists.txt` `else()` branch references a non-existent `trusttunnel-core/` layout (never works). | Remove the dead branch; keep only the `vpnlibs_trusttunnel` path + a clear error. |
| 4 | Versioning | Medium | Three sources of truth: CMake `project(VERSION 0.3.0)`, `FREETUNNEL_VERSION "0.12b"`, NSIS default `0.7`. | Single source (e.g. derive all from the git tag / one CMake var). |
| 5 | Integration fragility | Medium | Telemetry relies on patching upstream `client.{h,cpp}` in CI (`scripts/patch_core_wrapper.py`); deep-link/`tunnel_stats` assume a specific core API. | Pinned `UPSTREAM_REF` mitigates. On bump: re-run the patch, rebuild, re-check `VpnCallbacks`. Better: upstream a PR exposing the handler. |
| 6 | Dependency drift | Medium | `dns-libs` is re-pinned in CI because the upstream bootstrap exports the *latest* tag; `native_libs_common` could drift the same way later. | Current pin handles dns-libs; add the same pin for NLC if it ever drifts. |
| 7 | Distribution | Medium | Binaries are unsigned → Gatekeeper/SmartScreen friction. | Documented for users. Add Developer ID / Windows signing when certs are available (CI hooks ready). |
| 8 | Universality (Linux) | Medium | The Linux binary/AppImage inherits the build host glibc (ubuntu-latest = 2.39), so it won't run on Ubuntu 22.04 / Debian 12. | Build on `ubuntu-22.04` (glibc 2.35) with a modern clang; bundle Qt in the AppImage. |
| 9 | Credentials | Low | Deep links and TOML configs carry cleartext username/password (by design). | Don't log deep-link URIs; configs live in the per-user config dir. Consider a "credentials in this link" warning on import. |
| 10 | Testing | Low | Only the deep-link codec is unit-tested. Config parsing/inspection and the VPN wrapper are untested; no runtime/E2E (needs TUN). | Add unit tests for `ConfigInspector`/`AppSettings`; keep manual runtime checklist for releases. |
| 11 | Stale files | Low | `.gitignore` mentions `trusttunnel-qt-helper`; `conan_host_profile` hardcodes a personal path (`/Users/dontfffafk/...`) and is unused by CI. | Tidy both. |
| 12 | Security (elevation) | Low (OK) | Privilege escalation uses `osascript`/`pkexec` with escaped args on the app's own path (not user input). | No action; keep inputs non-user-controlled. |

## What is solid

- Reproducible core via pinned `UPSTREAM_REF`; conan/ccache caching.
- Multi-OS matrix, release-on-tag, auto-updater pointed at this repo.
- Telemetry (TrafficGraph) restored through a verified, idempotent wrapper patch.
- Self-contained, unit-tested deep-link codec (no core dependency).

## Suggested next steps (priority order)

1. Linux portability: build on `ubuntu-22.04` + bundle Qt in AppImage (finding 8).
2. Remove dead code / unify versioning (findings 2, 3, 4).
3. Split `MainWindow.cpp` (finding 1).
4. Code signing once certificates exist (finding 7).
5. Broaden unit tests (finding 10).
