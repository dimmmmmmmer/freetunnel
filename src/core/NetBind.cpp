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

#include <algorithm>

#if defined(Q_OS_LINUX)
#include <QFile>
#include <QRegularExpression>
#endif

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
    if (std::any_of(kVirt.cbegin(), kVirt.cend(),
                    [&name](const QString &p) { return name.startsWith(p); }))
        return true;
#if defined(Q_OS_WIN)
    // Wintun adapters created by TrustTunnel must not be treated as the physical uplink.
    const QString lower = name.toLower();
    if (lower.contains(QStringLiteral("wintun")) || lower.contains(QStringLiteral("trusttunnel")))
        return true;
#endif
    return false;
}

void pickInterfaceRouteAddresses(const QNetworkInterface &ni, QHostAddress *v4, QHostAddress *v6)
{
    for (const QNetworkAddressEntry &e : ni.addressEntries()) {
        const QHostAddress a = e.ip();
        if (a.isLoopback() || a.isLinkLocal())
            continue;
        if (a.protocol() == QAbstractSocket::IPv4Protocol && v4->isNull())
            *v4 = a;
        else if (a.protocol() == QAbstractSocket::IPv6Protocol && v6->isNull())
            *v6 = a;
    }
}

freetunnel::PhysicalRoute routeFromInterface(const QNetworkInterface &ni) {
    freetunnel::PhysicalRoute r;
    QHostAddress v4, v6;
    pickInterfaceRouteAddresses(ni, &v4, &v6);
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
    if (v6) {
        sockaddr_in6 sa{};
        sa.sin6_family = AF_INET6;
        sa.sin6_port = htons(53);
        if (::inet_pton(AF_INET6, "2606:4700:4700::1111", &sa.sin6_addr) != 1
                || ::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0)
            return {};
        sockaddr_in6 local{};
        ft_socklen len = sizeof(local);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &len) != 0)
            return {};
        return QHostAddress(reinterpret_cast<sockaddr *>(&local));
    }
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(53);
    if (::inet_pton(AF_INET, "1.1.1.1", &sa.sin_addr) != 1
            || ::connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa)) != 0)
        return {};
    sockaddr_in local{};
    ft_socklen len = sizeof(local);
    if (::getsockname(fd, reinterpret_cast<sockaddr *>(&local), &len) != 0)
        return {};
    return QHostAddress(reinterpret_cast<sockaddr *>(&local));
}

QHostAddress normalizeRouteSource(const QHostAddress &addr)
{
    if (addr.isNull() || addr.isLoopback() || addr.isLinkLocal())
        return {};
    return addr;
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
    const QHostAddress result = normalizeRouteSource(queryRouteSourceOnSocket(fd, v6));
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

#if defined(Q_OS_LINUX)
QString defaultRouteInterfaceName()
{
    QFile f(QStringLiteral("/proc/net/route"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    while (!f.atEnd()) {
        const QStringList fields =
                QString::fromUtf8(f.readLine()).trimmed().split(QRegularExpression(QStringLiteral("\\s+")),
                                                                Qt::SkipEmptyParts);
        if (fields.size() < 2)
            continue;
        if (fields.at(1) == QStringLiteral("00000000"))
            return fields.first();
    }
    return {};
}

std::optional<freetunnel::PhysicalRoute> routeForDefaultGateway()
{
    const QString ifName = defaultRouteInterfaceName();
    if (ifName.isEmpty())
        return std::nullopt;
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces()) {
        if (ni.name() != ifName || !interfaceEligibleForRoute(ni, false))
            continue;
        const freetunnel::PhysicalRoute r = routeFromInterface(ni);
        if (r.index > 0)
            return r;
    }
    return std::nullopt;
}
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
void closeNativeSocket(qintptr fd)
{
#if defined(Q_OS_WIN)
    ::closesocket(static_cast<SOCKET>(fd));
#else
    ::close(static_cast<int>(fd));
#endif
}

bool attachNativeBoundSocket(QTcpSocket *sock, const freetunnel::PhysicalRoute &r, bool v6)
{
    const int sockType = v6 ? AF_INET6 : AF_INET;
#if defined(Q_OS_WIN)
    const SOCKET fd = ::socket(sockType, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET)
        return false;
#else
    const int fd = ::socket(sockType, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
#endif
    if (!bindSocketToRouteIndex(fd, r, v6)) {
        closeNativeSocket(static_cast<qintptr>(fd));
        return false;
    }
    if (sock->setSocketDescriptor(static_cast<qintptr>(fd), QAbstractSocket::UnconnectedState))
        return true;
    closeNativeSocket(static_cast<qintptr>(fd));
    return false;
}
#endif

} // namespace

namespace freetunnel {

PhysicalRoute physicalOutboundRoute() {
#if defined(Q_OS_LINUX)
    if (const auto routed = routeForDefaultGateway())
        return *routed;
#endif
    for (bool v6 : {false, true}) {
        const QHostAddress src = osRouteSourceAddress(v6);
        if (src.isNull())
            continue;
        if (const auto matched = routeForSourceAddress(src))
            return *matched;
    }
    return firstPhysicalInterface(true);
}

void bindSocketToPhysicalRoute(QTcpSocket *sock,
                               QAbstractSocket::NetworkLayerProtocol proto) {
    if (!sock)
        return;
    const PhysicalRoute r = physicalOutboundRoute();
    if (r.index <= 0)
        return;
    const bool v6 = proto == QAbstractSocket::IPv6Protocol;

#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    attachNativeBoundSocket(sock, r, v6);
#else
    const QHostAddress src = v6 ? r.v6 : r.v4;
    if (!src.isNull())
        sock->bind(src);
#endif
}

QTcpSocket *makePhysicalBoundTcpSocket(QObject *parent,
                                       QAbstractSocket::NetworkLayerProtocol proto) {
    auto *sock = new QTcpSocket(parent);
    bindSocketToPhysicalRoute(sock, proto);
    return sock;
}

} // namespace freetunnel
