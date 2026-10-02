#!/usr/bin/env bash
# Check a release's SHA256SUMS.txt.sig against the public key the app is built
# with, the way the in-app updater will. Run by the release job in
# .github/workflows/build.yml right after signing, before anything is published.
#
# The signing key is a GitHub secret and the public key is compiled in from
# include/core/ReleaseSigning.h, and nothing else ties the two together. If the
# secret stopped matching the header, signing would still succeed and the
# release would publish, and every installed copy would refuse it as an update.
#
# The key is read from the header itself, so there is no second copy of it to
# keep in step: the string literals that initialise kReleaseSigningPublicKeyPem
# are joined and their escapes decoded, as the compiler does. Anything in that
# initialiser this script does not understand fails the check rather than being
# guessed at.
#
# usage: verify-release-signature.sh <ReleaseSigning.h> <SHA256SUMS.txt> <SHA256SUMS.txt.sig>
set -euo pipefail

usage="usage: verify-release-signature.sh <ReleaseSigning.h> <SHA256SUMS.txt> <SHA256SUMS.txt.sig>"
if [[ "$#" -ne 3 ]]; then
  echo "$usage" >&2
  exit 2
fi
header="$1"
manifest="$2"
signature="$3"
name=kReleaseSigningPublicKeyPem

fail() {
  echo "::error title=$1::$2"
  exit 1
}

# Sets $decoded to the character that the escape \$1 in a string literal stands
# for. A variable, not output: a command substitution would drop a newline.
unescape() {
  case "$1" in
    n) decoded=$'\n' ;;
    t) decoded=$'\t' ;;
    r) decoded=$'\r' ;;
    "\\" | '"' | "'" | '?') decoded="$1" ;;
    *) echo "unsupported escape \\$1 in a string literal" >&2; return 1 ;;
  esac
}

# Prints $1, which starts with a comment, from just after that comment.
after_comment() {
  local rest
  case "$1" in
    //*$'\n'*) printf '%s' "${1#*$'\n'}" ;;
    /\**\*/*) rest="${1#/\*}"; printf '%s' "${rest#*\*/}" ;;
    *) echo "unexpected '/' in the initialiser" >&2; return 1 ;;
  esac
}

# Prints the value of the C++ string literal(s) that make up the initialiser in
# $1, which starts just after the '='. Ordinary literals only: adjacent ones are
# joined, and comments and white space between them are skipped.
decode_literals() {
  local src="$1" out="" i=0 c in_string=0 decoded
  while [[ "$i" -lt "${#src}" ]]; do
    c="${src:i:1}"
    i=$((i + 1))
    if [[ "$in_string" -eq 1 ]]; then
      case "$c" in
        '"') in_string=0 ;;
        $'\n') echo "a string literal is not closed on its line" >&2; return 1 ;;
        "\\") unescape "${src:i:1}" || return 1; out+="$decoded"; i=$((i + 1)) ;;
        *) out+="$c" ;;
      esac
      continue
    fi
    case "$c" in
      ';') printf '%s' "$out"; return 0 ;;
      '"') in_string=1 ;;
      ' ' | $'\t' | $'\n' | $'\r') ;;
      '/') src="$(after_comment "${src:i-1}")" || return 1; i=0 ;;
      *) echo "unexpected '$c' in the initialiser (only plain string literals are read)" >&2; return 1 ;;
    esac
  done
  echo "the initialiser has no ';'" >&2
  return 1
}

for f in "$header" "$manifest" "$signature"; do
  [[ -f "$f" ]] || fail "Missing file" "$f does not exist"
done

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

src="$(cat "$header")"
re="${name}[[:space:]]*=(.*)"
if ! [[ "$src" =~ $re ]]; then
  fail "No release key" "$header has no '$name =' to read the public key from"
fi
if ! pem="$(decode_literals "${BASH_REMATCH[1]}" 2>"$tmp/err")"; then
  fail "Cannot read the release key" "$name in $header: $(cat "$tmp/err")"
fi
printf '%s\n' "$pem" > "$tmp/pub.pem"
if ! openssl pkey -pubin -in "$tmp/pub.pem" -noout 2>"$tmp/err"; then
  fail "Cannot read the release key" "$name in $header is not a public key openssl can read: $(head -1 "$tmp/err")"
fi

# The updater calls EVP_DigestVerify on the manifest's bytes with no digest of
# its own (src/core/ReleaseVerify.cpp); -rawin is that same one-shot check.
if ! openssl pkeyutl -verify -pubin -inkey "$tmp/pub.pem" -rawin \
       -in "$manifest" -sigfile "$signature"; then
  fail "Signature does not match the app's key" "$signature does not verify against the public key in $header, so installed copies of FreeTunnel would refuse this release as an update. In the release job, which signed it a step earlier, that means the ED25519_SIGNING_KEY secret is not the private half of that key: put the matching private key back in the secret, then re-run the job."
fi
echo "$signature verifies against $name in $header."
