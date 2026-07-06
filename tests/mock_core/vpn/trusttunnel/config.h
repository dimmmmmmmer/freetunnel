// cppcheck-suppress-file missingIncludeSystem
// Mock of the core's TrustTunnelConfig — just the fields the Qt wrapper touches.
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <toml++/toml.h>

#include "vpn/vpn.h"

namespace ag {

struct TrustTunnelConfig {
    struct TunListener {
        std::vector<std::string> included_routes;
        std::vector<std::string> excluded_routes;
    };

    LogLevel loglevel = LOG_LEVEL_INFO;
    std::variant<TunListener> listener; // defaults to a TunListener, like a tun config
    struct {
        std::vector<std::string> dns_upstreams;
    } location;
    std::string exclusions;
    VpnMode mode = VPN_MODE_GENERAL;
    bool killswitch_enabled = false;
    std::string log_file_path;

    static std::optional<TrustTunnelConfig> build_config(const toml::table &)
    {
        return TrustTunnelConfig{};
    }
};

} // namespace ag
