#include <QtTest>

#include <QHostAddress>
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
        QCOMPARE(s->state(), QAbstractSocket::UnconnectedState);
        delete s;
    }
}

QTEST_MAIN(TestNetBind)
#include "test_netbind.moc"
