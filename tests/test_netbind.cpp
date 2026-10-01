// cppcheck-suppress-file missingIncludeSystem
#include <QtGlobal>

// winsock2.h before anything that may pull in windows.h, as in NetBind.cpp.
#if defined(Q_OS_WIN)
// clang-format off
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
// clang-format on
#endif

#include <QtTest>

#include <QHostAddress>
#include <QNetworkInterface>
#include <QTcpSocket>

#include "core/NetBind.h"

// NetBind picks a physical (non-VPN) interface and hands back sockets bound to
// it. The exact interface depends on the runner's network, so these assert
// self-consistency and that binding never breaks socket creation, rather than a
// specific address.
class TestNetBind : public QObject {
    Q_OBJECT

private slots:
    void routeIsSelfConsistent();
    void boundSocketIsUsable();
    void tunnelNamesAreNeverThePhysicalRoute();
    void theRouteIsNeverATunnelAdapter();
    void theDefaultRouteIsPickedAsTheCorePicksIt();
    void windowsProbesUseTheAdapterWithTheDefaultRoute();
};

void TestNetBind::routeIsSelfConsistent()
{
    const freetunnel::PhysicalRoute r = freetunnel::physicalOutboundRoute();
    // A machine with no usable interface returns index 0; otherwise the route
    // must carry at least one non-loopback, non-link-local address.
    if (r.index > 0) {
        QVERIFY(!r.v4.isNull() || !r.v6.isNull());
        for (const QHostAddress &a : {r.v4, r.v6}) {
            if (a.isNull())
                continue;
            QVERIFY(!a.isLoopback());
            QVERIFY(!a.isLinkLocal());
        }
    }
}

void TestNetBind::boundSocketIsUsable()
{
    // Regardless of whether binding succeeds, a live QTcpSocket must come back
    // (the helper falls back to an unbound socket rather than returning null).
    for (auto proto : {QAbstractSocket::IPv4Protocol, QAbstractSocket::IPv6Protocol}) {
        QTcpSocket *s = freetunnel::makePhysicalBoundTcpSocket(this, proto);
        QVERIFY(s != nullptr);
        // Ready to connect: unbound (mac IP_BOUND_IF / no iface) or source-bound
        // (Linux bind() leaves it in BoundState) — never already connected.
        QVERIFY(s->state() == QAbstractSocket::UnconnectedState
                || s->state() == QAbstractSocket::BoundState);
        delete s;
    }
}

// The route is what the server probes on the Configs page bind to, so that they
// measure the network rather than a tunnel. A tunnel's name is how Linux and
// macOS give one away — wg0, utun4 — and a name dropped from the list is a
// tunnel the probes can be bound to.
void TestNetBind::tunnelNamesAreNeverThePhysicalRoute()
{
    for (const char *name : {"tun0", "tunl0", "utun4", "tap0", "ppp0", "ipsec0", "wg0", "gpd0",
                             "zt7nnig26", "ham0"})
        QVERIFY2(freetunnel::interfaceIsVirtual(QString::fromLatin1(name)), name);
    for (const char *name : {"eth0", "enp3s0", "wlp2s0", "wlan0", "en0", "en7", "bridge100"})
        QVERIFY2(!freetunnel::interfaceIsVirtual(QString::fromLatin1(name)), name);
}

// Whatever this machine has, the interface picked must be one that could be
// the way out: up, not loopback, not named like a tunnel — and on Windows, where
// names say nothing, not of a tunnel's adapter type either. Wintun declares
// itself IF_TYPE_PROP_VIRTUAL; it was once picked here by the VPN's own tunnel.
void TestNetBind::theRouteIsNeverATunnelAdapter()
{
    const freetunnel::PhysicalRoute r = freetunnel::physicalOutboundRoute();
    if (r.index <= 0)
        QSKIP("this machine has no interface that could be the physical route");
    const QNetworkInterface ni = QNetworkInterface::interfaceFromIndex(r.index);
    QVERIFY(ni.isValid());
    QVERIFY(ni.flags().testFlag(QNetworkInterface::IsUp));
    QVERIFY(!ni.flags().testFlag(QNetworkInterface::IsLoopBack));
    QVERIFY2(!freetunnel::interfaceIsVirtual(ni.name()), qPrintable(ni.name()));
#if defined(Q_OS_WIN)
    MIB_IF_ROW2 row{};
    row.InterfaceIndex = static_cast<NET_IFINDEX>(r.index);
    QCOMPARE(::GetIfEntry2(&row), static_cast<DWORD>(NO_ERROR));
    QVERIFY2(row.Type != IF_TYPE_PROP_VIRTUAL && row.Type != IF_TYPE_TUNNEL,
             qPrintable(ni.humanReadableName()));
#endif
}

// While connected, the server probes asked where a packet to a public address
// would go, which is into the tunnel, and then took the first adapter Windows
// listed: on a machine with Hyper-V or WSL, an internal switch with no way out.
// They now take the adapter of the default route, chosen as the VPN core
// chooses its own (vpn_win_detect_active_if), so the two agree.
void TestNetBind::theDefaultRouteIsPickedAsTheCorePicksIt()
{
    using freetunnel::DefaultRoute;
    const auto route = [](int index, bool v6, quint64 metric, bool connected = true) {
        DefaultRoute r;
        r.index = index;
        r.v6 = v6;
        r.metric = metric;
        r.connected = connected;
        return r;
    };
    // 5 is the tunnel, or another adapter the core does not count as physical.
    const auto physical = [](int index) { return index != 5; };

    // The lowest metric among the adapters that count.
    QCOMPARE(freetunnel::pickDefaultRouteInterface(
                     {route(5, false, 1), route(7, false, 35), route(9, false, 25)}, physical),
             9);
    // IPv4 first, however low an IPv6 route's metric.
    QCOMPARE(freetunnel::pickDefaultRouteInterface({route(9, true, 5), route(7, false, 50)}, physical),
             7);
    // Not over an interface that has no connection.
    QCOMPARE(freetunnel::pickDefaultRouteInterface(
                     {route(9, false, 5, false), route(7, false, 50)}, physical),
             7);
    // IPv6 when there is no IPv4 default route.
    QCOMPARE(freetunnel::pickDefaultRouteInterface({route(9, true, 30), route(12, true, 10)}, physical),
             12);
    // None that count: nothing, not the tunnel.
    QCOMPARE(freetunnel::pickDefaultRouteInterface({route(5, false, 1)}, physical), 0);
    QCOMPARE(freetunnel::pickDefaultRouteInterface({}, physical), 0);
}

// On Windows itself: the adapter picked is the one Windows sends a packet to a
// public address out of (with no VPN up on the runner, that is the default
// route's), is a physical type, and is the one the probes bind to.
void TestNetBind::windowsProbesUseTheAdapterWithTheDefaultRoute()
{
#if defined(Q_OS_WIN)
    const int picked = freetunnel::windowsDefaultRouteInterface();
    if (picked <= 0)
        QSKIP("this machine has no default route on a physical adapter");
    sockaddr_in publicHost{};
    publicHost.sin_family = AF_INET;
    QCOMPARE(::inet_pton(AF_INET, "1.1.1.1", &publicHost.sin_addr), 1);
    DWORD best = 0;
    QCOMPARE(::GetBestInterfaceEx(reinterpret_cast<sockaddr *>(&publicHost), &best),
             static_cast<DWORD>(NO_ERROR));
    QCOMPARE(picked, static_cast<int>(best));

    MIB_IF_ROW2 row{};
    row.InterfaceIndex = static_cast<NET_IFINDEX>(picked);
    QCOMPARE(::GetIfEntry2(&row), static_cast<DWORD>(NO_ERROR));
    QVERIFY2(row.Type == IF_TYPE_ETHERNET_CSMACD || row.Type == IF_TYPE_IEEE80211
                     || row.Type == IF_TYPE_WWANPP || row.Type == IF_TYPE_WWANPP2,
             qPrintable(QString::number(row.Type)));

    QCOMPARE(freetunnel::physicalOutboundRoute().index, picked);
#else
    QSKIP("the routing table is read this way on Windows only");
#endif
}

QTEST_MAIN(TestNetBind)
#include "test_netbind.moc"
