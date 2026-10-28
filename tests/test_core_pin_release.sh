#!/usr/bin/env bash
# Tests for scripts/check-core-pin-release.sh, the check that a v* release
# ships a stable core: scripts/upstream_ref.txt must be a commit that
# TrustTunnelClient tagged vX.Y.Z. That job only runs for a v* tag, so without
# these the script's first real run would be the release it is there to
# protect. A stand-in `gh` answers from fixtures; nothing reaches GitHub. The
# cases are mostly pins that must not publish anything, and every way the
# answer can be missing or unreadable, which must not publish either.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/check-core-pin-release.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
# The pin, and a commit that differs from it in its last hex digit only.
SHA=170609c24ca865819fed68437b01c013049bc3fa
NEAR=170609c24ca865819fed68437b01c013049bc3fb
OTHER=b1ddbd293e530d8ae49097727030773f964ef2cc

# Actions sets these for the job that runs this test; the script must be
# driven only by what each case gives it.
unset UPSTREAM_REPO RETRY_SECONDS

# Answers `gh api --paginate repos/<repo>/tags?per_page=100` with
# $FIXTURES/tags.<n>.json on its n-th call, or with the last fixture once they
# run out, and logs every call. A page fixture holds what --paginate prints:
# one JSON array per page, one after the other.
#   $FIXTURES/fail     exits 1 with an HTTP error. The file's text is how many
#                      calls fail from the start; empty means every call.
#   $FIXTURES/partial  prints the fixture, then exits 1, as a gh whose later
#                      page failed.
#   $FIXTURES/garbage  prints text that is not JSON, and exits 0.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
n=$(( $(cat "$FIXTURES/tags.count" 2>/dev/null || echo 0) + 1 ))
echo "$n" > "$FIXTURES/tags.count"
if [ -e "$FIXTURES/fail" ]; then
  limit="$(cat "$FIXTURES/fail")"
  if [ -z "$limit" ] || [ "$n" -le "$limit" ]; then
    echo "gh: HTTP 403: Resource not accessible by integration" >&2
    exit 1
  fi
fi
if [ -e "$FIXTURES/garbage" ]; then
  echo "<html><body>Bad gateway</body></html>"
  exit 0
fi
f="$FIXTURES/tags.$n.json"
[ -e "$f" ] || f="$(ls "$FIXTURES"/tags.*.json | sort -V | tail -1)"
cat "$f"
if [ -e "$FIXTURES/partial" ]; then
  echo "gh: HTTP 502: Bad Gateway" >&2
  exit 1
fi
EOF
chmod +x "$TMP/bin/gh"

# tag <name> <commit>: one entry of the tags endpoint's answer.
tag() {
  printf '{"name":"%s","commit":{"sha":"%s","url":"https://example.invalid/commits/%s"}}' "$1" "$2" "$2"
}
# page [tag...]: one page, a JSON array.
page() {
  local IFS=,
  printf '[%s]' "$*"
}

out=""
rc=0
written=""
fresh() {
  rm -rf "$TMP/f"
  mkdir -p "$TMP/f"
  : > "$TMP/f/output"
  printf '%s\n' "$SHA" > "$TMP/f/ref"
}
# check [<arguments of the script>]: the pin is $TMP/f/ref unless one is given.
check() {
  rc=0
  : > "$TMP/f/output"
  if [[ "$#" -eq 0 ]]; then
    set -- "$TMP/f/ref"
  fi
  out="$(PATH="$TMP/bin:$PATH" FIXTURES="$TMP/f" GITHUB_OUTPUT="$TMP/f/output" \
         RETRY_SECONDS=0 bash "$CHECK" "$@" 2>&1)" || rc=$?
  written="$(cat "$TMP/f/output")"
}
fail=0
report() {
  echo "FAIL: $1: exit $rc, wrote '$written':"
  printf '    %s\n' "${out//$'\n'/$'\n'    }"
  fail=1
}
# passes <case> <tag the output names> [<ERE the output must also match>]
passes() {
  if [[ "$rc" -ne 0 || "$written" != "tag=$2" ]] \
     || ! grep -qE -- "^TrustTunnelClient $SHA is $2, a stable release \(tags on it: .*$2" <<<"$out" \
     || ! grep -qE -- "${3:-}" <<<"$out"; then
    report "$1 (wanted exit 0, 'tag=$2' written and the tag named)"
  else
    echo "ok: $1"
  fi
}
# refused <case> <ERE the output must match>: exit 1, nothing written.
refused() {
  if [[ "$rc" -ne 1 || -n "$written" ]] || ! grep -qE -- "$2" <<<"$out"; then
    report "$1 (wanted exit 1, nothing written, output matching /$2/)"
  else
    echo "ok: $1"
  fi
}
calls() { cat "$TMP/f/tags.count" 2>/dev/null || echo 0; }
# asked <case> <times>
asked() {
  if [[ "$(calls)" -ne "$2" ]]; then
    echo "FAIL: $1: gh was called $(calls) times, wanted $2"
    fail=1
  fi
}

fresh
page "$(tag v1.1.6 "$OTHER")" "$(tag v1.1.7 "$SHA")" "$(tag v1.1.5 "$NEAR")" > "$TMP/f/tags.1.json"
check
passes "a stable tag on the pin" v1.1.7
asked "a stable tag on the pin" 1
if ! grep -qE -- '^api (--paginate .*)?repos/TrustTunnel/TrustTunnelClient/tags\?' "$TMP/f/calls.log" \
   || ! grep -qE -- '--paginate' "$TMP/f/calls.log"; then
  echo "FAIL: the tags were not asked for with --paginate from TrustTunnel/TrustTunnelClient:"
  cat "$TMP/f/calls.log"
  fail=1
fi

for pre in v1.3.0-rc.2 v1.3.0-beta.1 v1.3.0-alpha.1 v1.3.0-rc1 v1.3.0-pre; do
  fresh
  page "$(tag v1.1.7 "$NEAR")" "$(tag "$pre" "$SHA")" > "$TMP/f/tags.1.json"
  check
  refused "only $pre on the pin" "Core pin is a pre-release.*$SHA, tagged $pre, not vX\.Y\.Z"
done

fresh
page "$(tag v1.1.5-rc.6 "$SHA")" "$(tag v1.1.5 "$SHA")" > "$TMP/f/tags.1.json"
check
passes "an -rc and the final tag on one commit" v1.1.5 "tags on it: v1.1.5-rc.6, v1.1.5"

fresh
page "$(tag v1.1.5 "$SHA")" "$(tag v1.1.5-rc.6 "$SHA")" > "$TMP/f/tags.1.json"
check
passes "  in the other order" v1.1.5

fresh
page "$(tag v1.3.0-rc.1 "$SHA")" "$(tag v1.3.0-rc.2 "$SHA")" "$(tag v1.3.0 "$SHA")" > "$TMP/f/tags.1.json"
check
passes "  and two -rc tags beside it" v1.3.0

fresh
page "$(tag v1.3.0-rc.1 "$SHA")" "$(tag v1.3.0-rc.2 "$SHA")" > "$TMP/f/tags.1.json"
check
refused "two -rc tags on the pin are both named" "tagged v1\.3\.0-rc\.1, v1\.3\.0-rc\.2, not"

fresh
page "$(tag v1.1.7 "$NEAR")" "$(tag v1.1.6 "$OTHER")" > "$TMP/f/tags.1.json"
check
refused "no tag on the pin while other commits have tags" "Core pin is not a release.*$SHA has no tag in TrustTunnel/TrustTunnelClient"

fresh
page > "$TMP/f/tags.1.json"
check
refused "a repository with no tags at all" "Core pin is not a release.*has no tag"

fresh
page "$(tag v1.1.7 "$NEAR")" > "$TMP/f/tags.1.json"
check
refused "a stable tag on a commit differing in the last digit is not the pin's" "has no tag"

fresh
page "$(tag v1.1.7 "${SHA/#1/2}")" > "$TMP/f/tags.1.json"
check
refused "  nor one differing in the first digit" "has no tag"

fresh
page "$(tag v1.3.0-rc.2 "$SHA")" "$(tag v1.3.0 "$NEAR")" > "$TMP/f/tags.1.json"
check
refused "  and the next commit's stable tag does not rescue an -rc pin" "Core pin is a pre-release.*tagged v1\.3\.0-rc\.2, not"

fresh
page "$(tag v1.1.7 "${SHA}0")" > "$TMP/f/tags.1.json"
check
refused "  nor one that merely starts with the pin" "has no tag"

# Pages as --paginate prints them: the arrays, one after the other.
fresh
{ page "$(tag v1.1.6 "$OTHER")"; page "$(tag v1.1.7 "$SHA")"; } > "$TMP/f/tags.1.json"
check
passes "the stable tag only on page 2" v1.1.7

fresh
{ page "$(tag v1.3.0-rc.2 "$SHA")"; echo; page "$(tag v1.1.7 "$NEAR")"; echo; page "$(tag v1.3.0 "$SHA")"; echo; } > "$TMP/f/tags.1.json"
check
passes "  and on page 3, with the pages on lines of their own" v1.3.0

fresh
{ page "$(tag v1.3.0-rc.2 "$SHA")" "$(tag v1.1.6 "$OTHER")"; page "$(tag v1.1.7 "$NEAR")"; } > "$TMP/f/tags.1.json"
check
refused "an -rc on page 1 and nothing stable on page 2" "tagged v1\.3\.0-rc\.2, not"

# Not vX.Y.Z, so not a release: each of these alone on the pin.
for near in v1.3.0.1 1.3.0 v1.3 v1.3.0- release-1.3.0 V1.3.0 vv1.3.0 v1.3.0+build.5 v1.3.x v1..0 v1.3.0.rc2 v-1.3.0 v1.3.0-0 latest; do
  fresh
  page "$(tag "$near" "$SHA")" > "$TMP/f/tags.1.json"
  check
  # Anything that is not a letter, digit or hyphen stands for itself or for any
  # one character, which is enough here and needs no escaping.
  refused "'$near' alone on the pin" "Core pin is a pre-release.*tagged ${near//[^[:alnum:]-]/.}, not"
done

fresh
page "$(tag v1.3.0.1 "$SHA")" "$(tag release-1.3.0 "$SHA")" "$(tag v1.3.0-rc.2 "$SHA")" > "$TMP/f/tags.1.json"
check
refused "a pin with only near-misses on it" "Core pin is a pre-release"

# A stable name is stable wherever it is: big parts too.
fresh
page "$(tag v10.200.3000 "$SHA")" > "$TMP/f/tags.1.json"
check
passes "a stable tag with parts of several digits" v10.200.3000

# The answer cannot be had or read: nothing is known, nothing is published.
fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
touch "$TMP/f/fail"
check
refused "an API error is not a pass" "Cannot read the core's tags.*could not list the tags of TrustTunnel/TrustTunnelClient"
asked "an API error is not a pass" 3
grep -q "HTTP 403" <<<"$out" || { echo "FAIL: gh's own error was not shown:"; echo "$out"; fail=1; }

fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
echo 1 > "$TMP/f/fail"
check
passes "one failed attempt is retried" v1.1.7
asked "one failed attempt is retried" 2

fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
echo 2 > "$TMP/f/fail"
check
passes "  and two" v1.1.7
asked "  and two" 3

fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
echo 3 > "$TMP/f/fail"
check
refused "  but not three" "Cannot read the core's tags"
asked "  but not three" 3

fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
touch "$TMP/f/garbage"
check
refused "an answer that is not JSON is not a pass" "Cannot read the core's tags"
asked "an answer that is not JSON is not a pass" 3

fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
touch "$TMP/f/partial"
check
refused "a stable tag from a gh that then failed is not a pass" "Cannot read the core's tags"
asked "a stable tag from a gh that then failed is not a pass" 3

fresh
echo '{"message":"Not Found"}' > "$TMP/f/tags.1.json"
check
refused "an error document is not a pass" "Cannot read the core's tags"
asked "an error document is not a pass" 3

fresh
echo '[{"name":"v1.1.7"}]' > "$TMP/f/tags.1.json"
check
refused "a tag that names no commit is not the pin's" "has no tag"

fresh
: > "$TMP/f/tags.1.json"
check
refused "an empty answer is not a pass" "(has no tag|Cannot read the core's tags)"

# A pin that is not a commit SHA is refused before GitHub is asked anything.
for bad in main v1.1.7 170609c 170609C24CA865819FED68437B01C013049BC3FA \
           "${SHA}0" "${SHA%?}" "${SHA%?}g" "refs/tags/v1.1.7" "" "sha:$SHA"; do
  fresh
  page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
  printf '%s\n' "$bad" > "$TMP/f/ref"
  check
  refused "pin '$bad' is not a commit SHA" "Not a commit SHA.*40-hex commit SHA: '$bad'"
  asked "pin '$bad' is not a commit SHA" 0
done

fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
check "$TMP/f/no-such-file"
refused "a missing ref file" "No upstream_ref\.txt"
asked "a missing ref file" 0

# What a checkout or an editor leaves around the SHA is not part of it.
fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
printf '  %s \r\n\n' "$SHA" > "$TMP/f/ref"
check
passes "blanks and a CRLF around the pin" v1.1.7

# The tags are asked of the upstream repository, and UPSTREAM_REPO moves that.
fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
rc=0
out="$(PATH="$TMP/bin:$PATH" FIXTURES="$TMP/f" GITHUB_OUTPUT="$TMP/f/output" RETRY_SECONDS=0 \
       UPSTREAM_REPO=other/Core bash "$CHECK" "$TMP/f/ref" 2>&1)" || rc=$?
written="$(cat "$TMP/f/output")"
if [[ "$rc" -ne 0 ]] || ! grep -qE -- '(^| )repos/other/Core/tags\?' "$TMP/f/calls.log" \
   || ! grep -qE -- "^Core $SHA is v1\.1\.7" <<<"$out"; then
  report "UPSTREAM_REPO names the repository asked"
  cat "$TMP/f/calls.log"
else
  echo "ok: UPSTREAM_REPO names the repository asked"
fi

# Run outside Actions, with no GITHUB_OUTPUT.
fresh
page "$(tag v1.1.7 "$SHA")" > "$TMP/f/tags.1.json"
rc=0
out="$(env -u GITHUB_OUTPUT PATH="$TMP/bin:$PATH" FIXTURES="$TMP/f" RETRY_SECONDS=0 \
       bash "$CHECK" "$TMP/f/ref" 2>&1)" || rc=$?
if [[ "$rc" -ne 0 ]] || ! grep -qE -- "is v1\.1\.7, a stable release" <<<"$out"; then
  report "no GITHUB_OUTPUT is not an error"
else
  echo "ok: no GITHUB_OUTPUT is not an error"
fi

# With no argument the script reads scripts/upstream_ref.txt, whatever it pins.
fresh
repo_pin="$(tr -d '[:space:]' < "$ROOT/scripts/upstream_ref.txt")"
page "$(tag v9.9.9 "$repo_pin")" > "$TMP/f/tags.1.json"
rc=0
out="$(PATH="$TMP/bin:$PATH" FIXTURES="$TMP/f" GITHUB_OUTPUT="$TMP/f/output" RETRY_SECONDS=0 \
       bash "$CHECK" 2>&1)" || rc=$?
written="$(cat "$TMP/f/output")"
if [[ "$rc" -ne 0 || "$written" != "tag=v9.9.9" ]] \
   || ! grep -qF -- "TrustTunnelClient $repo_pin is v9.9.9" <<<"$out"; then
  report "the default is the repository's scripts/upstream_ref.txt"
else
  echo "ok: the default is the repository's scripts/upstream_ref.txt"
fi

rc=0
out="$(bash "$CHECK" a b 2>&1)" || rc=$?
if [[ "$rc" -ne 2 ]] || ! grep -q '^usage:' <<<"$out"; then
  report "two arguments are a usage error"
else
  echo "ok: two arguments are a usage error"
fi

exit "$fail"
