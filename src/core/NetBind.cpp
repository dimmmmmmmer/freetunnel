#include <QtGlobal>

// On Windows winsock2.h must be included before anything that may pull in
// windows.h (some Qt headers do), or the old winsock.h gets in first and clashes.
#if defined(Q_OS_WIN)
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#endif

#include "core/NetBind.h"

#include <QNetworkInterface>
#include <QStringList>
#include <QTcpSocket>

#if !defined(Q_OS_WIN)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(Q_OS_WIN)
using ft_socklen = int;
#else
using ft_socklen = socklen_t;
#endif

namespace {

// Interface name prefixes that denote a VPN / virtual / tunnel device.
bool interfaceIsVirtual(const QString &name) {
    static const QStringList kVirt = {QStringLiteral("utun"), QStringLiteral("tun"),
                                      QStringLiteral("tap"),  QStringLiteral("ppp"),
                                      QStringLiteral("ipsec"), QStringLiteral("wg"),
                                      QStringLiteral("gpd"),  QStringLiteral("zt"),
                                      QStringLiteral("ham")};
    for (const QString &p : kVirt)
        if (name.startsWith(p))
            return true;
    return false;
}

freetunnel::PhysicalRoute routeFromInterface(const QNetworkInterface &ni) {
    freetunnel::PhysicalRoute r;
    QHostAddress v4, v6;
    for (const QNetworkAddressEntry &e : ni.addressEntries()) {
        const QHostAddress a = e.ip();
        if (a.isLoopback() || a.isLinkLocal())
            continue;
        if (a.protocol() == QAbstractSocket::IPv4Protocol && v4.isNull())
            v4 = a;
        else if (a.protocol() == QAbstractSocket::IPv6Protocol && v6.isNull())
            v6 = a;
    }
    if (!v4.isNull() || !v6.isNull()) {
        r.index = ni.index();
        r.v4 = v4;
        r.v6 = v6;
    }
    return r;
}

// The source address the OS would use to reach a public host. A UDP "connect"
// resolves the route + source address without sending any packet. Returns null
// on failure. When a full tunnel is up this resolves to the VPN interface.
QHostAddress osRouteSourceAddress(bool v6) {
    const char *dst = v6 ? "2606:4700:4700::1111" : "1.1.1.1";
#if defined(Q_OS_WIN)
    SOCKET fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET)
        return {};
#else
    int fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return {};
#endif
    QHostAddress result;
    if (v6) {
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(53);
        if (::inet_pton(AF_INET6, dst, &sa.sin6_addr) == 1
                && ::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0) {
            sockaddr_in6 local{};
            ft_socklen len = sizeof(local);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &len) == 0)
                result = QHostAddress(reinterpret_cast<sockaddr *>(&local));
        }
    } else {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(53);
        if (::inet_pton(AF_INET, dst, &sa.sin_addr) == 1
                && ::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) == 0) {
            sockaddr_in local{};
            ft_socklen len = sizeof(local);
            if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &len) == 0)
                result = QHostAddress(reinterpret_cast<sockaddr *>(&local));
        }
    }
#if defined(Q_OS_WIN)
    ::closesocket(fd);
#else
    ::close(fd);
#endif
    if (result.isNull() || result.isLoopback() || result.isLinkLocal())
        return {};
    return result;
}

} // namespace

namespace freetunnel {

PhysicalRoute physicalOutboundRoute() {
    // Prefer the interface the OS actually routes the internet through. When a
    // full tunnel is up this resolves to the VPN interface, which we reject (it's
    // virtual) and fall back to scanning for the first physical interface.
    for (bool v6 : {false, true}) {
        const QHostAddress src = osRouteSourceAddress(v6);
        if (src.isNull())
            continue;
        for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
            const auto flags = ni.flags();
            if (!flags.testFlag(QNetworkInterface::IsUp)
                    || flags.testFlag(QNetworkInterface::IsLoopBack)
                    || interfaceIsVirtual(ni.name()))
                continue;
            for (const QNetworkAddressEntry &e : ni.addressEntries()) {
                if (e.ip() == src) {
                    const PhysicalRoute r = routeFromInterface(ni);
                    if (r.index > 0)
                        return r;
                }
            }
        }
    }
    // Fallback: first qualifying non-virtual interface.
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
        const auto flags = ni.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
                || !flags.testFlag(QNetworkInterface::IsRunning)
                || flags.testFlag(QNetworkInterface::IsLoopBack)
                || interfaceIsVirtual(ni.name()))
            continue;
        const PhysicalRoute r = routeFromInterface(ni);
        if (r.index > 0)
            return r;
    }
    return {};
}

QTcpSocket *makePhysicalBoundTcpSocket(QObject *parent,
                                       QAbstractSocket::NetworkLayerProtocol proto) {
    auto *sock = new QTcpSocket(parent);
    const PhysicalRoute r = physicalOutboundRoute();
    if (r.index <= 0)
        return sock; // no physical interface — fall back to default routing
    const bool v6 = proto == QAbstractSocket::IPv6Protocol;

#if defined(Q_OS_MACOS)
    const int fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
        unsigned int idx = static_cast<unsigned int>(r.index);
        const int level = v6 ? IPPROTO_IPV6 : IPPROTO_IP;
        const int opt = v6 ? IPV6_BOUND_IF : IP_BOUND_IF;
        if (::setsockopt(fd, level, opt, &idx, sizeof(idx)) == 0
                && sock->setSocketDescriptor(fd, QAbstractSocket::UnconnectedState))
            return sock;
        ::close(fd);
    }
#elif defined(Q_OS_WIN)
    const SOCKET fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd != INVALID_SOCKET) {
        bool ok = false;
        if (v6) {
            DWORD idx = static_cast<DWORD>(r.index);
            ok = ::setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_IF,
                              reinterpret_cast<char *>(&idx), sizeof(idx)) == 0;
        } else {
            // IPv4 IP_UNICAST_IF takes the interface index in network byte order.
            DWORD beIdx = htonl(static_cast<DWORD>(r.index));
            ok = ::setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF,
                              reinterpret_cast<char *>(&beIdx), sizeof(beIdx)) == 0;
        }
        if (ok && sock->setSocketDescriptor(static_cast<qintptr>(fd),
                                            QAbstractSocket::UnconnectedState))
            return sock;
        ::closesocket(fd);
    }
#else
    // Linux/other: SO_BINDTODEVICE needs root, so source-bind to the physical
    // address. Best-effort — effectiveness depends on the tunnel's routing.
    const QHostAddress src = v6 ? r.v6 : r.v4;
    if (!src.isNull())
        sock->bind(src);
#endif
    return sock;
}

} // namespace freetunnel
