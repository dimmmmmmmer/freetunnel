#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "$0")/.." && pwd)"
artifact="trusttunnel-client-android-1.1.11.aar"
expected="da0886c22a2a1fad1fdd25d8f5d3d85c6ffa89188292bd000d41e885db446eca"
destination="$repo_dir/android/app/libs/$artifact"
if [[ -f "$destination" ]] && printf '%s  %s\n' "$expected" "$destination" | shasum -a 256 -c - >/dev/null 2>&1; then
    printf 'Pinned TrustTunnel 1.1.11 already prepared.\n'
    exit 0
fi
temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/trusttunnel-android.XXXXXX")"
trap 'rm -rf "$temporary_dir"' EXIT
# The public package page supplies download links to the original artifacts.
# No PAT, old APK extraction, or alternate native core.
download_url="$(curl -fsSL --retry 2 --connect-timeout 15 --max-time 90 'https://github.com/TrustTunnel/TrustTunnelClient/packages/2733074?version=1.1.11' | python3 -c '
import html, re, sys
name = sys.argv[1]
links = [html.unescape(u) for u in re.findall(r"href=\"([^\"]+)\"", sys.stdin.read())]
url = next((u for u in links if "filename%3D" + name + "&" in u), None)
if not url:
    sys.exit("Official package asset missing")
print(url)
' "$artifact")"
curl -fsSL --retry 2 --connect-timeout 15 -o "$temporary_dir/$artifact" "$download_url"
printf '%s  %s\n' "$expected" "$temporary_dir/$artifact" | shasum -a 256 -c -
mkdir -p "$(dirname "$destination")"
mv "$temporary_dir/$artifact" "$destination"
printf 'Prepared official TrustTunnel 1.1.11.\n'
