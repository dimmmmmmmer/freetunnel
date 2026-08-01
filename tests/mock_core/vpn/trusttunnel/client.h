// cppcheck-suppress-file missingIncludeSystem
// Mock ag::TrustTunnelClient: reports lifecycle to mockcore::Controller and
// executes scripted connect/dns results (including blocking connects).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

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
    TrustTunnelConfig m_config;
    uint64_t m_id = 0;
};

} // namespace ag
