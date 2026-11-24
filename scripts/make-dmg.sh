#!/usr/bin/env bash
set -euo pipefail
APP="${1:?expected FreeTunnel.app}"
RW=out/rw.dmg
# Sized from what goes in, rather than a fixed 200m that the bundle
# would one day outgrow (create then fails with "No space left on
# device"): the .app and the volume icon, a quarter again for the
# filesystem's own bookkeeping, and 20 MB for the journal.
content_kb=$(( $(du -sk "$APP" | cut -f1) + $(du -sk "$APP/Contents/Resources/logo.icns" | cut -f1) ))
rw_kb=$(( content_kb * 5 / 4 + 20 * 1024 ))
echo "rw.dmg: ${rw_kb} KB for ${content_kb} KB of content"
# Every disk device out/rw.dmg is attached as: the first whole-disk
# node of each of its entries in hdiutil info. There can be more than
# one. A create that fails leaves the image it made attached, with no
# volume mounted, and the attach below then adds a second entry beside
# it; detaching only the device our own attach printed let a build
# wait out six attempts on an image that was still attached as the
# other one:
#
#   hdiutil: create failed - Resource busy
#   ##[error]out/rw.dmg is still attached (as /dev/disk7) after six detach attempts
#   (hdiutil info: out/rw.dmg on /dev/disk5, nothing mounted)
rw_devices() {
  hdiutil info 2>/dev/null | awk -v img="$(cd out && pwd -P)/rw.dmg" '
    /^image-path/ { sub(/^image-path[[:space:]]*:[[:space:]]*/, ""); cur = ($0 == img); first = 1; next }
    /^=+$/ { cur = 0; next }
    cur && first && $1 ~ /^\/dev\/disk[0-9]+$/ { print $1; first = 0 }'
}
# Spotlight or an earlier attempt can keep this working image busy.
# Release its devices before retrying the create.
for attempt in 1 2 3 4 5; do
  for dev in $(rw_devices); do
    hdiutil detach "$dev" -force >/dev/null 2>&1 || true
  done
  if hdiutil create -volname "FreeTunnel" -srcfolder "$APP" -size "${rw_kb}k" \
       -ov -format UDRW "$RW"; then
    break
  fi
  if [ "$attempt" = 5 ]; then
    echo "::error::hdiutil create failed five times — not a transient"
    exit 1
  fi
  echo "hdiutil create failed (attempt $attempt) — retrying in 15 s"
  sleep 15
done
# The system chooses a unique mount point if another FreeTunnel DMG is
# open. Only operate on the volume returned by this attach.
MOUNT=$(hdiutil attach -nobrowse -readwrite -noverify -noautofsck -plist "$RW" | python3 -Ic '
import plistlib, sys
[mount] = [item["mount-point"] for item in plistlib.loads(sys.stdin.buffer.read())["system-entities"] if "mount-point" in item]
print(mount)')
cp "$APP/Contents/Resources/logo.icns" "$MOUNT/.VolumeIcon.icns"
SetFile -a C "$MOUNT/.VolumeIcon.icns"
ln -sf /Applications "$MOUNT/Applications"
bless --folder "$MOUNT" --openfolder "$MOUNT" --icon "$MOUNT/.VolumeIcon.icns" || true
# Spotlight/fseventsd can hold the volume briefly — retry the detach,
# but judge the OUTCOME rather than the exit status. The first detach can
# report "Resource busy" and still complete a moment later, after which
# every retry fails with "No such file or directory" — which is the state
# we were asking for. The old loop read that as failure, and its
# unguarded -force on the last attempt then killed a release build under
# set -eux for having nothing left to do:
#
#   hdiutil: couldn't eject "disk5" - Resource busy
#   hdiutil: detach failed - No such file or directory   (x4, then -force)
#   ##[error]Process completed with exit code 1
for i in 1 2 3 4 5; do
  [ -d "$MOUNT" ] || break
  if hdiutil detach "$MOUNT" >/dev/null 2>&1; then break; fi
  if [ "$i" = 5 ]; then
    hdiutil detach "$MOUNT" -force >/dev/null 2>&1 || true
  fi
  sleep 5
done
if [ -d "$MOUNT" ]; then
  echo "::error::DMG volume still mounted at $MOUNT after five detach attempts"
  exit 1
fi
# The mount point disappearing is not the same as the image being free.
# Unmounting the volume and releasing the disk device behind it are two
# steps, and the second does not always follow. On a failed build the
# first detach reported an error, the volume went a moment later, and
# hdiutil info went on listing out/rw.dmg for the whole thirty seconds
# this used to wait. Convert then failed five times with
#
#   hdiutil: convert failed - Resource temporarily unavailable
#
# So detach the devices themselves, forcing them on the last attempt,
# until the image is not attached at all, and stop with their state if
# they never let go.
for i in 1 2 3 4 5 6; do
  devs=$(rw_devices)
  [ -n "$devs" ] || break
  for dev in $devs; do
    if [ "$i" = 6 ]; then
      hdiutil detach "$dev" -force >/dev/null 2>&1 || true
    else
      hdiutil detach "$dev" >/dev/null 2>&1 || true
    fi
  done
  sleep 5
done
devs=$(rw_devices)
if [ -n "$devs" ]; then
  echo "::error::out/rw.dmg is still attached (as ${devs//$'\n'/ }) after six detach attempts"
  hdiutil info || true
  exit 1
fi
for i in 1 2 3 4 5; do
  if hdiutil convert "$RW" -format UDZO -imagekey zlib-level=9 \
       -o out/freetunnel-macos-universal.dmg; then
    break
  fi
  if [ "$i" = 5 ]; then
    echo "::error::hdiutil convert failed five times — not a transient"
    exit 1
  fi
  echo "hdiutil convert failed (attempt $i) — retrying in 10 s; what has it open:"
  lsof "$RW" || true
  rm -f out/freetunnel-macos-universal.dmg
  sleep 10
done
rm -f "$RW"
