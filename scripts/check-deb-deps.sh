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
control="$root/DEBIAN/control"
[ -f "$control" ] || { echo "no control file at $control" >&2; exit 2; }

# Every shared library the shipped binaries, libraries and plugins resolve to
# something OUTSIDE the bundle. Those are the ones the system has to provide.
# ldd exits non-zero for anything that is not an ELF object — an icon, a
# .desktop file, a shell script — and the tree is full of those, so each file is
# asked separately and a refusal is not an error.
# shellcheck disable=SC2016  # $0 is the inner sh's argument, not ours to expand
mapfile -t external < <(
  find -L "$root" -type f \( -name '*.so' -o -name '*.so.*' -o -perm -u+x \) -print0 \
    | xargs -0 -r -n1 sh -c 'ldd "$1" 2>/dev/null || true' _ \
    | awk '/=>/ {print $3}' \
    | grep -v '^$' \
    | grep -v "^$root" \
    | sort -u
)
[ "${#external[@]}" -gt 0 ] || { echo "found no external libraries — is the root right?" >&2; exit 2; }

# Which package provides each, following symlinks: dpkg -S wants the real path.
# dpkg -S exits non-zero when it does not own a path, and it owns none of the
# bundled Qt libraries — which is exactly why they are not in the answer.
needed=$(for lib in "${external[@]}"; do readlink -f "$lib"; done | sort -u \
         | { xargs -r dpkg -S 2>/dev/null || true; } | sed 's/:.*//' | sort -u)

declared=$(sed -n 's/^Depends: *//p' "$control" | tr ',' '\n' \
           | sed 's/(.*)//; s/|.*//; s/ //g' | grep -v '^$' | sort -u)
[ -n "$declared" ] || { echo "control declares no dependencies" >&2; exit 2; }

# A declared package covers everything it pulls in, so compare against the
# closure rather than the literal list.
closure=$(for p in $declared; do
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
  echo "declared:"
  while IFS= read -r pkg; do echo "  $pkg"; done <<< "$declared"
  exit 1
fi
echo "deb dependencies cover every external library"
