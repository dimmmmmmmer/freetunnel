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

// Declared outside TrustTunnelConfig, where the real ones are nested. GCC decides
// whether a nested struct with default member initialisers can be default
// constructed before the enclosing class is complete, and then refuses to
// default-construct a variant of it in the class's own inline build_config.
struct MockTunListener {
    std::string device_name;
    std::vector<std::string> included_routes;
    std::vector<std::string> excluded_routes;
    bool use_existing = false;
    std::optional<std::string> netns;
};
struct MockSocksListener {
    std::string address;
};

struct TrustTunnelConfig {
    using TunListener = MockTunListener;
    using SocksListener = MockSocksListener;

    LogLevel loglevel = LOG_LEVEL_INFO;
    // TunListener first, unlike the real one, so that a default-constructed
    // config is a tun config: that is what every test here means by "a config".
    std::variant<TunListener, SocksListener> listener;
    struct {
        std::vector<std::string> dns_upstreams;
        // The endpoint's TLS settings. The real core keeps a pinned certificate
        // as a loaded CA store; this keeps its text, so a test can compare it.
        bool skip_verification = false;
        std::string certificate;
        std::string client_random;
        std::string client_random_mask;
    } location;
    std::string exclusions;
    VpnMode mode = VPN_MODE_GENERAL;
    bool killswitch_enabled = false;
    std::string killswitch_allow_ports;
    std::string log_file_path;
    std::optional<std::string> ssl_session_storage_path;

    // [endpoint]'s TLS keys, read as the real build_endpoint reads them, since
    // they decide whom the tunnel trusts and nothing past the constructor shows
    // them: a certificate is pinned only when verification is not skipped, and
    // client_random is "prefix" or "prefix/mask", split at the slash, with an
    // empty mask failing the whole config. They used to be ignored here, so no
    // test could tell whether a pin or a mask written by the app ever arrived.
    static bool readEndpointTls(const toml::table &t, TrustTunnelConfig &c)
    {
        auto &loc = c.location;
        loc.skip_verification = t["endpoint.skip_verification"].value<bool>().value_or(false);
        if (!loc.skip_verification)
            loc.certificate = t["endpoint.certificate"].value<std::string>().value_or("");
        const std::string random = t["endpoint.client_random"].value<std::string>().value_or("");
        const size_t slash = random.find('/');
        loc.client_random = random.substr(0, slash);
        if (slash == std::string::npos)
            return true;
        loc.client_random_mask = random.substr(slash + 1);
        return !loc.client_random_mask.empty();
    }

    // The real build_config rejects a table that parses as TOML but isn't a
    // config. Returning a value unconditionally made the wrapper's
    // "Invalid TrustTunnel config structure" branch dead under test, so require
    // the one field every real config has.
    //
    // The keys below are read the way the real one reads them because the
    // wrapper has to undo some of them before the core sees the config, and a
    // test can only tell "cleared" from "never read" if they were read.
    static std::optional<TrustTunnelConfig> build_config(const toml::table &t)
    {
        if (!t.contains("hostname") && !t.contains("endpoint.hostname"))
            return std::nullopt;
        TrustTunnelConfig c;
        if (!readEndpointTls(t, c))
            return std::nullopt;
        c.ssl_session_storage_path = t["ssl_session_cache_path"].value<std::string>();
        // Kept as the raw array text: the wrapper only has to empty it, or pass it
        // on untouched when the user lets the config decide.
        c.killswitch_allow_ports = t["killswitch_allow_ports"].value<std::string>().value_or("");
        if (const auto address = t["listener.socks.address"].value<std::string>()) {
            c.listener = SocksListener{*address};
            return c;
        }
        auto &tun = std::get<TunListener>(c.listener);
        tun.device_name = t["listener.tun.device_name"].value<std::string>().value_or("");
        tun.use_existing = t["listener.tun.use_existing"].value<bool>().value_or(false);
        tun.netns = t["listener.tun.netns"].value<std::string>();
        tun.included_routes = stringArray(t["listener.tun.included_routes"].value<std::string>());
        return c;
    }

private:
    // A one-line array of strings, which is all a test config here ever has.
    static std::vector<std::string> stringArray(const std::optional<std::string> &raw)
    {
        std::vector<std::string> out;
        if (!raw)
            return out;
        size_t open = raw->find('"');
        while (open != std::string::npos) {
            const size_t close = raw->find('"', open + 1);
            if (close == std::string::npos)
                break;
            out.push_back(raw->substr(open + 1, close - open - 1));
            open = raw->find('"', close + 1);
        }
        return out;
    }
};

} // namespace ag
