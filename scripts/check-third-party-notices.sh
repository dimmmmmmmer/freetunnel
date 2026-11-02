#!/usr/bin/env bash
# THIRD_PARTY_NOTICES.md against the repository: every license text it links is
# in licenses/ and every file in licenses/ is linked from it, and the pinned
# versions of what the downloads contain are the ones it names. So a bump of
# Qt, the core, QHotkey, QWindowKit, Wintun, NSIS, Inter or an Android
# dependency that leaves the notices behind fails here, rather than shipping
# notices for something else.
#
# It checks names, not licenses: a bump still needs someone to read the new
# version's license and update the entry and its text.
#
# usage: check-third-party-notices.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

notices=THIRD_PARTY_NOTICES.md
fail=0
complain() {
  echo "third-party-notices: $*" >&2
  fail=1
}

[ -f "$notices" ] || { echo "third-party-notices: no $notices" >&2; exit 1; }

# The license texts: linked as [text](licenses/<group>/<file>).
linked="$(grep -oE '\]\(licenses/[^)#]+\)' "$notices" | sed -E 's/^\]\(//; s/\)$//' \
          | grep -v '/$' | sort -u || true)"
[ -n "$linked" ] || complain "$notices links no license texts"
while IFS= read -r path; do
  [ -n "$path" ] || continue
  [ -f "$path" ] || complain "$notices links $path, which does not exist"
done <<< "$linked"
while IFS= read -r path; do
  grep -qxF "$path" <<< "$linked" || complain "$path is not linked from $notices"
done < <(find licenses -type f | sort)
for group in common desktop android; do
  [ -d "licenses/$group" ] || complain "licenses/$group is missing"
done

# need <what> <text>: the notices must contain the text.
need() {
  if [ -z "$2" ]; then
    complain "could not read $1 from the repository"
  elif ! grep -qF -- "$2" "$notices"; then
    complain "$notices does not name $1: $2"
  fi
}

need "the Qt version (QT_VER in .github/workflows/build.yml)" \
  "Qt $(sed -nE "s/^[[:space:]]*QT_VER:[[:space:]]*'([^']+)'.*/\1/p" .github/workflows/build.yml | head -n 1)"
# The desktop and the Android app run different cores, so these two are checked
# on the "Source, desktop" lines: the Android lines name other commits, and a
# desktop line left behind must not pass because the old commit is still
# named there.
core="$(tr -d '[:space:]' < scripts/upstream_ref.txt)"
[ -z "$core" ] || core="Source, desktop: <https://github.com/TrustTunnel/TrustTunnelClient/tree/$core>"
need "the TrustTunnelClient commit (scripts/upstream_ref.txt)" "$core"
nlc="$(sed -nE 's/^NLC_COMMIT="([0-9a-f]{40})"$/\1/p' scripts/export-patched-boringssl.sh)"
[ -z "$nlc" ] || nlc="Source, desktop: <https://github.com/AdguardTeam/NativeLibsCommon/tree/$nlc>"
need "the NativeLibsCommon commit (scripts/export-patched-boringssl.sh)" "$nlc"
need "the QHotkey commit (CMakeLists.txt)" \
  "$(sed -nE 's/.*GIT_TAG ([0-9a-f]{40}).*/\1/p' CMakeLists.txt | head -n 1)"
need "the QWindowKit commit (cmake/QWindowKit.cmake)" \
  "$(sed -nE 's/.*GIT_TAG ([0-9a-f]{40}).*/\1/p' cmake/QWindowKit.cmake | head -n 1)"
need "the Wintun version (.github/workflows/build.yml)" \
  "Wintun $(sed -nE 's/.*wintun-([0-9][0-9.]*[0-9])\.zip.*/\1/p' .github/workflows/build.yml | head -n 1)"
need "the NSIS version (.github/workflows/build.yml)" \
  "NSIS $(sed -nE "s/.*nsis = '([^']+)'.*/\1/p" .github/workflows/build.yml | head -n 1)"
need "the Inter version (assets/fonts/inter/SOURCE)" \
  "Inter $(sed -nE 's/^version=([0-9.]+)$/\1/p' assets/fonts/inter/SOURCE)"
need "the Inter release commit (assets/fonts/inter/SOURCE)" \
  "$(sed -nE 's/^tag_commit=([0-9a-f]{40})$/\1/p' assets/fonts/inter/SOURCE)"

# Android: every Maven coordinate the app's build implements, and the AAR.
coordinates="$(sed -nE "s/^[[:space:]]*implementation[[:space:]]+'([^']+)'.*/\1/p" android/app/build.gradle)"
[ -n "$coordinates" ] || complain "found no implementation dependencies in android/app/build.gradle"
while IFS= read -r coordinate; do
  [ -n "$coordinate" ] || continue
  need "an Android dependency (android/app/build.gradle)" "$coordinate"
done <<< "$coordinates"
# (its name is the one line `def coreAar = '...'`, from which build.gradle also reads the core's version)
need "the TrustTunnel AAR (android/app/build.gradle)" \
  "$(sed -nE "s/^def coreAar = '([^']+\.aar)'.*/\1/p" android/app/build.gradle | head -n 1)"

if [ "$fail" -ne 0 ]; then
  echo "third-party-notices: update $notices (and licenses/) for the change" >&2
  exit 1
fi
echo "third-party notices: every license text linked, every pinned version named"
