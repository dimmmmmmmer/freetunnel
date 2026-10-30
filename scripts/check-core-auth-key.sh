#!/usr/bin/env bash
set -euo pipefail

FT_SMOKE_BIN_DIR="${BIN_DIR:-}"
FT_SMOKE_FIXTURE_IP="${FIXTURE_IP:-}"
while (($#)); do
  case "$1" in
    --bin-dir) (($# >= 2)) || { echo 'Missing value for --bin-dir' >&2; exit 2; }; FT_SMOKE_BIN_DIR="$2"; shift 2 ;;
    --fixture-ip) (($# >= 2)) || { echo 'Missing value for --fixture-ip' >&2; exit 2; }; FT_SMOKE_FIXTURE_IP="$2"; shift 2 ;;
    -h|--help) echo 'Usage: scripts/check-core-auth-key.sh --bin-dir DIR --fixture-ip HOST_LOCAL_IPV4'; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
[[ -n "$FT_SMOKE_BIN_DIR" ]] || { echo 'Set BIN_DIR or pass --bin-dir.' >&2; exit 2; }
[[ -n "$FT_SMOKE_FIXTURE_IP" ]] || { echo 'Set FIXTURE_IP or pass --fixture-ip.' >&2; exit 2; }
[[ -d "$FT_SMOKE_BIN_DIR" ]] || { echo "BIN_DIR is not a directory: $FT_SMOKE_BIN_DIR" >&2; exit 2; }
FT_SMOKE_BIN_DIR="$(cd -- "$FT_SMOKE_BIN_DIR" && pwd -P)"
FT_SMOKE_ENDPOINT="$FT_SMOKE_BIN_DIR/trusttunnel_endpoint"
FT_SMOKE_CLIENT="$FT_SMOKE_BIN_DIR/trusttunnel_client"
for FT_SMOKE_BIN in "$FT_SMOKE_ENDPOINT" "$FT_SMOKE_CLIENT"; do
  [[ -x "$FT_SMOKE_BIN" ]] || { echo "missing executable: $FT_SMOKE_BIN" >&2; exit 2; }
done
for tool in openssl python3 curl ip grep; do
  command -v "$tool" >/dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done

python3 - "$FT_SMOKE_FIXTURE_IP" <<'PY'
import ipaddress, sys
try:
    address = ipaddress.ip_address(sys.argv[1])
except ValueError as error:
    raise SystemExit(f"FIXTURE_IP is not a valid IP address: {error}")
if address.version != 4:
    raise SystemExit("FIXTURE_IP must be IPv4")
if address.is_loopback:
    raise SystemExit("FIXTURE_IP cannot be loopback; the client bypasses loopback SOCKS destinations")
PY
fixture_route="$(ip route get "$FT_SMOKE_FIXTURE_IP" 2>/dev/null || true)"
if ! python3 - "$FT_SMOKE_FIXTURE_IP" "$fixture_route" <<'PY'
import re, sys
address, route = sys.argv[1:]
pattern = rf"^local {re.escape(address)} dev lo table local(?:\s|$)"
sys.exit(0 if re.search(pattern, route) else 1)
PY
then
  echo "FIXTURE_IP must be host-assigned and use local delivery; got: ${fixture_route:-no route}" >&2
  exit 2
fi

FT_SMOKE_DIR="$(mktemp -d /tmp/ft-auth-key-smoke.XXXXXX)"
TEST_AUTH_KEY="00112233445566778899aabbccddeeff"
FT_SMOKE_TEST_USER="local-test"
FT_SMOKE_TEST_PASSWORD="$(openssl rand -hex 24)"
FT_SMOKE_ENDPOINT_PID=""
FT_SMOKE_CLIENT_PID=""
FT_SMOKE_HTTP_PID=""
cleanup() {
  [[ -z "$FT_SMOKE_CLIENT_PID" ]] || kill "$FT_SMOKE_CLIENT_PID" 2>/dev/null || true
  [[ -z "$FT_SMOKE_ENDPOINT_PID" ]] || kill "$FT_SMOKE_ENDPOINT_PID" 2>/dev/null || true
  [[ -z "$FT_SMOKE_HTTP_PID" ]] || kill "$FT_SMOKE_HTTP_PID" 2>/dev/null || true
  [[ -z "$FT_SMOKE_CLIENT_PID" ]] || wait "$FT_SMOKE_CLIENT_PID" 2>/dev/null || true
  [[ -z "$FT_SMOKE_ENDPOINT_PID" ]] || wait "$FT_SMOKE_ENDPOINT_PID" 2>/dev/null || true
  [[ -z "$FT_SMOKE_HTTP_PID" ]] || wait "$FT_SMOKE_HTTP_PID" 2>/dev/null || true
  rm -rf -- "$FT_SMOKE_DIR"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Pick free loopback TCP ports, then close the probe sockets before starting services.
read -r ENDPOINT_PORT SOCKS_PORT HTTP_PORT < <(python3 - <<'PY'
import socket
sockets=[]
ports=[]
for _ in range(3):
    s=socket.socket(); s.bind(('127.0.0.1', 0)); sockets.append(s); ports.append(s.getsockname()[1])
print(*ports)
for s in sockets: s.close()
PY
)

mkdir "$FT_SMOKE_DIR/www"
printf 'local-auth-ok\n' > "$FT_SMOKE_DIR/www/index.html"
openssl req -new -x509 -sha256 -newkey rsa:2048 -nodes -days 1 \
  -keyout "$FT_SMOKE_DIR/key.pem" -out "$FT_SMOKE_DIR/cert.pem" \
  -subj '/CN=endpoint.test' -addext 'subjectAltName=DNS:endpoint.test' \
  >/dev/null 2>&1
cat > "$FT_SMOKE_DIR/endpoint.toml" <<EOF2
listen_address = "127.0.0.1:${ENDPOINT_PORT}"
allow_private_network_connections = true
credentials_file = "credentials.toml"
rules_file = "rules.toml"
[listen_protocols.http2]
EOF2
cat > "$FT_SMOKE_DIR/tls_hosts.toml" <<EOF2
[[main_hosts]]
hostname = "endpoint.test"
cert_chain_path = "cert.pem"
private_key_path = "key.pem"
EOF2
printf '[[client]]\nusername = "%s"\npassword = "%s"\n' \
  "$FT_SMOKE_TEST_USER" "$FT_SMOKE_TEST_PASSWORD" > "$FT_SMOKE_DIR/credentials.toml"
cat > "$FT_SMOKE_DIR/rules.toml" <<EOF2
[[rule]]
client_random_auth_key = "$TEST_AUTH_KEY"
action = "allow"
[[rule]]
action = "deny"
EOF2

cd "$FT_SMOKE_DIR"
[[ "$("$FT_SMOKE_ENDPOINT" --version)" == '1.3.0-beta.2' ]] || { echo 'Unexpected endpoint binary version' >&2; exit 2; }
[[ "$("$FT_SMOKE_CLIENT" --version)" == 'trusttunnel_client 1.3.0-rc.2' ]] || { echo 'Unexpected client binary version' >&2; exit 2; }
"$FT_SMOKE_ENDPOINT" --loglvl debug endpoint.toml tls_hosts.toml > endpoint.log 2>&1 &
FT_SMOKE_ENDPOINT_PID=$!
python3 -m http.server "$HTTP_PORT" --bind "$FT_SMOKE_FIXTURE_IP" --directory "$FT_SMOKE_DIR/www" > http.log 2>&1 &
FT_SMOKE_HTTP_PID=$!

write_client_config() {
  local mode="$1"
  cat > "$FT_SMOKE_DIR/client.toml" <<EOF2
loglevel = "debug"
vpn_mode = "general"
killswitch_enabled = false
[endpoint]
hostname = "endpoint.test"
addresses = ["127.0.0.1:${ENDPOINT_PORT}"]
username = "$FT_SMOKE_TEST_USER"
password = "$FT_SMOKE_TEST_PASSWORD"
skip_verification = true
upstream_protocol = "http2"
tls_profile = "default"
EOF2
  if [[ "$mode" == correct ]]; then
    printf 'client_random_auth_key = "%s"\n' "$TEST_AUTH_KEY" >> "$FT_SMOKE_DIR/client.toml"
  elif [[ "$mode" == wrong ]]; then
    printf 'client_random_auth_key = "%s"\n' 'deadbeefdeadbeefdeadbeefdeadbeef' >> "$FT_SMOKE_DIR/client.toml"
  fi
  cat >> "$FT_SMOKE_DIR/client.toml" <<EOF2
[listener.socks]
address = "127.0.0.1:${SOCKS_PORT}"
EOF2
}
start_client() {
  "$FT_SMOKE_CLIENT" --config "$FT_SMOKE_DIR/client.toml" > "$FT_SMOKE_DIR/client-$1.log" 2>&1 &
  FT_SMOKE_CLIENT_PID=$!
  sleep 1
  kill -0 "$FT_SMOKE_CLIENT_PID" 2>/dev/null || { echo "Client exited during $1 setup" >&2; cat "$FT_SMOKE_DIR/client-$1.log" >&2; exit 1; }
}
stop_client() {
  kill "$FT_SMOKE_CLIENT_PID" 2>/dev/null || true
  wait "$FT_SMOKE_CLIENT_PID" 2>/dev/null || true
  FT_SMOKE_CLIENT_PID=""
}

# Wait for local services to bind without touching routing, firewall, or interfaces.
for _ in $(seq 1 100); do
  if ! kill -0 "$FT_SMOKE_ENDPOINT_PID" 2>/dev/null; then cat endpoint.log; exit 1; fi
  if curl --noproxy '*' --silent --head --max-time 1 "http://${FT_SMOKE_FIXTURE_IP}:${HTTP_PORT}/" >/dev/null; then break; fi
  sleep 0.1
done

write_client_config correct
start_client correct
positive=""
for _ in $(seq 1 100); do
  positive="$(curl --noproxy '' --silent --show-error --max-time 2 --proxy "socks5h://127.0.0.1:${SOCKS_PORT}" "http://${FT_SMOKE_FIXTURE_IP}:${HTTP_PORT}/" 2>/dev/null || true)"
  [[ "$positive" == local-auth-ok ]] && break
  kill -0 "$FT_SMOKE_CLIENT_PID" 2>/dev/null || { cat "$FT_SMOKE_DIR/client-correct.log"; exit 1; }
  sleep 0.2
done
[[ "$positive" == local-auth-ok ]] || { echo "positive auth-key request failed" >&2; cat endpoint.log "$FT_SMOKE_DIR/client-correct.log"; exit 1; }
echo "PASS correct auth key: local HTTP fixture returned '$positive' through loopback SOCKS"
stop_client

for mode in wrong missing; do
  denials_before="$(grep -c 'auth key client_random validation failed' endpoint.log || true)"
  write_client_config "$mode"
  start_client "$mode"
  sleep 2
  result="$(curl --noproxy '' --silent --max-time 3 --proxy "socks5h://127.0.0.1:${SOCKS_PORT}" "http://${FT_SMOKE_FIXTURE_IP}:${HTTP_PORT}/" 2>/dev/null || true)"
  kill -0 "$FT_SMOKE_CLIENT_PID" 2>/dev/null || { echo "Client exited during $mode auth test" >&2; cat "$FT_SMOKE_DIR/client-$mode.log" >&2; exit 1; }
  if [[ "$result" == local-auth-ok ]]; then
    echo "FAIL $mode auth unexpectedly reached fixture" >&2
    cat endpoint.log "$FT_SMOKE_DIR/client-$mode.log"
    exit 1
  fi
  denials_after="$(grep -c 'auth key client_random validation failed' endpoint.log || true)"
  if ((denials_after <= denials_before)); then
    echo "Endpoint did not record a new auth-key denial for $mode" >&2
    cat endpoint.log >&2
    exit 1
  fi
  echo "PASS $mode auth: endpoint denied the key and local SOCKS request did not reach fixture"
  stop_client
done

if ! grep -Fq "Successfully connected to TcpConnectionMeta { client_address: 127.0.0.1, destination: Address(${FT_SMOKE_FIXTURE_IP}:${HTTP_PORT})" endpoint.log; then
  echo "FAIL endpoint log has no authenticated tunnel to the HTTP fixture" >&2
  cat endpoint.log
  exit 1
fi
failures="$(grep -c 'auth key client_random validation failed' endpoint.log || true)"
if [[ "$failures" -lt 2 ]]; then
  echo "FAIL endpoint did not log both wrong-key and missing-key denials" >&2
  cat endpoint.log
  exit 1
fi
get_count="$(grep -c 'GET / HTTP/1.1' http.log || true)"
if [[ "$get_count" -ne 1 ]]; then
  echo "FAIL expected exactly one fixture GET (the correct-key request), got $get_count" >&2
  cat http.log
  exit 1
fi
echo "PASS endpoint log confirms authenticated tunnel to ${FT_SMOKE_FIXTURE_IP}:${HTTP_PORT}"
echo "PASS endpoint log confirms wrong-key and missing-key authentication denials"
echo "PASS fixture received exactly one GET, from the correct-key request"
echo "All local auth-key checks passed."
