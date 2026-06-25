#!/usr/bin/env python3
"""

Patch the injected TrustTunnelClient C++ wrapper to surface telemetry events.

The public `ag::VpnCallbacks` wrapper exposes only 5 of the core's ~11 events.
In particular `VPN_EVENT_TUNNEL_CONNECTION_STATS` (per-connection upload/download
deltas) is received by the wrapper's dispatcher but dropped, so the Qt client's
TrafficGraph has no data source.

This script adds a `tunnel_stats_handler` to `VpnCallbacks` and routes the event
to it. It is applied in CI after the client is injected into the upstream tree.
The upstream ref is pinned in scripts/upstream_ref.txt and verified by
scripts/verify_upstream_patch.sh on every PR.

Usage: patch_core_wrapper.py <upstream_dir>
"""

import sys
from pathlib import Path

upstream = Path(sys.argv[1] if len(sys.argv) > 1 else "upstream")
header = upstream / "trusttunnel/include/vpn/trusttunnel/client.h"
source = upstream / "trusttunnel/src/client.cpp"


def patch(path: Path, anchor: str, replacement: str, marker: str) -> None:
    text = path.read_text()
    if marker in text:
        print(f"[skip] {path.name} already patched")
        return
    if anchor not in text:
        raise SystemExit(f"[error] anchor not found in {path}:\n{anchor}")
    path.write_text(text.replace(anchor, replacement, 1))
    print(f"[ok] patched {path.name}")


# 1) Add the callback member to the struct.
patch(
    header,
    anchor="    std::function<void(VpnConnectionInfoEvent *)> connection_info_handler;\n",
    replacement=(
        "    std::function<void(VpnConnectionInfoEvent *)> connection_info_handler;\n"
        "    // Added by FreeTunnel: per-connection traffic stats (upload/download deltas).\n"
        "    std::function<void(VpnTunnelConnectionStatsEvent *)> tunnel_stats_handler;\n"
    ),
    marker="tunnel_stats_handler;",
)

# 2) Route VPN_EVENT_TUNNEL_CONNECTION_STATS to the new handler instead of dropping it.
patch(
    source,
    anchor=(
        "    case VPN_EVENT_ENDPOINT_CONNECTION_STATS:\n"
        "    case VPN_EVENT_DNS_UPSTREAM_UNAVAILABLE:\n"
        "    case VPN_EVENT_TUNNEL_CONNECTION_STATS:\n"
        "    case VPN_EVENT_TUNNEL_CONNECTION_CLOSED:\n"
        "        // do nothing\n"
        "        break;\n"
    ),
    replacement=(
        "    case VPN_EVENT_TUNNEL_CONNECTION_STATS: {\n"
        "        auto *event = (VpnTunnelConnectionStatsEvent *) data;\n"
        "        if (m_callbacks.tunnel_stats_handler) {\n"
        "            m_callbacks.tunnel_stats_handler(event);\n"
        "        }\n"
        "        break;\n"
        "    }\n"
        "    case VPN_EVENT_ENDPOINT_CONNECTION_STATS:\n"
        "    case VPN_EVENT_DNS_UPSTREAM_UNAVAILABLE:\n"
        "    case VPN_EVENT_TUNNEL_CONNECTION_CLOSED:\n"
        "        // do nothing\n"
        "        break;\n"
    ),
    marker="m_callbacks.tunnel_stats_handler",
)

print("core wrapper telemetry patch complete")
