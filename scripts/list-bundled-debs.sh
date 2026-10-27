#!/usr/bin/env bash
# Write which Debian packages the shared libraries in a Linux bundle come from,
# with their exact versions and source packages, and give each package its
# Debian copyright file in the bundle.
#
# linuxdeploy copies the libraries that FreeTunnel and Qt need from the build
# container (Ubuntu 20.04) into the AppDir, and the copyright files of most of
# their packages with them, but not of all: it found none for libdbus-1-3,
# libsystemd0, libgcc-s1 and a few more. THIRD_PARTY_NOTICES.md points to these
# files, and its source offer to the package versions written here. Libraries
# no package owns, Qt's own and the ICU that comes with Qt, are left out: with
# QT_ROOT_DIR set, as install-qt-action sets it, a library of that name in Qt's
# lib directory counts as Qt's even where a system package has one too.
#
# usage: list-bundled-debs.sh <AppDir> <output file>
# Run on the machine the AppDir was deployed on: it asks that machine's dpkg.
set -euo pipefail

appdir="${1:?usage: list-bundled-debs.sh <AppDir> <output file>}"
out="${2:?usage: list-bundled-debs.sh <AppDir> <output file>}"
arch="$(dpkg --print-architecture)"

# owners <library file name>: the packages of this architecture that own a file
# of that name in the system's library directories, one per line.
owners() {
  { dpkg-query -S "*/$1" 2>/dev/null || true; } \
    | awk -F': ' '$1 !~ /^diversion / && $2 ~ /^(\/usr)?\/lib\/[^\/]*-linux-gnu\/[^\/]*$/ { print $1 }' \
    | tr ',' '\n' | sed 's/^ *//; s/ *$//' \
    | awk -v arch="$arch" 'index($0, ":") == 0 || $0 ~ (":" arch "$")'
}

packages=""
unowned=""
shopt -s nullglob
for lib in "$appdir"/usr/lib/*.so*; do
  [ -f "$lib" ] || continue
  name="$(basename "$lib")"
  if [ -n "${QT_ROOT_DIR:-}" ] && [ -e "$QT_ROOT_DIR/lib/$name" ]; then
    unowned="$unowned $name"
    continue
  fi
  found="$(owners "$name")"
  if [ -n "$found" ]; then
    packages="$packages"$'\n'"$found"
  else
    unowned="$unowned $name"
  fi
done
packages="$(printf '%s\n' "$packages" | sed '/^$/d' | sort -u)"
[ -n "$packages" ] || { echo "no library in $appdir/usr/lib comes from a Debian package" >&2; exit 1; }

mkdir -p "$(dirname "$out")"
{
  echo "# The Debian packages whose shared libraries this bundle carries in"
  echo "# usr/lib, unmodified, as: package, version, source package, source version."
  echo "# Written when the bundle was built (scripts/list-bundled-debs.sh); see"
  echo "# THIRD_PARTY_NOTICES.md. Each source package is at"
  echo "# https://launchpad.net/ubuntu/+source/<source package>/<source version>,"
  echo "# or, for a version from a PPA, in that PPA."
  while IFS= read -r p; do
    # Source is empty when the source package has the binary package's name and
    # version, "name" when only the name differs, and "name (version)" otherwise.
    line="$(dpkg-query -W -f='${binary:Package}\t${Version}\t${Source}\n' "$p")"
    pkg="$(cut -f1 <<< "$line")"
    ver="$(cut -f2 <<< "$line")"
    src="$(cut -f3 <<< "$line")"
    src_name="${src%% (*}"
    src_ver="$ver"
    case "$src" in *" ("*")") src_ver="${src##* (}"; src_ver="${src_ver%)}" ;; esac
    [ -n "$src_name" ] || src_name="${pkg%%:*}"
    printf '%s\t%s\t%s\t%s\n' "$pkg" "$ver" "$src_name" "$src_ver"
  done <<< "$packages"
} > "$out"

# The copyright files linuxdeploy did not copy. A package's doc directory can be
# a link to another package's (libgcc-s1's to gcc's), hence -e and cp -L.
missing=""
while IFS= read -r p; do
  bare="${p%%:*}"
  dest="$appdir/usr/share/doc/$bare/copyright"
  [ -e "$dest" ] && continue
  if [ -e "/usr/share/doc/$bare/copyright" ]; then
    mkdir -p "$(dirname "$dest")"
    cp -L "/usr/share/doc/$bare/copyright" "$dest"
    echo "added the copyright file of $bare"
  else
    missing="$missing $bare"
  fi
done <<< "$packages"

echo "bundled Debian packages ($(wc -l <<< "$packages")), written to $out:"
sed '/^#/d; s/^/  /' "$out"
[ -z "$unowned" ] || echo "not from a Debian package:$unowned"
if [ -n "$missing" ]; then
  echo "::warning::no Debian copyright file on this system for:$missing"
fi
