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

#if defined(Q_OS_MACOS)
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace freetunnel {

PhysicalRoute physicalOutboundRoute() {
    PhysicalRoute r;
    // Interface name prefixes that denote a VPN / virtual / tunnel device.
    static const QStringList virt = {QStringLiteral("utun"), QStringLiteral("tun"),
                                     QStringLiteral("tap"),  QStringLiteral("ppp"),
                                     QStringLiteral("ipsec"), QStringLiteral("wg"),
                                     QStringLiteral("gpd"),  QStringLiteral("zt"),
                                     QStringLiteral("ham")};
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
        const auto flags = ni.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
                || !flags.testFlag(QNetworkInterface::IsRunning)
                || flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;
        const QString name = ni.name();
        bool isVirtual = false;
        for (const QString &p : virt)
            if (name.startsWith(p)) { isVirtual = true; break; }
        if (isVirtual)
            continue;
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
            return r; // first qualifying physical interface
        }
    }
    return r;
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
