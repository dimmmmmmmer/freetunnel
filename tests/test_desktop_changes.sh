#!/usr/bin/env bash
# Tests for scripts/check-desktop-changes.sh, which tells Build, Tests and
# Security whether a pull request touches anything but Android's files. A
# false from it skips the work of every desktop check, so the cases here are
# mostly the ones that must not give one. A stand-in `gh` answers from
# fixtures; nothing reaches GitHub.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GATE="$ROOT/scripts/check-desktop-changes.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
HEAD=0123456789abcdef0123456789abcdef01234567
OTHER=89abcdef0123456789abcdef0123456789abcdef

# Answers `gh api --paginate repos/<repo>/pulls/<n>/files?per_page=100` with
# $FIXTURES/files.json and `gh api repos/<repo>/pulls/<n>` with
# $FIXTURES/pr.json, or fails like gh when <name>.fail exists. Logs every call.
mkdir -p "$TMP/bin"
cat > "$TMP/bin/gh" <<'EOF'
#!/usr/bin/env bash
set -eu
printf '%s\n' "$*" >> "$FIXTURES/calls.log"
what=""
for a in "$@"; do
  case "$a" in
    repos/*/pulls/*/files*) what=files ;;
    repos/*/pulls/*) what=pr ;;
  esac
done
[ -n "$what" ] || { echo "gh stand-in: unexpected call: $*" >&2; exit 2; }
if [ -e "$FIXTURES/$what.fail" ]; then
  echo "gh: HTTP 502: Bad Gateway" >&2
  exit 1
fi
cat "$FIXTURES/$what.json"
EOF
chmod +x "$TMP/bin/gh"

# file <path> [previous path]: one entry of the files list.
file() {
  if [[ $# -gt 1 ]]; then
    printf '{"filename":"%s","status":"renamed","previous_filename":"%s"}' "$1" "$2"
  else
    printf '{"filename":"%s","status":"modified"}' "$1"
  fi
}
# page [entry...]: one page of the list, as the API returns it.
page() {
  local IFS=,
  printf '[%s]' "$*"
}
# pr <head sha> <changed_files>
pr() {
  printf '{"number":7,"head":{"sha":"%s"},"changed_files":%s}\n' "$1" "$2"
}
# files [entry...]: a one-page list, and a pull request at HEAD of that size.
files() {
  page "$@" > "$TMP/f/files.json"
  pr "$HEAD" "$#" > "$TMP/f/pr.json"
}

fresh() {
  rm -rf "$TMP/f"
  mkdir -p "$TMP/f"
  : > "$TMP/f/calls.log"
}
out=""
rc=0
answer=""
# gate [VAR=value...]: runs the script for pull request 7 at HEAD, with any
# environment the case changes.
gate() {
  rc=0
  : > "$TMP/f/output"
  out="$(env PATH="$TMP/bin:$PATH" FIXTURES="$TMP/f" GITHUB_OUTPUT="$TMP/f/output" \
             GITHUB_REPOSITORY=owner/repo GITHUB_EVENT_NAME=pull_request \
             PR_NUMBER=7 PR_HEAD_SHA="$HEAD" GH_TOKEN=x "$@" \
             bash "$GATE" 2>&1)" || rc=$?
  answer="$(cat "$TMP/f/output")"
}
fail=0
# expect <case> <desktop=true|desktop=false> <ERE the output must match>
expect() {
  if [[ "$rc" -ne 0 || "$answer" != "$2" ]] || ! grep -qE -- "$3" <<<"$out"; then
    echo "FAIL: $1: exit $rc, wrote '$answer' (wanted 0 and '$2'), output must match /$3/:"
    printf '    %s\n' "${out//$'\n'/$'\n'    }"
    fail=1
  else
    echo "ok: $1"
  fi
}

ANDROID=(
  "$(file android/app/build.gradle)"
  "$(file android/app/src/main/java/org/freetunnel/android/MainActivity.java)"
  "$(file .github/workflows/android.yml)"
  "$(file scripts/prepare-trusttunnel-android.sh)"
  "$(file scripts/build-android-release.sh)"
)

fresh
files "${ANDROID[@]}"
gate
expect "only Android's files" desktop=false "all 5 files of #7 are Android's"
expect "  says so in an annotation" desktop=false "::notice title=Android-only change::"
grep -q -- '--paginate repos/owner/repo/pulls/7/files?per_page=100' "$TMP/f/calls.log" \
  || { echo "FAIL: the files were not asked for page by page:"; cat "$TMP/f/calls.log"; fail=1; }
grep -qx 'api repos/owner/repo/pulls/7' "$TMP/f/calls.log" \
  || { echo "FAIL: the pull request was not read:"; cat "$TMP/f/calls.log"; fail=1; }

for event in push schedule workflow_dispatch pull_request_target ""; do
  fresh
  files "${ANDROID[@]}"
  gate GITHUB_EVENT_NAME="$event"
  expect "a '$event' event runs the desktop jobs" desktop=true "not a pull request"
  [[ ! -s "$TMP/f/calls.log" ]] || { echo "FAIL: '$event' asked GitHub:"; cat "$TMP/f/calls.log"; fail=1; }
done

fresh
files "${ANDROID[@]}" "$(file src/core/UpdateChecker.cpp)"
gate
expect "one desktop file among Android's" desktop=true "#7 touches desktop paths \\(1\\): src/core/UpdateChecker.cpp"

# Near misses: each beside Android's files is a desktop change.
for path in android androidx/x.kt Android/app/build.gradle docs/android/x.md \
            .github/workflows/android.yml.orig .github/workflows/tests.yml \
            scripts/build-android-release.sh.bak scripts/check-desktop-changes.sh \
            .gitignore README.md; do
  fresh
  files "${ANDROID[@]}" "$(file "$path")"
  gate
  expect "$path is not Android's" desktop=true "desktop paths \\(1\\): $path"
done

fresh
files "$(file android/app/src/main/cpp/split.cpp src/core/SplitRules.cpp)"
gate
expect "a desktop file moved into android/" desktop=true "desktop paths \\(1\\): src/core/SplitRules.cpp"

fresh
files "$(file android/app/new.kt android/app/old.kt)" "${ANDROID[@]}"
gate
expect "a rename inside android/" desktop=false "all 6 files of #7 are Android's"

# Two pages, the way gh --paginate prints them; the desktop file on the second.
fresh
{ page "${ANDROID[@]}"; page "$(file android/b.kt)" "$(file CMakeLists.txt)"; } > "$TMP/f/files.json"
pr "$HEAD" 7 > "$TMP/f/pr.json"
gate
expect "a desktop file on the second page" desktop=true "desktop paths \\(1\\): CMakeLists.txt"

fresh
{ page "${ANDROID[@]}"; page "$(file android/b.kt)"; } > "$TMP/f/files.json"
pr "$HEAD" 6 > "$TMP/f/pr.json"
gate
expect "two pages of Android's files" desktop=false "all 6 files of #7 are Android's"

fresh
files
gate
expect "no files at all" desktop=true "#7 lists no files"

fresh
files "${ANDROID[@]}"
pr "$HEAD" 3500 > "$TMP/f/pr.json"
gate
expect "a list shorter than the pull request (the API stops at 3000)" desktop=true \
  "the list of #7 has 5 files and the pull request 3500"

fresh
files "${ANDROID[@]}"
pr "$OTHER" 5 > "$TMP/f/pr.json"
gate
expect "the head moved on after the run started" desktop=true "#7 is at $OTHER now and this run is for $HEAD"

fresh
files "${ANDROID[@]}"
touch "$TMP/f/files.fail"
gate
expect "the files cannot be listed" desktop=true "::warning title=Desktop CI runs in full::could not list the files of #7: gh: HTTP 502"

fresh
files "${ANDROID[@]}"
touch "$TMP/f/pr.fail"
gate
expect "the pull request cannot be read" desktop=true "could not read #7: gh: HTTP 502"

fresh
files "${ANDROID[@]}"
echo '{"message":"Not Found","status":"404"}' > "$TMP/f/files.json"
gate
expect "an answer that is not a list" desktop=true "is not one the API returns"

fresh
files "${ANDROID[@]}"
echo '[{"filename":"android/a.kt"},{"status":"removed"}]' > "$TMP/f/files.json"
gate
expect "an entry with no filename" desktop=true "is not one the API returns"

fresh
files "${ANDROID[@]}"
echo '{"head":{"sha":"'"$HEAD"'"}}' > "$TMP/f/pr.json"
gate
expect "a pull request with no changed_files" desktop=true "the pull request an unknown number"

for bad in "PR_NUMBER=" "PR_NUMBER=7x" "PR_HEAD_SHA=" "PR_HEAD_SHA=main" "GITHUB_REPOSITORY="; do
  fresh
  files "${ANDROID[@]}"
  gate "$bad"
  expect "$bad" desktop=true "no repository, pull request number or head commit"
done

exit "$fail"
