#!/usr/bin/env bash
# Usage: check-android-release-unpublished.sh <tag>
#
# Succeeds only when GitHub says no release is published for <tag>. A
# published version's files never change: apps that installed it, and anyone
# who checked its SHA256SUMS-android.txt, rely on that. The release workflow
# runs this in its first job, to fail fast, and again right before it
# publishes, because a partial re-run ("Re-run job" on a later job) skips the
# first job and would otherwise replace the files of a live release.
#
# GitHub's "get a release by tag" finds published releases only, never a
# draft, so a release whose earlier attempt stopped halfway is still finished
# by a re-run. Any answer but 404 stops the release: this check never passes
# on a doubt. Needs gh with GH_TOKEN, and GITHUB_REPOSITORY.
set -u

tag="${1:?usage: check-android-release-unpublished.sh <tag>}"
repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is not set}"
out="$(mktemp)"
err="$(mktemp)"
trap 'rm -f "$out" "$err"' EXIT

# -i puts the status line first. Its exit status is not used: gh fails on a
# 404, and the status line is what decides.
gh api -i "repos/$repo/releases/tags/$tag" > "$out" 2> "$err" || true
status=""
read -r _ status _ < "$out" || true
status="${status%$'\r'}"
case "$status" in
  404)
    echo "No release is published for $tag yet."
    ;;
  200)
    echo "::error title=Already published::$tag is already published, and its files never change. Release a new version instead."
    exit 1
    ;;
  *)
    echo "::error title=Cannot tell whether $tag is published::GitHub answered '${status:-nothing}': $(head -n 1 "$err")"
    exit 1
    ;;
esac
