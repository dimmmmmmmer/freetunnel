# Contributing to FreeTunnel

Thank you for helping improve FreeTunnel. This guide covers local development,
testing, translations, and CI.

## Prerequisites

- **TrustTunnelClient** upstream checkout ([TrustTunnel/TrustTunnelClient](https://github.com/TrustTunnel/TrustTunnelClient))
- CMake 3.24+ for the app (the upstream tree requires it; the unit tests alone
  configure with 3.16+), C++20 compiler (clang recommended for Linux)
- Qt 6.8+ (Gui, Qml, Quick, Network, Svg); CI and the releases use 6.8.3, and the
  translation check needs exactly that (see Translations)
- Python 3 + Conan 2.31.1 (for upstream native deps — same pin as CI)
- Ninja (recommended)

FreeTunnel is **not standalone**: it must be built as a subdirectory of the
upstream CMake tree so the `vpnlibs_trusttunnel` target exists.

## Local build

Steps 1 and 2 are already automated — `scripts/setup-upstream-tree.sh` clones
upstream at the pinned ref, copies this client in as `FreeTunnel/`, appends the
`add_subdirectory()` hook and applies the vendored patches. The release build in
`.github/workflows/build.yml` takes the same steps itself, and the Linux coverage
job runs this script (through `scripts/coverage-upstream-report.sh`), so use it
to reproduce a CI build. It **copies** the client rather than linking it, so for
day-to-day work on FreeTunnel itself follow the manual steps below and keep
editing your own checkout.

### 1. Clone upstream and inject FreeTunnel

```bash
git clone https://github.com/TrustTunnel/TrustTunnelClient.git trusttunnel
cd trusttunnel

rm -rf FreeTunnel
git clone https://github.com/dimmmmmmmer/freetunnel.git FreeTunnel   # or symlink your fork

# The pinned upstream commit lives in exactly one place. Read it, never retype it —
# CI verifies the vendored patches against this same file.
git checkout "$(tr -d '[:space:]' < FreeTunnel/scripts/upstream_ref.txt)"

# Ensure upstream CMakeLists.txt adds the subdirectory when BUILD_TRUSTTUNNEL_QT=ON
```

If `add_subdirectory(FreeTunnel)` is not present, append:

```cmake
if (BUILD_TRUSTTUNNEL_QT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/FreeTunnel/CMakeLists.txt")
    add_subdirectory(FreeTunnel)
endif ()
```

### 2. Patch upstream

From the upstream root:

```bash
for p in FreeTunnel/vendor/trusttunnel/*.patch; do patch -p1 --fuzz=0 < "$p"; done
```

The patches are numbered because they are not independent — each one's context
lines assume the previous is applied, so apply them in filename order. Today
there are three, all in the core's C++ wrapper (`trusttunnel/`):

- `01-tunnel-stats-handler.patch` — per-connection upload/download counts, for
  the live speeds in the UI.
- `02-connect-request-handler.patch` — the per-connection hook that
  per-application split tunnelling decides on.
- `03-live-exclusions-and-connect-retry.patch` — hands a running session new
  split-tunnelling rules and mode instead of rebuilding it, and with the kill
  switch on keeps a first connect that fails retrying inside its session rather
  than ending it.

`--fuzz=0` makes a hunk whose context has changed upstream fail rather than land
a few lines off. Verified in CI via `FreeTunnel/scripts/verify_upstream_patch.sh`,
which also requires the lines each hunk expects to occur exactly once in its
file, so that a hunk cannot apply cleanly in the wrong place either. Name no
patch file in a script, a workflow or this guide's commands: everything applies
the directory's `*.patch` in order, and `scripts/check-pinned-deps.sh` fails on
a path to one patch, or on a `patch -p1` without `--fuzz=0`.

### 3. Bootstrap Conan deps

```bash
pip install -r requirements.txt "conan==2.31.1"  # same pin as CI
./scripts/bootstrap_conan_deps.py
```

### 4. Configure and build

From the **upstream root** (not `FreeTunnel/`):

```bash
export QT_ROOT_DIR=/path/to/Qt/6.8.3/gcc_64   # or macOS/Windows Qt prefix

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TRUSTTUNNEL_QT=ON \
  -DDISABLE_HTTP3=ON \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"

cmake --build build --target FreeTunnel -j8
```

**HTTP/3 builds** (optional; the core's QUIC is ngtcp2/nghttp3, which conan
builds with the rest, so no Rust toolchain is needed):

```bash
cmake -S . -B build-http3 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TRUSTTUNNEL_QT=ON \
  -DDISABLE_HTTP3=OFF \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
```

CI release builds already use `DISABLE_HTTP3=OFF` (see `.github/workflows/build.yml`).

Or use the Makefile inside `FreeTunnel/` when already configured from upstream:

```bash
cd FreeTunnel
make QT_DISABLE_HTTP3=OFF CMAKE_PREFIX_PATH="$QT_ROOT_DIR" build
```

### 5. Run

| Platform | Binary |
| --- | --- |
| Linux | `build/FreeTunnel/FreeTunnel` |
| macOS | `build/FreeTunnel/FreeTunnel.app/Contents/MacOS/FreeTunnel` |
| Windows | `build\FreeTunnel\FreeTunnel.exe` |

VPN connect requires elevation (UAC on Windows, an administrator prompt on macOS,
pkexec on Linux, or sudo where pkexec cannot run).

## Unit tests (fast, no VPN core)

From `FreeTunnel/tests/`:

```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build-tests -j
QT_QPA_PLATFORM=offscreen bash ../scripts/run-ctest.sh build-tests
```

The first configure fetches QHotkey (and on Windows QWindowKit) at their pinned
commits, so it needs the network once.

CI runs exactly that wrapper, and on Linux it matters: `credentialstore` talks to
a real Secret Service, so on a desktop without an unlocked keyring it is the one
suite that fails for environmental reasons. `run-ctest.sh` unlocks
gnome-keyring on Linux CI; locally, either unlock yours or skip that suite with
`bash ../scripts/run-ctest.sh build-tests -E '^credentialstore$'` — CI still
covers it.

On macOS and Windows the configure needs OpenSSL, as the app's does: without
it the update-signature tests would skip the path every release takes. On macOS
that is `brew install openssl@3` and `-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)`.
On Windows, install OpenSSL 3 for 64-bit Windows (for example
`choco install openssl`); CMake looks for it in `C:\Program Files\OpenSSL`, the
installer's default, and anywhere else needs `-DOPENSSL_ROOT_DIR=` pointing at it.

CI runs this via `.github/workflows/tests.yml` (matrix: Linux, macOS, Windows)
on pushes to `main`, on pull requests and on `v*` tags, where the release waits
for it to pass on the tagged commit, plus a scheduled run every Monday so a
quiet `main` still gets sampled. Additional Linux-only jobs: **gcov/lcov coverage**
(`scripts/coverage-upstream-report.sh`, merges unit tests + upstream instrumented
build) and **ASan+UBSan** (`-DFT_ENABLE_SANITIZERS=ON`), where a UBSan finding
fails the test that hit it, as an ASan one does (`ubsan_canary` checks that).

Test suites — `ctest -N` lists them all, and which ones a platform builds
differs (`windows_chrome` is Windows-only, `release_ci_gate` Linux-only and
needs jq, and `ubsan_canary`, which must fail, is built only with the
sanitizers): deep links (incl. structured fuzz) and config import, config store
and paths, settings, both TOML writers, safe file reads, credentials (Keychain /
Credential Manager / libsecret, and whether Linux has a Secret Service to keep
them in), release verify (with and without OpenSSL) and version comparison,
control commands, app startup and the single-instance socket, helper IPC from
both ends (client, server, and fuzzing of both the real helper and its test
double), the real helper's lifecycle and settings, the command each system is
asked to run elevated, split-tunnel bypass rules, program rules (the
installed-apps list, shortcuts, socket-owner lookup) and interface binding, the
core wrapper against a mock core and its events, the Backend's own units (logs,
settings, config, split tunnel, updates), the window chrome on Linux and
Windows, QML UI tests and the QML/Backend property parity, the release job's CI
gate, and integration tests (config workflow, Backend + mock VPN, single
instance, helper client, UpdateChecker end-to-end against a mock HTTP server).

Security CI (`.github/workflows/security.yml`), on pushes to `main`, pull
requests and `v*` tags (the release waits for it too), and weekly:
cppcheck on `src/` and `include/`, **clang-tidy** (`scripts/run-clang-tidy.sh`),
PR **dependency review**, upstream patch verification
(`scripts/verify_upstream_patch.sh` against `scripts/upstream_ref.txt`), i18n
catalog freshness, and pinned-dependency checks
(`scripts/check-pinned-deps.sh`).

See [SECURITY.md](SECURITY.md) and [docs/security-threats.md](docs/security-threats.md) for the
threat model and known limitations.

## Codacy (code quality dashboard)

FreeTunnel uses [Codacy](https://www.codacy.com/) for static analysis and optional
coverage tracking on the repository page (badges in [README.md](README.md)).

### One-time setup

1. Sign in at [app.codacy.com](https://app.codacy.com/) with GitHub.
2. **Add project** → `dimmmmmmmer/freetunnel` (or your fork, then update badge URLs).
3. Codacy reads [`.codacy.yml`](.codacy.yml) for exclude paths and analyzes each push.

If the Codacy badge stays gray, open the project on Codacy and re-copy the badge
markdown from **Settings → General → Badges** (it embeds your project UUID). Append
`?branch=main` to both Grade and Coverage badge URLs so GitHub shows the latest
`main` analysis (without it the badge can look stale).

### Coverage badge (required for non-zero Codacy coverage)

CI generates lcov from unit tests (~79% of instrumented `src/`/`include/` today). Codacy
shows **0%** until the report is uploaded with the correct token.

1. Codacy → **freetunnel** → **Settings → Coverage** → copy the **Project API token**
   (not **Account → Access management → API tokens** — that token gives “Request URL not found”).
2. GitHub → **Settings → Secrets and variables → Actions** → set `CODACY_PROJECT_TOKEN`
   to that Coverage project token (update the secret if you previously used the wrong one).
3. Re-run **Coverage (Linux)** or push to `main`.

The upload step is in [`.github/workflows/tests.yml`](.github/workflows/tests.yml). Local report:
`bash scripts/coverage-upstream-report.sh`, which builds the unit tests with
coverage and then, where conan is installed, the whole app from an upstream tree
it sets up. For the unit tests alone, run `bash scripts/coverage-report.sh`, or
the first script with `FT_SKIP_UPSTREAM_COVERAGE=1`; without conan it stops after
the unit tests by itself.

### Branch protection and Codacy status checks

`main` requires these checks in GitHub branch protection:

```text
Unit tests (ubuntu-latest)          cppcheck                            pinned dependency refs
Unit tests (windows-latest)         clang-tidy                          i18n catalog freshness
Unit tests (macos-15)               ASan+UBSan (Linux)                  Codacy Static Code Analysis
Build (freetunnel-linux-x86_64)     Build (freetunnel-macos-universal)  Build (freetunnel-windows-x86_64)
Render bundle icons
```

All three unit-test platforms gate a merge, not just Linux. Windows is roughly
half of all downloads and macOS is most of the rest, and both are where this
project's hard bugs have actually lived — a green Linux run says very little
about either. ASan is required for the same reason: it has caught a real
use-after-free here, not a hypothetical one. And the release build itself, on
all three platforms: it is the only job that packages anything, and a pull
request used to be able to merge with it red.

Branches must also be up to date with `main` before merging (**strict**), so a
dependabot PR that has fallen behind needs `gh pr update-branch` first.

Codacy still shows **main branch isn't protected** until:

1. **Codacy → freetunnel → Settings (⚙) → Integrations** — toggle **Status checks**
   (there is no separate “GitHub → Status checks” submenu; options live on the Integrations tab).
2. At least one analysis finishes on `main` (Codacy posts the status check to GitHub).
3. Every quality-gate rule you enforce in Codacy is also a **required** check on `main`.

Repository-side hygiene for Codacy:

- [`cppcheck-suppressions.txt`](cppcheck-suppressions.txt) — the suppression list
  CI passes via `--suppressions-list`, and the one to use locally
- [`.codacy.yml`](.codacy.yml) — excludes, **cppcheck `extra_lines`**, lizard/metric excludes
- [`scripts/check-pinned-deps.sh`](scripts/check-pinned-deps.sh) — CI-enforced: every third-party
  Action in `.github/workflows` must be pinned to a full commit SHA (dependabot bumps stay
  mergeable); QHotkey and QWindowKit are fetched at full commit SHAs, QWindowKit
  declared only in `cmake/QWindowKit.cmake`, which the app and the tests both
  include; the upstream ref, the boringssl recipe, linuxdeploy and the Linux build
  container are pinned; `QT_VER` / `CONAN_VER` must agree across workflows, and no
  leg may take Homebrew's Qt instead of `QT_VER`'s; and the vendored patches are
  applied as a directory, with `--fuzz=0`

**Note:** Codacy takes cppcheck flags only from `engines.cppcheck.extra_lines` in
`.codacy.yml` — it cannot read a suppressions file. So the ids are written twice
on purpose, and the cppcheck job fails if the two copies drift apart. (A
`cppcheck.cfg` in the repo root does nothing: cppcheck has no such auto-loaded
config file. One used to sit here claiming otherwise.)

Quality gate (default): add **Codacy Static Code Analysis** — already required on `main`.

Coverage gates (optional): enable **Diff coverage is under** or **Coverage variation is under**
in Codacy **Settings → Quality settings**, set GitHub secret `CODACY_PROJECT_TOKEN`, then
also require **Codacy Diff Coverage** and/or **Codacy Coverage Variation** on `main`.

GitHub → **Settings → Branches** → edit `main` if you change which Codacy checks are enforced.

Optional: set quality gates for **new code** only so historical issues do not block merges.

## Translations (i18n)

Strings use `qsTr()` in QML and `tr()` in C++. Russian is in `i18n/freetunnel_ru.ts`.

### Update translations

```bash
./scripts/i18n-update.sh
```

This runs `lupdate` (extract new/changed strings) and `lrelease` (compile
`.qm`). It takes Qt's linguist tools from `$QT_ROOT_DIR/bin` when that is set,
and from PATH otherwise. Use Qt 6.8.3, the exact version CI pins (`QT_VER` in
the workflows): CI rebuilds the `.ts` and the `.qm` with it and compares the
bytes, and another Qt's tools, a later patch release included, may write
different ones. Both scripts print the tools' versions, and warn when they are
not that one.

Edit `i18n/freetunnel_ru.ts` in Qt Linguist or by hand, mark each translation
finished (in Linguist, or by removing `type="unfinished"`), then run the script
again to refresh `freetunnel_ru.qm`. The app embeds the committed `.qm`, not the
`.ts`.

CI runs `scripts/i18n-verify.sh` with the Security workflow (see
`.github/workflows/security.yml`, which a release also waits for) to ensure the catalog matches the current
QML/C++ sources, the committed `.qm` is the one built from the `.ts`, and no
translation is left unfinished (lrelease ships those as if they were done). The
script scans only `qml/`, `src/`, `include/`, and `main.cpp` — not test trees or
FetchContent dependencies.

Qt's own words (the file dialog Qt draws on Linux, the reason in a network
error) come from Qt's catalogues, which the release packaging ships beside the
app on all three platforms and `build.yml` checks for.

## Code signing (distribution)

Release builds from `.github/workflows/build.yml` are **unsigned** by design
(no Apple Developer ID or Authenticode certificate in CI). Users must approve
first launch manually (see README).

For production distribution:

| Platform | Recommendation |
| --- | --- |
| **macOS** | Sign with Developer ID Application + notarize with `notarytool`; staple the ticket on the `.dmg`. |
| **Windows** | Sign the installer and bundled binaries with an Authenticode cert (EV recommended for SmartScreen reputation). |
| **Linux** | `.deb` packages; release integrity via Ed25519-signed `SHA256SUMS.txt` |

Code signing is orthogonal to the in-app **update manifest** signing
(Ed25519 on `SHA256SUMS.txt`) documented below.

## Signed updates (Ed25519)

Release manifests (`SHA256SUMS.txt`) are signed in CI so the in-app updater
verifies them against the public key in `include/core/ReleaseSigning.h`.

**This repo is already configured:** the public key is in `ReleaseSigning.h`,
the private key lives in the GitHub Actions secret `ED25519_SIGNING_KEY`, and
tagged releases publish `SHA256SUMS.txt` + `SHA256SUMS.txt.sig` (see v1.0.6).
The release job opens the manifest with a `#version=<tag without v>` line, and
the updater refuses a manifest without one or naming another version, so a
manifest made by any other route must carry it too.

To rotate keys:

1. Generate a new pair:

   ```bash
   ./scripts/gen-release-signing-key.sh release-signing.pem
   ```

2. Paste the printed public PEM into `include/core/ReleaseSigning.h`
   (`kReleaseSigningPublicKeyPem`).

3. Update the `ED25519_SIGNING_KEY` repository secret to the matching private PEM.

Never commit the private key.

### Binary code signing (paid — optional)

Apple Developer ID and Authenticode certificates cost money. CI uses **ad-hoc**
`codesign` on macOS (free, not notarized) so the bundle launches after the user
approves Gatekeeper manually. Windows/Linux installer binaries remain unsigned;
integrity is covered by SHA256 + Ed25519 on the release manifest instead.

## Deep links

See [DEEP_LINK.md](DEEP_LINK.md) for the `tt://` TLV specification, and for the
`freetunnel://` control links: how a link opened by the system is told from the
same URL run as a command, and which links ask first.

## CI workflows

| Workflow | Purpose |
| --- | --- |
| `.github/workflows/build.yml` | Release builds (HTTP/3 enabled), Linux/macOS/Windows. A `v*` tag publishes only once Tests and Security have passed on the same commit |
| `.github/workflows/tests.yml` | Fast unit tests (Linux + macOS + Windows); Linux coverage + ASan. Also on `v*` tags and weekly (Mon) |
| `.github/workflows/security.yml` | cppcheck, clang-tidy, dependency review (PRs), upstream patch verify, i18n freshness, pinned deps. Also on `v*` tags and weekly (Mon) |

Upstream ref is pinned in [`scripts/upstream_ref.txt`](scripts/upstream_ref.txt) —
the workflows read that file rather than carrying a SHA of their own. Bump it
with the patch script re-verified. That script also fails when the new core reads
a config key the old one did not: the elevated helper passes the core every key
it is sent, except those `clearKeysRootMustNotTakeFromAConfig()` in
`src/vpn/qt_trusttunnel_client.cpp` clears. Read what the core does with the new
key; if it points the core, running as root, at a file, an interface or a port,
clear it there as well. Then add it to the list in the script, by its full TOML
path (`listener.tun.netns`, not `netns`). The script fails, too, on a read it
cannot place in a table and on a listed key it no longer finds; its comments say
why, and what to do about each.

## Reporting bugs

Open an issue with the [bug report form](https://github.com/dimmmmmmmer/freetunnel/issues/new/choose);
it asks for the OS, the build, and what the log says, which is most of what any
answer depends on. Russian is fine — the forms say so. Security bugs go through
[Security Advisories](https://github.com/dimmmmmmmer/freetunnel/security/advisories/new)
instead, never a public issue.

## Pull requests

- Keep changes focused; match existing code style.
- Run unit tests locally before pushing.
- Unsigned release binaries are expected; do not commit secrets or signing keys.

## License

By contributing, you agree that your contributions will be licensed under the
Apache License 2.0 (see [LICENSE](LICENSE)).
