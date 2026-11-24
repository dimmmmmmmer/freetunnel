#!/usr/bin/env bash
# Stage exactly the files each update client can install. Both signed manifests
# bind their unversioned file names to the app version.
set -euo pipefail
version="${1:?expected a release version}"
source_dir="${2:?expected the downloaded build artifacts}"
output="${3:?expected a new staging directory}"
export LC_ALL=C

mkdir "$output"
stage() {
  local channel="$1" name
  shift
  mkdir "$output/$channel"
  for name in "$@"; do
    test -s "$source_dir/$name" || { echo "Missing release file: $name" >&2; return 1; }
    cp "$source_dir/$name" "$output/$channel/$name"
  done
  (
    cd "$output/$channel"
    printf '#version=%s\n' "$version" > SHA256SUMS.txt
    sha256sum "$@" >> SHA256SUMS.txt
    tail -n +2 SHA256SUMS.txt | sha256sum --strict -c -
  )
}

stage desktop freetunnel-linux-x86_64.deb freetunnel-macos-universal.dmg \
  freetunnel-windows-x86_64-Setup.exe freetunnel-x86_64.AppImage
stage helpers freetunnel-helper-linux-x86_64.tar.gz freetunnel-helper-macos-universal.tar.gz
