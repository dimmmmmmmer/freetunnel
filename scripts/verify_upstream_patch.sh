#!/usr/bin/env bash
# Verify scripts/patch_core_wrapper.py still applies to the pinned upstream ref.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REF="$(tr -d '[:space:]' < "$ROOT/scripts/upstream_ref.txt")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

git clone --filter=blob:none --no-checkout \
  https://github.com/TrustTunnel/TrustTunnelClient.git "$TMP/upstream"
git -C "$TMP/upstream" fetch --depth 1 origin "$REF"
git -C "$TMP/upstream" checkout FETCH_HEAD

python3 "$ROOT/scripts/patch_core_wrapper.py" "$TMP/upstream"

grep -q 'tunnel_stats_handler' \
  "$TMP/upstream/trusttunnel/include/vpn/trusttunnel/client.h"
grep -q 'm_callbacks.tunnel_stats_handler' \
  "$TMP/upstream/trusttunnel/src/client.cpp"

echo "upstream patch verified for ${REF}"
