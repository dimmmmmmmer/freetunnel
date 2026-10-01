#!/usr/bin/env bash
# Tests for scripts/check-release-ci.sh, the release job's check that Tests and
# Security passed on the commit being released. That job only runs for a v*
# tag, so without these the script's first real run would be the release it is
# there to protect. A stand-in `gh` answers from fixtures; nothing reaches GitHub.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GATE="$ROOT/scripts/check-release-ci.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
SHA=0123456789abcdef0123456789abcdef01234567

# Answers `gh api -X GET repos/<repo>/actions/workflows/<wf>/runs -f head_sha=…`
# with $FIXTURES/<wf>.<n>.json on its n-th call for that workflow, or with the
# last fixture once they run out, and logs every call.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
wf=""
for a in "$@"; do
  case "$a" in repos/*/actions/workflows/*/runs) wf="${a%/runs}"; wf="${wf##*/}" ;; esac
done
n=$(( $(cat "$FIXTURES/$wf.count" 2>/dev/null || echo 0) + 1 ))
echo "$n" > "$FIXTURES/$wf.count"
if [ -e "$FIXTURES/$wf.fail" ]; then
  echo "gh: HTTP 403: Resource not accessible by integration" >&2
  exit 1
fi
f="$FIXTURES/$wf.$n.json"
[ -e "$f" ] || f="$(ls "$FIXTURES/$wf".*.json | sort -V | tail -1)"
cat "$f"
EOF
chmod +x "$TMP/bin/gh"

# run <status> <conclusion as JSON> <event> <id>
run() {
  printf '{"status":"%s","conclusion":%s,"event":"%s","html_url":"https://example.invalid/runs/%s"}' \
    "$1" "$2" "$3" "$4"
}
# runs [run...] > fixture
runs() {
  local IFS=,
  printf '{"total_count":%d,"workflow_runs":[%s]}\n' "$#" "$*"
}

out=""
rc=0
fresh() {
  rm -rf "$TMP/f"
  mkdir -p "$TMP/f"
  # Security passed unless a case says otherwise.
  runs "$(run completed '"success"' push 90)" > "$TMP/f/security.yml.1.json"
}
gate() {
  rc=0
  out="$(PATH="$TMP/bin:$PATH" FIXTURES="$TMP/f" GITHUB_REPOSITORY=owner/repo \
         GITHUB_REF_NAME=v9.9.9 WAIT_MINUTES="${WAIT:-5}" POLL_SECONDS=0 \
         bash "$GATE" "$SHA" tests.yml security.yml 2>&1)" || rc=$?
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
calls() { cat "$TMP/f/$1.count"; }

fresh
runs "$(run completed '"success"' push 1)" > "$TMP/f/tests.yml.1.json"
gate
expect "both passed on a push" 0 "Every required workflow passed"
grep -q "repos/owner/repo/actions/workflows/tests.yml/runs .*head_sha=$SHA" "$TMP/f/calls.log" \
  || { echo "FAIL: the runs were not asked for by this commit:"; cat "$TMP/f/calls.log"; fail=1; }

fresh
runs "$(run completed '"success"' pull_request 2)" > "$TMP/f/tests.yml.1.json"
gate
expect "a pull request's run does not count" 1 "tests.yml has no run on $SHA"

fresh
runs "$(run completed '"failure"' push 3)" > "$TMP/f/tests.yml.1.json"
gate
expect "a failed run stops the release" 1 "no tests.yml run on $SHA succeeded"
expect "  and is named" 1 "failure \(push\): https://example.invalid/runs/3"

fresh
runs "$(run completed '"success"' push 4)" > "$TMP/f/tests.yml.1.json"
runs "$(run completed '"cancelled"' push 5)" > "$TMP/f/security.yml.1.json"
gate
expect "the second workflow counts too" 1 "no security.yml run on $SHA succeeded"

fresh
runs "$(run in_progress null push 6)" > "$TMP/f/tests.yml.1.json"
runs "$(run completed '"success"' push 6)" > "$TMP/f/tests.yml.2.json"
gate
expect "waits for a run still going, then passes" 0 "Waiting for tests.yml"
[[ "$(calls tests.yml)" -eq 2 ]] || { echo "FAIL: asked $(calls tests.yml) times, wanted 2"; fail=1; }

fresh
runs "$(run completed '"failure"' push 7)" "$(run completed '"success"' schedule 8)" \
  > "$TMP/f/tests.yml.1.json"
gate
expect "one success is enough beside a failure" 0 "tests.yml passed"

fresh
runs "$(run completed '"failure"' push 9)" "$(run queued null push 10)" > "$TMP/f/tests.yml.1.json"
runs "$(run completed '"failure"' push 9)" "$(run completed '"failure"' push 10)" \
  > "$TMP/f/tests.yml.2.json"
gate
expect "a failure beside a queued run waits for it" 1 "runs/10"
[[ "$(calls tests.yml)" -eq 2 ]] || { echo "FAIL: asked $(calls tests.yml) times, wanted 2"; fail=1; }

fresh
runs "$(run in_progress null push 11)" > "$TMP/f/tests.yml.1.json"
WAIT=0 gate
expect "gives up on a run that outlasts the wait" 1 "tests.yml had not finished"

fresh
runs > "$TMP/f/tests.yml.1.json"
gate
expect "no run at all is not a pass" 1 "gh workflow run tests.yml --ref v9.9.9"

fresh
runs "$(run completed '"success"' push 12)" > "$TMP/f/tests.yml.1.json"
touch "$TMP/f/tests.yml.fail"
gate
expect "an API error is not a pass" 1 "could not list the tests.yml runs"
[[ "$(calls tests.yml)" -eq 3 ]] || { echo "FAIL: asked $(calls tests.yml) times, wanted 3"; fail=1; }

exit "$fail"
