#!/usr/bin/env bash
# Check an Android release APK against the signing certificate the repository
# pins in android/release-certificate.sha256. Run by
# .github/workflows/android-release.yml: once with the pin alone, before the
# CI, and once more with the signed APK, before it is published.
#
# Android installs an update only when it is signed with the same key as the
# installed app, so the first official release fixes the key for good. The
# keystore lives in a GitHub secret, and nothing else says which keystore that
# is: a secret replaced by mistake would sign and publish without complaint,
# and every app installed from earlier releases could never update to it.
# scripts/build-android-release.sh checks the APK against the keystore it was
# given; this checks it against the certificate the repository names.
#
# The pin is one line: the SHA-256 of the certificate's DER bytes, in lowercase
# hex without colons. That is the value apksigner prints as "certificate
# SHA-256 digest"; android/README.md (Releasing) shows how to make it.
#
# usage: check-android-signer.sh <release-certificate.sha256> [<apk>]
# env:   APKSIGNER, the apksigner to run (only with an APK); by default
#        Build Tools 35.0.0's, under ANDROID_HOME or ANDROID_SDK_ROOT.
set -euo pipefail
export LC_ALL=C

if [[ "$#" -lt 1 || "$#" -gt 2 ]]; then
  echo "usage: check-android-signer.sh <release-certificate.sha256> [<apk>]" >&2
  exit 2
fi
pin_file="$1"
apk="${2:-}"
how="See 'Releasing' in android/README.md for how to make it from the release keystore."

fail() {
  echo "::error title=$1::$2"
  exit 1
}

if [[ ! -f "$pin_file" ]]; then
  fail "No release certificate pin" "$pin_file does not exist. It names the certificate every official Android release must be signed with, and nothing is signed or published without it. $how"
fi
pin="$(cat "$pin_file")"
if ! [[ "$pin" =~ ^[0-9a-f]{64}$ ]]; then
  fail "Bad release certificate pin" "$pin_file must hold one line of 64 lowercase hex characters, the certificate's SHA-256 without colons, and holds something else. $how"
fi
if [[ -z "$apk" ]]; then
  echo "$pin_file pins the release certificate $pin."
  exit 0
fi

[[ -f "$apk" ]] || fail "No APK" "$apk does not exist"
signer="${APKSIGNER:-${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}/build-tools/35.0.0/apksigner}"
[[ -x "$signer" ]] || fail "No apksigner" "$signer is not an executable apksigner"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
if ! "$signer" verify --print-certs "$apk" > "$tmp/certs" 2> "$tmp/err"; then
  cat "$tmp/err" >&2
  fail "APK does not verify" "apksigner does not verify $apk: $(head -n 1 "$tmp/err")"
fi
# Every certificate apksigner names, not only the first: a second signer, a
# rotated key (printed as "Signer (minSdkVersion=...)") or a source stamp
# would each mean the APK is not simply signed with the one pinned key.
digests="$(grep -E 'certificate SHA-256 digest:' "$tmp/certs" || true)"
count=0
if [[ -n "$digests" ]]; then
  count="$(wc -l <<<"$digests")"
fi
digest_re='^Signer #1 certificate SHA-256 digest: ([0-9a-fA-F]{64})$'
if [[ "$count" -ne 1 ]] || ! [[ "$digests" =~ $digest_re ]]; then
  fail "Unexpected APK signers" "$apk must have exactly one signer, and apksigner lists $count certificate(s): $(paste -sd ';' - <<<"$digests")"
fi
actual="$(tr 'A-F' 'a-f' <<<"${BASH_REMATCH[1]}")"
if [[ "$actual" != "$pin" ]]; then
  fail "APK signed with another key" "$apk is signed by the certificate $actual, and $pin_file pins $pin. Installed apps would refuse it as an update. If the ANDROID_KEYSTORE_BASE64 secret holds the wrong keystore, put the release keystore back in it and re-run the build. Change the pin to match only if no release signed with the pinned key was ever published."
fi
echo "$apk is signed by the pinned release certificate $pin."
