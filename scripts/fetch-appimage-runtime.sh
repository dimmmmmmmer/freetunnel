#!/usr/bin/env bash
# Fetch the AppImage runtime, the small program at the start of the AppImage,
# and write down which one it is.
#
# AppImage/type2-runtime publishes it as its "continuous" release, which is
# replaced in place whenever upstream changes. appimagetool would download that
# itself and record nothing, so neither the notices nor anyone reading them
# could tell which runtime, and with it which libfuse (LGPL-2.1, linked
# statically), an AppImage carries. Here the runtime is downloaded first,
# checked against the SHA-256 that GitHub gives for the release asset, and
# recorded with the commit the release was made from. build.yml then hands this
# same file to appimagetool (LDAI_RUNTIME_FILE), so the record names what the
# AppImage carries, and THIRD_PARTY_NOTICES.md points to the record.
#
# Not pinned: the only fixed release upstream has, 20251108, lacks a later fix to
# the runtime (8f39b89e, extraction directories created with mode 0700). To pin
# it, mirror runtime-x86_64 in a build-tools release, as build.yml does with
# linuxdeploy, and check its digest here.
#
# usage: fetch-appimage-runtime.sh <runtime file> <record file>
# GH_TOKEN, when set, authenticates the GitHub API request.
set -euo pipefail

runtime="${1:?usage: fetch-appimage-runtime.sh <runtime file> <record file>}"
record="${2:?usage: fetch-appimage-runtime.sh <runtime file> <record file>}"
repo=AppImage/type2-runtime
asset="runtime-x86_64"

auth=()
[ -z "${GH_TOKEN:-}" ] || auth=(-H "Authorization: Bearer $GH_TOKEN")
release="$(curl -fsSL --retry 3 "${auth[@]}" -H 'Accept: application/vnd.github+json' \
  "https://api.github.com/repos/$repo/releases/tags/continuous")"
fields="$(python3 -I -c '
import json, sys
release = json.load(sys.stdin)
assets = [a for a in release.get("assets", []) if a.get("name") == sys.argv[1]]
if len(assets) != 1:
    sys.exit("the release has no single asset named " + sys.argv[1])
print(release.get("target_commitish") or "-", assets[0].get("digest") or "-",
      assets[0]["browser_download_url"])
' "$asset" <<< "$release")"
read -r commit digest url <<< "$fields"

if ! [[ "$commit" =~ ^[0-9a-f]{40}$ ]]; then
  echo "::error::$repo's continuous release names no commit (target_commitish: $commit)" >&2
  exit 1
fi
case "$digest" in
  sha256:*) sha="${digest#sha256:}" ;;
  *) echo "::error::GitHub gives no SHA-256 for $asset in $repo's continuous release" >&2; exit 1 ;;
esac

curl -fsSL --retry 3 -o "$runtime" "$url"
if ! echo "$sha  $runtime" | sha256sum -c - >/dev/null; then
  echo "::error::$runtime does not match the digest GitHub gives for it: upstream replaced the continuous release during the build. Run it again." >&2
  exit 1
fi

mkdir -p "$(dirname "$record")"
{
  echo "# The AppImage runtime at the start of this AppImage, written when it was"
  echo "# built (scripts/fetch-appimage-runtime.sh); see THIRD_PARTY_NOTICES.md."
  echo "# It links libfuse and squashfuse as that commit's"
  echo "# scripts/common/install-dependencies.sh downloads them, libfuse with the"
  echo "# commit's patches/libfuse, and musl, zstd, zlib and mimalloc from the Alpine"
  echo "# release its scripts/docker/Dockerfile names."
  printf 'file\t%s\n' "$asset"
  printf 'commit\t%s\n' "$commit"
  printf 'sha256\t%s\n' "$sha"
  printf 'source\thttps://github.com/%s/tree/%s\n' "$repo" "$commit"
} > "$record"
echo "AppImage runtime: $asset of $repo at $commit, sha256 $sha"
