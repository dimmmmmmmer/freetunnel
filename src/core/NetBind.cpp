// cppcheck-suppress-file missingIncludeSystem
#include <QtGlobal>

// On Windows winsock2.h must be included before anything that may pull in
// windows.h (some Qt headers do), or the old winsock.h gets in first and clashes.
#if defined(Q_OS_WIN)
// clang-format off
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on

#include <set>
#include <vector>
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

#if defined(Q_OS_WIN)
// Ask the system what an adapter IS, instead of reading a name for clues.
//
// The clues are not there. Qt's QNetworkInterface::name() on Windows is a LUID
// alias, not the adapter's title — ConvertInterfaceLuidToNameW, which has a
// prefix only for the interface types it knows and falls back to
// "iftype<N>_<M>" for the rest. Wintun declares IfType 53, IF_TYPE_PROP_VIRTUAL
// (wintun.inf: *IfType = 53, Characteristics = NCF_VIRTUAL), and Qt's Windows
// switch has no case for that either, so type() is Unknown as well. Neither the
// prefixes below nor a search for "wintun" could ever match, and the tunnel this
// application had just created was eligible to be chosen as the physical uplink
// it should be bound around.
//
// Type is the question that has an answer. Not Unknown-means-virtual: mobile
// broadband is IF_TYPE_WWANPP, which Qt also maps to Unknown, and excluding a
// tethered uplink would be the same fault pointing the other way.
bool windowsInterfaceIsVirtual(int index)
{
    if (index <= 0)
        return false;
    MIB_IF_ROW2 row{};
    row.InterfaceIndex = static_cast<NET_IFINDEX>(index);
    if (::GetIfEntry2(&row) != NO_ERROR)
        return false;
    return row.Type == IF_TYPE_PROP_VIRTUAL || row.Type == IF_TYPE_TUNNEL;
}
#endif

} // namespace

// Outside the anonymous namespace only so that test_netbind can hold the list
// to what it is for; nothing else calls it.
bool freetunnel::interfaceIsVirtual(const QString &name) {
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

namespace {

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
    QHostAddress v4;
    QHostAddress v6;
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
            || freetunnel::interfaceIsVirtual(ni.name()))
        return false;
#if defined(Q_OS_WIN)
    if (windowsInterfaceIsVirtual(ni.index()))
        return false;
#endif
    if (requireRunning && !flags.testFlag(QNetworkInterface::IsRunning))
        return false;
    return true;
}

// qintptr, not int: on Win64 a SOCKET is a 64-bit UINT_PTR, so taking an int here
// narrowed the handle at every call. Windows documents socket handles as fitting
// in 32 bits for interop, which is why this worked — but a handle silently losing
// half its width is not something to leave resting on that. The syscalls want the
// platform's own type back, so the cast happens once, here.
QHostAddress queryRouteSourceOnSocket(qintptr fdArg, bool v6)
{
#if defined(Q_OS_WIN)
    const SOCKET fd = static_cast<SOCKET>(fdArg);
#else
    const int fd = static_cast<int>(fdArg);
#endif
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
// SOCKET, not int, for the reason queryRouteSourceOnSocket gives: the caller
// holds a 64-bit handle, and an int parameter narrowed it on the way in.
bool bindSocketToRouteIndex(SOCKET fd, const freetunnel::PhysicalRoute &r, bool v6)
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

#if defined(Q_OS_WIN)
// The adapters the core counts as physical (is_physical_adapter in
// native_libs_common): Ethernet, Wi-Fi and mobile broadband, up or dormant, with
// an address. Wintun is IF_TYPE_PROP_VIRTUAL and never one; a Hyper-V or WSL
// switch is Ethernet to Windows, and is kept out by having no default route.
std::set<int> windowsPhysicalAdapters()
{
    constexpr ULONG kFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    std::set<int> out;
    ULONG size = 15 * 1024;
    std::vector<unsigned char> buf;
    ULONG ret = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && ret == ERROR_BUFFER_OVERFLOW; ++tries) {
        buf.resize(size);
        ret = ::GetAdaptersAddresses(AF_UNSPEC, kFlags, nullptr,
                                     reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buf.data()), &size);
    }
    if (ret != NO_ERROR)
        return out;
    for (auto *a = reinterpret_cast<const IP_ADAPTER_ADDRESSES *>(buf.data()); a != nullptr;
         a = a->Next) {
        const bool physicalType = a->IfType == IF_TYPE_ETHERNET_CSMACD || a->IfType == IF_TYPE_IEEE80211
                || a->IfType == IF_TYPE_WWANPP || a->IfType == IF_TYPE_WWANPP2;
        const bool online = a->OperStatus == IfOperStatusUp || a->OperStatus == IfOperStatusDormant;
        if (!physicalType || !online || a->FirstUnicastAddress == nullptr)
            continue;
        if (a->IfIndex != 0)
            out.insert(static_cast<int>(a->IfIndex));
        if (a->Ipv6IfIndex != 0)
            out.insert(static_cast<int>(a->Ipv6IfIndex));
    }
    return out;
}

// Every default route in the table for @p family, with its interface's metric
// added and whether that interface is connected, as the core reads them.
void appendWindowsDefaultRoutes(ADDRESS_FAMILY family, QList<freetunnel::DefaultRoute> *out)
{
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    if (::GetIpForwardTable2(family, &table) != NO_ERROR || table == nullptr)
        return;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IPFORWARD_ROW2 &route = table->Table[i];
        if (route.DestinationPrefix.PrefixLength != 0)
            continue;
        const SOCKADDR_INET &prefix = route.DestinationPrefix.Prefix;
        const bool any = family == AF_INET ? prefix.Ipv4.sin_addr.s_addr == INADDR_ANY
                                           : IN6_IS_ADDR_UNSPECIFIED(&prefix.Ipv6.sin6_addr) != FALSE;
        if (!any)
            continue;
        MIB_IPINTERFACE_ROW row;
        ::InitializeIpInterfaceEntry(&row);
        row.Family = family;
        row.InterfaceIndex = route.InterfaceIndex;
        freetunnel::DefaultRoute r;
        r.index = static_cast<int>(route.InterfaceIndex);
        r.v6 = family == AF_INET6;
        if (::GetIpInterfaceEntry(&row) == NO_ERROR) {
            r.connected = row.Connected != FALSE;
            r.metric = static_cast<quint64>(route.Metric) + row.Metric;
        }
        out->append(r);
    }
    ::FreeMibTable(table);
}

// Linux reads the default route's interface out of /proc/net/route below; this
// is the Windows side of it. Without it, a probe made while connected asked
// where a packet to a public address would go, which is into the tunnel, and
// fell back to the first adapter Windows lists, which on a machine with Hyper-V
// or WSL can be an internal switch with no way out.
std::optional<freetunnel::PhysicalRoute> routeForDefaultGateway()
{
    const int index = freetunnel::windowsDefaultRouteInterface();
    if (index <= 0)
        return std::nullopt;
    const QNetworkInterface ni = QNetworkInterface::interfaceFromIndex(index);
    if (!ni.isValid() || !interfaceEligibleForRoute(ni, false))
        return std::nullopt;
    const freetunnel::PhysicalRoute r = routeFromInterface(ni);
    if (r.index > 0)
        return r;
    return std::nullopt;
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

int pickDefaultRouteInterface(const QList<DefaultRoute> &routes,
                              const std::function<bool(int index)> &eligible)
{
    for (const bool v6 : {false, true}) {
        const DefaultRoute *best = nullptr;
        for (const DefaultRoute &r : routes) {
            if (r.v6 != v6 || !r.connected || r.index <= 0 || !eligible(r.index))
                continue;
            if (best == nullptr || r.metric < best->metric)
                best = &r;
        }
        if (best != nullptr)
            return best->index;
    }
    return 0;
}

#if defined(Q_OS_WIN)
int windowsDefaultRouteInterface()
{
    const std::set<int> physical = windowsPhysicalAdapters();
    if (physical.empty())
        return 0;
    QList<DefaultRoute> routes;
    appendWindowsDefaultRoutes(AF_INET, &routes);
    appendWindowsDefaultRoutes(AF_INET6, &routes);
    return pickDefaultRouteInterface(routes,
                                     [&physical](int index) { return physical.count(index) > 0; });
}
#endif

PhysicalRoute physicalOutboundRoute() {
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
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
