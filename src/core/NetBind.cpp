// cppcheck-suppress-file missingIncludeSystem
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

#include <optional>

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

bool interfaceIsVirtual(const QString &name) {
    static const QStringList kVirt = {QStringLiteral("utun"), QStringLiteral("tun"),
                                      QStringLiteral("tap"),  QStringLiteral("ppp"),
                                      QStringLiteral("ipsec"), QStringLiteral("wg"),
                                      QStringLiteral("gpd"),  QStringLiteral("zt"),
                                      QStringLiteral("ham")};
    for (const QString &p : kVirt) {
        if (name.startsWith(p))
            return true;
    }
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

bool interfaceEligibleForRoute(const QNetworkInterface &ni, bool requireRunning)
{
    const auto flags = ni.flags();
    if (!flags.testFlag(QNetworkInterface::IsUp) || flags.testFlag(QNetworkInterface::IsLoopBack)
            || interfaceIsVirtual(ni.name()))
        return false;
    if (requireRunning && !flags.testFlag(QNetworkInterface::IsRunning))
        return false;
    return true;
}

QHostAddress queryRouteSourceOnSocket(int fd, bool v6)
{
    const char *dst = v6 ? "2606:4700:4700::1111" : "1.1.1.1";
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
    if (result.isNull() || result.isLoopback() || result.isLinkLocal())
        return {};
    return result;
}

// The source address the OS would use to reach a public host. A UDP "connect"
// resolves the route + source address without sending any packet.
QHostAddress osRouteSourceAddress(bool v6) {
#if defined(Q_OS_WIN)
    SOCKET fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd == INVALID_SOCKET)
        return {};
#else
    int fd = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return {};
#endif
    const QHostAddress result = queryRouteSourceOnSocket(fd, v6);
#if defined(Q_OS_WIN)
    ::closesocket(fd);
#else
    ::close(fd);
#endif
    return result;
}

std::optional<freetunnel::PhysicalRoute> routeForSourceAddress(const QHostAddress &src)
{
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
        if (!interfaceEligibleForRoute(ni, false))
            continue;
        for (const QNetworkAddressEntry &e : ni.addressEntries()) {
            if (e.ip() != src)
                continue;
            const freetunnel::PhysicalRoute r = routeFromInterface(ni);
            if (r.index > 0)
                return r;
        }
    }
    return std::nullopt;
}

freetunnel::PhysicalRoute firstPhysicalInterface(bool requireRunning)
{
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
        if (!interfaceEligibleForRoute(ni, requireRunning))
            continue;
        const freetunnel::PhysicalRoute r = routeFromInterface(ni);
        if (r.index > 0)
            return r;
    }
    return {};
}

#if defined(Q_OS_MACOS)
bool bindSocketToRouteIndex(int fd, const freetunnel::PhysicalRoute &r, bool v6)
{
    unsigned int idx = static_cast<unsigned int>(r.index);
    const int level = v6 ? IPPROTO_IPV6 : IPPROTO_IP;
    const int opt = v6 ? IPV6_BOUND_IF : IP_BOUND_IF;
    return ::setsockopt(fd, level, opt, &idx, sizeof(idx)) == 0;
}
#elif defined(Q_OS_WIN)
bool bindSocketToRouteIndex(int fd, const freetunnel::PhysicalRoute &r, bool v6)
{
    if (v6) {
        DWORD idx = static_cast<DWORD>(r.index);
        return ::setsockopt(fd, IPPROTO_IPV6, IPV6_UNICAST_IF,
                          reinterpret_cast<char *>(&idx), sizeof(idx)) == 0;
    }
    DWORD beIdx = htonl(static_cast<DWORD>(r.index));
    return ::setsockopt(fd, IPPROTO_IP, IP_UNICAST_IF,
                        reinterpret_cast<char *>(&beIdx), sizeof(beIdx)) == 0;
}
#endif

} // namespace

namespace freetunnel {

PhysicalRoute physicalOutboundRoute() {
    for (bool v6 : {false, true}) {
        const QHostAddress src = osRouteSourceAddress(v6);
        if (src.isNull())
            continue;
        if (const auto matched = routeForSourceAddress(src))
            return *matched;
    }
    return firstPhysicalInterface(true);
}

QTcpSocket *makePhysicalBoundTcpSocket(QObject *parent,
                                       QAbstractSocket::NetworkLayerProtocol proto) {
    auto *sock = new QTcpSocket(parent);
    const PhysicalRoute r = physicalOutboundRoute();
    if (r.index <= 0)
        return sock;
    const bool v6 = proto == QAbstractSocket::IPv6Protocol;

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    const int sockType = v6 ? AF_INET6 : AF_INET;
#if defined(Q_OS_WIN)
    const SOCKET fd = ::socket(sockType, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
        return sock;
#else
    const int fd = ::socket(sockType, SOCK_STREAM, 0);
    if (fd < 0)
        return sock;
#endif
    if (bindSocketToRouteIndex(fd, r, v6)
            && sock->setSocketDescriptor(static_cast<qintptr>(fd),
                                         QAbstractSocket::UnconnectedState))
        return sock;
#if defined(Q_OS_WIN)
    ::closesocket(fd);
#else
    ::close(fd);
#endif
#else
    const QHostAddress src = v6 ? r.v6 : r.v4;
    if (!src.isNull())
        sock->bind(src);
#endif
    return sock;
}

} // namespace freetunnel
