#!/usr/bin/env bash
# Tests for scripts/check-android-release-unpublished.sh, which stops an
# android-X.Y.Z release whose version is already published. Only a 404 may let
# it through. A stand-in `gh` answers from fixtures; nothing reaches GitHub.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHECK="$ROOT/scripts/check-android-release-unpublished.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Prints $FIXTURES/answer as gh api -i would and exits with $FIXTURES/exit.
# Logs every call.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/gh" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
cat "$FIXTURES/answer" 2>/dev/null
[ -e "$FIXTURES/stderr" ] && cat "$FIXTURES/stderr" >&2
exit "$(cat "$FIXTURES/exit" 2>/dev/null || echo 0)"
EOF
chmod +x "$TMP/bin/gh"

indent() { sed 's/^/    /'; }

fail=0
n=0
# run <name> <expect: pass|fail> <status line or ''> <gh exit> [stderr] [grep for output]
# The tag is $TAG, android-1.0.0 unless a case sets it.
TAG=android-1.0.0
run() {
  local name="$1" expect="$2" line="$3" code="$4" stderr="${5:-}" want="${6:-}"
  n=$((n + 1))
  export FIXTURES="$TMP/case$n"
  mkdir -p "$FIXTURES"
  if [ -n "$line" ]; then
    printf '%s\r\nContent-Type: application/json\r\n\r\n{}\n' "$line" > "$FIXTURES/answer"
  fi
  echo "$code" > "$FIXTURES/exit"
  [ -z "$stderr" ] || printf '%s\n' "$stderr" > "$FIXTURES/stderr"
  local got=pass out
  out="$(PATH="$TMP/bin:$PATH" GITHUB_REPOSITORY=owner/repo \
         bash "$CHECK" "$TAG" 2>&1)" || got=fail
  if [ "$got" != "$expect" ]; then
    echo "not ok $n - $name: expected $expect, got $got"
    indent <<<"$out"
    fail=1
    return
  fi
  if [ -n "$want" ] && ! grep -qF -- "$want" <<<"$out"; then
    echo "not ok $n - $name: output lacks '$want'"
    indent <<<"$out"
    fail=1
    return
  fi
  if ! grep -qxF "api -i repos/owner/repo/releases/tags/$TAG" "$FIXTURES/calls.log"; then
    echo "not ok $n - $name: gh was not asked for this tag's release"
    fail=1
    return
  fi
  echo "ok $n - $name"
}

run "not published (gh exits 1 on a 404)" pass "HTTP/2.0 404 Not Found" 1 \
    "gh: Not Found (HTTP 404)" "No release is published for android-1.0.0 yet."
run "not published, HTTP/1.1" pass "HTTP/1.1 404 Not Found" 1
run "published" fail "HTTP/2.0 200 OK" 0 "" "::error title=Already published::"
run "published, HTTP/1.1" fail "HTTP/1.1 200 OK" 0
run "forbidden" fail "HTTP/2.0 403 Forbidden" 1 "gh: Forbidden (HTTP 403)" \
    "GitHub answered '403': gh: Forbidden (HTTP 403)"
run "rate limited" fail "HTTP/2.0 429 Too Many Requests" 1
run "server error" fail "HTTP/2.0 500 Internal Server Error" 1
run "a redirect is not a 404" fail "HTTP/2.0 301 Moved Permanently" 0
run "no answer at all" fail "" 1 "error connecting to api.github.com" \
    "GitHub answered 'nothing': error connecting to api.github.com"
run "404 in the body only, not the status" fail "HTTP/2.0 200 OK 404" 0
run "a status line without a reason phrase" pass "HTTP/2 404" 1
TAG=android-12.34.56
run "another tag is asked about by its own name" pass "HTTP/2.0 404 Not Found" 1 "" \
    "No release is published for android-12.34.56 yet."
TAG=android-1.0.0

# No gh at all: never passes. The PATH has the other tools the script uses and
# no gh (a runner has gh in /usr/bin, so a trimmed system PATH would not do).
n=$((n + 1))
mkdir -p "$TMP/nogh"
for tool in mktemp head rm; do
  ln -s "$(command -v "$tool")" "$TMP/nogh/$tool"
done
if out="$(PATH="$TMP/nogh" GITHUB_REPOSITORY=owner/repo "$BASH" "$CHECK" android-1.0.0 2>&1)"; then
  echo "not ok $n - no gh: passed"
  fail=1
elif ! grep -qF "GitHub answered 'nothing'" <<<"$out"; then
  echo "not ok $n - no gh: failed for another reason"
  indent <<<"$out"
  fail=1
else
  echo "ok $n - no gh: fails"
fi

# Without its arguments or environment it refuses to run, with gh ready to
# answer 404, so nothing but the missing input can make it fail.
export FIXTURES="$TMP/refuse"
mkdir -p "$FIXTURES"
printf 'HTTP/2.0 404 Not Found\r\n\r\n{}\n' > "$FIXTURES/answer"
echo 1 > "$FIXTURES/exit"
n=$((n + 1))
if env -u GITHUB_REPOSITORY PATH="$TMP/bin:$PATH" bash "$CHECK" android-1.0.0 >/dev/null 2>&1; then
  echo "not ok $n - no GITHUB_REPOSITORY: passed"
  fail=1
else
  echo "ok $n - no GITHUB_REPOSITORY: fails"
fi
n=$((n + 1))
if GITHUB_REPOSITORY=owner/repo PATH="$TMP/bin:$PATH" bash "$CHECK" >/dev/null 2>&1; then
  echo "not ok $n - no tag: passed"
  fail=1
else
  echo "ok $n - no tag: fails"
fi

exit "$fail"
