// cppcheck-suppress-file missingIncludeSystem
// Test-side control plane for the mock TrustTunnel core. The mock
// ag::TrustTunnelClient reports its lifecycle here; tests script connect
// results (success / error / block) and inject core events — including on
// already-destroyed clients, which is exactly how late queued core events
// behave in production.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "net/network_manager.h"
#include "vpn/vpn.h"

namespace mockcore {

// A copy of the ag::TrustTunnelConfig the core client was constructed with.
//
// The real core swallows the config into private state and acts on it, so the
// instant it crosses the constructor is the only place a test can observe the
// VALUES the GUI asked for. Observing them matters more than it looks: every
// double along the kill-switch chain used to record the command NAME only, and a
// name says nothing about whether the switch arrived as true — a key read under
// the wrong spelling yields false from QJsonValue::toBool() with nothing
// anywhere failing, which is the kill switch off in every session while the
// toggle in the GUI still reads ON.
//
// Deliberately plain types (no ag:: anything): test_helper_server reads this
// back from a file without compiling against the mock core at all.
struct CoreConfigSnapshot {
    bool captured = false; // false until a core client was actually built
    bool killswitch_enabled = false;
    int mode = -1;     // ag::VpnMode
    int loglevel = -1; // ag::LogLevel
    std::string exclusions;
    std::vector<std::string> included_routes;
    std::vector<std::string> excluded_routes;
    // Keys an imported config can carry that would have root act on a name the
    // file chose, or open the kill switch. The wrapper clears them; these show
    // whether it did.
    std::optional<std::string> ssl_session_storage_path;
    std::string killswitch_allow_ports;
    std::string device_name;
    bool use_existing = false;
    std::optional<std::string> netns;
};

// One call of TrustTunnelClient::update_exclusions() on a running session: the
// mode and exclusion list a session was handed live, as the wrapper of the real
// core (vendor/trusttunnel/03-*.patch) hands them to vpn_update_exclusions().
struct ExclusionsUpdate {
    uint64_t client = 0;
    int mode = -1; // ag::VpnMode
    std::string exclusions;
};

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
        m_lastConfig = CoreConfigSnapshot{};
        m_configCaptures = 0;
        m_certError.clear();
        m_lastVerifiedCert.clear();
        m_lastVerifiedChain.clear();
        m_verifyCalls = 0;
        ag::Logger::resetCounters();
        g_outboundInterface.store(0);
        g_tunnelActive.store(false);
        ag::g_mockActiveIf.store(0);
        ag::g_mockActiveIfLooks.store(0);
        m_updates.clear();
        m_updateHook = nullptr;
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

    // Script the outcome of the core's TLS check. Empty means the chain is good;
    // anything else is the message the real core returns for a chain it refuses,
    // and is what drives qt_trusttunnel_verify_server_certificate down its reject
    // branch. Without this the mock accepted every certificate, so that branch
    // was unreachable and deleting the verify handler entirely broke nothing.
    void setCertError(std::string err)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_certError = std::move(err);
    }

    // Which adapter the core's own Windows detection calls active (0: none —
    // no adapter has a default route). This is what the machine's network
    // looks like to the wrapper's uplink follower; changing it is how a test
    // unplugs a cable or docks a laptop.
    void setActiveUplink(uint32_t ifIndex) { ag::g_mockActiveIf.store(ifIndex); }
    // How many times the core was asked for the active adapter.
    int activeUplinkLooks() const { return ag::g_mockActiveIfLooks.load(); }

    // The core's process-wide state, which the mock otherwise only reports.
    void setOutboundInterface(uint32_t ifIndex) { g_outboundInterface.store(ifIndex); }
    void setTunnelActive(bool up) { g_tunnelActive.store(up); }

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

    // Every network change the client was told about, oldest first, each
    // paired with the outbound interface the core had at that moment — the
    // core's contract is that the interface is set BEFORE the notification,
    // and the pair is what lets a test hold the wrapper to that order. The
    // count of system DNS reads made by then does the same for the DNS servers,
    // which the core's DNS restarts with on the notification.
    struct NetworkChange {
        ag::VpnNetworkState state;
        uint32_t outbound;
        std::size_t dnsReads;
    };
    std::vector<NetworkChange> networkChanges(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        return it != m_clients.end() ? it->second.networkChanges : std::vector<NetworkChange>{};
    }

    // Every set_system_dns() call on the client, oldest first, as the outbound
    // interface the core had when it was made.
    std::vector<uint32_t> systemDnsReads(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        return it != m_clients.end() ? it->second.dnsReads : std::vector<uint32_t>{};
    }

    // The outbound interface the core would bind its own sockets to now.
    static uint32_t outboundInterface() { return ag::vpn_network_manager_get_outbound_interface(); }

    // The config the most recently built core client was handed. `captured` is
    // false until a client has actually been constructed, so a test cannot pass
    // by reading a default-constructed snapshot that nothing ever filled in.
    CoreConfigSnapshot lastCoreConfig()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastConfig;
    }

    int coreConfigCaptureCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_configCaptures;
    }

    std::string lastVerifiedCert()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastVerifiedCert;
    }

    std::string lastVerifiedChain()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lastVerifiedChain;
    }

    int verifyCallCount()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_verifyCalls;
    }

    // Core log lines that went through a FILE the core had already closed, and
    // lines that went to the process's stderr — which, in the helper, is a pipe
    // or a file nobody reads. Both since the last reset().
    int coreLogWritesThroughClosedFile() { return ag::Logger::writesThroughClosedFile(); }
    int coreLogLinesToStderr() { return ag::Logger::linesToStderr(); }
    int coreLoggerCallbackSets() { return ag::Logger::callbackSets(); }

    // Run inside every update_exclusions(), on the caller's thread, at the moment
    // the core is handed the update. The real core completes connections in the
    // order its queue was filled, so what the rest of the client says at that
    // instant is what a connection completed right after the update is routed by.
    void setUpdateHook(std::function<void(uint64_t client)> hook)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_updateHook = std::move(hook);
    }

    // Every live update a running session was handed, oldest first. A session is
    // built from its config once; after that, this is the only way a new rule
    // list or mode reaches it without building another.
    std::vector<ExclusionsUpdate> exclusionUpdates()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_updates;
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

    // Ask the client what to do with one connection, the way the wrapper's
    // extra loop does. Returns the decision the handler wrote — including the
    // untouched VPN_CA_DEFAULT when no handler is installed, which is exactly
    // what the real wrapper would then complete the request with.
    ag::VpnConnectDecision fireConnectRequest(uint64_t id, const ag::VpnConnectRequestSnapshot &req)
    {
        ag::VpnCallbacks cbs = callbacksFor(id);
        ag::VpnConnectDecision decision;
        if (!cbs.connect_request_handler)
            return decision;
        cbs.connect_request_handler(req, &decision);
        return decision;
    }

    // Log a line the way any part of the core does: through the process-wide
    // logger, to whatever its callback was last set to. The core logs from its
    // own threads whenever it has something to say, including after the client
    // that set the callback is gone, so this belongs to no client.
    void coreLog(ag::LogLevel level, const std::string &text) { ag::Logger("MOCK_CORE").log(level, text); }

    // Returned by fireVerifyCertificate when the client installed no verify
    // handler at all. A missing handler leaves event->result at whatever the core
    // initialised it to — 0, i.e. ACCEPT — so "no handler" and "certificate is
    // fine" are indistinguishable from the result alone. A value no handler can
    // produce is what turns the missing assignment in makeCallbacks() into a
    // failing test instead of a silently accepted certificate.
    static constexpr int kNoVerifyHandler = -424242;

    // Drive the core's certificate callback the way the core's TLS layer does
    // during the tunnel handshake, and hand back the verdict the wrapper wrote
    // into the event.
    int fireVerifyCertificate(uint64_t id, const char *cert, const char *chain)
    {
        ag::VpnCallbacks cbs = callbacksFor(id);
        if (!cbs.verify_handler)
            return kNoVerifyHandler;
        ag::VpnVerifyCertificateEvent ev;
        ev.cert = cert;
        ev.chain = chain;
        ev.result = 0; // the core's own starting value: accept unless told otherwise
        cbs.verify_handler(&ev);
        return ev.result;
    }

    // Ask the client to protect one of the core's own sockets, as the core does
    // before each connection it opens itself. Returns the handler's verdict, or
    // kNoProtectHandler when there is no handler. The socket is not a real one:
    // what a test looks at is what protecting it did to the outbound interface.
    static constexpr int kNoProtectHandler = -434343;
    int fireProtect(uint64_t id)
    {
        ag::VpnCallbacks cbs = callbacksFor(id);
        if (!cbs.protect_handler)
            return kNoProtectHandler;
        sockaddr_in peer{};
        peer.sin_family = AF_INET;
        ag::SocketProtectEvent ev;
        ev.peer = reinterpret_cast<const sockaddr *>(&peer);
        cbs.protect_handler(&ev);
        return ev.result;
    }

    // ---- hooks used by the mock ag::TrustTunnelClient ----
    uint64_t registerClient(ag::VpnCallbacks cbs)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const uint64_t id = m_nextId++;
        m_clients[id] = Record{std::move(cbs), true, 0, {}, {}};
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

    std::string onSetSystemDns(uint64_t id)
    {
        const uint32_t outbound = ag::vpn_network_manager_get_outbound_interface();
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        if (it != m_clients.end())
            it->second.dnsReads.push_back(outbound);
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

    void onNetworkChange(uint64_t id, ag::VpnNetworkState state)
    {
        const uint32_t outbound = ag::vpn_network_manager_get_outbound_interface();
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        if (it != m_clients.end())
            it->second.networkChanges.push_back(
                    NetworkChange{state, outbound, it->second.dnsReads.size()});
    }

    void onUpdateExclusions(uint64_t id, int mode, std::string exclusions)
    {
        ExclusionsUpdate update{id, mode, std::move(exclusions)};
        int count = 0;
        std::function<void(uint64_t)> hook;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_updates.push_back(update);
            count = static_cast<int>(m_updates.size());
            hook = m_updateHook;
        }
        if (hook)
            hook(id); // outside the lock: it may well ask the client something
        // Mirrored to a file for the same reason as the config below: in
        // test_helper_server the session lives in the helper PROCESS.
        dumpExclusionsUpdate(update, count);
    }

    // Called by the mock ag::TrustTunnelClient constructor, i.e. at the exact
    // moment the config crosses into the core.
    void recordCoreConfig(CoreConfigSnapshot snap)
    {
        snap.captured = true;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastConfig = snap;
            ++m_configCaptures;
        }
        // The privileged helper is a separate PROCESS in test_helper_server, so
        // its Controller is unreachable from the test that drives it. Mirroring
        // the snapshot to a file named by the environment is what lets that test
        // assert the kill switch survived the whole GUI -> IPC -> helper -> core
        // chain, rather than only that the helper saw a command by that name.
        dumpCoreConfig(snap);
    }

    // Called by the mock ag::tls_verify_cert (net/tls.h). Returns the scripted
    // failure message, or nullptr when the chain is good; the pointer stays valid
    // until the next setCertError()/reset(), which is as much as the real core
    // promises about its own error strings.
    const char *verifyCertificate(const char *cert, const char *chain)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ++m_verifyCalls;
        // Recorded so a test can prove the EVENT's payload is what gets checked.
        // A handler that ignores the event and checks something else would still
        // return the right verdict for the scripted case and pass otherwise.
        m_lastVerifiedCert = cert != nullptr ? cert : "";
        m_lastVerifiedChain = chain != nullptr ? chain : "";
        return m_certError.empty() ? nullptr : m_certError.c_str();
    }

private:
    struct Record {
        ag::VpnCallbacks callbacks;
        bool alive = false;
        int disconnects = 0;
        std::vector<NetworkChange> networkChanges;
        std::vector<uint32_t> dnsReads; // outbound interface at each set_system_dns()
    };

    ag::VpnCallbacks callbacksFor(uint64_t id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_clients.find(id);
        return it != m_clients.end() ? it->second.callbacks : ag::VpnCallbacks{};
    }

    // The dump is one `key=value` record per line, so a value carrying a newline
    // would be read back as a bogus extra record. Nothing legitimate puts one in
    // a route or an exclusion; folding it to a space keeps the reader honest.
    static std::string oneLine(std::string s)
    {
        for (char &c : s) {
            if (c == '\n' || c == '\r')
                c = ' ';
        }
        return s;
    }

    static std::string joinList(const std::vector<std::string> &items)
    {
        std::string out;
        for (const auto &item : items) {
            if (!out.empty())
                out.push_back(',');
            out += item;
        }
        return out;
    }

    // An optional the core reads as "absent" must not look like an empty value
    // that is present: the core treats those two differently.
    static std::string orUnset(const std::optional<std::string> &value)
    {
        return value ? oneLine(*value) : std::string("(unset)");
    }

    static void dumpExclusionsUpdate(const ExclusionsUpdate &update, int count)
    {
        const char *path = std::getenv("FT_TEST_CORE_UPDATE_DUMP");
        if (path == nullptr || *path == '\0')
            return;
        const std::string target(path);
        const std::string partial = target + ".partial";
        {
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            out << "updates=" << count << '\n';
            out << "mode=" << (update.mode == ag::VPN_MODE_SELECTIVE ? "selective" : "general")
                << '\n';
            out << "exclusions=" << oneLine(update.exclusions) << '\n';
        }
        // Published by rename, as dumpCoreConfig() below explains.
        std::remove(target.c_str());
        std::rename(partial.c_str(), target.c_str());
    }

    static void dumpCoreConfig(const CoreConfigSnapshot &snap)
    {
        const char *path = std::getenv("FT_TEST_CORE_CONFIG_DUMP");
        if (path == nullptr || *path == '\0')
            return;
        const std::string target(path);
        const std::string partial = target + ".partial";
        {
            std::ofstream out(partial, std::ios::binary | std::ios::trunc);
            if (!out)
                return;
            out << "killswitch_enabled=" << (snap.killswitch_enabled ? 1 : 0) << '\n';
            // Written as a NAME rather than the enum's number: the reader is a
            // test that does not compile against the mock core, and must not have
            // to hardcode whichever integer VPN_MODE_SELECTIVE happens to be.
            out << "mode=" << (snap.mode == ag::VPN_MODE_SELECTIVE ? "selective" : "general")
                << '\n';
            out << "loglevel=" << snap.loglevel << '\n';
            out << "exclusions=" << oneLine(snap.exclusions) << '\n';
            out << "included_routes=" << oneLine(joinList(snap.included_routes)) << '\n';
            out << "excluded_routes=" << oneLine(joinList(snap.excluded_routes)) << '\n';
            out << "ssl_session_cache_path=" << orUnset(snap.ssl_session_storage_path) << '\n';
            out << "killswitch_allow_ports=" << oneLine(snap.killswitch_allow_ports) << '\n';
            out << "device_name=" << oneLine(snap.device_name) << '\n';
            out << "use_existing=" << (snap.use_existing ? 1 : 0) << '\n';
            out << "netns=" << orUnset(snap.netns) << '\n';
        }
        // Publish by rename so a reader polling for the file never gets a
        // half-written one. The remove() is for Windows, whose rename() refuses
        // an existing target; the file only ever appears fully formed either way.
        std::remove(target.c_str());
        std::rename(partial.c_str(), target.c_str());
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
    CoreConfigSnapshot m_lastConfig;
    int m_configCaptures = 0;
    std::string m_certError;
    std::string m_lastVerifiedCert;
    std::string m_lastVerifiedChain;
    int m_verifyCalls = 0;
    std::vector<ExclusionsUpdate> m_updates;
    std::function<void(uint64_t)> m_updateHook;
};

} // namespace mockcore
