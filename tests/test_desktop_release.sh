#!/usr/bin/env bash
# Exercise the tag-only release path offline. The git remote is real and local.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cd "$tmp"
mkdir -p artifacts bin
desktop=(freetunnel-linux-x86_64.deb freetunnel-macos-universal.dmg
  freetunnel-windows-x86_64-Setup.exe freetunnel-x86_64.AppImage)
helpers=(freetunnel-helper-linux-x86_64.tar.gz freetunnel-helper-macos-universal.tar.gz)
for name in "${desktop[@]}" "${helpers[@]}" gcc-runtime-source.tar; do
  printf '%s\n' "$name" > "artifacts/$name"
done
bash "$root/scripts/stage-desktop-release.sh" 1.4.0 artifacts staged
manifest=staged/SHA256SUMS.txt
test "$(head -1 "$manifest")" = '#version=1.4.0'
diff <(printf '%s\n' "${desktop[@]}" | sort) <(tail -n +2 "$manifest" | awk '{print $2}' | sort)
diff <(printf '%s\n' SHA256SUMS.txt "${desktop[@]}" | sort) <(cd staged && printf '%s\n' * | sort)
mv "artifacts/${desktop[1]}" missing
if bash "$root/scripts/stage-desktop-release.sh" 1.4.0 artifacts incomplete; then
  echo 'Staged a release without its macOS installer' >&2; exit 1
fi

git init --quiet --bare origin.git
git init --quiet -b main checkout
cd checkout
git -c user.name=test -c user.email=test@example.invalid commit --quiet --allow-empty -m initial
git remote add origin "$tmp/origin.git"
commit="$(git rev-parse HEAD)"
export MOCK_RELEASE="$tmp/remote-release"
cat > "$tmp/bin/gh" <<'MOCK'
#!/usr/bin/env bash
set -euo pipefail
test "$1" = release
[[ "$3" == v1.4.0 ]]
case "$2" in
  view)
    state="$(cat "$MOCK_RELEASE/state")"
    if [[ "$5" == isDraft ]]; then
      printf '%s\n' "$state"
    else
      test "$5" = assets
      cd "$MOCK_RELEASE/assets"
      sha256sum -- * | awk '{print "sha256:" $1 "\t" $2}'
    fi ;;
  create)
    [[ "$*" == *' --draft '* ]]
    mkdir "$MOCK_RELEASE" "$MOCK_RELEASE/assets"
    echo true > "$MOCK_RELEASE/state" ;;
  upload)
    test "$(cat "$MOCK_RELEASE/state")" = true
    shift 3
    for file in "$@"; do
      [[ "$file" == --clobber ]] || cp "$file" "$MOCK_RELEASE/assets/"
    done ;;
  edit)
    [[ "$*" == *' --draft=false'* ]]
    echo false > "$MOCK_RELEASE/state" ;;
  *) exit 1 ;;
esac
MOCK
chmod +x "$tmp/bin/gh"
export PATH="$tmp/bin:$PATH"
git -c user.name=test -c user.email=test@example.invalid tag -a v1.4.0 "$commit" -m desktop
git push origin refs/tags/v1.4.0
printf signature > "$tmp/staged/SHA256SUMS.txt.sig"
printf notes > notes.md
desktop_release() { bash "$root/scripts/publish-staged-release.sh" v1.4.0 "$1" "$tmp/staged" notes.md; }
if bash "$root/scripts/publish-staged-release.sh" helper-1.4.0 "$commit" "$tmp/staged" notes.md; then
  echo 'Accepted a separate helper release' >&2; exit 1
fi
test -z "$(git ls-remote --tags origin refs/tags/helper-1.4.0)"
test ! -e "$MOCK_RELEASE"
desktop_release "$commit"
test "$(cat "$MOCK_RELEASE/state")" = false
desktop_release "$commit"   # Verify the already-public files.
echo true > "$MOCK_RELEASE/state"
rm "$MOCK_RELEASE/assets/${desktop[1]}"
desktop_release "$commit"   # Recover an interrupted draft.
test -s "$MOCK_RELEASE/assets/${desktop[1]}"
echo different > "$MOCK_RELEASE/assets/${desktop[0]}"
if desktop_release "$commit"; then echo 'Overwrote mismatched public assets' >&2; exit 1; fi
git -c user.name=test -c user.email=test@example.invalid commit --quiet --allow-empty -m next
if desktop_release "$(git rev-parse HEAD)"; then echo 'Released a different desktop commit' >&2; exit 1; fi
