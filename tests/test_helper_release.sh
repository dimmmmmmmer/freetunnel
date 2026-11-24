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
for channel in desktop helpers; do
  manifest="staged/$channel/SHA256SUMS.txt"
  test "$(head -1 "$manifest")" = '#version=1.4.0'
  if [[ "$channel" == desktop ]]; then files=("${desktop[@]}"); else files=("${helpers[@]}"); fi
  diff <(printf '%s\n' "${files[@]}" | sort) <(tail -n +2 "$manifest" | awk '{print $2}' | sort)
  test ! -e "staged/$channel/gcc-runtime-source.tar"
done
mv "artifacts/${helpers[1]}" missing
if bash "$root/scripts/stage-desktop-release.sh" 1.4.0 artifacts incomplete; then
  echo 'Staged a release without its macOS helper' >&2; exit 1
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
[[ "$3" == helper-1.4.0 || "$3" == v1.4.0 ]]
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
    if [[ "$3" == helper-* ]]; then [[ "$*" == *' --latest=false '* ]]; fi
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
    if [[ "$3" == helper-* ]]; then [[ "$*" == *' --latest=false' ]]; fi
    echo false > "$MOCK_RELEASE/state" ;;
  *) exit 1 ;;
esac
MOCK
chmod +x "$tmp/bin/gh"
export PATH="$tmp/bin:$PATH"
printf signature > "$tmp/staged/helpers/SHA256SUMS.txt.sig"
publish() { bash "$root/scripts/publish-staged-release.sh" helper-1.4.0 "$1" "$tmp/staged/helpers"; }

publish "$commit"
test "$(cat "$MOCK_RELEASE/state")" = false
test "$(git --git-dir="$tmp/origin.git" rev-parse refs/tags/helper-1.4.0)" = "$commit"
publish "$commit"   # Already public: validate without uploading.
echo true > "$MOCK_RELEASE/state"
rm "$MOCK_RELEASE/assets/${helpers[1]}"
publish "$commit"   # Resume an interrupted draft.
test -s "$MOCK_RELEASE/assets/${helpers[1]}"
echo different > "$MOCK_RELEASE/assets/${helpers[0]}"
if publish "$commit"; then echo 'Overwrote mismatched public assets' >&2; exit 1; fi
git -c user.name=test -c user.email=test@example.invalid commit --quiet --allow-empty -m next
if publish "$(git rev-parse HEAD)"; then echo 'Reused a helper tag on another commit' >&2; exit 1; fi


export MOCK_RELEASE="$tmp/desktop-release"
git -c user.name=test -c user.email=test@example.invalid tag -a v1.4.0 "$commit" -m desktop
git push origin refs/tags/v1.4.0
printf signature > "$tmp/staged/desktop/SHA256SUMS.txt.sig"
printf notes > notes.md
desktop_release() { bash "$root/scripts/publish-staged-release.sh" v1.4.0 "$1" "$tmp/staged/desktop" notes.md; }
desktop_release "$commit"
desktop_release "$commit"
test "$(cat "$MOCK_RELEASE/state")" = false
if desktop_release "$(git rev-parse HEAD)"; then echo 'Released a different desktop commit' >&2; exit 1; fi
