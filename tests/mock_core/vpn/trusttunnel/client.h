// cppcheck-suppress-file missingIncludeSystem
// Mock ag::TrustTunnelClient: reports lifecycle to mockcore::Controller and
// executes scripted connect/dns results (including blocking connects).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "mock_core_controller.h"
#include "vpn/trusttunnel/config.h"
#include "vpn/vpn.h"

namespace ag {

class TrustTunnelClient {
public:
    struct AutoSetup {};

    struct Error {
        std::string text;
        std::string str() const { return text; }
    };

    // Signature mirrors the real core (vpn/trusttunnel/client.h): BOTH arguments
    // are rvalue references there. Taking them by value here let a call that
    // passes an lvalue compile against the mock and fail the real build — which
    // is precisely the kind of drift a mock must not have.
    TrustTunnelClient(TrustTunnelConfig &&config, VpnCallbacks &&callbacks)
        : m_config(std::move(config)),
          m_id(mockcore::Controller::instance().registerClient(std::move(callbacks)))
    {
        // Snapshot the config the moment it crosses into the core. Kill switch,
        // routing mode, split routes and domain exclusions all end their journey
        // here, and the real core keeps them private afterwards — so this is the
        // only point at which a test can assert the VALUE arrived, instead of
        // asserting that some command with the right name was sent somewhere
        // along the way.
        mockcore::Controller::instance().recordCoreConfig(snapshotOf(m_config));
    }

    TrustTunnelClient(TrustTunnelClient &&) = delete;

    ~TrustTunnelClient() { mockcore::Controller::instance().clientDestroyed(m_id); }

    TrustTunnelClient(const TrustTunnelClient &) = delete;
    TrustTunnelClient &operator=(const TrustTunnelClient &) = delete;

    std::optional<Error> set_system_dns()
    {
        const std::string err = mockcore::Controller::instance().onSetSystemDns(m_id);
        if (err.empty())
            return std::nullopt;
        return Error{err};
    }

    std::optional<Error> connect(AutoSetup)
    {
        const std::string err = mockcore::Controller::instance().onConnect(m_id);
        if (err.empty())
            return std::nullopt;
        return Error{err};
    }

    void disconnect() { mockcore::Controller::instance().onDisconnect(m_id); }

    uint64_t mockId() const { return m_id; }

private:
    static mockcore::CoreConfigSnapshot snapshotOf(const TrustTunnelConfig &cfg)
    {
        mockcore::CoreConfigSnapshot snap;
        snap.killswitch_enabled = cfg.killswitch_enabled;
        snap.mode = static_cast<int>(cfg.mode);
        snap.loglevel = static_cast<int>(cfg.loglevel);
        snap.exclusions = cfg.exclusions;
        // A non-tun listener carries no routes at all, which is why this is a
        // get_if and not a get: reading the wrong alternative would throw inside
        // a core constructor, and the wrapper is allowed to hand over either.
        if (const auto *tun = std::get_if<TrustTunnelConfig::TunListener>(&cfg.listener)) {
            snap.included_routes = tun->included_routes;
            snap.excluded_routes = tun->excluded_routes;
        }
        return snap;
    }

    TrustTunnelConfig m_config;
    uint64_t m_id = 0;
};

} // namespace ag
