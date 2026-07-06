// cppcheck-suppress-file missingIncludeSystem
// Test-side control plane for the mock TrustTunnel core. The mock
// ag::TrustTunnelClient reports its lifecycle here; tests script connect
// results (success / error / block) and inject core events — including on
// already-destroyed clients, which is exactly how late queued core events
// behave in production.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "vpn/vpn.h"

namespace mockcore {

class Controller {
public:
    static Controller &instance()
    {
        static Controller c;
        return c;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_clients.clear();
        m_connectError.clear();
        m_dnsError.clear();
        m_blockConnect = false;
        m_connectCalls = 0;
        m_lastClientId = 0;
        m_nextId = 1;
    }

    // ---- scripting from the test ----
    void setConnectError(std::string err)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connectError = std::move(err);
    }

    void setDnsError(std::string err)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dnsError = std::move(err);
    }

    void setBlockConnect(bool on)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_blockConnect = on;
        if (!on)
            m_cv.notify_all();
    }

    void releaseConnect() { setBlockConnect(false); }

    // ---- introspection ----
    int connectCallCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connectCalls;
    }

    uint64_t lastClientId()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastClientId;
    }

    bool clientAlive(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        return it != m_clients.end() && it->second.alive;
    }

    int disconnectCalls(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        return it != m_clients.end() ? it->second.disconnects : 0;
    }

    // ---- event injection ----
    void fireStateChanged(uint64_t id, ag::VpnSessionState state, int code = ag::VPN_EC_NOERROR,
                          const char *text = nullptr)
    {
        ag::VpnCallbacks cbs = callbacksFor(id);
        if (!cbs.state_changed_handler)
            return;
        ag::VpnStateChangedEvent ev;
        ev.state = state;
        ev.error.code = code;
        ev.error.text = text;
        if (state == ag::VPN_SS_WAITING_RECOVERY)
            ev.waiting_recovery_info.error = ev.error;
        cbs.state_changed_handler(&ev);
    }

    void fireTunnelStats(uint64_t id, uint64_t up, uint64_t down)
    {
        ag::VpnCallbacks cbs = callbacksFor(id);
        if (!cbs.tunnel_stats_handler)
            return;
        ag::VpnTunnelConnectionStatsEvent ev;
        ev.upload = up;
        ev.download = down;
        cbs.tunnel_stats_handler(&ev);
    }

    // ---- hooks used by the mock ag::TrustTunnelClient ----
    uint64_t registerClient(ag::VpnCallbacks cbs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t id = m_nextId++;
        m_clients[id] = Record{std::move(cbs), true, 0};
        m_lastClientId = id;
        return id;
    }

    void clientDestroyed(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        if (it != m_clients.end())
            it->second.alive = false; // keep callbacks: stale events still fire
    }

    std::string onSetSystemDns(uint64_t)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dnsError;
    }

    // May block (scripted via setBlockConnect) to simulate a native connect()
    // stuck on an unreachable server.
    std::string onConnect(uint64_t id)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        ++m_connectCalls;
        m_lastClientId = id;
        m_cv.wait(lock, [this]() { return !m_blockConnect; });
        return m_connectError;
    }

    void onDisconnect(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        if (it != m_clients.end())
            ++it->second.disconnects;
    }

private:
    struct Record {
        ag::VpnCallbacks callbacks;
        bool alive = false;
        int disconnects = 0;
    };

    ag::VpnCallbacks callbacksFor(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        return it != m_clients.end() ? it->second.callbacks : ag::VpnCallbacks{};
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<uint64_t, Record> m_clients;
    std::string m_connectError;
    std::string m_dnsError;
    bool m_blockConnect = false;
    int m_connectCalls = 0;
    uint64_t m_lastClientId = 0;
    uint64_t m_nextId = 1;
};

} // namespace mockcore
