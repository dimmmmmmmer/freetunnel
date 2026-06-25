#!/usr/bin/env bash
# Prepare an upstream TrustTunnelClient tree with this client injected as FreeTunnel/.
# Used by CI build/coverage jobs. Requires git and python3.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CLIENT="${CLIENT_DIR:-$ROOT}"
UPSTREAM="${UPSTREAM_DIR:-$ROOT/upstream-tree}"
REF="$(tr -d '[:space:]' < "$CLIENT/scripts/upstream_ref.txt")"
REPO="${UPSTREAM_REPO:-TrustTunnel/TrustTunnelClient}"

echo "==> Cloning ${REPO} @ ${REF} into ${UPSTREAM}"
rm -rf "$UPSTREAM"
git clone --filter=blob:none --no-checkout "https://github.com/${REPO}.git" "$UPSTREAM"
git -C "$UPSTREAM" fetch --depth 1 origin "$REF"
git -C "$UPSTREAM" checkout "$REF"

echo "==> Injecting client from ${CLIENT}"
rm -rf "$UPSTREAM/FreeTunnel"
mkdir -p "$UPSTREAM/FreeTunnel"
(cd "$CLIENT" && tar --exclude='./.git' --exclude='./upstream-tree' --exclude='./build-coverage' --exclude='./build-test-local' -cf - .) \
  | (cd "$UPSTREAM/FreeTunnel" && tar -xf -)

if ! grep -q 'injected by FreeTunnel CI' "$UPSTREAM/CMakeLists.txt"; then
  cat >> "$UPSTREAM/CMakeLists.txt" <<'EOF'

# --- injected by FreeTunnel CI ---
if (BUILD_TRUSTTUNNEL_QT AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/FreeTunnel/CMakeLists.txt")
    add_subdirectory(FreeTunnel)
endif ()
EOF
fi

python3 "$CLIENT/scripts/patch_core_wrapper.py" "$UPSTREAM"
echo "Upstream tree ready at ${UPSTREAM}"
