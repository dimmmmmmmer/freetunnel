#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "$0")/.." && pwd)"
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["version"])' "$repo_dir/android/core-source.json")"
artifact="$repo_dir/android/app/libs/trusttunnel-client-android-$version.aar"
if [[ -f "$artifact" || -f "${artifact%.aar}.json" ]]; then
    # A stale or altered artifact must fail, not silently trigger a replacement.
    python3 "$repo_dir/scripts/android-core-manifest.py" verify "$artifact"
    printf 'Verified stable Android core with FreeTunnel routing patch.\n'
else
    exec "$repo_dir/scripts/build-trusttunnel-android.sh"
fi
