// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_client.h"

#include <QDir>
#include <QStandardPaths>

#include <string>

#include "net/network_manager.h"
#include "net/tls.h"

#if defined(__linux__)
#include <net/if.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include "net/os_tunnel.h"
#include <windows.h>

bool qt_trusttunnel_is_process_elevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return ok && elevation.TokenIsElevated;
}
#endif

QString qt_trusttunnel_default_core_log_path()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/vpn-core.log");
}

static QString vpnErrorCodeName(int code)
{
    switch (code) {
    case VPN_EC_NOERROR:
        return QStringLiteral("OK");
    case VPN_EC_ERROR:
        return QStringLiteral("ERROR");
    case VPN_EC_INVALID_SETTINGS:
        return QStringLiteral("INVALID_SETTINGS");
    case VPN_EC_ADDR_IN_USE:
        return QStringLiteral("ADDR_IN_USE");
    case VPN_EC_INVALID_STATE:
        return QStringLiteral("INVALID_STATE");
    case VPN_EC_AUTH_REQUIRED:
        return QStringLiteral("AUTH_REQUIRED");
    case VPN_EC_LOCATION_UNAVAILABLE:
        return QStringLiteral("LOCATION_UNAVAILABLE");
    case VPN_EC_CERTIFICATE_VERIFICATION_FAILED:
        return QStringLiteral("CERTIFICATE_VERIFICATION_FAILED");
    case VPN_EC_EVENT_LOOP_FAILURE:
        return QStringLiteral("EVENT_LOOP_FAILURE");
    case VPN_EC_INITIAL_CONNECT_FAILED:
        return QStringLiteral("INITIAL_CONNECT_FAILED");
    case VPN_EC_FATAL_CONNECTIVITY_ERROR:
        return QStringLiteral("FATAL_CONNECTIVITY_ERROR");
    default:
        return QStringLiteral("ERR_%1").arg(code);
    }
}

QString qt_trusttunnel_format_vpn_error(int code, const QString &text)
{
    if (code == VPN_EC_NOERROR && text.trimmed().isEmpty())
        return QString();
    const QString trimmed = text.trimmed();
    const QString codeName = vpnErrorCodeName(code);
    if (trimmed.isEmpty())
        return codeName;
    if (code == VPN_EC_NOERROR || code == VPN_EC_ERROR)
        return trimmed;
    return QStringLiteral("%1 (%2)").arg(trimmed, codeName);
}

ag::LogLevel qt_trusttunnel_parse_log_level(const QString &level)
{
    const QString l = level.toLower();
    if (l == "error")
        return ag::LOG_LEVEL_ERROR;
    if (l == "warn" || l == "warning")
        return ag::LOG_LEVEL_WARN;
    if (l == "debug")
        return ag::LOG_LEVEL_DEBUG;
    if (l == "trace")
        return ag::LOG_LEVEL_TRACE;
    return ag::LOG_LEVEL_INFO;
}

#if defined(__APPLE__)
static void protectOutboundSocketApple(ag::SocketProtectEvent *event)
{
    const uint32_t idx = ag::vpn_network_manager_get_outbound_interface();
    if (idx == 0)
        return;
    const int level = event->peer->sa_family == AF_INET6 ? IPPROTO_IPV6 : IPPROTO_IP;
    const int opt = event->peer->sa_family == AF_INET6 ? IPV6_BOUND_IF : IP_BOUND_IF;
    if (setsockopt(event->fd, level, opt, &idx, sizeof(idx)) != 0)
        event->result = -1;
}
#endif

#if defined(__linux__)
static void protectOutboundSocketLinux(ag::SocketProtectEvent *event)
{
    if (geteuid() != 0) {
        event->result = -1;
        return;
    }
    const uint32_t idx = ag::vpn_network_manager_get_outbound_interface();
    if (idx == 0) {
        event->result = -1;
        return;
    }
    char ifname[IFNAMSIZ]{};
    if (if_indextoname(static_cast<unsigned int>(idx), ifname) == nullptr)
        event->result = -1;
    else {
        const socklen_t optlen =
                static_cast<socklen_t>(std::char_traits<char>::length(ifname) + 1);
        if (setsockopt(event->fd, SOL_SOCKET, SO_BINDTODEVICE, ifname, optlen) != 0)
            event->result = -1;
    }
}
#endif

#ifdef _WIN32
static void protectOutboundSocketWin(ag::SocketProtectEvent *event)
{
    const uint32_t outbound = ag::vpn_network_manager_get_outbound_interface();
    if (!ag::vpn_win_socket_protect(event->fd, event->peer)) {
        qWarning("Windows socket protect failed (outbound ifindex %u)", outbound);
        event->result = -1;
    }
}
#endif

void qt_trusttunnel_protect_outbound_socket(ag::SocketProtectEvent *event)
{
    if (!event)
        return;
    event->result = 0;
#if defined(__APPLE__)
    protectOutboundSocketApple(event);
#elif defined(__linux__)
    protectOutboundSocketLinux(event);
#elif defined(_WIN32)
    protectOutboundSocketWin(event);
#endif
}

void qt_trusttunnel_verify_server_certificate(ag::VpnVerifyCertificateEvent *event)
{
    if (!event)
        return;
    const char *err = ag::tls_verify_cert(event->cert, event->chain, nullptr);
    if (err != nullptr) {
        qWarning("TrustTunnel certificate verification failed: %s", err);
        event->result = -1;
        return;
    }
    event->result = 0;
}

QString qt_trusttunnel_connection_info_line(ag::VpnConnectionInfoEvent *event)
{
    if (!event)
        return QStringLiteral("connection info");
    QString action;
    switch (event->action) {
    case ag::VPN_FCA_BYPASS: action = QStringLiteral("bypass"); break;
    case ag::VPN_FCA_TUNNEL: action = QStringLiteral("tunnel"); break;
    case ag::VPN_FCA_REJECT: action = QStringLiteral("reject"); break;
    default: action = QStringLiteral("unknown"); break;
    }
    const QString domain = event->domain ? QString::fromUtf8(event->domain) : QStringLiteral("-");
    return QStringLiteral("%1 %2").arg(action, domain);
}
