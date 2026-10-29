#!/usr/bin/env bash
# The Android app says what the desktop says. Where a control is the same on both, its English
# text is the desktop's source text and its Russian is the desktop's translation
# (i18n/freetunnel_ru.ts, under the same context), so the two apps never word one thing two ways.
# This fails when
#   - a string in the table below differs from the desktop's, in English or in Russian;
#   - a string of the app is neither in the table nor on the Android-only list under it
#     (so a new string has to be placed on one or the other, on purpose);
#   - an entry of either list is no string of the app, or is on both lists, or twice;
#   - values-ru does not have what values has (a language name that is never translated,
#     translatable="false", is in values alone).
# Android's XML rules are applied before the comparison: the quotes round a value, \n, \', \",
# \\ and \uXXXX, white space that is collapsed outside quotes, &amp; and the like, and
# %1$s or %1$d for the desktop's %1, %% for a % that is no placeholder's.
#
# To add a string: if the desktop has the same control, copy its text and translation into both
# strings.xml and add a row to the table; if it has none, add the name to a group of the
# Android-only list, or start one with a reason.
#
#   bash tests/test_android_strings_match_desktop.sh    the checks on fixtures, then the real files
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# One row for each string the desktop has too: [android name, desktop context, desktop source text].
cat > "$TMP/table.jsonl" <<'TABLE'
["text_add_configuration", "ConfigsPage", "Add a config"]
["text_connecting", "HomePage", "Connecting…"]
["text_connected", "Backend", "Connected"]
["text_ms", "Backend", " ms"]
["text_paste_from_clipboard", "ConfigsPage", "Paste from clipboard"]
["text_clipboard_is_empty", "Backend", "Clipboard is empty"]
["text_cancel", "ConfirmDialog", "Cancel"]
["text_new_configuration", "CreateConfigOverlay", "New config"]
["text_edit_configuration", "CreateConfigOverlay", "Edit config"]
["text_save", "CreateConfigOverlay", "Save"]
["text_delete", "ConfigsPage", "Delete"]
["text_split_tunnelling", "SplitPage", "Split Tunneling"]
["text_mode", "SplitPage", "Mode"]
["text_bypass_vpn_61", "SplitPage", "Bypass VPN"]
["text_through_vpn_62", "SplitPage", "Through VPN"]
["text_rules_through_vpn", "SplitPage", "Rules — via VPN"]
["text_rules_bypass_vpn", "SplitPage", "Rules — bypass VPN"]
["text_clear", "LogsPage", "Clear"]
["text_applications_through_vpn", "SplitPage", "Applications — via VPN"]
["text_applications_bypass_vpn", "SplitPage", "Applications — bypass VPN"]
["text_choose", "SplitPage", "Choose…"]
["text_general", "SettingsPage", "General"]
["text_theme", "SettingsPage", "Theme"]
["text_theme_system", "SettingsPage", "System"]
["text_light_78", "SettingsPage", "Light"]
["text_dark_79", "SettingsPage", "Dark"]
["text_security", "SettingsPage", "Security"]
["text_enable_logging", "SettingsPage", "Enable logging"]
["text_copy", "LogsPage", "Copy"]
["text_log_copied", "LogsPage", "Log copied"]
["text_subscription_links_are_not_supported_yet", "ConfigImport", "Subscription links aren't supported yet. Ask your provider for a regular link, or update FreeTunnel when subscriptions arrive."]
["update_check", "SettingsPage", "Check for updates"]
["update_checking", "Backend", "Checking…"]
["update_available", "Backend", "Version %1 is available"]
["update_downloading", "Backend", "Downloading… %1%"]
["add_from_file", "ConfigsPage", "From file…"]
["add_create_new", "ConfigsPage", "Create new…"]
["import_question_link", "Backend", "Add a VPN server from this link?"]
["import_question_file", "Backend", "Add a VPN server from this file?"]
["import_server", "Backend", "Server: %1"]
["import_exists", "Backend", "“%1” already exists. Replace it, or add this as a separate config?"]
["import_skips_link", "Backend", "This link turns off server certificate verification."]
["import_skips_file", "Backend", "This file turns off server certificate verification."]
["import_mixed_name", "Backend", "This name mixes letters from different alphabets."]
["import_subscription_note", "ConfigImport", "Subscriptions aren't supported yet: this server won't update itself, and its subscription address isn't saved."]
["import_certificate_note", "ConfigImport", "Extra data in the certificate was ignored."]
["import_confirm", "Main", "Import"]
["import_add_copy", "Main", "Add copy"]
["import_replace", "Main", "Replace"]
["config_added", "Main", "Config added: %1"]
["delete_config_question", "ConfigsPage", "Delete config “%1”?"]
["editor_name", "CreateConfigOverlay", "Name"]
["editor_name_placeholder", "CreateConfigOverlay", "Germany · Frankfurt"]
["editor_host", "CreateConfigOverlay", "Server host"]
["editor_addresses", "CreateConfigOverlay", "Address(es) · host:port (comma-separated)"]
["editor_username", "CreateConfigOverlay", "Username"]
["editor_password", "CreateConfigOverlay", "Password"]
["editor_protocol", "CreateConfigOverlay", "Protocol"]
["editor_dns", "CreateConfigOverlay", "DNS servers"]
["editor_sni", "CreateConfigOverlay", "Custom SNI"]
["editor_random", "CreateConfigOverlay", "Client random (hex)"]
["editor_ipv6", "CreateConfigOverlay", "Allow IPv6"]
["editor_skip_verification", "CreateConfigOverlay", "Skip certificate check"]
["editor_anti_dpi", "CreateConfigOverlay", "Anti-DPI"]
["editor_certificate", "CreateConfigOverlay", "Certificate (PEM) · optional"]
["editor_load_certificate", "CreateConfigOverlay", "Load from file…"]
["editor_error_missing", "Backend", "Fill in host, address, username and password"]
["editor_error_address", "Backend", "Address must be host:port, e.g. 1.2.3.4:443"]
["editor_error_dns", "Backend", "DNS must be an IP or DoT/DoH URL (e.g. 1.1.1.1, tls://8.8.8.8)"]
["editor_error_random", "Backend", "Client random must be hexadecimal in whole bytes (an even number of digits, at most 64), optionally followed by /mask"]
["editor_error_certificate", "Backend", "The certificate must be in PEM format, as one or more BEGIN CERTIFICATE blocks"]
["editor_error_name_too_long", "Backend", "The name is too long: %1 characters at most"]
["editor_file_empty", "Backend", "That file is empty"]
["editor_file_too_large", "Backend", "That file is over 1 MB, too large to be a certificate"]
["editor_discard_question", "CreateConfigOverlay", "Discard unsaved changes?"]
["editor_discard", "CreateConfigOverlay", "Discard"]
["picker_title", "AppPickerOverlay", "Add an application"]
["picker_search", "AppPickerOverlay", "Search"]
["picker_scanning", "AppPickerOverlay", "Looking for installed applications…"]
["picker_nothing", "AppPickerOverlay", "Nothing matches that."]
["split_profile", "SplitPage", "Profile"]
["split_profile_default", "SplitPage", "Default"]
["split_profile_name_hint", "SplitPage", "profile name, then Enter"]
["split_profile_delete_question", "SplitPage", "Delete profile “%1”?"]
["split_profile_exists", "Backend", "A profile called “%1” already exists"]
["split_recommended", "SplitPage", "Recommended for Russia"]
["split_clear_all", "SplitPage", "Clear all"]
["split_clear_domains_question", "SplitPage", "Clear all domains?"]
["split_clear_apps_question", "SplitPage", "Clear all applications?"]
["split_domains_hint", "SplitPage", "domains, comma-separated, then Enter"]
["split_refused", "Backend", "Not a valid domain or subnet: %1"]
["split_refused_wildcard", "Backend", "Not a valid domain or subnet: %1. “*.” and a leading dot go only before a domain name; enter an address or subnet without them."]
["split_refused_every", "Backend", "Not a valid domain or subnet: %1. A subnet ending in /0 is every address: under “Bypass VPN” it would take all of its traffic out of the tunnel."]
["split_notice_nothing", "SplitPage", "“%1” uses the profile “%2”, which has no rules that can be used, so \"Through VPN\" would send nothing through the tunnel. The full tunnel stays on until that profile has a rule that can be used."]
["split_notice_nothing_no_configs", "SplitPage", "Add a rule to use \"Through VPN\" — with an empty list nothing would go through the tunnel, so the full tunnel stays on."]
["editor_split_profile", "CreateConfigOverlay", "Split profile"]
["text_disconnecting", "HomePage", "Disconnecting…"]
["text_mb_per_s", "HomePage", " MB/s"]
["config_badge_connected", "ConfigsPage", "connected"]
["config_badge_connecting", "ConfigsPage", "connecting…"]
["text_export_config", "ConfigsPage", "Export config"]
["export_toml", "ConfigsPage", "Export .toml…"]
["export_deep_link", "ConfigsPage", "Copy deep-link"]
["config_exported", "ConfigsPage", "Config exported — the file contains the password"]
["deep_link_copied", "ConfigsPage", "Deep-link copied — it contains the password, share it carefully"]
["deep_link_failed", "Backend", "Couldn’t build deep-link"]
["export_failed", "Backend", "Export failed"]
["config_gone", "ConfigsPage", "That configuration is no longer there."]
["settings_language", "SettingsPage", "Language"]
["settings_connect_on_startup", "SettingsPage", "Connect on startup"]
["settings_kill_switch", "SettingsPage", "Kill switch"]
["settings_kill_switch_hint", "SettingsPage", "block traffic if the VPN drops"]
["settings_excluded_routes", "SettingsPage", "Excluded routes"]
["settings_restore_defaults", "SettingsPage", "Restore defaults"]
["settings_routes_restore_question", "SettingsPage", "Replace the excluded routes with the defaults?"]
["settings_routes_clear_question", "SettingsPage", "Clear all excluded routes?"]
["settings_routes_hint", "SettingsPage", "IP or subnet (e.g. 10.0.0.0/8), then Enter"]
["settings_routes_refused", "Backend", "Not a valid IP or subnet: %1"]
["settings_routes_refused_every", "Backend", "Not a valid IP or subnet: %1. A subnet ending in /0 is every address, so excluding it would take all of its traffic out of the tunnel."]
["settings_logging", "SettingsPage", "Logging"]
["settings_maintenance", "SettingsPage", "Maintenance"]
["settings_core", "SettingsPage", "TrustTunnel core "]
["update_check_failed", "Backend", "Update check failed: %1"]
["update_latest", "Backend", "You have the latest version"]
["logs_off", "LogsPage", "Logging is off."]
["logs_turn_on", "LogsPage", "Turn on"]
["logs_placeholder", "LogsPage", "Logs will appear after connecting"]
["logs_auto_scroll", "LogsPage", "Auto-scroll"]
TABLE

# Every other string, in groups that each say why the desktop has no such text.
cat > "$TMP/only.txt" <<'ONLY'
## Content descriptions of the controls the desktop draws as icons alone (its nav bar and its logo have no words), and of the + chip
text_home
text_configurations
text_routing
text_settings
text_log
text_disconnect_freetunnel
text_connect_freetunnel
text_check_server_tcp_latency
split_profile_add
text_back

## The VPN: its notification, the quick settings tile, and what goes wrong in them (VPN errors, state 6 and its Reconnect)
app_name
text_disconnect_vpn
text_recovering_connection
text_disconnected
text_preparing_vpn
text_vpn_disconnected
text_vpn_settings_were_not_applied
text_could_not_start_vpn
text_connection_to_the_server_was_lost
text_vpn_permission_was_revoked
text_vpn_permission_was_not_granted
text_vpn_consent_unavailable
text_open_the_app_and_check_the_configuration
text_open_the_app_and_check_the_saved_configuration
vpn_failure_details
vpn_traffic_blocked
vpn_reconnect
vpn_blocked_title
vpn_blocked_details
config_validation_error

## Split routing: Android cannot send applications and domains through the tunnel together, and keeps what an older preview saved
text_applications_and_addresses_are_selected_full_vpn_pro
text_no_through_vpn_rules_full_vpn_is_enabled
split_notice_both
legacy_rules_removed
imported_routing_rules

## The update's own states, and the Android installer the update goes through (the desktop opens its installer or a folder)
update_download
update_install
update_ready
update_installing
update_installing_again
update_error_network
update_error_network_inline
update_error_rate_limited
update_error_verification
update_error_verification_inline
update_error_installer
update_unofficial
update_disconnect_warning
update_allow_installs

## Receive from phone, which only a TV has, and the page it serves to the phone
text_receive_from_phone
text_receive_hint
text_receive_no_network
text_receive_failed
text_receive_timed_out
text_receive_timed_out_after_texts
text_receive_received
text_receive_locked_out
text_nsent_from_s
text_from_another_device
text_no_file_picker_receive
receive_page_title
receive_page_prompt
receive_page_file
receive_page_send
receive_page_note
receive_page_sent
receive_page_retry
receive_page_too_large
receive_page_busy
receive_page_wrong_code
receive_page_locked_out

## Importing and editing a config, where Android has its own words: a file or a link it cannot read, a file picker that is missing, storage it cannot reach
text_from_clipboard
text_configuration
text_file_is_too_large
text_could_not_open_file
text_no_file_picker
editor_no_file_picker
editor_error_unwritable
text_invalid_trusttunnel_link
text_unsupported_link_size
text_unsupported_link_version
text_the_link_is_missing_required_server_settings
text_the_link_is_too_long
text_expected_a_trusttunnel_link
text_the_link_contains_no_configuration
text_the_link_is_damaged
text_the_link_contains_invalid_text
text_the_link_contains_an_invalid_certificate
text_the_link_contains_an_invalid_subscription_address
## This refusal is Android-only: Android bundles TrustTunnel 1.1.7, while desktop core 1.3 supports client-random auth keys; Android refuses these rather than importing an unusable configuration.
text_this_server_needs_a_newer_trusttunnel_core
text_the_configuration_contains_an_invalid_certificate
text_the_configuration_is_missing_required_server_settings
text_storage_is_unavailable_saved_data_has_been_preserved
text_storage_is_unavailable_data_has_not_been_changed
text_configurations_are_unavailable
text_android_could_not_read_storage_saved_data_is_unchang
text_could_not_save_configurations_data_on_disk_is_unchan

## The app picker, which lists installed applications and has no file to choose instead; the Split page's field for an application, which takes its name and not a path or a dropped file; Export, which has no file picker on some TVs
picker_none
split_apps_hint
export_no_picker

## Logs and Settings: a log that cannot be read to copy, and a link with no browser to open it
text_could_not_read_log
text_no_browser

## Settings > Language names each language in itself, which is never translated (and so is in res/values alone)
language_english
language_russian
ONLY

cat > "$TMP/check.py" <<'PY'
import json
import re
import sys
import xml.etree.ElementTree as ET

ts_path, en_path, ru_path, table_path, only_path = sys.argv[1:6]
problems = []

def problem(text):
    problems.append(text)

# The desktop's texts: (context, source) -> translation, or None when it has none, or an unfinished one.
desktop = {}
for context in ET.parse(ts_path).getroot().findall("context"):
    cname = context.findtext("name")
    for message in context.findall("message"):
        translation = message.find("translation")
        text = translation.text if translation is not None and translation.get("type") not in ("unfinished", "obsolete", "vanished") else None
        desktop.setdefault((cname, message.findtext("source") or ""), text or None)

def aapt(raw):
    """A value as Android makes it: quotes, escapes and collapsed white space."""
    out = []          # (character, collapsed unquoted space?)
    quoted = False
    i = 0
    while i < len(raw):
        c = raw[i]
        if c == "\\" and i + 1 < len(raw):
            n = raw[i + 1]
            if n == "n":
                out.append(("\n", False)); i += 2; continue
            if n == "t":
                out.append(("\t", False)); i += 2; continue
            if n == "u" and re.fullmatch(r"[0-9a-fA-F]{4}", raw[i + 2:i + 6]):
                out.append((chr(int(raw[i + 2:i + 6], 16)), False)); i += 6; continue
            out.append((n, False)); i += 2; continue
        if c == '"':
            quoted = not quoted; i += 1; continue
        if not quoted and c.isspace():
            if not out or not out[-1][1]:
                out.append((" ", True))
            i += 1; continue
        out.append((c, False)); i += 1
    # Outside quotes, white space at either end goes.
    while out and out[-1][1]:
        out.pop()
    while out and out[0][1]:
        out.pop(0)
    return "".join(c for c, _ in out)

PLACEHOLDER = re.compile(r"%\d+\$[sd]")

def plain(raw):
    """What a string reads as, with the desktop's %1 for Android's %1$s and %1$d, and % for %%."""
    return PLACEHOLDER.sub(lambda m: "%" + m.group(0)[1:-2], aapt(raw)).replace("%%", "%")

def stray_percent(raw):
    """A % in a string that has placeholders, and so is formatted, must be written %%, or formatting it throws."""
    text = aapt(raw)
    return bool(PLACEHOLDER.search(text)) and "%" in PLACEHOLDER.sub("", text).replace("%%", "")

def strings(path):
    out = {}
    flags = {}
    for element in ET.parse(path).getroot():
        if element.tag != "string":
            continue
        name = element.get("name")
        if name in out:
            problem("%s: %s is defined twice" % (path, name))
        out[name] = "".join(element.itertext())
        flags[name] = element.get("translatable") == "false"
    return out, flags

en, en_flags = strings(en_path)
ru, ru_flags = strings(ru_path)
for lang, values in (("English", en), ("Russian", ru)):
    for name in sorted(values):
        if stray_percent(values[name]):
            problem("%s: the %s has a %% that is not written %%%%, in a string that is formatted" % (name, lang))

table = {}
for number, line in enumerate(open(table_path, encoding="utf-8"), 1):
    if not line.strip():
        continue
    try:
        name, context, source = json.loads(line)
    except ValueError:
        problem("table line %d is not [name, context, source]: %s" % (number, line.strip()))
        continue
    if name in table:
        problem("%s is in the table twice" % name)
    table[name] = (context, source)

only = {}
group = None
for line in open(only_path, encoding="utf-8"):
    line = line.strip()
    if not line:
        continue
    if line.startswith("##"):
        group = line[2:].strip()
        continue
    if group is None:
        problem("the Android-only list has %s before any group says why" % line)
    if line in only:
        problem("%s is on the Android-only list twice" % line)
    only[line] = group

for name, (context, source) in sorted(table.items()):
    if name not in en:
        problem("%s is in the table but has no string in values/strings.xml" % name)
        continue
    if name in only:
        problem("%s is in the table and on the Android-only list" % name)
    if (context, source) not in desktop:
        problem("%s: the desktop has no %r in the context %s" % (name, source, context))
        continue
    if plain(en[name]) != source:
        problem("%s: English is %r, the desktop's %s says %r" % (name, plain(en[name]), context, source))
    translation = desktop[(context, source)]
    if translation is None:
        problem("%s: the desktop has no finished Russian for %r (%s)" % (name, source, context))
    elif name not in ru:
        problem("%s: no Russian in values-ru/strings.xml" % name)
    elif plain(ru[name]) != translation:
        problem("%s: Russian is %r, the desktop's %s says %r" % (name, plain(ru[name]), context, translation))

for name in sorted(en):
    if name not in table and name not in only:
        problem("%s (%r) is in neither the table nor the Android-only list" % (name, plain(en[name])[:60]))
for name in sorted(only):
    if name not in en:
        problem("%s is on the Android-only list but is no string of the app" % name)

for name in sorted(en):
    if en_flags[name]:
        if name in ru:
            problem("%s is translatable=\"false\" and has a Russian string" % name)
    elif name not in ru:
        problem("%s has no string in values-ru/strings.xml" % name)
for name in sorted(ru):
    if name not in en:
        problem("%s is in values-ru/strings.xml but not in values/strings.xml" % name)

if problems:
    print("\n".join(problems))
    sys.exit(1)
print("%d strings: %d the desktop's, %d Android's own" % (len(en), len(table), len(only)))
PY

indent() { sed 's/^/    /'; }
fail=0

# run <ts> <en> <ru> <table> <only>: the checker's output, and its status.
run() { python3 -I "$TMP/check.py" "$@"; }

# Fixtures: a desktop file, the two strings files, a table and a list; each way round the
# rules fails, and what is allowed passes.
fx="$TMP/fx"
mkdir -p "$fx"
cat > "$fx/ts.xml" <<'XML'
<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="ru_RU">
<context><name>Page</name>
    <message><source>Hello</source><translation>Привет</translation></message>
    <message><source>Delete &quot;%1&quot;? It&apos;s final &amp; gone</source><translation>Удалить «%1»? Это навсегда &amp; насовсем</translation></message>
    <message><source>Downloading… %1%</source><translation>Загрузка… %1%</translation></message>
    <message><source>Two words </source><translation>Два слова </translation></message>
    <message><source>Line one
line two</source><translation>Строка раз
строка два</translation></message>
    <message><source>Unfinished</source><translation type="unfinished"></translation></message>
</context>
<context><name>Other</name>
    <message><source>Hello</source><translation>Здравствуйте</translation></message>
</context>
</TS>
XML
cat > "$fx/en.xml" <<'XML'
<resources>
    <string name="hello">Hello</string>
    <string name="delete">"Delete \"%1$s\"? It's final &amp; gone"</string>
    <string name="progress">"Downloading… %1$d%%"</string>
    <string name="two">"Two words "</string>
    <string name="lines">Line one\nline two</string>
    <string name="mine">Only here</string>
    <string name="never" translatable="false">Русский</string>
</resources>
XML
cat > "$fx/ru.xml" <<'XML'
<resources>
    <string name="hello">Привет</string>
    <string name="delete">"Удалить «%1$s»? Это навсегда &amp; насовсем"</string>
    <string name="progress">"Загрузка… %1$d%%"</string>
    <string name="two">"Два слова "</string>
    <string name="lines">Строка раз\nстрока два</string>
    <string name="mine">Только здесь</string>
</resources>
XML
cat > "$fx/table.jsonl" <<'TABLE'
["hello", "Page", "Hello"]
["delete", "Page", "Delete \"%1\"? It's final & gone"]
["progress", "Page", "Downloading… %1%"]
["two", "Page", "Two words "]
["lines", "Page", "Line one\nline two"]
TABLE
cat > "$fx/only.txt" <<'ONLY'
## Words that only this app has
mine
never
ONLY

# expect <name> <pass|fail> <message that a failure must hold, or ""> then the files
expect() {
  local name=$1 want=$2 message=$3 got=pass out
  out="$(run "$fx/ts.xml" "$fx/en.xml" "$fx/ru.xml" "$fx/table.jsonl" "$fx/only.txt" 2>&1)" || got=fail
  if [ "$got" = "$want" ] && { [ -z "$message" ] || grep -qF -- "$message" <<<"$out"; }; then
    echo "ok - $name"
  else
    echo "not ok - $name: expected $want${message:+ with \"$message\"}, got $got"
    indent <<<"$out"
    fail=1
  fi
}
# with <file> <old> <new>: that fixture file with a change, for the next expect; reset puts them back.
mutate() { python3 -I - "$1" "$2" "$3" <<'PY'
import sys
path, old, new = sys.argv[1:4]
text = open(path, encoding="utf-8").read()
if old not in text:
    sys.exit("fixture has no %r" % old)
open(path, "w", encoding="utf-8").write(text.replace(old, new, 1))
PY
}
snapshot() { rm -rf "$TMP/fx0"; cp -R "$fx" "$TMP/fx0"; }
reset() { rm -rf "$fx"; cp -R "$TMP/fx0" "$fx"; }
snapshot

expect "words that match, quotes, escapes, placeholders and an unfinished string left alone" pass ""

mutate "$fx/en.xml" '>Hello<' '>Hullo<'; expect "English that differs" fail "English is 'Hullo'"; reset
mutate "$fx/ru.xml" '>Привет<' '>Приветик<'; expect "Russian that differs" fail "Russian is 'Приветик'"; reset
mutate "$fx/ru.xml" 'Загрузка… %1$d%%' 'Загрузка… %1$d'; expect "a percent sign that was dropped" fail "progress: Russian"; reset
mutate "$fx/en.xml" '%1$d%%' '%1$d%'; expect "a percent sign that is not escaped" fail "progress: the English has a %"; reset
mutate "$fx/en.xml" '%1$s' '%2$s'; expect "a placeholder with another number" fail "delete: English"; reset
mutate "$fx/ru.xml" '«%1$s»' '«»'; expect "a placeholder that is missing" fail "delete: Russian"; reset
mutate "$fx/en.xml" 'Two words ' 'Two words'; expect "a trailing space that is lost" fail "two: English"; reset
mutate "$fx/en.xml" 'Line one\nline two' 'Line one line two'; expect "a line break that is a space" fail "lines: English"; reset
mutate "$fx/table.jsonl" '"Page", "Hello"' '"Other", "Hello"'; expect "the desktop's other context says another Russian" fail "Russian is 'Привет', the desktop's Other says 'Здравствуйте'"; reset
mutate "$fx/table.jsonl" '"Page", "Hello"' '"Page", "Hallo"'; expect "a source the desktop does not have" fail "the desktop has no 'Hallo'"; reset
mutate "$fx/en.xml" '<string name="mine">' '<string name="extra">Extra</string><string name="mine">'; expect "a string on neither list" fail "extra ('Extra') is in neither"; reset
mutate "$fx/only.txt" 'never' 'never
stale'; expect "an Android-only entry that is no string" fail "stale is on the Android-only list but is no string"; reset
mutate "$fx/only.txt" 'never' 'never
hello'; expect "a string on both lists" fail "hello is in the table and on the Android-only list"; reset
mutate "$fx/only.txt" 'mine' 'mine
mine'; expect "an Android-only entry twice" fail "mine is on the Android-only list twice"; reset
mutate "$fx/table.jsonl" '["hello", "Page", "Hello"]' '["hello", "Page", "Hello"]
["hello", "Page", "Hello"]'; expect "a row twice" fail "hello is in the table twice"; reset
mutate "$fx/table.jsonl" '["hello", "Page", "Hello"]' '["gone", "Page", "Hello"]'; expect "a row for no string" fail "gone is in the table but has no string"; reset
mutate "$fx/ru.xml" '<string name="mine">Только здесь</string>' ''; expect "a string with no Russian" fail "mine has no string in values-ru"; reset
mutate "$fx/ru.xml" '</resources>' '<string name="surplus">Лишнее</string></resources>'; expect "a Russian string with no English" fail "surplus is in values-ru"; reset
mutate "$fx/ru.xml" '</resources>' '<string name="never">Русский</string></resources>'; expect "a language name that is translated" fail "never is translatable=\"false\" and has a Russian"; reset
mutate "$fx/table.jsonl" '["two", "Page", "Two words "]' '["two", "Page", "Two words "]
["Unfinished", "Page", "Unfinished"]'; expect "a row whose desktop translation is unfinished" fail "is in the table but has no string"; reset
mutate "$fx/en.xml" '<string name="mine">' '<string name="Unfinished">Unfinished</string><string name="mine">'
mutate "$fx/ru.xml" '<string name="mine">' '<string name="Unfinished">Что-то</string><string name="mine">'
mutate "$fx/table.jsonl" '["two", "Page", "Two words "]' '["two", "Page", "Two words "]
["Unfinished", "Page", "Unfinished"]'; expect "no finished Russian on the desktop to match" fail "no finished Russian for 'Unfinished'"; reset
mutate "$fx/en.xml" '<resources>' '<resources><string name="hello">Hello</string>'; expect "a string defined twice" fail "hello is defined twice"; reset
echo "not json" >> "$fx/table.jsonl"; expect "a table line that is no row" fail "is not [name, context, source]"; reset

# The real files.
real_failed=0
real="$(run "$ROOT/i18n/freetunnel_ru.ts" "$ROOT/android/app/src/main/res/values/strings.xml" \
        "$ROOT/android/app/src/main/res/values-ru/strings.xml" "$TMP/table.jsonl" "$TMP/only.txt" 2>&1)" || real_failed=1
if [ "$real_failed" -eq 0 ]; then
  echo "ok - the app's strings match the desktop's ($real)"
else
  echo "not ok - the app's strings match the desktop's"
  indent <<<"$real"
  fail=1
fi
exit "$fail"
