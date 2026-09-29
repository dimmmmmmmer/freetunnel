#!/usr/bin/env bash
# Does the .deb ask for everything the binaries actually need?
#
# The answer used to be no, and nothing said so: the package declared libgl1,
# the binary links libOpenGL.so.0, and libgl1 does not pull libopengl0 anywhere
# in its dependency tree. dpkg reported every dependency satisfied and the
# application died in the dynamic linker with a message that a .desktop launch
# throws away. It works on any machine that has a system Qt installed, which is
# every machine this is developed on.
#
# Usage: check-deb-deps.sh <unpacked package root>
# The root is the directory holding DEBIAN/control and opt/freetunnel.
set -euo pipefail

root="${1:?usage: check-deb-deps.sh <unpacked package root>}"
root="${root%/}"
# ldd prints a library found through $ORIGIN as an absolute path, so a bundled
# library is recognised by this prefix, not by the root as it was given.
case "$root" in /*) here="$root" ;; *) here="$(pwd -P)/$root" ;; esac
control="$root/DEBIAN/control"
[ -f "$control" ] || { echo "no control file at $control" >&2; exit 2; }

# Every shared library the shipped binaries, libraries and plugins resolve to
# something OUTSIDE the bundle. Those are the ones the system has to provide.
# ldd exits non-zero for anything that is not an ELF object — an icon, a
# .desktop file, a shell script — and the tree is full of those, so each file is
# asked separately and a refusal is not an error.
# Kept as "library<TAB>file that resolved it": naming only the package left a
# failure with no way to tell which of three hundred files was behind it.
pairs=$(
  find -L "$root" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) -print0 \
    | while IFS= read -r -d '' f; do
        { ldd "$f" 2>/dev/null || true; } | awk -v f="$f" '/=>/ && $3 != "" {print $3 "\t" f}'
      done \
    | awk -F'\t' -v a="$here/" -v r="$root/" 'index($1, a) != 1 && index($1, r) != 1' \
    | sort -u
)
mapfile -t external < <(cut -f1 <<< "$pairs" | sort -u)
[ -n "$pairs" ] || { echo "found no external libraries — is the root right?" >&2; exit 2; }

# Which package provides each. dpkg is asked by the path ldd printed and by the
# real path, because on a merged-/usr system each finds only some: ldd prints
# /lib/x86_64-linux-gnu/..., and dpkg recorded libc6 there but libx11-6 under
# /usr/lib. Asking by the real path alone lost libc6, zlib1g, libcom-err2 and
# the rest of /lib without a word, so the check could not fail for them.
# dpkg -S exits non-zero when it does not own a path.
# "package<TAB>library<TAB>first file that needs it", one line per library.
owners=$(for lib in "${external[@]}"; do
  file=$(awk -F'\t' -v l="$lib" '$1 == l {print $2; exit}' <<< "$pairs")
  { dpkg -S "$lib" "$(readlink -f "$lib")" 2>/dev/null || true; } | sed 's/:.*//' \
    | while IFS= read -r pkg; do printf '%s\t%s\t%s\n' "$pkg" "$lib" "${file#"$root"/}"; done
done | sort -u)
needed=$(cut -f1 <<< "$owners" | sort -u)

echo "::group::what the system has to provide, and a file that needs each"
awk -F'\t' '!seen[$1]++ {printf "  %-28s %s  (%s)\n", $1, $3, $2}' <<< "$owners"
echo "::endgroup::"

# One entry per Depends field, alternatives kept together ("pkexec|policykit-1").
declared=$(sed -n 's/^Depends: *//p' "$control" | tr ',' '\n' \
           | sed 's/([^)]*)//g; s/ //g' | grep -v '^$' | sort -u)
[ -n "$declared" ] || { echo "control declares no dependencies" >&2; exit 2; }

# A declared package covers everything it pulls in, so compare against the
# closure rather than the literal list. Of alternatives, the first one this
# release has is the one apt installs: Ubuntu 20.04 and Debian 11 have no pkexec
# package (it was split out of policykit-1 later), and asking apt-cache about it
# used to end this script with status 1 and no message.
closure=$(for group in $declared; do
  p=""
  for alt in ${group//|/ }; do
    if apt-cache show "$alt" >/dev/null 2>&1; then p="$alt"; break; fi
  done
  if [ -z "$p" ]; then
    echo "::error::no alternative of '$group' exists in this system's apt cache" >&2
    exit 1
  fi
  apt-cache depends --recurse --no-recommends --no-suggests --no-conflicts \
    --no-breaks --no-replaces --no-enhances "$p" 2>/dev/null \
    | grep -v '^ ' | grep -v '^|' | tr -d ' '
done | sort -u)

missing=""
for p in $needed; do
  printf '%s\n' "$closure" | grep -qx "$p" || missing="$missing $p"
done

if [ -n "$missing" ]; then
  echo "::error::the package needs these but does not depend on them:$missing"
  for p in $missing; do
    awk -F'\t' -v p="$p" '$1 == p {printf "  %s: %s needs %s\n", p, $3, $2; exit}' <<< "$owners"
  done
  echo "declared:"
  while IFS= read -r pkg; do echo "  $pkg"; done <<< "$declared"
  exit 1
fi
echo "deb dependencies cover every external library"
