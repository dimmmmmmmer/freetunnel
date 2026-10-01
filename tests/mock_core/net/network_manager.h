// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <atomic>
#include <cstdint>

namespace mockcore {

// Process-wide, as in the real network manager: the outbound interface is one
// value shared by every core client in the process, not a per-client setting.
// Remembering what was set is what lets a test see which adapter the wrapper
// pointed the core at — a stub that dropped it made every such check vacuous.
inline std::atomic<uint32_t> g_outboundInterface{0};
// Whether a TUN interface is up in this process. The real core raises it when
// its tunnel comes up; the mock never makes one, so tests raise it themselves.
inline std::atomic<bool> g_tunnelActive{false};

} // namespace mockcore

namespace ag {

inline uint32_t vpn_network_manager_get_outbound_interface()
{
    return mockcore::g_outboundInterface.load();
}

inline void vpn_network_manager_set_outbound_interface(uint32_t idx)
{
    mockcore::g_outboundInterface.store(idx);
}

inline bool vpn_network_manager_get_tunnel_active()
{
    return mockcore::g_tunnelActive.load();
}

} // namespace ag
