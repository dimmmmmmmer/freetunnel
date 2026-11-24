#!/usr/bin/env bash
# Recover interrupted drafts and verify public releases on retry. A changed
# public release requires an explicit maintainer decision.
set -euo pipefail
tag="${1:?expected a release tag}"
commit="${2:?expected the desktop release commit}"
assets="${3:?expected the staged files}"
export LC_ALL=C

[[ "$tag" == v[0-9]* ]] || { echo "Unexpected desktop release tag: $tag" >&2; exit 1; }
notes="${4:?expected desktop release notes}"
git fetch --no-tags origin "refs/tags/$tag"
test "$(git rev-parse 'FETCH_HEAD^{commit}')" = "$commit"
if ! draft="$(gh release view "$tag" --json isDraft --jq .isDraft)"; then
  gh release create "$tag" --verify-tag --draft --title "$tag" --notes-file "$notes"
  draft=true
fi

if [[ "$draft" == true ]]; then
  gh release upload "$tag" "$assets"/* --clobber
fi

# Compare every asset, including the detached signature. Validate the server
# response separately so an API failure cannot look like an empty file list.
remote="$(gh release view "$tag" --json assets --jq \
  '.assets[] | [.digest, .name] | @tsv')"
expected="$(cd "$assets" && sha256sum -- * | awk '{print "sha256:" $1 "\t" $2}' | sort)"
if [[ "$(sort <<<"$remote")" != "$expected" ]]; then
  echo "Public or staged assets do not match the signed files for $tag" >&2
  exit 1
fi
if [[ "$draft" == true ]]; then
  gh release edit "$tag" --draft=false
fi
