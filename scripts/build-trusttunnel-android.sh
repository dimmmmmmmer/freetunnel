#!/usr/bin/env bash
# Ubuntu 24.04, JDK 17, Clang 18, Conan 2.31.1, Rust/cargo-ndk and NDK 29.
# Builds stable upstream with only the Android routing patch; no core upgrade.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "$0")/.." && pwd)"
readarray -t pin < <(python3 - "$repo_dir/android/core-source.json" <<'PY'
import json, sys
pin = json.load(open(sys.argv[1]))
print(pin['version'])
print(pin['commit'])
print('\n'.join(pin['patches']))
PY
)
version="${pin[0]}"
commit="${pin[1]}"
[[ "$commit" =~ ^[0-9a-f]{40}$ ]]
: "${ANDROID_HOME:?Set ANDROID_HOME to an SDK with NDK 29.0.14206865}"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/29.0.14206865"
[[ -d "$ANDROID_NDK_HOME" ]]
work="$(mktemp -d "${TMPDIR:-/tmp}/freetunnel-android-core.XXXXXX")"
trap 'rm -rf "$work"' EXIT
git init -q "$work/source"
git -C "$work/source" remote add origin https://github.com/TrustTunnel/TrustTunnelClient.git
git -C "$work/source" fetch --depth=1 origin "$commit"
git -C "$work/source" checkout --detach FETCH_HEAD
[[ "$(git -C "$work/source" rev-parse HEAD)" == "$commit" ]]
for patch_file in "${pin[@]:2}"; do
    patch --directory="$work/source" -p1 --fuzz=0 < "$repo_dir/$patch_file"
done
# The upstream Android toolchain consults Conan's default build profile.
# Use a private configuration, while preserving an explicitly supplied cache.
export CONAN_HOME="${FT_ANDROID_CONAN_HOME:-$work/conan}"
mkdir -p "$CONAN_HOME/profiles"
cat > "$CONAN_HOME/profiles/default" <<'PROFILE'
[settings]
os=Linux
arch=x86_64
compiler=clang
compiler.version=18
compiler.libcxx=libstdc++11
compiler.cppstd=20
build_type=Release
[conf]
tools.build:jobs=4
tools.build:compiler_executables={"c": "clang-18", "cpp": "clang++-18"}
PROFILE
export CC=clang-18 CXX=clang++-18
# Upstream's private packages are exported from their pinned public sources.
(cd "$work/source" && python3 scripts/bootstrap_conan_deps.py)
bash "$repo_dir/scripts/export-patched-boringssl.sh"
rustup target add aarch64-linux-android armv7-linux-androideabi x86_64-linux-android i686-linux-android --toolchain 1.95
cargo ndk --version | grep -F '4.1.2'
printf 'sdk.dir=%s\ncmake.dir=/usr\n' "$ANDROID_HOME" > "$work/source/platform/android/local.properties"
(cd "$work/source/platform/android" && ./gradlew :lib:assembleRelease "-PttClientVersion=$version" --max-workers=4 --no-daemon)
artifact="$repo_dir/android/app/libs/trusttunnel-client-android-$version.aar"
mkdir -p "$(dirname "$artifact")"
cp "$work/source/platform/android/lib/build/outputs/aar/lib-release.aar" "$artifact"
python3 "$repo_dir/scripts/android-core-manifest.py" write "$artifact"
python3 "$repo_dir/scripts/android-core-manifest.py" verify "$artifact"
