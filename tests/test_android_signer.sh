#!/usr/bin/env bash
# Tests for scripts/check-android-signer.sh, the Android release's check that
# the APK is signed with the certificate android/release-certificate.sha256
# pins. That workflow only runs for an android-* tag, so without these the
# script's first real run would be the release it is there to protect. A
# stand-in apksigner prints what the real one does (apksigner verify
# --print-certs, Build Tools 35); no Android SDK is needed.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/check-android-signer.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PIN=09b3aaa6a6d7b43fbedbf555a2d88f2226b04c3bb0aeb78f77b139fe12a2e3ae
OTHER=5f7c1e0d2a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5

# Prints $FIXTURES/certs and exits 0, or prints $FIXTURES/err and exits 1 when
# it exists. Logs its arguments.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/apksigner" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
if [ -e "$FIXTURES/err" ]; then
  cat "$FIXTURES/err" >&2
  exit 1
fi
cat "$FIXTURES/certs"
EOF
chmod +x "$TMP/bin/apksigner"

# signer <label> <sha-256>: the four lines apksigner prints for one certificate.
signer() {
  printf '%s certificate DN: CN=FreeTunnel\n' "$1"
  printf '%s certificate SHA-256 digest: %s\n' "$1" "$2"
  printf '%s certificate SHA-1 digest: 0123456789abcdef0123456789abcdef01234567\n' "$1"
  printf '%s certificate MD5 digest: 0123456789abcdef0123456789abcdef\n' "$1"
}

fresh() {
  rm -rf "$TMP/f"
  mkdir -p "$TMP/f"
  : > "$TMP/f/calls.log"
  echo "$PIN" > "$TMP/f/pin"
  echo "not really an APK" > "$TMP/f/app.apk"
  signer "Signer #1" "$PIN" > "$TMP/f/certs"
}
out=""
rc=0
# check [<apk>]: the pin in $TMP/f/pin, and the APK if given.
check() {
  rc=0
  out="$(FIXTURES="$TMP/f" APKSIGNER="${APKSIGNER_UNDER_TEST:-$TMP/bin/apksigner}" \
         bash "$CHECK" "$TMP/f/pin" "$@" 2>&1)" || rc=$?
}
fail=0
# expect <case> <exit code> <ERE the output must match>
expect() {
  if [[ "$rc" -ne "$2" ]] || ! grep -qE -- "$3" <<<"$out"; then
    echo "FAIL: $1: exit $rc (wanted $2), output must match /$3/:"
    printf '    %s\n' "${out//$'\n'/$'\n'    }"
    fail=1
  else
    echo "ok: $1"
  fi
}

fresh
check
expect "a good pin, no APK" 0 "pins the release certificate $PIN"
[[ ! -s "$TMP/f/calls.log" ]] || { echo "FAIL: apksigner ran without an APK"; fail=1; }

fresh
printf '%s' "$PIN" > "$TMP/f/pin"
check
expect "a pin with no newline at the end" 0 "pins the release certificate"

fresh
rm "$TMP/f/pin"
check
expect "no pin file" 1 "::error title=No release certificate pin::.*does not exist.*android/README.md"
fresh
rm "$TMP/f/pin"
check "$TMP/f/app.apk"
expect "no pin file, with an APK" 1 "::error title=No release certificate pin::"
[[ ! -s "$TMP/f/calls.log" ]] || { echo "FAIL: apksigner ran with no pin"; fail=1; }

# bad_pin <case> <file content>: what a pin can look like when it was not made
# the way the README says. Refused before apksigner is asked anything.
bad_pin() {
  fresh
  printf '%s\n' "$2" > "$TMP/f/pin"
  check "$TMP/f/app.apk"
  expect "pin: $1" 1 "::error title=Bad release certificate pin::.*64 lowercase hex"
  [[ ! -s "$TMP/f/calls.log" ]] || { echo "FAIL: apksigner ran with a bad pin"; fail=1; }
}
bad_pin "empty" ""
bad_pin "capitals, as keytool -list prints it" "${PIN^^}"
bad_pin "colons, as keytool -list prints it" "$(sed 's/../&:/g; s/:$//' <<<"$PIN")"
bad_pin "sha256sum's whole line, from stdin" "$PIN  -"
bad_pin "sha256sum's whole line, from a file" "$PIN  release.cer"
bad_pin "63 characters" "${PIN:1}"
bad_pin "65 characters" "${PIN}0"
bad_pin "a Windows line ending" "$PIN"$'\r'
bad_pin "two pins" "$PIN"$'\n'"$OTHER"
bad_pin "a leading space" " $PIN"
bad_pin "keytool's label left in" "SHA256: $PIN"
bad_pin "64 characters that are not hex" "$(printf '%064d' 0 | tr 0 g)"

fresh
check "$TMP/f/app.apk"
expect "an APK signed with the pinned key" 0 "is signed by the pinned release certificate $PIN"
grep -qx -- "verify --print-certs $TMP/f/app.apk" "$TMP/f/calls.log" \
  || { echo "FAIL: apksigner was not asked to verify the APK:"; cat "$TMP/f/calls.log"; fail=1; }

fresh
signer "Signer #1" "${PIN^^}" > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "apksigner printing the digest in capitals" 0 "is signed by the pinned release certificate $PIN"

fresh
signer "Signer #1" "$OTHER" > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "an APK signed with another key" 1 \
  "::error title=APK signed with another key::.*signed by the certificate $OTHER, and .* pins $PIN"

fresh
{ signer "Signer #1" "$PIN"; signer "Signer #2" "$OTHER"; } > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "a second signer" 1 "::error title=Unexpected APK signers::.*lists 2 certificate"

fresh
{ signer "Signer #1" "$OTHER"; signer "Signer #2" "$PIN"; } > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "the pinned key as the second signer" 1 "::error title=Unexpected APK signers::"

# apksigner's v3.1 output, for a key rotated on newer Android versions.
fresh
{ signer "Signer (minSdkVersion=33, maxSdkVersion=2147483647)" "$OTHER"
  signer "Signer (minSdkVersion=26, maxSdkVersion=32)" "$PIN"; } > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "a rotated key" 1 "::error title=Unexpected APK signers::.*lists 2 certificate"

fresh
{ signer "Signer #1" "$PIN"; signer "Source Stamp Signer" "$OTHER"; } > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "a source stamp" 1 "::error title=Unexpected APK signers::"

fresh
signer "Signer (minSdkVersion=26, maxSdkVersion=2147483647)" "$PIN" > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "the pinned key printed as a v3.1 signer" 1 "::error title=Unexpected APK signers::.*lists 1 certificate"

fresh
: > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "no certificates listed" 1 "::error title=Unexpected APK signers::.*lists 0 certificate"

fresh
signer "Signer #1" "${PIN:1}" > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "a digest one character short" 1 "::error title=Unexpected APK signers::"

fresh
signer "Signer #1" "${PIN}0" > "$TMP/f/certs"
check "$TMP/f/app.apk"
expect "a digest one character long, the pin in front" 1 "::error title=Unexpected APK signers::"

fresh
echo "DOES NOT VERIFY" > "$TMP/f/err"
check "$TMP/f/app.apk"
expect "an APK that does not verify" 1 "::error title=APK does not verify::.*DOES NOT VERIFY"

fresh
check "$TMP/f/missing.apk"
expect "no APK" 1 "::error title=No APK::"

fresh
APKSIGNER_UNDER_TEST="$TMP/bin/no-such-apksigner" check "$TMP/f/app.apk"
expect "no apksigner" 1 "::error title=No apksigner::"

# Without APKSIGNER, Build Tools 35.0.0's under ANDROID_HOME.
fresh
mkdir -p "$TMP/sdk/build-tools/35.0.0"
cp "$TMP/bin/apksigner" "$TMP/sdk/build-tools/35.0.0/apksigner"
rc=0
out="$(env -u APKSIGNER FIXTURES="$TMP/f" ANDROID_HOME="$TMP/sdk" \
       bash "$CHECK" "$TMP/f/pin" "$TMP/f/app.apk" 2>&1)" || rc=$?
expect "apksigner found under ANDROID_HOME" 0 "is signed by the pinned release certificate"

rc=0
out="$(bash "$CHECK" 2>&1)" || rc=$?
expect "no arguments" 2 "usage:"

exit "$fail"
