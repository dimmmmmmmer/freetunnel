#!/usr/bin/env bash
# Stage the four desktop installers and bind their checksums to the release version.
set -euo pipefail
version="${1:?expected a release version}"
source_dir="${2:?expected the downloaded build artifacts}"
output="${3:?expected a new staging directory}"
export LC_ALL=C

installers=(freetunnel-linux-x86_64.deb freetunnel-macos-universal.dmg
  freetunnel-windows-x86_64-Setup.exe freetunnel-x86_64.AppImage)
mkdir "$output"
for name in "${installers[@]}"; do
  test -s "$source_dir/$name" || { echo "Missing release file: $name" >&2; exit 1; }
  cp "$source_dir/$name" "$output/$name"
done
cd "$output"
printf '#version=%s\n' "$version" > SHA256SUMS.txt
sha256sum "${installers[@]}" >> SHA256SUMS.txt
tail -n +2 SHA256SUMS.txt | sha256sum --strict -c -
