#!/usr/bin/env bash
# Is there a stable TrustTunnelClient release newer than the core we pin?
#
# Run daily by .github/workflows/core-update.yml, which opens a pull request
# moving scripts/upstream_ref.txt to it; and by hand, to see where the pin
# stands. It only reads: tags come from `git ls-remote`, which needs no token.
#
# "Stable" is the rule scripts/check-core-pin-release.sh applies to a release:
# a tag named vX.Y.Z, nothing after the numbers. The newest one by version is
# compared with the pin:
#   - a pin on a stable tag is behind when the newest version is higher;
#   - a pin on a pre-release (vX.Y.Z-rc.N and the like) is behind once vX.Y.Z,
#     or anything higher, is out;
#   - a pin on a commit with no tag at all is somebody's experiment, and is left
#     alone: this says so and reports nothing to do.
#
# usage: latest-core-release.sh [<upstream_ref.txt>]
#        (default: scripts/upstream_ref.txt)
# env:   UPSTREAM_URL (default https://github.com/TrustTunnel/TrustTunnelClient.git);
#        GITHUB_OUTPUT, if set, gets newer=true|false, and with newer=true
#        tag=vX.Y.Z, commit=<sha> and pinned=<the pin's tag, or empty>.
set -euo pipefail
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF_FILE="${1:-$ROOT/scripts/upstream_ref.txt}"
URL="${UPSTREAM_URL:-https://github.com/TrustTunnel/TrustTunnelClient.git}"

pin="$(tr -d '[:space:]' < "$REF_FILE")"
if [[ ! "$pin" =~ ^[0-9a-f]{40}$ ]]; then
  echo "latest-core-release: $REF_FILE does not hold a full commit SHA: '$pin'" >&2
  exit 1
fi

# "<commit> <tag>" for every tag, annotated ones peeled to their commit.
tags="$(git ls-remote --tags "$URL" | awk '
  { name = $2; sub("^refs/tags/", "", name) }
  name ~ /\^\{\}$/ { sub(/\^\{\}$/, "", name); peeled[name] = $1; next }
  { plain[name] = $1 }
  END { for (n in plain) print (n in peeled ? peeled[n] : plain[n]), n }')"
if [[ -z "$tags" ]]; then
  echo "latest-core-release: no tags read from $URL" >&2
  exit 1
fi

stable_re='^v[0-9]+\.[0-9]+\.[0-9]+$'
newest_tag="$(awk '{print $2}' <<< "$tags" | grep -E "$stable_re" | sort -V | tail -n 1 || true)"
if [[ -z "$newest_tag" ]]; then
  echo "latest-core-release: $URL has no vX.Y.Z tag" >&2
  exit 1
fi
newest_commit="$(awk -v t="$newest_tag" '$2 == t {print $1}' <<< "$tags")"

# The tags on the pinned commit: a stable one if there is one, else any.
pin_tags="$(awk -v c="$pin" '$1 == c {print $2}' <<< "$tags" | sort -V)"
pin_stable="$(grep -E "$stable_re" <<< "$pin_tags" | tail -n 1 || true)"
pin_any="$(tail -n 1 <<< "$pin_tags")"

# Whether version $1 is strictly higher than version $2 (both without the v).
higher() { [[ "$1" != "$2" && "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" == "$1" ]]; }

newer=false
pinned=""
if [[ "$pin" == "$newest_commit" ]]; then
  pinned="$newest_tag"
  echo "The pin is $newest_tag, the newest stable release."
elif [[ -n "$pin_stable" ]]; then
  pinned="$pin_stable"
  if higher "${newest_tag#v}" "${pin_stable#v}"; then
    newer=true
  else
    echo "The pin is $pin_stable; nothing newer is out (newest stable: $newest_tag)."
  fi
elif [[ -n "$pin_any" ]]; then
  # A pre-release: its own release, vX.Y.Z, counts as newer.
  pinned="$pin_any"
  base="${pin_any#v}"
  base="${base%%-*}"
  if [[ "${newest_tag#v}" == "$base" ]] || higher "${newest_tag#v}" "$base"; then
    newer=true
  else
    echo "The pin is the pre-release $pin_any; its release is not out yet (newest stable: $newest_tag)."
  fi
else
  echo "The pin $pin carries no tag upstream; leaving it alone (newest stable: $newest_tag)."
fi

if [[ "$newer" == true ]]; then
  echo "Newer stable release: $newest_tag ($newest_commit); the pin is ${pinned:-untagged} ($pin)."
fi

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
  {
    echo "newer=$newer"
    if [[ "$newer" == true ]]; then
      echo "tag=$newest_tag"
      echo "commit=$newest_commit"
      echo "pinned=$pinned"
    fi
  } >> "$GITHUB_OUTPUT"
fi
