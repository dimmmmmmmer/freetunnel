#!/usr/bin/env bash
# Decide whether a run needs the desktop jobs, and write desktop=true or
# desktop=false to $GITHUB_OUTPUT. Run by the "desktop changes" job of Build,
# Tests and Security.
#
# The answer is false only for a pull request every one of whose files is
# Android's: anything under android/, its workflow and its two scripts. Those
# have their own workflow, android.yml, and nothing in the desktop ones builds
# or tests them. Everything else is true: a push, a tag, a dispatch, the weekly
# run, and a pull request that touches any other file. So is every doubt: a
# list that cannot be read, an empty one, one that may be incomplete, one that
# may be of another commit. A wrong true costs a desktop run; a wrong false
# would let a desktop change merge with nothing built or tested, so whatever
# goes wrong here ends in true, and nothing is false on a guess.
#
# A false does not keep the desktop checks from reporting: main's branch
# protection requires them by name. The workflows skip the work and still
# report every check under its name (see the changes job in tests.yml).
#
# The files come from the API: the pull request's whole diff against its base,
# not only what its last push changed. A rename counts by both its names:
# moving a desktop file into android/ takes it out of the desktop build. The
# list is checked against the pull request it came from, after it is read: the
# head must still be the commit this run is for, and the count must be the
# pull request's changed_files. The API lists at most 3000 files, and a longer
# list fails that count.
#
# usage: check-desktop-changes.sh
# env:   GITHUB_EVENT_NAME, GITHUB_REPOSITORY, GITHUB_OUTPUT and GH_TOKEN, as
#        Actions provides them; PR_NUMBER and PR_HEAD_SHA, from the
#        pull_request event.
set -uo pipefail # not -e: whatever fails has to end in desktop=true

out="${GITHUB_OUTPUT:?GITHUB_OUTPUT is not set}"
tmp=""
trap '[[ -n "$tmp" ]] && rm -rf "$tmp"' EXIT

decide() {
  echo "desktop=$1" >> "$out" || exit 1
  echo "desktop=$1: $2"
  exit 0
}
doubt() {
  echo "::warning title=Desktop CI runs in full::$1"
  decide true "$1"
}
# gh's error, on one line and short enough for an annotation.
first_line() {
  head -n 1 "$1" | cut -c 1-200
}

event="${GITHUB_EVENT_NAME:-}"
if [[ "$event" != pull_request ]]; then
  decide true "${event:-an unnamed} event, not a pull request: the desktop jobs always run"
fi

repo="${GITHUB_REPOSITORY:-}"
pr="${PR_NUMBER:-}"
head="${PR_HEAD_SHA:-}"
if [[ -z "$repo" || ! "$pr" =~ ^[1-9][0-9]*$ || ! "$head" =~ ^[0-9a-f]{40}$ ]]; then
  doubt "no repository, pull request number or head commit to ask about"
fi

tmp="$(mktemp -d)" || doubt "no temporary directory"

if ! gh api --paginate "repos/$repo/pulls/$pr/files?per_page=100" \
       > "$tmp/files.json" 2> "$tmp/err"; then
  doubt "could not list the files of #$pr: $(first_line "$tmp/err")"
fi
# Every page an array, every entry a filename. Anything else is not the list.
if ! jq -e -s 'all(.[]; type == "array") and all(.[][]; (.filename | type) == "string")' \
       "$tmp/files.json" > /dev/null 2>&1; then
  doubt "the file list of #$pr is not one the API returns"
fi
listed="$(jq -s '[.[][]] | length' "$tmp/files.json")" || doubt "could not count the files of #$pr"
if [[ "$listed" -eq 0 ]]; then
  doubt "#$pr lists no files"
fi

if ! gh api "repos/$repo/pulls/$pr" > "$tmp/pr.json" 2> "$tmp/err"; then
  doubt "could not read #$pr: $(first_line "$tmp/err")"
fi
now_head="$(jq -r '.head.sha // ""' "$tmp/pr.json" 2>/dev/null)" || now_head=""
changed="$(jq -r '.changed_files // ""' "$tmp/pr.json" 2>/dev/null)" || changed=""
if [[ "$now_head" != "$head" ]]; then
  doubt "#$pr is at ${now_head:-an unknown commit} now and this run is for $head: its file list may be another commit's"
fi
if [[ "$changed" != "$listed" ]]; then
  doubt "the list of #$pr has $listed files and the pull request ${changed:-an unknown number}"
fi

# The paths that are not Android's.
jq -r -s '[.[][] | .filename, (.previous_filename // empty)]
          | map(select((startswith("android/")
                        or . == ".github/workflows/android.yml"
                        or . == "scripts/prepare-trusttunnel-android.sh"
                        or . == "scripts/build-android-release.sh") | not))
          | .[]' "$tmp/files.json" > "$tmp/desktop" \
  || doubt "could not sort the files of #$pr"
desktop_files="$(wc -l < "$tmp/desktop")"
if [[ "$desktop_files" -gt 0 ]]; then
  decide true "#$pr touches desktop paths ($desktop_files): $(head -n 5 "$tmp/desktop" | paste -sd ' ' -)"
fi

echo "::notice title=Android-only change::all $listed files of #$pr are Android's, so the desktop jobs report without running"
decide false "all $listed files of #$pr are Android's"
