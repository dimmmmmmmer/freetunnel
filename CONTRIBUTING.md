# Contributing to FreeTunnel

Thank you for helping improve FreeTunnel. This guide is for developers: how to
build and test the client, what CI checks before a merge, the conventions the
repository follows, and how a release is cut. To install or use the app, see
the [README](README.md).

## Contents

- [Quick start for contributors](#quick-start-for-contributors)
- [Full build with the upstream core](#full-build-with-the-upstream-core)
- [Unit tests (fast, no VPN core)](#unit-tests-fast-no-vpn-core)
- [CI workflows](#ci-workflows)
- [Project layout](#project-layout)
- [Conventions](#conventions)
- [Releasing](#releasing)
- [Codacy (code quality dashboard)](#codacy-code-quality-dashboard)
- [Deep links](#deep-links)
- [Reporting bugs](#reporting-bugs)
- [Why it works this way](#why-it-works-this-way)
- [License](#license)

## Quick start for contributors

Most changes can be built and tested without the VPN core. The unit tests are a
standalone CMake tree in `tests/` that needs Qt, CMake 3.16+ and a C++20
compiler, not the upstream tree.

### Build and run the unit tests

```bash
export QT_ROOT_DIR=/path/to/Qt/6.8.3/gcc_64   # or macOS/Windows Qt prefix
cd tests   # FreeTunnel/tests/ inside an upstream tree
cmake -S . -B build-tests -G Ninja -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build-tests -j
QT_QPA_PLATFORM=offscreen bash ../scripts/run-ctest.sh build-tests
```

- **Qt:** use 6.8.3, the version CI and the releases use.
- **Network:** the first configure fetches QHotkey (and on Windows QWindowKit)
  at their pinned commits, so it needs the network once.
- **Warnings:** GCC and Clang build the tests with `-Wall -Wextra -Werror`, so a
  warning fails the build.
- **Linux:** `credentialstore` fails without an unlocked keyring. Unlock yours,
  or skip that suite (CI still covers it):

  ```bash
  bash ../scripts/run-ctest.sh build-tests -E '^credentialstore$'
  ```

- **macOS and Windows:** install OpenSSL first; see
  [OpenSSL on macOS and Windows](#openssl-on-macos-and-windows).

### What CI checks

Pull requests run three workflows: Build, Tests and Security (see
[CI workflows](#ci-workflows)). `main` requires these checks in GitHub branch
protection:

| Workflow | Required checks |
| --- | --- |
| Tests | `Unit tests (ubuntu-latest)`, `Unit tests (windows-latest)`, `Unit tests (macos-15)`, `ASan+UBSan (Linux)` |
| Security | `cppcheck`, `clang-tidy`, `pinned dependency refs`, `i18n catalog freshness` |
| Build | `Build (freetunnel-linux-x86_64)`, `Build (freetunnel-macos-universal)`, `Build (freetunnel-windows-x86_64)`, `Render bundle icons` |
| Codacy | `Codacy Static Code Analysis` |

A branch must also be up to date with `main` before it merges (**strict**), so
a dependabot PR that has fallen behind needs `gh pr update-branch` first. Why
every platform is required: [Why it works this way](#why-it-works-this-way).

### Open a pull request

1. Branch from `main`. Keep the change focused and match the existing code
   style.
2. Run the unit tests locally before pushing.
3. Changed a user-visible string? Update the catalogue as described in
   [Translations (i18n)](#translations-i18n).
4. Will users notice the change? Add an entry to `CHANGELOG.md` under
   `## Unreleased` (see [Writing release notes](#writing-release-notes)).
5. Write the commit message as described in [Commit messages](#commit-messages).
6. Do not commit secrets or signing keys. Unsigned release binaries are
   expected.

## Full build with the upstream core

FreeTunnel is **not standalone**: the app must be built as a subdirectory of the
upstream CMake tree, so that the `vpnlibs_trusttunnel` target exists. You need
this to run the app; the unit tests do not.

### Prerequisites

| Tool | Version | Notes |
| --- | --- | --- |
| **TrustTunnelClient** upstream checkout | The commit in `scripts/upstream_ref.txt` | [TrustTunnel/TrustTunnelClient](https://github.com/TrustTunnel/TrustTunnelClient) |
| CMake | 3.24+ | The upstream tree requires it; the unit tests alone configure with 3.16+ |
| C++ compiler | C++20 | clang recommended for Linux |
| Qt | 6.8+ (Gui, Qml, Quick, Network, Svg) | CI and the releases use 6.8.3, and the translation check needs exactly that (see [Translations (i18n)](#translations-i18n)) |
| Python 3 + Conan | Conan 2.31.1 | For upstream native deps; same pin as CI |
| Ninja | Any | Recommended |

### Set up the tree with the script (as CI does)

`scripts/setup-upstream-tree.sh` does steps 1 and 2 below: it clones upstream
at the pinned ref, copies this client in as `FreeTunnel/`, appends the
`add_subdirectory()` hook and applies the vendored patches.

```bash
bash scripts/setup-upstream-tree.sh   # deletes and re-creates upstream-tree/
```

- **Use it to reproduce a CI build.** The release build in
  `.github/workflows/build.yml` takes the same steps itself, and the Linux
  coverage job runs this script (through `scripts/coverage-upstream-report.sh`).
- **Not for day-to-day work.** It **copies** the client rather than linking it,
  so for work on FreeTunnel itself follow the manual steps below and keep
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

- The loop applies them in filename order, which they need: each one's context
  lines assume the previous one is applied.
- `--fuzz=0` makes a hunk whose context has changed upstream fail rather than
  land a few lines off.
- What each patch does, and the rules for changing them:
  [Vendored core patches](#vendored-core-patches).

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

**HTTP/3 builds** are optional locally. CI release builds already use
`DISABLE_HTTP3=OFF` (see `.github/workflows/build.yml`). The core's QUIC is
ngtcp2/nghttp3, which conan builds with the rest, so no Rust toolchain is
needed:

```bash
cmake -S . -B build-http3 -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TRUSTTUNNEL_QT=ON \
  -DDISABLE_HTTP3=OFF \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
```

Or, once configured from upstream, use the Makefile inside `FreeTunnel/`:

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

VPN connect requires elevation: UAC on Windows, an administrator prompt on
macOS, pkexec on Linux, or sudo where pkexec cannot run.

## Unit tests (fast, no VPN core)

The commands are in the quick start:
[Build and run the unit tests](#build-and-run-the-unit-tests). Run them from
`FreeTunnel/tests/` in an upstream tree, or `tests/` in a plain checkout. This
section covers what differs per platform and what the suites test.

### Keyring on Linux

- CI runs the tests through the same wrapper, `scripts/run-ctest.sh`, and on
  Linux that matters.
- `credentialstore` talks to a real Secret Service, so on a desktop without an
  unlocked keyring it is the one suite that fails for environmental reasons.
- `run-ctest.sh` unlocks gnome-keyring on Linux CI. Locally, either unlock
  yours or skip that suite with the `-E '^credentialstore$'` command from the
  quick start; CI still covers it.

### OpenSSL on macOS and Windows

On macOS and Windows the configure needs OpenSSL, as the app's does. Without it
the update-signature tests would skip the path every release takes.

| Platform | Install | Point CMake at it |
| --- | --- | --- |
| macOS | `brew install openssl@3` | `-DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)` |
| Windows | OpenSSL 3 for 64-bit Windows, for example `choco install openssl` | CMake looks in `C:\Program Files\OpenSSL`, the installer's default; anywhere else needs `-DOPENSSL_ROOT_DIR=` pointing at it |

### Test suites

`ctest -N` lists them all. Which ones a platform builds differs:

| Suite | Built |
| --- | --- |
| `windows_chrome` | On Windows only |
| `release_ci_gate` | On Linux only; needs jq |
| `release_signature` | On Linux only; needs openssl |
| `ubsan_canary` | Only with the sanitizers; it must fail |

What the suites cover:

- Deep links (incl. structured fuzz) and config import
- Config store and paths; settings
- Both TOML writers; safe file reads
- Credentials (Keychain / Credential Manager / libsecret), and whether Linux
  has a Secret Service to keep them in
- Release verify (with and without OpenSSL) and version comparison
- Control commands; app startup and the single-instance socket
- Helper IPC from both ends (client, server, and fuzzing of both the real
  helper and its test double)
- The real helper's lifecycle and settings
- The command each system is asked to run elevated
- Split-tunnel bypass rules; program rules (the installed-apps list,
  shortcuts, socket-owner lookup) and interface binding
- The core wrapper against a mock core, and its events
- The Backend's own units (logs, settings, config, split tunnel, updates)
- The window chrome on Linux and Windows
- QML UI tests and the QML/Backend property parity
- The release job's CI gate and signature check
- Integration tests: config workflow, Backend + mock VPN, single instance,
  helper client, UpdateChecker end-to-end against a mock HTTP server

### Coverage and sanitizers

CI runs two extra Linux-only jobs:

| Job | How | Notes |
| --- | --- | --- |
| **gcov/lcov coverage** | `scripts/coverage-upstream-report.sh` | Merges unit tests + upstream instrumented build |
| **ASan+UBSan** | `-DFT_ENABLE_SANITIZERS=ON` | A UBSan finding fails the test that hit it, as an ASan one does (`ubsan_canary` checks that) |

For a local coverage report:

```bash
bash scripts/coverage-upstream-report.sh
```

- It builds the unit tests with coverage and then, where conan is installed,
  the whole app from an upstream tree it sets up.
- Without conan it stops after the unit tests by itself.
- For the unit tests alone, run `bash scripts/coverage-report.sh`, or the first
  script with `FT_SKIP_UPSTREAM_COVERAGE=1`.

## CI workflows

| Workflow | What it runs | When |
| --- | --- | --- |
| `.github/workflows/build.yml` | Release builds (HTTP/3 enabled), Linux/macOS/Windows. A `v*` tag publishes only once Tests and Security have passed on the same commit | Pushes to `main`, pull requests, `v*` tags |
| `.github/workflows/tests.yml` | Fast unit tests (Linux + macOS + Windows) through `scripts/run-ctest.sh`; Linux coverage + ASan+UBSan ([details](#coverage-and-sanitizers)) | Pushes to `main`, pull requests, `v*` tags, and weekly (Mon) |
| `.github/workflows/security.yml` | The checks below | Pushes to `main`, pull requests, `v*` tags, and weekly (Mon) |

- **On a `v*` tag** the release waits for Tests and Security to pass on the
  tagged commit.
- **The weekly run** (Mondays) means a quiet `main` still gets sampled.

Security's checks:

| Check | What it runs |
| --- | --- |
| `cppcheck` | cppcheck on `src/` and `include/` |
| `clang-tidy` | `scripts/run-clang-tidy.sh` |
| `dependency review` | Dependency review, on pull requests |
| `upstream patch verify` | `scripts/verify_upstream_patch.sh` against `scripts/upstream_ref.txt` |
| `i18n catalog freshness` | `scripts/i18n-verify.sh` |
| `pinned dependency refs` | `scripts/check-pinned-deps.sh` |

See [SECURITY.md](SECURITY.md) and
[docs/security-threats.md](docs/security-threats.md) for the threat model and
known limitations.

## Project layout

| Path | What is there |
| --- | --- |
| `main.cpp` | Entry point: starts the GUI, or the elevated helper when run with `--helper` |
| `src/app/`, `include/app/` | App startup, the `Backend` the QML talks to (split into `Backend*.cpp` by job), window chrome, the log model |
| `src/core/`, `include/core/` | Configs and their TOML, settings, credentials, deep links and control commands, split-tunnel and program rules, update checking and verification |
| `src/vpn/`, `include/vpn/` | The wrapper around the VPN core (`qt_trusttunnel_*`) and the elevated helper (client, server, launch, elevation) |
| `qml/` | The UI: `Main.qml`, `pages/`, `components/` |
| `i18n/` | The Russian catalogue, `freetunnel_ru.ts`, and its compiled `freetunnel_ru.qm` |
| `vendor/trusttunnel/` | Patches applied to the upstream core |
| `scripts/` | Build, test, CI and release scripts; `upstream_ref.txt` pins the core |
| `cmake/` | `QWindowKit.cmake` (included by the app and the tests) and `FreetunnelDeps.cmake` |
| `tests/` | The standalone unit-test tree; `mock_core/` stands in for the core |
| `assets/`, `macos/`, `win/` | Icons and logos; the macOS `Info.plist` template; the Windows resource template and NSIS installer script |
| `docs/` | `security-threats.md` |
| `.github/workflows/` | Build, Tests and Security |

## Conventions

### Commit messages

Follow what `git log` shows:

- **Subject:** what the commit does, in plain words and the imperative, with no
  type prefix, e.g. `Split the downloadReady handler out of wireUpdaterSignals`.
  A change confined to one platform or to CI says so first: `Windows: ...`,
  `CI: ...`.
- **Body:** what changed and why, as prose wrapped by hand; a bullet list where
  there are several separate points.
- **Verification:** end with what you checked (the unit tests,
  `i18n-verify.sh`, cppcheck, clang-tidy) and what only CI can check, such as
  the Windows and macOS builds.
- **Release commits** are titled with the bare version, e.g. `1.2.3`.

### CHANGELOG.md

- A change users will notice adds its entry to `CHANGELOG.md` under
  `## Unreleased`, in the same PR.
- The release commit renames that section to the version. The release notes on
  GitHub are built from it, so it is written before tagging.
- How to write an entry: [Writing release notes](#writing-release-notes).

### Translations (i18n)

Strings use `qsTr()` in QML and `tr()` in C++. Russian is in
`i18n/freetunnel_ru.ts`.

1. Extract new and changed strings:

   ```bash
   ./scripts/i18n-update.sh
   ```

   This runs `lupdate` (extract new/changed strings) and `lrelease` (compile
   `.qm`).
2. Edit `i18n/freetunnel_ru.ts` in Qt Linguist or by hand, and mark each
   translation finished (in Linguist, or by removing `type="unfinished"`).
3. Run the script again to refresh `freetunnel_ru.qm`. The app embeds the
   committed `.qm`, not the `.ts`.

**Use Qt 6.8.3**, the exact version CI pins (`QT_VER` in the workflows):

- The script takes Qt's linguist tools from `$QT_ROOT_DIR/bin` when that is
  set, and from PATH otherwise.
- CI rebuilds the `.ts` and the `.qm` with Qt 6.8.3 and compares the bytes.
  Another Qt's tools, a later patch release included, may write different ones.
- Both scripts print the tools' versions, and warn when they are not that one.

**What CI checks.** `scripts/i18n-verify.sh` runs with the Security workflow
(see `.github/workflows/security.yml`, which a release also waits for). It
ensures that:

- the catalog matches the current QML/C++ sources;
- the committed `.qm` is the one built from the `.ts`;
- no translation is left unfinished (lrelease ships those as if they were done).

The script scans only `qml/`, `src/`, `include/`, and `main.cpp`, not test
trees or FetchContent dependencies.

**Qt's own words** (the file dialog Qt draws on Linux, the reason in a network
error) come from Qt's catalogues. The release packaging ships those beside the
app on all three platforms, and `build.yml` checks for them.

### Vendored core patches

The patches in `vendor/trusttunnel/` change the upstream core. Today there are
three, all in the core's C++ wrapper (`trusttunnel/`):

| Patch | What it adds |
| --- | --- |
| `01-tunnel-stats-handler.patch` | Per-connection upload/download counts, for the live speeds in the UI |
| `02-connect-request-handler.patch` | The per-connection hook that per-application split tunnelling decides on |
| `03-live-exclusions-and-connect-retry.patch` | Hands a running session new split-tunnelling rules and mode instead of rebuilding it; with the kill switch on, keeps a first connect that fails retrying inside its session rather than ending it |

Rules:

- **Apply them in filename order.** They are numbered because they are not
  independent: each one's context lines assume the previous is applied.
- **Apply them with `--fuzz=0`.** A hunk whose context has changed upstream
  then fails rather than land a few lines off.
- **Name no patch file** in a script, a workflow or this guide's commands.
  Everything applies the directory's `*.patch` in order, and
  `scripts/check-pinned-deps.sh` fails on a path to one patch, or on a
  `patch -p1` without `--fuzz=0`.

CI verifies them via `FreeTunnel/scripts/verify_upstream_patch.sh`, which also
requires the lines each hunk expects to occur exactly once in its file, so that
a hunk cannot apply cleanly in the wrong place either.

### Bumping the upstream core

The upstream ref is pinned in
[`scripts/upstream_ref.txt`](scripts/upstream_ref.txt). The workflows read that
file rather than carrying a SHA of their own.

1. Change the commit in `scripts/upstream_ref.txt`.
2. Re-verify the patches with the patch script:

   ```bash
   bash scripts/verify_upstream_patch.sh
   ```

3. If the script fails because the new core reads a config key the old one did
   not, read what the core does with the new key. The elevated helper passes
   the core every key it is sent, except those
   `clearKeysRootMustNotTakeFromAConfig()` in
   `src/vpn/qt_trusttunnel_client.cpp` clears.
4. If the key points the core, running as root, at a file, an interface or a
   port, clear it there as well.
5. Then add it to the list in the script, by its full TOML path
   (`listener.tun.netns`, not `netns`).

The script fails, too, on a read it cannot place in a table and on a listed key
it no longer finds. Its comments say why, and what to do about each.

### Pinned dependencies

[`scripts/check-pinned-deps.sh`](scripts/check-pinned-deps.sh) is CI-enforced
(the `pinned dependency refs` check). It requires that:

- every third-party Action in `.github/workflows` is pinned to a full commit
  SHA (dependabot bumps stay mergeable);
- QHotkey and QWindowKit are fetched at full commit SHAs, with QWindowKit
  declared only in `cmake/QWindowKit.cmake`, which the app and the tests both
  include;
- the upstream ref, the boringssl recipe, linuxdeploy and the Linux build
  container are pinned;
- `QT_VER` / `CONAN_VER` agree across workflows, and no leg takes Homebrew's
  Qt instead of `QT_VER`'s;
- the vendored patches are applied as a directory, with `--fuzz=0`.

### Static analysis: cppcheck, clang-tidy, Codacy

| Tool | Where it runs | Configuration |
| --- | --- | --- |
| cppcheck | Security workflow, on `src/` and `include/`; also inside Codacy | [`cppcheck-suppressions.txt`](cppcheck-suppressions.txt): the suppression list CI passes via `--suppressions-list`, and the one to use locally |
| clang-tidy | Security workflow, via `scripts/run-clang-tidy.sh` | `.clang-tidy` |
| Codacy | Each push ([setup](#codacy-code-quality-dashboard)) | [`.codacy.yml`](.codacy.yml): excludes, **cppcheck `extra_lines`**, lizard/metric excludes |

**Keep the two cppcheck suppression lists in step.** Codacy takes cppcheck
flags only from `engines.cppcheck.extra_lines` in `.codacy.yml`; it cannot read
a suppressions file. So the ids are written twice on purpose, and the cppcheck
job fails if the two copies drift apart.

A `cppcheck.cfg` in the repo root does nothing: cppcheck has no such
auto-loaded config file. (One used to sit here claiming otherwise.)

## Releasing

### Release procedure

1. **Open a release PR**, titled with the bare version (e.g. `1.2.3`). In it:
   - bump `project(FreeTunnel VERSION X.Y.Z ...)` in `CMakeLists.txt`;
   - rename `## Unreleased` in `CHANGELOG.md` to `## X.Y.Z`, and check each
     entry against the code (see
     [Writing release notes](#writing-release-notes)).
2. **Merge it once CI is green:** every required check passes and the branch is
   up to date with `main`.
3. **Tag the release commit on `main` and push the tag:**

   ```bash
   git tag -a vX.Y.Z -m "FreeTunnel X.Y.Z"
   git push origin vX.Y.Z
   ```

4. **Let the release job publish.** The tag runs Build, Tests and Security. The
   Build workflow's `Publish release` job then:
   - waits until Tests and Security have passed on this very commit
     (`scripts/check-release-ci.sh`; runs from pull requests do not count);
   - writes `SHA256SUMS.txt` and signs it with `ED25519_SIGNING_KEY` (see
     [Signed updates (Ed25519)](#signed-updates-ed25519));
   - checks that signature against the public key the app is built with, in
     `include/core/ReleaseSigning.h` at the tagged commit
     (`scripts/verify-release-signature.sh`);
   - takes the release notes from the `## X.Y.Z` section of `CHANGELOG.md`, and
     appends the install notes and a compare link to the previous release;
   - publishes the installers, `SHA256SUMS.txt` and `SHA256SUMS.txt.sig` to
     GitHub Releases.

The release stops, and publishes nothing, when:

- the tag does not match the version in `CMakeLists.txt`;
- `CHANGELOG.md` has no `## X.Y.Z` section;
- the `ED25519_SIGNING_KEY` secret is not set;
- the signature does not verify against the key in `ReleaseSigning.h`, as when
  the secret no longer matches it;
- Tests or Security has no passing run on the commit.

### Writing release notes

`CHANGELOG.md` says what changed for people using FreeTunnel, and each
version's section becomes that release's notes on GitHub. Write each entry for
users:

- **One bold sentence** saying what changed for users.
- **At most two short lines after it:** who is affected, and what to do.
- **No background.** Why and how belong in the PR, not the notes.
- **Related fixes** may share one bold sentence, as short sub-bullets under it.
- **Group entries** under `### Security`, `### Changed` and `### Fixed`.
- **Many entries?** Start the section with a short `### Highlights` list of 3-6
  bullets.

```markdown
## X.Y.Z

### Security

- **A freetunnel:// link asks before it turns the VPN off.**
  Linux: if AppImageLauncher or Gear Lever added the AppImage to your menu,
  add it again once you have updated.
```

### Signed updates (Ed25519)

CI signs the release manifest, `SHA256SUMS.txt`, and the in-app updater
verifies it against the public key in `include/core/ReleaseSigning.h`.

**This repo is already configured:**

- the public key is in `ReleaseSigning.h`;
- the private key lives in the GitHub Actions secret `ED25519_SIGNING_KEY`;
- tagged releases publish `SHA256SUMS.txt` + `SHA256SUMS.txt.sig` (see
  v1.0.6).

The release job opens the manifest with a `#version=<tag without v>` line. The
updater refuses a manifest without one or naming another version, so a manifest
made by any other route must carry it too.

To rotate keys:

1. Generate a new pair:

   ```bash
   ./scripts/gen-release-signing-key.sh release-signing.pem
   ```

2. Paste the printed public PEM into `include/core/ReleaseSigning.h`
   (`kReleaseSigningPublicKeyPem`).
3. Update the `ED25519_SIGNING_KEY` repository secret to the matching private
   PEM.

**Never commit the private key.**

### Code signing (distribution)

Release builds from `.github/workflows/build.yml` are **unsigned** by design:
there is no Apple Developer ID or Authenticode certificate in CI, and both cost
money. Users must approve the first launch manually (see the README).

| Platform | What CI does today | For production distribution |
| --- | --- | --- |
| **macOS** | **Ad-hoc** `codesign` (free, not notarized), so the bundle launches after the user approves Gatekeeper manually | Sign with Developer ID Application + notarize with `notarytool`; staple the ticket on the `.dmg` |
| **Windows** | Installer binaries remain unsigned | Sign the installer and bundled binaries with an Authenticode cert (EV recommended for SmartScreen reputation) |
| **Linux** | Installer binaries remain unsigned | `.deb` packages; release integrity via Ed25519-signed `SHA256SUMS.txt` |

On Windows and Linux, integrity is covered by SHA256 + Ed25519 on the release
manifest instead. Code signing is orthogonal to that in-app **update manifest**
signing; see [Signed updates (Ed25519)](#signed-updates-ed25519).

## Codacy (code quality dashboard)

For maintainers. FreeTunnel uses [Codacy](https://www.codacy.com/) for static
analysis and optional coverage tracking on the repository page (badges in
[README.md](README.md)).

### One-time setup

1. Sign in at [app.codacy.com](https://app.codacy.com/) with GitHub.
2. **Add project** → `dimmmmmmmer/freetunnel` (or your fork, then update badge
   URLs).
3. Codacy reads [`.codacy.yml`](.codacy.yml) for exclude paths and analyzes
   each push.

**Badge stays gray?** Open the project on Codacy and re-copy the badge markdown
from **Settings → General → Badges** (it embeds your project UUID).

**Badge looks stale?** Append `?branch=main` to both Grade and Coverage badge
URLs, so GitHub shows the latest `main` analysis.

### Coverage badge (required for non-zero Codacy coverage)

CI generates lcov from unit tests (~79% of instrumented `src/`/`include/`
today). Codacy shows **0%** until the report is uploaded with the correct
token.

1. Codacy → **freetunnel** → **Settings → Coverage** → copy the **Project API
   token**. Not **Account → Access management → API tokens**: that token gives
   “Request URL not found”.
2. GitHub → **Settings → Secrets and variables → Actions** → set
   `CODACY_PROJECT_TOKEN` to that Coverage project token (update the secret if
   you previously used the wrong one).
3. Re-run **Coverage (Linux)** or push to `main`.

The upload step is in
[`.github/workflows/tests.yml`](.github/workflows/tests.yml). For a local
report, see [Coverage and sanitizers](#coverage-and-sanitizers).

### Branch protection and Codacy status checks

The checks `main` requires are listed in [What CI checks](#what-ci-checks).
Change them under GitHub → **Settings → Branches** → edit `main`, for example
if you change which Codacy checks are enforced.

Codacy still shows **main branch isn't protected** until:

1. **Codacy → freetunnel → Settings (⚙) → Integrations**: toggle **Status
   checks**. There is no separate “GitHub → Status checks” submenu; the options
   live on the Integrations tab.
2. At least one analysis finishes on `main` (Codacy posts the status check to
   GitHub).
3. Every quality-gate rule you enforce in Codacy is also a **required** check
   on `main`.

Gates:

| Gate | Setup |
| --- | --- |
| Quality gate (default) | Add **Codacy Static Code Analysis**; already required on `main` |
| Coverage gates (optional) | Enable **Diff coverage is under** or **Coverage variation is under** in Codacy **Settings → Quality settings**, set GitHub secret `CODACY_PROJECT_TOKEN`, then also require **Codacy Diff Coverage** and/or **Codacy Coverage Variation** on `main` |
| New code only (optional) | Set quality gates for **new code** only, so historical issues do not block merges |

## Deep links

See [DEEP_LINK.md](DEEP_LINK.md) for:

- the `tt://` TLV specification;
- the `freetunnel://` control links: how a link opened by the system is told
  from the same URL run as a command, and which links ask first.

## Reporting bugs

- **Bugs:** open an issue with the
  [bug report form](https://github.com/dimmmmmmmer/freetunnel/issues/new/choose).
  It asks for the OS, the build, and what the log says, which is most of what
  any answer depends on. Russian is fine; the forms say so.
- **Security bugs:** go through
  [Security Advisories](https://github.com/dimmmmmmmer/freetunnel/security/advisories/new)
  instead, never a public issue.

## Why it works this way

### Why every platform gates a merge

All three unit-test platforms gate a merge, not just Linux. Windows is roughly
half of all downloads and macOS is most of the rest, and both are where this
project's hard bugs have actually lived. A green Linux run says very little
about either.

ASan is required for the same reason: it has caught a real use-after-free here,
not a hypothetical one.

The release build is required too, on all three platforms. It is the only job
that packages anything, and a pull request used to be able to merge with it
red.

## License

By contributing, you agree that your contributions will be licensed under the
Apache License 2.0 (see [LICENSE](LICENSE)).
