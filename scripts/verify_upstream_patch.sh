#!/usr/bin/env bash
# Verify vendor/trusttunnel/tunnel-stats-handler.patch applies to the pinned upstream ref.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="$(tr -d '[:space:]' < "$ROOT/scripts/upstream_ref.txt")"
PATCH="$ROOT/vendor/trusttunnel/tunnel-stats-handler.patch"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git clone --filter=blob:none --no-checkout \
  https://github.com/TrustTunnel/TrustTunnelClient.git "$TMP/upstream"
git -C "$TMP/upstream" fetch --depth 1 origin "$REF"
git -C "$TMP/upstream" checkout FETCH_HEAD

patch -p1 -d "$TMP/upstream" < "$PATCH"

grep -q 'tunnel_stats_handler' \
  "$TMP/upstream/trusttunnel/include/vpn/trusttunnel/client.h"
grep -q 'm_callbacks.tunnel_stats_handler' \
  "$TMP/upstream/trusttunnel/src/client.cpp"

echo "upstream patch verified for ${REF}"
