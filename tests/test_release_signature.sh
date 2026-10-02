#!/usr/bin/env bash
# Tests for scripts/verify-release-signature.sh, the release job's check that
# the SHA256SUMS.txt.sig it just made verifies against the public key compiled
# into the app. That job only runs for a v* tag, so without these the script's
# first real run would be the release it is there to protect. The keys here are
# throwaway ones made for the run; the release key is not involved.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/verify-release-signature.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

openssl genpkey -algorithm ed25519 -out "$TMP/a.pem"
openssl genpkey -algorithm ed25519 -out "$TMP/b.pem"
openssl pkey -in "$TMP/a.pem" -pubout -out "$TMP/a.pub.pem"
openssl pkey -in "$TMP/b.pem" -pubout -out "$TMP/b.pub.pem"

# A manifest like the release job's, signed by key a the way the job signs it.
printf '#version=9.9.9\n%s  freetunnel-linux-x86_64.deb\n' \
  "$(printf deb | sha256sum | cut -d' ' -f1)" > "$TMP/SHA256SUMS.txt"
openssl pkeyutl -sign -inkey "$TMP/a.pem" -rawin \
  -in "$TMP/SHA256SUMS.txt" -out "$TMP/SHA256SUMS.txt.sig"

# header <public PEM> > ReleaseSigning.h, laid out as include/core/ReleaseSigning.h
# is: one literal per PEM line, each ending in \n.
header() {
  printf '#pragma once\n\nnamespace freetunnel {\n\n'
  printf '// The public half; the updater reads kReleaseSigningPublicKeyPem.\n'
  printf 'inline constexpr const char *kReleaseSigningPublicKeyPem =\n'
  sed -e 's/.*/    "&\\n"/' -e '$ s/$/;/' "$1"
  printf '\n} // namespace freetunnel\n'
}
# The base64 line of a public PEM.
body() { sed -n 2p "$1"; }

out=""
rc=0
check() {
  rc=0
  out="$(bash "$CHECK" "$@" 2>&1)" || rc=$?
}
fail=0
# expect <case> <exit status> <ERE the output must match>
expect() {
  if [[ "$rc" -ne "$2" ]] || ! grep -qE -- "$3" <<<"$out"; then
    echo "FAIL: $1: exit $rc (wanted $2), output must match /$3/:"
    printf '    %s\n' "${out//$'\n'/$'\n'    }"
    fail=1
  else
    echo "ok: $1"
  fi
}
sums="$TMP/SHA256SUMS.txt"
sig="$TMP/SHA256SUMS.txt.sig"
mismatch="::error title=Signature does not match the app's key::"

header "$TMP/a.pub.pem" > "$TMP/a.h"
check "$TMP/a.h" "$sums" "$sig"
expect "a signature by the header's key passes" 0 "verifies against kReleaseSigningPublicKeyPem"

header "$TMP/b.pub.pem" > "$TMP/b.h"
check "$TMP/b.h" "$sums" "$sig"
expect "a signature by another key stops the release" 1 "$mismatch"

sed 's/^#version=9.9.9$/#version=9.9.8/' "$sums" > "$TMP/changed.txt"
check "$TMP/a.h" "$TMP/changed.txt" "$sig"
expect "a manifest changed after signing does not pass" 1 "$mismatch"

: > "$TMP/empty.sig"
check "$TMP/a.h" "$sums" "$TMP/empty.sig"
expect "an empty signature does not pass" 1 "$mismatch"

check "$TMP/a.h" "$sums" "$TMP/missing.sig"
expect "a missing signature does not pass" 1 "missing.sig does not exist"

# The header the app is built with: its key is read, and so the check gets as
# far as the signature, which key a did not make.
check "$ROOT/include/core/ReleaseSigning.h" "$sums" "$sig"
expect "the app's own header gives a key" 1 "$mismatch"

# Other ways the same literal could be written. Each holds key a.
{
  printf 'inline constexpr const char *kReleaseSigningPublicKeyPem = '
  printf '"-----BEGIN PUBLIC KEY-----\\n%s\\n-----END PUBLIC KEY-----\\n";\n' "$(body "$TMP/a.pub.pem")"
} > "$TMP/one-line.h"
check "$TMP/one-line.h" "$sums" "$sig"
expect "one literal on one line" 0 "verifies against"

{
  printf 'inline constexpr const char *kReleaseSigningPublicKeyPem=\n'
  printf '    "-----BEGIN PUBLIC" " KEY-----\\n" // the first line\n'
  printf '    /* the key */ "%s\\n"\n' "$(body "$TMP/a.pub.pem")"
  printf '    "-----END PUBLIC KEY-----\\n"\n    ;\n'
} > "$TMP/split.h"
check "$TMP/split.h" "$sums" "$sig"
expect "literals split mid-line, with comments between" 0 "verifies against"

sed 's/$/\r/' "$TMP/a.h" > "$TMP/crlf.h"
check "$TMP/crlf.h" "$sums" "$sig"
expect "a checkout with CRLF line endings" 0 "verifies against"

# What it does not read, it refuses rather than guesses at.
sed 's/-----BEGIN/\\x2d----BEGIN/' "$TMP/a.h" > "$TMP/hex.h"
check "$TMP/hex.h" "$sums" "$sig"
expect "an escape it does not decode" 1 "unsupported escape \\\\x"

{
  printf 'inline constexpr const char *kReleaseSigningPublicKeyPem = R"(\n'
  cat "$TMP/a.pub.pem"
  printf ')";\n'
} > "$TMP/raw.h"
check "$TMP/raw.h" "$sums" "$sig"
expect "a raw string literal" 1 "unexpected 'R'"

sed 's/\\n"$/\\n/' "$TMP/a.h" > "$TMP/open.h"
check "$TMP/open.h" "$sums" "$sig"
expect "a literal left open" 1 "not closed on its line"

sed 's/kReleaseSigningPublicKeyPem =/kOtherKey =/' "$TMP/a.h" > "$TMP/renamed.h"
check "$TMP/renamed.h" "$sums" "$sig"
expect "a header without the key" 1 "has no 'kReleaseSigningPublicKeyPem ='"

sed '/MCow/d' "$TMP/a.h" > "$TMP/no-body.h"
check "$TMP/no-body.h" "$sums" "$sig"
expect "a PEM that is not a key" 1 "not a public key openssl can read"

exit "$fail"
