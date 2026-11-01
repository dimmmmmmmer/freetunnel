#!/usr/bin/env bash
# Exercise release ordering and annotated tags without network access.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
trap 'rm -rf "$fixture"' EXIT
mkdir "$fixture/bin"
cat > "$fixture/bin/git" <<'GIT'
#!/usr/bin/env bash
set -euo pipefail
[[ "$1" == ls-remote && "$2" == --tags ]]
cat "$TAGS_FILE"
GIT
chmod +x "$fixture/bin/git"
export PATH="$fixture/bin:$PATH" TAGS_FILE="$fixture/tags" GITHUB_OUTPUT="$fixture/output"
a=1111111111111111111111111111111111111111
b=2222222222222222222222222222222222222222
c=3333333333333333333333333333333333333333
d=4444444444444444444444444444444444444444
cat > "$TAGS_FILE" <<TAGS
$a refs/tags/v1.1.7
$c refs/tags/v1.1.11
$b refs/tags/v1.1.11^{}
$d refs/tags/v1.3.0-rc.2
TAGS
check() {
  printf '%s\n' "$1" > "$fixture/pin"
  : > "$GITHUB_OUTPUT"
  bash "$root/scripts/latest-core-release.sh" "$fixture/pin"
  grep -qx "newer=$2" "$GITHUB_OUTPUT"
}
check "$a" true
grep -qx 'tag=v1.1.11' "$GITHUB_OUTPUT"
grep -qx "commit=$b" "$GITHUB_OUTPUT"
check "$b" false
check "$d" false
check 5555555555555555555555555555555555555555 false
printf '%s refs/tags/v1.3.0\n' "$c" >> "$TAGS_FILE"
check "$d" true
printf 'bad pin\n' > "$fixture/pin"
if bash "$root/scripts/latest-core-release.sh" "$fixture/pin"; then
  echo 'invalid pin was accepted' >&2; exit 1
fi
printf '%s\n' "$a" > "$fixture/pin"
printf '%s refs/tags/v1.3.0-rc.2\n' "$d" > "$TAGS_FILE"
if bash "$root/scripts/latest-core-release.sh" "$fixture/pin"; then
  echo 'prerelease-only upstream was accepted' >&2; exit 1
fi
: > "$TAGS_FILE"
if bash "$root/scripts/latest-core-release.sh" "$fixture/pin"; then
  echo 'empty upstream was accepted' >&2; exit 1
fi
printf 'Core release detection: OK\n'
