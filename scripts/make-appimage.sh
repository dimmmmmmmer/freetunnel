#!/usr/bin/env bash
# AppDir was fully deployed in the build container at the supported glibc floor.
# Package those exact bytes without resolving any libraries on this runner.
set -euo pipefail
test -x AppDir/AppRun
test -x AppDir/usr/bin/FreeTunnel
record=AppDir/usr/share/doc/freetunnel/appimage-runtime.txt
test "$(sha256sum runtime-x86_64 | cut -d' ' -f1)" = "$(awk '$1 == "sha256" {print $2}' "$record")"
mksquashfs AppDir filesystem.squashfs -all-root -noappend -comp gzip -no-progress
image=out/freetunnel-x86_64.AppImage
cat runtime-x86_64 filesystem.squashfs > "$image"
chmod +x "$image"
test "$("$image" --appimage-offset)" = "$(stat -c %s runtime-x86_64)"
