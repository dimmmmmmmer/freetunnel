#!/usr/bin/env bash
# Build the reviewed stable core for real iPhones and arm64 simulators.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="${FT_IOS_BUILD_DIR:-$root/ios/.build}"
ref="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["commit"])' "$root/android/core-source.json")"
[[ "$ref" =~ ^[0-9a-f]{40}$ ]] || exit 1
mkdir -p "$work"
if [[ ! -d "$work/upstream/.git" ]]; then git init "$work/upstream"; fi
git -C "$work/upstream" fetch --depth 1 https://github.com/TrustTunnel/TrustTunnelClient.git "$ref"
git -C "$work/upstream" checkout --detach FETCH_HEAD
[[ "$(git -C "$work/upstream" rev-parse HEAD)" == "$ref" ]]
# Refuse a dirty cached source tree; each build starts with a fresh checkout.
[[ -z "$(git -C "$work/upstream" status --porcelain --untracked-files=no)" ]]
python3 - "$root" "$work/upstream" <<'PY'
import json
from pathlib import Path
import subprocess
import sys
root, upstream = map(Path, sys.argv[1:])
pin = json.loads((root / 'android/core-source.json').read_text())
for patch in pin['patches']:
    with (root / patch).open('rb') as source:
        subprocess.run(['patch', '-p1', '--fuzz=0'], stdin=source, cwd=upstream, check=True)
PY
# Cargo is invoked from an out-of-tree CMake build directory. Carry the
# upstream toolchain into that directory instead of using the runner default.
RUSTUP_TOOLCHAIN="$(cd "$work/upstream" && rustup show active-toolchain | cut -d ' ' -f 1)"
export RUSTUP_TOOLCHAIN
(
  cd "$work/upstream"
  rustup target add --toolchain "$RUSTUP_TOOLCHAIN" aarch64-apple-ios aarch64-apple-ios-sim
  python3 scripts/bootstrap_conan_deps.py
)
bash "$root/scripts/export-patched-boringssl.sh"
conan profile detect --force
for sdk in iphoneos iphonesimulator; do
  target=ios
  [[ "$sdk" == iphonesimulator ]] && target=iphonesimulator
  cmake -S "$work/upstream/platform/apple/VpnClient" -B "$work/core-$sdk" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$sdk" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    -DTARGET_OS="$target" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
  cmake --build "$work/core-$sdk" --target VpnClientFramework --parallel 3
done
xcodebuild -create-xcframework \
  -framework "$work/core-iphoneos/VpnClientFramework.framework" \
  -framework "$work/core-iphonesimulator/VpnClientFramework.framework" \
  -output "$work/VpnClientFramework.xcframework"
