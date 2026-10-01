// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Bind outbound sockets to the physical (non-VPN) interface so they take the
// original network path even while a full-tunnel VPN is up. Used for the config
// pinger, which should measure real reachability rather than latency through an
// existing tunnel.
//
// Reliable bypass needs per-socket interface binding (a source-address bind is
// not enough: a full tunnel routes by destination): IP_BOUND_IF on macOS,
// IP_UNICAST_IF on Windows, and a best-effort source-bind on Linux (where
// SO_BINDTODEVICE needs root the GUI doesn't have).

#include <QAbstractSocket>
#include <QHostAddress>
#include <QList>

#include <functional>

class QObject;
class QTcpSocket;

namespace freetunnel {

// The physical interface to send through. index <= 0 means none was found.
struct PhysicalRoute {
    int index = 0;
    QHostAddress v4;
    QHostAddress v6;
};

PhysicalRoute physicalOutboundRoute();

// A default route (0.0.0.0/0 or ::/0) as the routing table lists it.
struct DefaultRoute {
    int index = 0;          // the interface it goes out of
    bool v6 = false;
    quint64 metric = 0;     // the route's metric plus its interface's
    bool connected = false; // the interface has a connection (Windows' own word)
};

// The interface the system uses for the internet, picked the way the VPN core
// picks it on Windows (vpn_win_detect_active_if): among the default routes on
// connected interfaces that @p eligible accepts, the lowest metric, IPv4 before
// IPv6. 0 when there is none. Platform-free, so it is tested everywhere.
int pickDefaultRouteInterface(const QList<DefaultRoute> &routes,
                              const std::function<bool(int index)> &eligible);

#if defined(Q_OS_WIN)
// That pick from this machine's routing table, among the adapters the core takes
// for physical ones: Ethernet, Wi-Fi and mobile broadband that are up and have an
// address. Never Wintun, nor a Hyper-V or WSL switch, which carries no default
// route. Asks the core nothing, so it logs nothing when there is no network.
int windowsDefaultRouteInterface();
#endif

// Whether an interface's name marks it as a tunnel or VPN adapter (tun, utun,
// wg, ...), which is never the physical route. By name only: on Linux and macOS
// the name is what there is to go by, but on Windows Qt's names do not reliably
// say what kind of adapter it is, so a caller there must check the adapter's
// type as well (interfaceEligibleForRoute in NetBind.cpp does).
bool interfaceIsVirtual(const QString &name);

// Bind an existing socket to the physical interface for the given protocol.
// No-op when no physical interface is available or binding isn't supported.
// Works on any QTcpSocket subclass (e.g. QSslSocket) via virtual dispatch.
void bindSocketToPhysicalRoute(QTcpSocket *sock,
                               QAbstractSocket::NetworkLayerProtocol proto);

// A TCP socket bound to the physical interface for the given protocol. Falls
// back to an ordinary (unbound) socket when no physical interface is available
// or binding isn't supported. Caller takes ownership (parented to `parent`).
QTcpSocket *makePhysicalBoundTcpSocket(QObject *parent,
                                       QAbstractSocket::NetworkLayerProtocol proto);

} // namespace freetunnel
