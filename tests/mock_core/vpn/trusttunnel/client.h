// cppcheck-suppress-file missingIncludeSystem
// Mock ag::TrustTunnelClient: reports lifecycle to mockcore::Controller and
// executes scripted connect/dns results (including blocking connects).
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
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
        // What the real constructor does with a log path (trusttunnel/src/
        // client.cpp): open the file itself and point the process-wide logger
        // at it. The destructor closes the file and leaves the logger pointing
        // at it — exactly as the real one does, because that is the hazard a
        // long-lived process has to stay clear of.
        if (!m_config.log_file_path.empty()) {
            m_logfile_handler.emplace(m_config.log_file_path);
            m_logtofile.emplace(m_logfile_handler->get_file());
            Logger::set_callback(*m_logtofile);
        }
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
        if (err.empty()) {
            m_running = true;
            return std::nullopt;
        }
        return Error{err};
    }

    void disconnect()
    {
        m_running = false;
        mockcore::Controller::instance().onDisconnect(m_id);
    }

    // Mirrors what vendor/trusttunnel/03-*.patch adds to the real wrapper,
    // including that it does nothing unless a session is running: the real one
    // asks the core only while it holds a Vpn, from connect() until disconnect().
    void update_exclusions(VpnMode mode, std::string_view exclusions)
    {
        if (!m_running)
            return;
        mockcore::Controller::instance().onUpdateExclusions(m_id, static_cast<int>(mode),
                                                           std::string(exclusions));
    }

    void notify_network_change(VpnNetworkState state)
    {
        mockcore::Controller::instance().onNetworkChange(m_id, state);
    }

    uint64_t mockId() const { return m_id; }

private:
    // As in the real header, fclose() and all. A file that failed to open is not
    // passed to fclose here, which the real one does do; that is a crash of its
    // own, and not one a test of the wrapper wants to reproduce.
    class FileHandler {
    public:
        explicit FileHandler(const std::string &filename) : m_file(std::fopen(filename.c_str(), "w"))
        {
            if (m_file)
                Logger::noteFileOpened(m_file);
        }
        ~FileHandler()
        {
            if (!m_file)
                return;
            std::fclose(m_file);
            Logger::noteFileClosed(m_file);
        }
        FileHandler(const FileHandler &) = delete;
        FileHandler &operator=(const FileHandler &) = delete;
        FILE *get_file() { return m_file; }

    private:
        FILE *m_file;
    };

    static mockcore::CoreConfigSnapshot snapshotOf(const TrustTunnelConfig &cfg)
    {
        mockcore::CoreConfigSnapshot snap;
        snap.killswitch_enabled = cfg.killswitch_enabled;
        snap.mode = static_cast<int>(cfg.mode);
        snap.loglevel = static_cast<int>(cfg.loglevel);
        snap.exclusions = cfg.exclusions;
        snap.ssl_session_storage_path = cfg.ssl_session_storage_path;
        snap.killswitch_allow_ports = cfg.killswitch_allow_ports;
        // A non-tun listener carries no routes at all, which is why this is a
        // get_if and not a get: reading the wrong alternative would throw inside
        // a core constructor. The wrapper refuses a SOCKS listener, but a test
        // that proves so by disabling the refusal must still get as far as here.
        if (const auto *tun = std::get_if<TrustTunnelConfig::TunListener>(&cfg.listener)) {
            snap.included_routes = tun->included_routes;
            snap.excluded_routes = tun->excluded_routes;
            snap.device_name = tun->device_name;
            snap.use_existing = tun->use_existing;
            snap.netns = tun->netns;
        }
        return snap;
    }

    TrustTunnelConfig m_config;
    uint64_t m_id = 0;
    bool m_running = false; // connect() succeeded and disconnect() has not run
    // In the real header's order, so the file is closed at the same point of
    // destruction as there.
    std::optional<FileHandler> m_logfile_handler;
    std::optional<Logger::LogToFile> m_logtofile;
};

} // namespace ag
