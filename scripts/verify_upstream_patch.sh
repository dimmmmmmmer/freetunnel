#!/usr/bin/env bash
# Verify every patch in vendor/trusttunnel/ applies to the pinned upstream ref,
# in filename order — they build on each other, see setup-upstream-tree.sh.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="$(tr -d '[:space:]' < "$ROOT/scripts/upstream_ref.txt")"
PATCHES=("$ROOT"/vendor/trusttunnel/*.patch)
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git clone --filter=blob:none --no-checkout \
  https://github.com/TrustTunnel/TrustTunnelClient.git "$TMP/upstream"
git -C "$TMP/upstream" fetch --depth 1 origin "$REF"
git -C "$TMP/upstream" checkout FETCH_HEAD

for p in "${PATCHES[@]}"; do
  echo "==> $(basename "$p")"
  patch -p1 -d "$TMP/upstream" < "$p"
done

# A patch that applies but lands the wrong thing is still broken, so assert the
# symbol each one exists for. grep -q on the file the patch claims to change.
grep -q 'tunnel_stats_handler' \
  "$TMP/upstream/trusttunnel/include/vpn/trusttunnel/client.h"
grep -q 'm_callbacks.tunnel_stats_handler' \
  "$TMP/upstream/trusttunnel/src/client.cpp"
grep -q 'connect_request_handler' \
  "$TMP/upstream/trusttunnel/include/vpn/trusttunnel/client.h"
grep -q 'ft_fill_connect_snapshot' \
  "$TMP/upstream/trusttunnel/src/client.cpp"

# The elevated helper hands the core every key of the config the GUI sends,
# except the few that would point root at a file, an interface or a hole in the
# kill switch, which clearKeysRootMustNotTakeFromAConfig() in
# src/vpn/qt_trusttunnel_client.cpp clears by name. A key upstream starts reading
# would reach root without anyone having looked at it, so the bump stops here
# until someone has read what the core does with it, cleared it there if it is
# that kind, and listed it below. Keys are listed by TOML path, not by name: the
# helper clears [listener.tun] netns, and a netns read from another table would
# be a new key that nobody has checked.
CHECKED_CONFIG_KEYS="
  loglevel vpn_mode killswitch_enabled killswitch_allow_ports
  post_quantum_group_enabled exclusions_tcp_early_ack_enabled
  exclusions_preresolve_enabled exclusions_preresolve_max_queries
  exclusions_scannable_ports ssl_session_cache_path exclusions dns_upstreams
  endpoint listener
  endpoint.hostname endpoint.custom_sni endpoint.addresses endpoint.username
  endpoint.password endpoint.skip_verification endpoint.anti_dpi
  endpoint.has_ipv6 endpoint.certificate endpoint.upstream_protocol
  endpoint.client_random endpoint.dns_upstreams
  listener.socks listener.tun
  listener.socks.address listener.socks.username listener.socks.password
  listener.tun.bound_if listener.tun.use_existing listener.tun.device_name
  listener.tun.mtu_size listener.tun.tcp_recv_buf_size
  listener.tun.tcp_send_buf_size listener.tun.change_system_dns
  listener.tun.netns listener.tun.included_routes listener.tun.excluded_routes"

# Read every file of the core that handles a TOML table, not only config.cpp, so
# that a parser moved into a file of its own is still read. The CLI and the
# per-platform front-ends parse configs of their own, and FreeTunnel uses neither.
mapfile -t TOML_SOURCES < <(grep -rlE \
  --include='*.cpp' --include='*.cc' --include='*.h' --include='*.hpp' \
  --include='*.mm' --exclude-dir=.git --exclude-dir=third-party \
  --exclude-dir=platform 'toml::' "$TMP/upstream" \
  | grep -v '/trusttunnel/src/trusttunnel_client\.cpp$' | LC_ALL=C sort)
if (( ${#TOML_SOURCES[@]} == 0 )); then
  echo "error: no upstream source handles a TOML table; where is the config read now?" >&2
  exit 1
fi

# A scan, not a C++ parser. It finds a key wherever toml++ is asked for one by a
# literal name: table["x"] (chained ones too), the members at_path, at, get,
# get_as, contains and find, and the free at_path; a key named through a constant
# is not seen. Each read is placed in its table by the variable it is on and the
# function it is in (upstream starts a definition in column 0). One it cannot
# place is reported rather than guessed: read that code, and teach place() which
# table it is. A listed key the scan no longer finds is reported too, because it
# may only have moved where the scan cannot see, and the next key added there
# would pass unseen. If upstream really dropped it, take it off the list.
problems="$(LC_ALL=C awk -v checked="${CHECKED_CONFIG_KEYS//$'\n'/ }" '
  BEGIN {
    n = split(checked, keys); for (i = 1; i <= n; i++) listed[keys[i]] = 1
    # A table (a variable, (*variable), or the "]" or ")" ending an expression),
    # then ["key"] or a lookup member called with "key"; or at_path(table, "key").
    name = "[A-Za-z_][A-Za-z0-9_]*"
    member = "(\\.|->)(at_path|at|get|get_as[[:space:]]*<[^>]*>|contains|find)[[:space:]]*\\("
    READ = "(\\(\\*" name "\\)|" name "|[])])[[:space:]]*(\\[|" member ")[[:space:]]*\"[^\"]*\""
    FREE_AT_PATH = "at_path[[:space:]]*\\([^,\"]*,[[:space:]]*\"[^\"]*\""
  }
  FNR == 1 { fn = "" }
  /^[A-Za-z_].*\(/ { fn = $0; sub(/[[:space:]]*\(.*/, "", fn); sub(/.*[^A-Za-z0-9_]/, "", fn) }
  {
    rest = $0; prev = ""
    while (match(rest, READ)) {
      m = substr(rest, RSTART, RLENGTH); obj = m; sub(/[[:space:]]*(\[|\.|->).*/, "", obj)
      # config["a"]["b"]: b is in table a.
      if (obj == "]" && RSTART == 1 && prev != "" && prev !~ / /) prev = prev "." key_of(m)
      else prev = place(obj, key_of(m))
      read_at(prev)
      rest = substr(rest, RSTART + RLENGTH)
    }
    rest = $0
    while (match(rest, FREE_AT_PATH)) {
      m = substr(rest, RSTART, RLENGTH); obj = m; sub(/^[^(]*\(/, "", obj); sub(/,.*/, "", obj)
      read_at(place(obj, key_of(m)))
      rest = substr(rest, RSTART + RLENGTH)
    }
  }
  END { for (i = 1; i <= n; i++) if (!(keys[i] in seen)) print keys[i] ": listed, but no longer read" }
  function key_of(m) { sub(/^[^"]*"/, "", m); sub(/"$/, "", m); return m }
  # config is whichever table the function was handed; tun_config and
  # socks_config are the two kinds of listener.
  function place(obj, key) {
    gsub(/[[:space:]*()]/, "", obj)
    if (obj == "tun_config") return "listener.tun." key
    if (obj == "socks_config") return "listener.socks." key
    if (obj == "config" && fn == "build_config") return key
    if (obj == "config" && fn == "build_endpoint") return "endpoint." key
    if (obj == "config" && fn ~ /_listener_config$/) return "listener." key
    if (obj !~ /^[A-Za-z_]/) obj = "an expression"
    return "\"" key "\" on " obj " in " fn "(), which this scan cannot place in a table"
  }
  function read_at(path) {
    if (path in listed) seen[path] = 1
    else printf "%s:%d: %s\n", FILENAME, FNR, path
  }
' "${TOML_SOURCES[@]}")"
if [[ -n "$problems" ]]; then
  echo "error: the config keys upstream reads are not the ones the root helper was checked against:" >&2
  echo "${problems//"$TMP/upstream/"/}" >&2
  exit 1
fi

echo "upstream patches verified for ${REF} (${#PATCHES[@]} applied)"
