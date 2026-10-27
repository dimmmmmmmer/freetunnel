#!/usr/bin/env bash
# Put the source of the GCC runtime libraries that a Linux bundle carries
# (libstdc++.so.6 and libgcc_s.so.1) into one tarball, for the release.
#
# They come from ppa:ubuntu-toolchain-r/test: installing g++-13 from it upgrades
# libstdc++6 and libgcc-s1 to the newest build the PPA has, which can be a later
# GCC than 13 (in October 2026, a GCC 16 snapshot). The PPA keeps only its
# newest version, and Launchpad removes the files of one that is replaced about
# a day later. These libraries are GPL-3.0 with the GCC Runtime Library
# Exception, so the release carries their exact source rather than pointing to
# the PPA, and THIRD_PARTY_NOTICES.md points to it there. The files are as the
# PPA publishes them (the .dsc and the tarballs it names), and apt checks them
# against the PPA's signed index. A tar, not the files themselves: GitHub
# renames release assets with a '~' in the name, which the .dsc names its files
# by.
#
# usage: archive-gcc-runtime-source.sh <output .tar>
# Run on the machine the bundle was deployed on, with the PPA's deb-src line
# enabled (add-apt-repository -s).
set -euo pipefail

out="${1:?usage: archive-gcc-runtime-source.sh <output .tar>}"
mkdir -p "$(dirname "$out")"
out="$(cd "$(dirname "$out")" && pwd)/$(basename "$out")"
arch="$(dpkg --print-architecture)"

# The source package and version of each library's binary package, read as
# list-bundled-debs.sh reads them, so the tarball is what that list names.
sources=""
for p in libstdc++6 libgcc-s1; do
  line="$(dpkg-query -W -f='${Version}\t${Source}\n' "$p:$arch")"
  ver="$(cut -f1 <<< "$line")"
  src="$(cut -f2 <<< "$line")"
  src_name="${src%% (*}"
  src_ver="$ver"
  case "$src" in *" ("*")") src_ver="${src##* (}"; src_ver="${src_ver%)}" ;; esac
  [ -n "$src_name" ] || src_name="$p"
  sources="$sources"$'\n'"$src_name=$src_ver"
done
sources="$(printf '%s\n' "$sources" | sed '/^$/d' | sort -u)"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
while IFS= read -r spec; do
  name="${spec%%=*}"
  ver="${spec#*=}"
  # Launchpad is sometimes slow to answer, as add-apt-repository is in build.yml.
  for attempt in 1 2 3; do
    (cd "$work" && apt-get source --download-only "$spec") && break
    if [ "$attempt" -eq 3 ]; then
      echo "::error::could not download the source of $spec" >&2
      exit 1
    fi
    echo "apt-get source $spec failed (attempt $attempt), retrying..."
    sleep 20
  done
  # A .dsc is named without the version's epoch.
  dsc="$work/${name}_${ver#*:}.dsc"
  [ -f "$dsc" ] || { echo "::error::apt-get source wrote no $(basename "$dsc")" >&2; exit 1; }
done <<< "$sources"

(cd "$work" && find . -maxdepth 1 -type f -printf '%P\n' | sort | tar -cf "$out" -T -)
echo "the source of the GCC runtime ($(tr '\n' ' ' <<< "$sources")), in $out:"
(cd "$work" && find . -maxdepth 1 -type f -printf '%P\n' | sort | xargs sha256sum | sed 's/^/  /')
