#!/usr/bin/env bash
# Refuse a desktop release whose core is not a stable upstream release. Run by
# the core-pin job of .github/workflows/build.yml on a v* tag; the release job
# needs that job, so a tag it refuses publishes nothing.
#
# A v* release ships the TrustTunnel core it was built with, and that core is
# the commit in scripts/upstream_ref.txt. While a new core is being tried the
# pin may sit on its -rc, -beta or -alpha, or on a commit with no tag at all,
# and Build, Tests and Security treat it like any other pin. A release is the
# one thing that must not ship it: people install a release as the tested,
# finished thing, and a pre-release core is neither.
#
# So the pin has to be a commit that TrustTunnelClient tagged vX.Y.Z. The tags
# are asked of the GitHub API, which gives each tag's commit already peeled,
# lightweight and annotated alike, so it compares with the pin as it is. One
# stable tag is enough: a commit can carry both v1.3.0-rc.2 and v1.3.0.
#
# Stricter than "not an -rc or -beta" on purpose. A commit with no tag is not a
# release either, and neither is one whose only tags are not of the form
# vX.Y.Z. Whatever cannot be shown to be a stable release is refused. So is an
# answer that cannot be read: the API is asked three times, and when it still
# does not answer, nothing is known about the pin, so the release stops and the
# job can be re-run.
#
# usage: check-core-pin-release.sh [<upstream_ref.txt>]
#        (default: scripts/upstream_ref.txt)
# env:   GH_TOKEN, as Actions provides it, or the maintainer's own gh login;
#        UPSTREAM_REPO (default TrustTunnel/TrustTunnelClient);
#        RETRY_SECONDS between the three attempts (default 10);
#        GITHUB_OUTPUT, if set, gets tag=vX.Y.Z once the pin passes.
set -euo pipefail
# [0-9] and the rest mean ASCII here, whatever the runner's locale.
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [[ "$#" -gt 1 ]]; then
  echo "usage: check-core-pin-release.sh [<upstream_ref.txt>]" >&2
  exit 2
fi
ref_file="${1:-$ROOT/scripts/upstream_ref.txt}"
repo="${UPSTREAM_REPO:-TrustTunnel/TrustTunnelClient}"
retry="${RETRY_SECONDS:-10}"
project="${repo##*/}"

fail() {
  echo "::error title=$1::$2"
  exit 1
}

if [[ ! -f "$ref_file" ]]; then
  fail "No upstream_ref.txt" "$ref_file does not exist, so there is no core pin to check."
fi
sha="$(tr -d '[:space:]' < "$ref_file")"
sha_re='^[0-9a-f]{40}$'
if ! [[ "$sha" =~ $sha_re ]]; then
  fail "Not a commit SHA" "upstream_ref.txt is not a 40-hex commit SHA: '$sha'"
fi

# The names of the tags that point at $sha, one per line, none when there are
# none. --paginate prints one JSON array per page and jq reads them as a stream.
# Equality, not a prefix: a tag on the commit next to the pin is not the pin.
# Three attempts, as in check-release-ci.sh: an API hiccup should not cost a
# release, and a real permission error fails the same way three times. A gh
# that fails after printing some pages counts as failed, whatever it printed.
tags_on_pin() {
  local json names attempt
  for attempt in 1 2 3; do
    if json="$(gh api --paginate "repos/$repo/tags?per_page=100")" \
       && names="$(jq -r --arg sha "$sha" \
                     '.[] | select(.commit.sha == $sha) | .name' <<<"$json")"; then
      printf '%s' "$names"
      return 0
    fi
    if [[ "$attempt" -lt 3 ]]; then
      sleep "$retry"
    fi
  done
  return 1
}

tags=""
if ! tags="$(tags_on_pin)"; then
  fail "Cannot read the core's tags" "could not list the tags of $repo, so whether $sha is a stable release is unknown. Re-run this job."
fi
names=()
if [[ -n "$tags" ]]; then
  mapfile -t names <<<"$tags"
fi

stable_re='^v[0-9]+\.[0-9]+\.[0-9]+$'
stable=""
for name in "${names[@]}"; do
  if [[ "$name" =~ $stable_re ]]; then
    stable="$name"
    break
  fi
done

if [[ "${#names[@]}" -gt 0 ]]; then
  printf -v listed '%s, ' "${names[@]}"
  listed="${listed%, }"
fi

if [[ -n "$stable" ]]; then
  if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "tag=$stable" >> "$GITHUB_OUTPUT"
  fi
  echo "$project $sha is $stable, a stable release (tags on it: $listed)."
  exit 0
fi

what="move the pin to a commit $repo tagged vX.Y.Z (CONTRIBUTING.md, Bumping the upstream core), then tag a new release commit."
if [[ "${#names[@]}" -gt 0 ]]; then
  fail "Core pin is a pre-release" "scripts/upstream_ref.txt pins $project $sha, tagged $listed, not vX.Y.Z. A v* release ships the core it was built with, so it waits for a stable one: $what"
fi
fail "Core pin is not a release" "scripts/upstream_ref.txt pins $project $sha. $sha has no tag in $repo, so it is not a release. A v* release ships the core it was built with, so it waits for a stable one: $what"
