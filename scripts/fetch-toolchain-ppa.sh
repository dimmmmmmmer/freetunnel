#!/usr/bin/env bash
# Read an apt-get --print-uris plan produced with Acquire::ForceHash=SHA256.
# Launchpad's file service can still serve the exact file while its PPA pool is down.
set -euo pipefail
out="${1:?expected an APT archive directory}"
stage="$(mktemp -d "$out/.ppa-download.XXXXXXXX")"
trap 'rm -rf "$stage"' EXIT
while read -r uri filename _size digest; do
  case "$uri" in
    \'https://ppa.launchpadcontent.net/ubuntu-toolchain-r/test/ubuntu/pool/*\') ;;
    *) continue ;;
  esac
  if [[ -z "$filename" || "$filename" == .* || "$filename" == */* || ! "$digest" =~ ^SHA256:[[:xdigit:]]{64}$ ]]; then
    echo "Invalid file or SHA-256 in APT download plan" >&2
    exit 1
  fi
  uri="${uri%\'}"
  /usr/lib/apt/apt-helper download-file \
    "https://launchpad.net/~ubuntu-toolchain-r/+archive/ubuntu/test/+files/${uri##*/}" "$stage/$filename" "$digest"
  mv -- "$stage/$filename" "$out/$filename"
done
