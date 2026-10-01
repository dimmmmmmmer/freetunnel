// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <cstdlib>
#include <string>
#include <utility>

#include "net/network_manager.h"
#include "vpn/vpn.h"

namespace ag {

class TrustTunnelClient;

class AutoNetworkMonitor {
public:
    // explicit, and takes bound_if BY VALUE — same as the real core. A mock that
    // is more permissive than the thing it stands in for lets code compile here
    // and fail the real build; that already cost one red CI run.
    explicit AutoNetworkMonitor(TrustTunnelClient *, std::string boundIf)
        : m_boundIf(std::move(boundIf))
    {
    }

    // What the real start() does to the outbound interface, and nothing else:
    // an override names the adapter (by index, as the wrapper passes it on
    // Windows); without one the core takes the adapter it detects as active,
    // and leaves the setting alone when it detects none.
    bool start()
    {
        const uint32_t idx = m_boundIf.empty()
                ? vpn_win_detect_active_if()
                : static_cast<uint32_t>(std::strtoul(m_boundIf.c_str(), nullptr, 10));
        if (idx != 0)
            vpn_network_manager_set_outbound_interface(idx);
        return true;
    }
    void stop() {}

private:
    std::string m_boundIf;
};

} // namespace ag
