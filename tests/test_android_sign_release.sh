#!/usr/bin/env bash
# Tests for scripts/sign-android-release.sh, which signs the Android release APK
# with Build Tools only and checks it before anything is published. Android CI
# runs the script itself with real Build Tools and a throwaway keystore; these
# cases are the refusals that run cannot reach. Stand-ins for zipalign,
# apksigner and aapt2 print what the real ones do (Build Tools 35), and the
# "APK" is a text file that tells them what to report.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SIGN="$ROOT/scripts/sign-android-release.sh"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
PIN=09b3aaa6a6d7b43fbedbf555a2d88f2226b04c3bb0aeb78f77b139fe12a2e3ae
OTHER=5f7c1e0d2a3b4c5d6e7f8091a2b3c4d5e6f708192a3b4c5d6e7f8091a2b3c4d5
STOREPASS="store-pass-$RANDOM-never-printed"
KEYPASS="key-pass-$RANDOM-never-printed"

mkdir -p "$TMP/bt"
# zipalign -c ... <apk>: refuses an APK with an 'align: bad' line.
cat > "$TMP/bt/zipalign" <<'EOF'
#!/usr/bin/env bash
printf 'zipalign %s\n' "$*" >> "$FIXTURES/calls.log"
apk="${*: -1}"
echo "Verifying alignment of $apk (4)..."
if grep -qx 'align: bad' "$apk"; then
  echo "  176128 lib/arm64-v8a/libtrusttunnel.so (BAD - 4096)"
  echo "Verification FAILED"
  exit 1
fi
echo "  180224 lib/arm64-v8a/libtrusttunnel.so (OK)"
echo "Verification succesful"
EOF
# aapt2 dump badging <apk>: the APK's 'badging: ' lines.
cat > "$TMP/bt/aapt2" <<'EOF'
#!/usr/bin/env bash
printf 'aapt2 %s\n' "$*" >> "$FIXTURES/calls.log"
if [[ -e "$FIXTURES/aapt2-fails" ]]; then
  echo "W/ziparchive: Unable to open '$3': Invalid file" >&2
  exit 1
fi
sed -n 's/^badging: //p' "$3"
EOF
# apksigner sign: copies the APK and adds 'signed-by: <$FIXTURES/cert>', once
# it has read both passwords from the variables named; writes an .idsig, as the
# real one does, unless v4 is off. apksigner verify: refuses an unsigned APK.
cat > "$TMP/bt/apksigner" <<'EOF'
#!/usr/bin/env bash
printf 'apksigner %s\n' "$*" >> "$FIXTURES/calls.log"
cmd="$1"; shift
apk="${*: -1}"
case "$cmd" in
  sign)
    ks="" ks_pass="" key_pass="" out=""
    while [[ "$#" -gt 1 ]]; do
      case "$1" in
        --ks) ks="$2"; shift 2 ;;
        --ks-pass) ks_pass="$2"; shift 2 ;;
        --key-pass) key_pass="$2"; shift 2 ;;
        --out) out="$2"; shift 2 ;;
        *) shift ;;
      esac
    done
    [[ -f "$ks" ]] || { echo "Failed to load signer: $ks not found" >&2; exit 1; }
    var="${ks_pass#env:}"; [[ "$ks_pass" == env:* && "${!var:-}" == "$(cat "$FIXTURES/storepass")" ]] \
      || { echo "Failed to load signer: keystore password was incorrect" >&2; exit 1; }
    var="${key_pass#env:}"; [[ "$key_pass" == env:* && "${!var:-}" == "$(cat "$FIXTURES/keypass")" ]] \
      || { echo "Failed to load signer: key password was incorrect" >&2; exit 1; }
    [[ ! -e "$FIXTURES/sign-fails" ]] || { echo "Failed to sign" >&2; exit 1; }
    {
      grep -vx 'align: bad' "$apk"
      [[ ! -e "$FIXTURES/sign-misaligns" ]] || echo 'align: bad'
      echo "signed-by: $(cat "$FIXTURES/cert")"
    } > "$out"
    if [[ -e "$FIXTURES/sign-rebadges" ]]; then
      sed -i "s/versionName='[^']*'/versionName='9.9.9'/" "$out"
    fi
    if [[ " $* " != *" --v4-signing-enabled false "* ]]; then
      echo "v4" > "$out.idsig"
    fi
    ;;
  verify)
    signer="$(sed -n 's/^signed-by: //p' "$apk")"
    if [[ -z "$signer" || -e "$FIXTURES/verify-fails" ]]; then
      echo "DOES NOT VERIFY"
      echo "ERROR: Missing META-INF/MANIFEST.MF"
      exit 1
    fi
    if [[ " $* " == *" --print-certs "* ]]; then
      echo "Signer #1 certificate DN: CN=FreeTunnel, O=FreeTunnel"
      echo "Signer #1 certificate SHA-256 digest: $signer"
      echo "Signer #1 certificate SHA-1 digest: 0123456789abcdef0123456789abcdef01234567"
      echo "Signer #1 certificate MD5 digest: 0123456789abcdef0123456789abcdef"
    else
      cat "$FIXTURES/verify"
    fi
    ;;
  *) echo "unknown command $cmd" >&2; exit 2 ;;
esac
EOF
chmod +x "$TMP/bt/zipalign" "$TMP/bt/aapt2" "$TMP/bt/apksigner"

# verbose <v2> <v3> <signers>: what apksigner verify --verbose prints.
verbose() {
  echo "Verifies"
  echo "Verified using v1 scheme (JAR signing): false"
  echo "Verified using v2 scheme (APK Signature Scheme v2): $1"
  echo "Verified using v3 scheme (APK Signature Scheme v3): $2"
  echo "Verified using v3.1 scheme (APK Signature Scheme v3.1): false"
  echo "Verified using v4 scheme (APK Signature Scheme v4): false"
  echo "Verified for SourceStamp: false"
  echo "Number of signers: $3"
}

# apk <package> <versionCode> <versionName>: the unsigned APK aapt2 reads.
apk() {
  printf "badging: package: name='%s' versionCode='%s' versionName='%s' platformBuildVersionName='15' platformBuildVersionCode='35' compileSdkVersion='35' compileSdkVersionCodename='15'\n" "$1" "$2" "$3"
  echo "badging: sdkVersion:'26'"
  echo "badging: targetSdkVersion:'35'"
  echo "badging: native-code: 'arm64-v8a' 'armeabi-v7a' 'x86' 'x86_64'"
}

F="$TMP/f"
fresh() {
  rm -rf "$F"
  mkdir -p "$F/tmp"
  : > "$F/calls.log"
  echo "$PIN" > "$F/pin"
  echo "$PIN" > "$F/cert"
  printf '%s' "$STOREPASS" > "$F/storepass"
  printf '%s' "$KEYPASS" > "$F/keypass"
  echo "not really a keystore" > "$F/release.p12"
  verbose true true 1 > "$F/verify"
  apk org.freetunnel.android 10000 1.0.0 > "$F/unsigned.apk"
  TOOLS="$TMP/bt"
  KS="$F/release.p12"
  EXTRA=(env)
}
out=""
rc=0
# run [<version>] [<out>]: the script on $F/unsigned.apk, with the stand-in
# Build Tools in $TOOLS, the keystore $KS and both passwords set, nothing else
# of the caller's Android variables, and then $EXTRA: env -u NAME, NAME=value.
run() {
  rc=0
  local -a vars=(FIXTURES="$F" TMPDIR="$F/tmp" ANDROID_KEYSTORE_PATH="$KS"
                 ANDROID_KEYSTORE_PASSWORD="$STOREPASS" ANDROID_KEY_PASSWORD="$KEYPASS")
  if [[ -n "$TOOLS" ]]; then
    vars+=(BUILD_TOOLS="$TOOLS")
  fi
  out="$(env -u BUILD_TOOLS -u ANDROID_HOME -u ANDROID_SDK_ROOT -u ANDROID_KEY_ALIAS "${vars[@]}" \
         "${EXTRA[@]}" bash "$SIGN" "$F/unsigned.apk" "${2-$F/dist/freetunnel-android.apk}" \
         "$F/pin" "${1-1.0.0}" 2>&1)" || rc=$?
}
fail=0
# expect <case> <exit code> <ERE the output must match>
expect() {
  if [[ "$rc" -ne "$2" ]] || ! grep -qE -- "$3" <<<"$out"; then
    echo "FAIL: $1: exit $rc (wanted $2), output must match /$3/:"
    printf '    %s\n' "${out//$'\n'/$'\n'    }"
    fail=1
  else
    echo "ok: $1"
  fi
}
problem() {
  echo "FAIL: $1"
  fail=1
}
# Nothing at the output, nothing left in the temporary directory.
nothing_out() {
  [[ ! -e "$F/dist/freetunnel-android.apk" ]] || problem "$1: a signed APK was left at the output"
  [[ -z "$(ls -A "$F/tmp")" ]] || problem "$1: files left in TMPDIR: $(ls -A "$F/tmp")"
}
not_signed() {
  ! grep -q '^apksigner sign' "$F/calls.log" || problem "$1: apksigner signed it"
  nothing_out "$1"
}
# Never in an argument, never in the output.
no_passwords() {
  if grep -qF -e "$STOREPASS" -e "$KEYPASS" "$F/calls.log" \
     || grep -qF -e "$STOREPASS" -e "$KEYPASS" <<<"$out"; then
    problem "$1: a password appears in an argument or the output"
  fi
}

fresh
run
expect "a good APK" 0 "Signed $F/dist/freetunnel-android.apk: org.freetunnel.android 1.0.0 \\(10000\\), certificate $PIN"
grep -qx "signed-by: $PIN" "$F/dist/freetunnel-android.apk" \
  || problem "the output is not the signed APK"
shopt -s nullglob dotglob
written=("$F/dist/"*)
shopt -u nullglob dotglob
[[ "${written[*]}" == "$F/dist/freetunnel-android.apk" ]] || problem "more than the APK in the output directory: ${written[*]}"
[[ -z "$(ls -A "$F/tmp")" ]] || problem "files left in TMPDIR: $(ls -A "$F/tmp")"
no_passwords "a good APK"
grep -qx "Verified using v3 scheme (APK Signature Scheme v3): true" <<<"$out" \
  || problem "apksigner verify's report is not in the log"
want_sign="apksigner sign --ks $F/release.p12 --ks-key-alias freetunnel"
want_sign+=" --ks-pass env:ANDROID_KEYSTORE_PASSWORD --key-pass env:ANDROID_KEY_PASSWORD"
want_sign+=" --v1-signing-enabled false --v2-signing-enabled true --v3-signing-enabled true"
want_sign+=" --v4-signing-enabled false --debuggable-apk-permitted false --out $F/tmp/[^ ]+/signed.apk $F/unsigned.apk"
# Every tool call, in order: nothing is signed before both checks of the
# unsigned APK, and the signed one is checked four ways.
calls="$(sed -E "s#^apksigner sign .*#SIGN#; s#$F/tmp/[^ /]+/#TMP/#g" "$F/calls.log")"
want_calls="zipalign -c -P 16 -v 4 $F/unsigned.apk
aapt2 dump badging $F/unsigned.apk
SIGN
apksigner verify --verbose TMP/signed.apk
zipalign -c -P 16 -v 4 TMP/signed.apk
apksigner verify --print-certs TMP/signed.apk
aapt2 dump badging TMP/signed.apk"
grep -qxE -- "$want_sign" "$F/calls.log" || problem "apksigner sign was not called as expected: $(grep '^apksigner sign' "$F/calls.log")"
[[ "$calls" == "$want_calls" ]] \
  || problem "the tools were not called in the expected order:
$calls"

fresh
EXTRA=(env ANDROID_KEY_ALIAS=other-alias)
run
expect "ANDROID_KEY_ALIAS names another key" 0 "Signed "
grep -q -- '--ks-key-alias other-alias ' "$F/calls.log" || problem "ANDROID_KEY_ALIAS was not passed to apksigner"

fresh
echo "an older build" > "$F/old.apk"
mkdir -p "$F/dist"
cp "$F/old.apk" "$F/dist/freetunnel-android.apk"
touch "$F/aapt2-fails"
run
expect "an earlier output, then a failure" 1 "::error title=Cannot read the APK::"
nothing_out "an earlier output, then a failure"

# versionCode is X*10000 + Y*100 + Z.
for case in "0.9.0 900" "1.2.3 10203" "12.34.56 123456" "10.0.99 100099" "209999.99.99 2099999999" "0.0.0 0"; do
  read -r v code <<<"$case"
  fresh
  apk org.freetunnel.android "$code" "$v" > "$F/unsigned.apk"
  run "$v"
  expect "version $v, versionCode $code" 0 "Signed .*: org.freetunnel.android $v \\($code\\)"
done

for v in 1.0 1.0.0.0 v1.0.0 01.0.0 1.00.0 1.0.01 1.100.0 1.0.100 1234567.0.0 "1.0.0 " " 1.0.0" "1.0.0"$'\n' 1.0.x ""; do
  fresh
  run "$v"
  expect "version '$v'" 1 "::error title=Bad version::"
  [[ ! -s "$F/calls.log" ]] || problem "version '$v': a tool ran: $(cat "$F/calls.log")"
done

fresh
run 1.0.1
expect "versionName and versionCode not the release's" 1 \
  "::error title=Wrong APK::the unsigned APK is package 'org.freetunnel.android', versionName '1.0.0', versionCode '10000'; this release is org.freetunnel.android, 1.0.1, 10001"
not_signed "versionName and versionCode not the release's"

fresh
apk org.freetunnel.android 10000 1.0.0-rc > "$F/unsigned.apk"
run
expect "a versionName with a suffix" 1 "::error title=Wrong APK::.*versionName '1.0.0-rc'"
not_signed "a versionName with a suffix"

fresh
apk org.freetunnel.android 10001 1.0.0 > "$F/unsigned.apk"
run
expect "another versionCode" 1 "::error title=Wrong APK::.*versionCode '10001'"
not_signed "another versionCode"

fresh
apk org.freetunnel.android.debug 10000 1.0.0 > "$F/unsigned.apk"
run
expect "another package" 1 "::error title=Wrong APK::.*package 'org.freetunnel.android.debug'"
not_signed "another package"

fresh
grep -v '^badging: package:' "$F/unsigned.apk" > "$F/x" && mv "$F/x" "$F/unsigned.apk"
run
expect "no package line" 1 "::error title=Wrong APK::.*package '', versionName '', versionCode ''"
not_signed "no package line"

fresh
touch "$F/aapt2-fails"
run
expect "aapt2 cannot read the APK" 1 "::error title=Cannot read the APK::aapt2 dump badging cannot read the unsigned APK"
grep -q "Unable to open" <<<"$out" || problem "aapt2's own error is not shown"
not_signed "aapt2 cannot read the APK"

fresh
echo 'align: bad' >> "$F/unsigned.apk"
run
expect "an unaligned APK" 1 "::error title=APK not aligned::the unsigned APK is not aligned"
grep -q "libtrusttunnel.so (BAD - 4096)" <<<"$out" || problem "the misaligned entry is not shown"
not_signed "an unaligned APK"

fresh
touch "$F/sign-fails"
run
expect "apksigner fails to sign" 1 "::error title=Signing failed::apksigner could not sign .* with the key freetunnel"
nothing_out "apksigner fails to sign"

fresh
printf '%s' "wrong" > "$F/storepass"
run
expect "a wrong keystore password" 1 "::error title=Signing failed::"
grep -q "keystore password was incorrect" <<<"$out" || problem "apksigner's error is not shown"
nothing_out "a wrong keystore password"
no_passwords "a wrong keystore password"

fresh
touch "$F/verify-fails"
run
expect "the signed APK does not verify" 1 "::error title=Signed APK does not verify::"
grep -q "DOES NOT VERIFY" <<<"$out" || problem "apksigner verify's output is not shown"
nothing_out "the signed APK does not verify"

# what apksigner verify --verbose says, and what is refused.
for case in "false true 1 v2" "true false 1 v3" "true true 2 signers: 1" "true true 10 signers: 1"; do
  read -r v2 v3 n what <<<"$case"
  fresh
  verbose "$v2" "$v3" "$n" > "$F/verify"
  run
  expect "verify says v2 $v2, v3 $v3, $n signer(s)" 1 "::error title=Unexpected signature::.*does not say '.*$what"
  nothing_out "verify says v2 $v2, v3 $v3, $n signer(s)"
done
fresh
grep -vx 'Verifies' "$F/verify" > "$F/x" && mv "$F/x" "$F/verify"
run
expect "verify does not say Verifies" 1 "::error title=Unexpected signature::.*'Verifies'"

fresh
touch "$F/sign-misaligns"
run
expect "apksigner leaves it unaligned" 1 "::error title=APK not aligned::the signed APK is not aligned"
nothing_out "apksigner leaves it unaligned"

fresh
echo "$OTHER" > "$F/cert"
run
expect "signed with another key" 1 "::error title=APK signed with another key::.*$OTHER.*pins $PIN"
nothing_out "signed with another key"

fresh
touch "$F/sign-rebadges"
run
expect "the signed APK reads differently" 1 "::error title=Wrong APK::the signed APK is .*versionName '9.9.9'"
nothing_out "the signed APK reads differently"

fresh
rm "$F/pin"
run
expect "no pin" 1 "::error title=No release certificate pin::"
[[ ! -s "$F/calls.log" ]] || problem "no pin: a tool ran: $(cat "$F/calls.log")"
fresh
echo "${PIN^^}" > "$F/pin"
run
expect "a bad pin" 1 "::error title=Bad release certificate pin::"
[[ ! -s "$F/calls.log" ]] || problem "a bad pin: a tool ran: $(cat "$F/calls.log")"

for name in ANDROID_KEYSTORE_PATH ANDROID_KEYSTORE_PASSWORD ANDROID_KEY_PASSWORD; do
  fresh
  EXTRA=(env -u "$name")
  run
  expect "$name not set" 1 "::error title=Signing variables missing::Not set: $name\$"
  [[ ! -s "$F/calls.log" ]] || problem "$name not set: a tool ran"
  EXTRA=(env "$name=")
  run
  expect "$name empty" 1 "::error title=Signing variables missing::Not set: $name\$"
done
fresh
EXTRA=(env -u ANDROID_KEYSTORE_PASSWORD -u ANDROID_KEY_PASSWORD)
run
expect "both passwords not set" 1 "Not set: ANDROID_KEYSTORE_PASSWORD ANDROID_KEY_PASSWORD\$"

fresh
KS="$F/missing.p12"
run
expect "no keystore file" 1 "::error title=No keystore::ANDROID_KEYSTORE_PATH is $F/missing.p12, which does not exist"
[[ ! -s "$F/calls.log" ]] || problem "no keystore file: a tool ran"

for tool in zipalign apksigner aapt2; do
  fresh
  mkdir -p "$TMP/partial"
  cp "$TMP/bt/"* "$TMP/partial/"
  rm "$TMP/partial/$tool"
  TOOLS="$TMP/partial"
  run
  expect "no $tool" 1 "::error title=No Build Tools::$TMP/partial/$tool is not an executable"
  [[ ! -s "$F/calls.log" ]] || problem "no $tool: a tool ran"
  rm -rf "$TMP/partial"
done

# Without BUILD_TOOLS, Build Tools 35.0.0 under ANDROID_HOME, or ANDROID_SDK_ROOT.
mkdir -p "$TMP/sdk/build-tools/35.0.0"
cp "$TMP/bt/"* "$TMP/sdk/build-tools/35.0.0/"
fresh
TOOLS=""
EXTRA=(env ANDROID_HOME="$TMP/sdk")
run
expect "Build Tools under ANDROID_HOME" 0 "Signed "
grep -q "^zipalign" "$F/calls.log" || problem "the tools under ANDROID_HOME were not used"
fresh
TOOLS=""
EXTRA=(env ANDROID_SDK_ROOT="$TMP/sdk")
run
expect "Build Tools under ANDROID_SDK_ROOT" 0 "Signed "
fresh
TOOLS=""
run
expect "no Build Tools anywhere" 1 "::error title=No Build Tools::/build-tools/35.0.0/zipalign is not an executable"

fresh
rm "$F/unsigned.apk"
run
expect "no unsigned APK" 1 "::error title=No APK::"

fresh
mkdir -p "$F/dist"
run 1.0.0 "$F/dist"
expect "the output is a directory" 1 "::error title=Output is a directory::"
fresh
run 1.0.0 "$F/unsigned.apk"
expect "the output is the unsigned APK" 1 "::error title=Output is the input::"
[[ -f "$F/unsigned.apk" ]] || problem "the unsigned APK was removed"

for args in "" "a" "a b c" "a b c d e"; do
  rc=0
  # shellcheck disable=SC2086 # word splitting is the point
  out="$(bash "$SIGN" $args 2>&1)" || rc=$?
  expect "$(wc -w <<<"$args") argument(s)" 2 "^usage: sign-android-release.sh"
done

exit "$fail"
