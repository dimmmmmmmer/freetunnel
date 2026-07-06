// cppcheck-suppress-file missingIncludeSystem
// Minimal mock of the TrustTunnel core API surface used by the Qt wrapper
// (src/vpn/qt_trusttunnel_*). Lets tests compile QtTrustTunnelClient without
// the native core and drive its state machine via mockcore::Controller.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace ag {

enum VpnSessionState {
    VPN_SS_DISCONNECTED,
    VPN_SS_CONNECTING,
    VPN_SS_CONNECTED,
    VPN_SS_RECOVERING,
    VPN_SS_WAITING_RECOVERY,
    VPN_SS_WAITING_FOR_NETWORK,
};

enum VpnErrorCode {
    VPN_EC_NOERROR = 0,
    VPN_EC_ERROR,
    VPN_EC_INVALID_SETTINGS,
    VPN_EC_ADDR_IN_USE,
    VPN_EC_INVALID_STATE,
    VPN_EC_AUTH_REQUIRED,
    VPN_EC_LOCATION_UNAVAILABLE,
    VPN_EC_CERTIFICATE_VERIFICATION_FAILED,
    VPN_EC_EVENT_LOOP_FAILURE,
    VPN_EC_INITIAL_CONNECT_FAILED,
    VPN_EC_FATAL_CONNECTIVITY_ERROR,
};

enum VpnFilteredConnectionAction {
    VPN_FCA_BYPASS,
    VPN_FCA_TUNNEL,
    VPN_FCA_REJECT,
};

enum LogLevel {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
};

enum VpnMode {
    VPN_MODE_GENERAL,
    VPN_MODE_SELECTIVE,
};

struct VpnError {
    int code = VPN_EC_NOERROR;
    const char *text = nullptr;
};

struct VpnStateChangedEvent {
    VpnSessionState state = VPN_SS_DISCONNECTED;
    VpnError error;
    struct {
        VpnError error;
    } waiting_recovery_info;
};

struct MockIoChunk {
    size_t iov_len = 0;
};

struct VpnClientOutputEvent {
    struct {
        size_t chunks_num = 0;
        const MockIoChunk *chunks = nullptr;
    } packet;
};

struct VpnTunnelConnectionStatsEvent {
    uint64_t upload = 0;
    uint64_t download = 0;
};

struct VpnConnectionInfoEvent {
    int action = VPN_FCA_TUNNEL;
    const char *domain = nullptr;
};

struct SocketProtectEvent {
    int fd = -1;
    const sockaddr *peer = nullptr;
    int result = 0;
};

struct VpnVerifyCertificateEvent {
    const char *cert = nullptr;
    const char *chain = nullptr;
    int result = 0;
};

struct VpnCallbacks {
    std::function<void(SocketProtectEvent *)> protect_handler;
    std::function<void(VpnVerifyCertificateEvent *)> verify_handler;
    std::function<void(VpnStateChangedEvent *)> state_changed_handler;
    std::function<void(VpnClientOutputEvent *)> client_output_handler;
    std::function<void(VpnTunnelConnectionStatsEvent *)> tunnel_stats_handler;
    std::function<void(VpnConnectionInfoEvent *)> connection_info_handler;
};

struct Logger {
    static void set_log_level(LogLevel) {}
};

} // namespace ag
