// cppcheck-suppress-file missingIncludeSystem
// Attributing a connection to the program that opened it is the half of per-app
// split tunnelling that is different on every platform, so the important test
// here is the live one: open a real socket and ask who owns it. It runs on
// Linux, macOS and Windows and must find this very test binary. A platform
// backend that silently answers "don't know" would otherwise look exactly like
// a working one — every rule would simply never match.
#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>

#include "core/ProcessLookup.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#endif

using freetunnel::AppIdentity;
using freetunnel::LocalFlow;
using freetunnel::ProcessLookup;
using freetunnel::SocketAddress;
using freetunnel::SocketOwnerTable;

namespace {

// The lookup only walks processes a rule names, so every live test here has to
// say that this test binary is one of them — which is also the arrangement the
// application uses, rather than a test-only mode.
QStringList watchSelf()
{
    return {QDir::toNativeSeparators(
            QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath())};
}

SocketAddress addressOf(const char *text)
{
    const QHostAddress parsed{QString::fromLatin1(text)};
    SocketAddress out{};
    bool isV4 = false;
    const quint32 v4 = parsed.toIPv4Address(&isV4);
    if (isV4 && parsed.protocol() == QAbstractSocket::IPv4Protocol) {
        out = {static_cast<std::uint8_t>(v4 >> 24), static_cast<std::uint8_t>(v4 >> 16),
               static_cast<std::uint8_t>(v4 >> 8), static_cast<std::uint8_t>(v4)};
    } else {
        const Q_IPV6ADDR raw = parsed.toIPv6Address();
        std::copy_n(raw.c, 16, out.begin());
    }
    return out;
}

// For a test that needs the lookup to walk again soon after a walk. Looking again
// is paid for from credit that comes back at a quarter of real time, so on a
// loaded machine, where one walk can cost tens of milliseconds, the second look
// may be refused for want of it — correctly, and reported as lookWasSkipped. Four
// times what the last walk cost buys the next one. It is also far inside the
// table's lifetime, at least twenty walks' worth, so the table a test relies on
// is still the one it built.
void letItAffordAnotherWalk(const ProcessLookup &lookup)
{
    std::this_thread::sleep_for(std::chrono::microseconds(4 * lookup.lastScan().elapsedUs + 1000));
}

QString canonical(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

// Another program, for the tests about two programs on one port number: this
// same binary, copied under a name no rule here uses, holding a TCP listener or
// a bound UDP socket on the address and port it is given until the test closes
// its stdin. Its own half is holdASocket(), which main() runs when asked to.
class AnotherProgram {
public:
    // The port it is holding, or 0 when it could not bind there — an address
    // this machine does not have, say.
    quint16 start(const QHostAddress &address, quint16 port = 0, const char *proto = "tcp")
    {
        const QString self = QCoreApplication::applicationFilePath();
        // Beside this binary rather than in a temporary directory, so that it
        // finds its libraries exactly as this one does, on every platform.
        m_copy = QFileInfo(self).absolutePath()
                + QStringLiteral("/another-program-%1").arg(QCoreApplication::applicationPid())
#ifdef Q_OS_WIN
                + QStringLiteral(".exe")
#endif
                ;
        QFile::remove(m_copy);
        if (!QFile::copy(self, m_copy))
            return 0;
        m_process.start(m_copy, {QStringLiteral("--hold"), QString::fromLatin1(proto), address.toString(),
                                 QString::number(port)});
        if (!m_process.waitForStarted(10000))
            return 0;
        while (!m_process.canReadLine() && m_process.waitForReadyRead(10000)) {}
        const QByteArray line = m_process.readLine().trimmed();
        return line.startsWith("PORT ") ? static_cast<quint16>(line.mid(5).toUInt()) : 0;
    }
    QString path() const { return m_copy; }
    ~AnotherProgram()
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(10000))
            m_process.kill();
        m_process.waitForFinished(10000);
        QFile::remove(m_copy);
    }

private:
    QString m_copy;
    QProcess m_process;
};

int holdASocket(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // "::" is an IPv6 address to Qt, and Qt binds that IPv6-only; "any" would be
    // QHostAddress::Any, which it binds dual-stack.
    const QHostAddress address(QString::fromLatin1(argv[3]));
    const quint16 port = QByteArray(argv[4]).toUShort();
    QTcpServer server;
    QUdpSocket datagrams;
    const bool udp = std::strcmp(argv[2], "udp") == 0;
    const bool bound = udp ? datagrams.bind(address, port) : server.listen(address, port);
    if (!bound) {
        std::printf("ERROR %s\n", qPrintable(udp ? datagrams.errorString() : server.errorString()));
        std::fflush(stdout);
        return 1;
    }
    std::printf("PORT %u\n", static_cast<unsigned>(udp ? datagrams.localPort() : server.serverPort()));
    std::fflush(stdout);
    // Until the test lets go: its end of our stdin closing.
    std::cin.ignore(std::numeric_limits<std::streamsize>::max());
    return 0;
}

} // namespace

class TestProcessLookup : public QObject {
    Q_OBJECT

private slots:
    void findsTheProcessBehindARealTcpSocket();
    void findsTheProcessBehindARealUdpSocket();
    void aPortNobodyHasOpenResolvesToNothing();
    void portZeroIsNeverLookedUp();
    void parsesAProcNetTable();
    void parsesAnIpv6ProcNetTable();
    void ignoresTheHeaderAndAnythingMalformed();
    void anUnknownPidHasNoIdentity();
    void theScanReportsWhatItSaw();
    void aSocketOpenedAfterTheLastWalkIsStillFound_data();
    void aSocketOpenedAfterTheLastWalkIsStillFound();
    void addingAProgramToTheRulesTakesEffectAtOnce();
    void withNoRulesNothingIsWalkedAtAll();
    void connectionsFromUnnamedProgramsDoNotEachBuyAWalk();
    void aBurstFromTheWatchedProgramSharesOneWalk();
    void bothWaysOfReadingTheSocketTablesAgree();
    void anIpv6SocketIsFoundWhicheverFamilyTheFlowClaims();
    void aConnectionFromAnIpv6SocketToAnIpv4AddressIsFound();
    void theTableMatchesAFlowToItsOwnSocket_data();
    void theTableMatchesAFlowToItsOwnSocket();
    void aSocketsFirstOwnerIsKeptUnlessItWasNobody();
    void aFlowWithoutAnAddressIsMatchedByPortAlone();
    void anIpv6OnlyRowDoesNotAnswerForIpv4();
    void anIpv4FlowOnASocketBoundToTheMappedWildcardIsFound();
    void anotherProgramsIpv6OnlySocketDoesNotAnswerForOurIpv4();
    void aNewSocketIsNotAnsweredByAnotherProgramsOnTheSamePort_data();
    void aNewSocketIsNotAnsweredByAnotherProgramsOnTheSamePort();
    void anotherProgramsNewSocketIsNotAnsweredByOurs_data();
    void anotherProgramsNewSocketIsNotAnsweredByOurs();
    void aClosedConnectionDoesNotAnswerForTheNextSocketOnItsAddress();
};

// The whole chain, on the real operating system: a socket exists, therefore the
// lookup must name this process and the path must be this binary.
void TestProcessLookup::findsTheProcessBehindARealTcpSocket()
{
    QTcpServer server;
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.errorString()));
    const quint16 port = server.serverPort();
    QVERIFY(port != 0);

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    const AppIdentity id = lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, port, QStringLiteral("127.0.0.1")});

    QVERIFY2(!id.executablePath.isEmpty(),
            "the socket is open right now, so the platform backend must attribute it");
    const QString self = QCoreApplication::applicationFilePath();
    QCOMPARE(QFileInfo(id.executablePath).canonicalFilePath(), QFileInfo(self).canonicalFilePath());
    QCOMPARE(id.name, QFileInfo(self).fileName());
}

// UDP goes through a different table on every platform, so it is not covered by
// the TCP case. QUIC — which is what a FreeTunnel HTTP/3 config uses — is UDP.
void TestProcessLookup::findsTheProcessBehindARealUdpSocket()
{
    QUdpSocket socket;
    QVERIFY2(socket.bind(QHostAddress::LocalHost, 0), qPrintable(socket.errorString()));
    const quint16 port = socket.localPort();
    QVERIFY(port != 0);

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    const AppIdentity id = lookup.resolve(LocalFlow{AF_INET, IPPROTO_UDP, port, QStringLiteral("127.0.0.1")});

    QVERIFY2(!id.executablePath.isEmpty(), "a bound UDP socket must be attributable too");
    QCOMPARE(QFileInfo(id.executablePath).canonicalFilePath(),
            QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath());
}

// A miss must be a miss. If an unattributable socket came back as some
// arbitrary process, a bypass rule for that process would start pulling
// unrelated traffic out of the tunnel.
void TestProcessLookup::aPortNobodyHasOpenResolvesToNothing()
{
    // Bind and release, so the port is known to have no owner at the moment we
    // ask — rather than picking a number and hoping.
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        port = probe.serverPort();
    }
    QVERIFY(port != 0);

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    const AppIdentity id = lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, port, QStringLiteral("127.0.0.1")});
    QVERIFY2(id.executablePath.isEmpty() && id.name.isEmpty(),
            "a closed port must not be attributed to anyone");
}

void TestProcessLookup::portZeroIsNeverLookedUp()
{
    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    const AppIdentity id = lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, 0, QString()});
    QVERIFY(id.executablePath.isEmpty());
}

// The Linux table parser, exercised on every platform: the format is a kernel
// interface, it does not change under us, and a fixture tests it where the
// tests actually run rather than only on a machine that has a /proc.
void TestProcessLookup::parsesAProcNetTable()
{
    const QString table = QStringLiteral(
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 0100007F:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 41234 1 0000 100 0\n"
            "   1: 0100007F:C350 0100007F:1F90 01 00000000:00000000 00:00000000 00000000  1000        0 41235 1 0000 20 4 30 10 -1\n");

    const QList<freetunnel::SocketOwner> rows =
            freetunnel::parseProcNetTable(table, IPPROTO_TCP, AF_INET);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].family, AF_INET);
    QCOMPARE(rows[0].port, quint16(8080)); // 0x1F90
    QCOMPARE(rows[0].inode, quint64(41234));
    QCOMPARE(rows[0].proto, IPPROTO_TCP);
    QCOMPARE(rows[1].port, quint16(50000)); // 0xC350
    QCOMPARE(rows[1].inode, quint64(41235));
    // The address too, now that the table keys on it: the kernel prints each
    // word as it lies in memory, and every machine this runs on is little-endian.
    QCOMPARE(rows[0].address, addressOf("127.0.0.1"));
}

// tcp6 writes the address as 32 hex digits, so the port is not at a fixed
// offset into the field — it is whatever follows the colon.
void TestProcessLookup::parsesAnIpv6ProcNetTable()
{
    const QString table = QStringLiteral(
            "  sl  local_address                         remote_address                        st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
            "   0: 00000000000000000000000000000000:0016 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000     0        0 22 1 0000 100 0\n");

    const QList<freetunnel::SocketOwner> rows =
            freetunnel::parseProcNetTable(table, IPPROTO_TCP, AF_INET6);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].port, quint16(22)); // 0x0016
    QCOMPARE(rows[0].inode, quint64(22));
    // The family is stamped from the file the rows came out of, and it has to
    // reach the row: /proc/net/tcp and /proc/net/tcp6 have identical columns and
    // separate port spaces, so a row is only meaningful with the family attached.
    // Without it one port number means two sockets and the table keeps whichever
    // was read first — which is one program answering for another's connection.
    QCOMPARE(rows[0].family, AF_INET6);
    QCOMPARE(rows[0].address, SocketAddress{}); // ::, every address

    // And two that are not all zeroes, each 32-bit word as a little-endian machine
    // prints it: getting the word order or the byte order wrong shows here.
    const QString addressed = QStringLiteral(
            "   0: 00000000000000000000000001000000:1F90 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 91 1 0000 100 0\n"
            "   1: 0000000000000000FFFF00000100007F:1F91 00000000000000000000000000000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 92 1 0000 100 0\n");
    const QList<freetunnel::SocketOwner> more = freetunnel::parseProcNetTable(addressed, IPPROTO_TCP, AF_INET6);
    QCOMPARE(more.size(), 2);
    QCOMPARE(more[0].address, addressOf("::1"));
    QCOMPARE(more[1].address, addressOf("::ffff:127.0.0.1"));
}

// A parser that accepts the header line would invent a socket on some port and
// attribute it to inode 0, which resolves to nobody — harmless here, but the
// same leniency is what turns a truncated read into a wrong answer.
void TestProcessLookup::ignoresTheHeaderAndAnythingMalformed()
{
    QVERIFY(freetunnel::parseProcNetTable(QString(), IPPROTO_TCP, AF_INET).isEmpty());
    QVERIFY(freetunnel::parseProcNetTable(
                    QStringLiteral("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"),
                    IPPROTO_TCP, AF_INET)
                    .isEmpty());
    // Too few fields, no colon in the local address, and a zero port.
    QVERIFY(freetunnel::parseProcNetTable(QStringLiteral("   0: 0100007F:1F90 00000000:0000\n"), IPPROTO_TCP, AF_INET)
                    .isEmpty());
    QVERIFY(freetunnel::parseProcNetTable(
                    QStringLiteral("   0: nonsense 00000000:0000 0A 0:0 00:0 0 1000 0 41234 1 0000 100 0\n"),
                    IPPROTO_TCP, AF_INET)
                    .isEmpty());
    QVERIFY(freetunnel::parseProcNetTable(
                    QStringLiteral("   0: 0100007F:0000 00000000:0000 0A 0:0 00:0 0 1000 0 41234 1 0000 100 0\n"),
                    IPPROTO_TCP, AF_INET)
                    .isEmpty());
}

void TestProcessLookup::anUnknownPidHasNoIdentity()
{
    QVERIFY(freetunnel::identityForPid(-1).executablePath.isEmpty());
    QVERIFY(freetunnel::identityForPid(0).executablePath.isEmpty());
}

// The report exists because the boolean it replaced could not fire: it asked
// whether the owner table was empty, and it never is — this process always owns
// a socket and can always inspect itself. So "no sockets found" looked exactly
// like success, and asking a person whether that line appeared would have told
// us nothing either way.
void TestProcessLookup::theScanReportsWhatItSaw()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, server.serverPort(), QStringLiteral("127.0.0.1")});

    const auto r = lookup.lastScan();
    QVERIFY2(r.ok, "the walk ran");
    QVERIFY2(r.entries > 0, "this process has a socket open, so the table cannot be empty");
    QVERIFY2(r.distinctPids >= 1, "at least this process owns something");

    // The counters have to be filled, not merely declared: a report of zeroes
    // reads as "this machine has nothing" and would send the next person
    // looking in the wrong place entirely.
    //
    // Windows is handed a finished socket table by the IP helper API and never
    // walks processes at all, so the two process counters are meaningless there.
    // entries and distinctPids above already cover what it does do.
#ifndef Q_OS_WIN
    QVERIFY2(r.pidsScanned > 1, "the walk looked at more than one process");
    QVERIFY2(r.socketsSeen > 0, "and read descriptors while doing it");
    QCOMPARE(r.euid, static_cast<int>(::geteuid()));
#endif
    // Only Linux decides which processes to open, so only there is there
    // anything to count: the pair "hundreds scanned, none watched" is what
    // distinguishes a walk that cannot see other processes from a rule that
    // names nothing running.
#ifdef Q_OS_LINUX
    QVERIFY2(r.pidsWatched >= 1, "one of them was the program the rule names");
#endif

    // Per walk, not since the beginning. These used to accumulate, which was
    // survivable while the table was rebuilt twice a minute and is not now that
    // a walk can happen on any connection: the line a person is asked to paste
    // would grow without bound and mean nothing.
    //
    // The walks are forced by asking about sockets that did not exist when the
    // last one ran, which is what a connection is. Calling invalidate() would
    // not do: it zeroes the report itself, so the counters would look fresh
    // however the walk behaved.
    const int firstScan = r.pidsScanned;
    QList<QTcpServer *> later;
    for (int i = 0; i < 3; ++i) {
        auto *fresh = new QTcpServer;
        QVERIFY(fresh->listen(QHostAddress::LocalHost, 0));
        later.append(fresh);
        letItAffordAnotherWalk(lookup);
        bool skipped = false;
        QVERIFY(!lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, fresh->serverPort(), QStringLiteral("127.0.0.1")},
                                &skipped)
                         .executablePath.isEmpty());
        QVERIFY2(!skipped, "the walk this needs was refused for want of budget");
    }
    qDeleteAll(later);
    QVERIFY2(lookup.lastScan().pidsScanned < firstScan * 2 + 2,
             qPrintable(QStringLiteral("counters accumulated: %1 then %2")
                                .arg(firstScan)
                                .arg(lookup.lastScan().pidsScanned)));
}

// A page's worth of connections from programs nobody named must not each buy a
// walk of the machine. They are the overwhelming majority of what the handler
// sees, and if every one of them rebuilt the table there would be no budget
// left for the connections a rule is actually about — the intermittent failure
// this all exists to remove, returning as "it depends what else was busy".
//
// What makes them cheap is that the walk records the ports it saw and could not
// attribute, so "not one of yours" is an answer rather than a gap.
void TestProcessLookup::connectionsFromUnnamedProgramsDoNotEachBuyAWalk()
{
    ProcessLookup lookup;
    // Nothing on the list is running, so every socket below belongs to a program
    // the rules do not name — which is what every other program on a real
    // machine looks like from here.
    lookup.setWatchList({QStringLiteral("a-program-that-is-not-running")});

    QList<QTcpServer *> sockets;
    for (int i = 0; i < 40; ++i) {
        auto *s = new QTcpServer;
        QVERIFY(s->listen(QHostAddress::LocalHost, 0));
        sockets.append(s);
    }

    // One question to build a table that has seen all of them.
    lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, sockets.first()->serverPort(), QStringLiteral("127.0.0.1")});
    QVERIFY(lookup.lastScan().ok);
    const qint64 before = lookup.walksTaken();

    for (QTcpServer *s : sockets) {
        QVERIFY2(lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, s->serverPort(), QStringLiteral("127.0.0.1")})
                         .executablePath.isEmpty(),
                 "no rule names this program, so it must not be attributed");
    }
    const qint64 walks = lookup.walksTaken() - before;
    qDeleteAll(sockets);

    QVERIFY2(walks <= 1,
             qPrintable(QStringLiteral("%1 connections from unnamed programs cost %2 walks")
                                .arg(40)
                                .arg(walks)));
}

// The same property from the other side: connections the rules DO name, made
// before the walk, are answered by that one walk rather than each forcing
// another. This is what makes a page load affordable.
void TestProcessLookup::aBurstFromTheWatchedProgramSharesOneWalk()
{
    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());

    QList<QTcpServer *> sockets;
    for (int i = 0; i < 20; ++i) {
        auto *s = new QTcpServer;
        QVERIFY(s->listen(QHostAddress::LocalHost, 0));
        sockets.append(s);
    }

    lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, sockets.first()->serverPort(), QStringLiteral("127.0.0.1")});
    const qint64 before = lookup.walksTaken();
    for (QTcpServer *s : sockets) {
        QVERIFY(!lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, s->serverPort(), QStringLiteral("127.0.0.1")})
                         .executablePath.isEmpty());
    }
    const qint64 walks = lookup.walksTaken() - before;
    qDeleteAll(sockets);

    QVERIFY2(walks <= 1,
             qPrintable(QStringLiteral("20 connections already open cost %1 walks").arg(walks)));
}

// The reason the rest of this exists. A table is a snapshot, and a connection is
// by definition made after the last snapshot was taken — so if a miss were
// answered from the snapshot, whether a rule applied would depend on how long
// ago some unrelated connection happened to be. That is what a person saw as
// "it works on some tabs and not others".
//
// The operating system binds the port before the packet that carries it exists,
// so the socket IS there to be found; nothing here has to wait for anything.
//
// Also asked without the address. The core always gives one, but a flow that
// came without it can only be matched by port number, and a row found that way
// may be another program's socket on the same number — so it must buy a fresh
// look rather than answer. This test was the one that failed one run in three
// hundred, whenever the new port happened to be one some other program held on
// another address.
void TestProcessLookup::aSocketOpenedAfterTheLastWalkIsStillFound_data()
{
    QTest::addColumn<QString>("address");
    QTest::newRow("with its address") << QStringLiteral("127.0.0.1");
    QTest::newRow("without one") << QString();
}

void TestProcessLookup::aSocketOpenedAfterTheLastWalkIsStillFound()
{
    QFETCH(QString, address);
    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());

    // A first question, purely so that a table exists and is as fresh as it can
    // be — this is the worst case for the old behaviour, not the best.
    QTcpServer first;
    QVERIFY(first.listen(QHostAddress::LocalHost, 0));
    lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, first.serverPort(), QStringLiteral("127.0.0.1")});
    QVERIFY(lookup.lastScan().ok);

    // Now a socket that table cannot contain, asked about with no wait at all.
    QTcpServer later;
    QVERIFY(later.listen(QHostAddress::LocalHost, 0));
    letItAffordAnotherWalk(lookup);
    bool skipped = false;
    const AppIdentity id = lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, later.serverPort(), address}, &skipped);
    QVERIFY2(!skipped, "the walk this needs was refused for want of budget");
    QVERIFY2(!id.executablePath.isEmpty(),
             "a socket opened after the last walk must still be attributed, immediately");
}

// A rule added while the tunnel is up applies to the next connection, not the
// next session. The table only contains the programs it was told to look for,
// so a list that has changed is a table that never asked about the program the
// user has just added — and they would be left watching a rule do nothing.
void TestProcessLookup::addingAProgramToTheRulesTakesEffectAtOnce()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const LocalFlow flow{AF_INET, IPPROTO_TCP, server.serverPort(), QStringLiteral("127.0.0.1")};

    ProcessLookup lookup;
    lookup.setWatchList({QStringLiteral("some-program-that-is-not-running")});
    QVERIFY2(lookup.resolve(flow).executablePath.isEmpty(),
             "a program no rule names must not be attributed to anyone");

    lookup.setWatchList(watchSelf());
    QVERIFY2(!lookup.resolve(flow).executablePath.isEmpty(),
             "and the moment a rule names it, the very next connection sees it");
}

// Off costs nothing. With no rules there is no program to look for, so the walk
// — the expensive part, and the only part that touches the rest of the machine
// — must not happen at all.
void TestProcessLookup::withNoRulesNothingIsWalkedAtAll()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    ProcessLookup lookup;
    const AppIdentity id =
            lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, server.serverPort(), QStringLiteral("127.0.0.1")});
    QVERIFY(id.executablePath.isEmpty());
    QVERIFY2(!lookup.lastScan().ok, "no rules, so nothing was walked");
}

// The kernel is asked for its open sockets in binary, through the same netlink
// interface `ss` uses, and read back as text from /proc/net only where that is
// refused — a kernel built without the inet_diag modules. Both are live code,
// and the text one runs on no machine this is developed or tested on, which is
// exactly how a fallback rots unnoticed until the day it is needed.
//
// So it is run here, against the same question, and the two must agree. They
// were measured to agree row for row — same inodes, protocols and ports — but
// that was measured once, by hand; this is the part that keeps being true.
void TestProcessLookup::bothWaysOfReadingTheSocketTablesAgree()
{
#ifndef Q_OS_LINUX
    QSKIP("only Linux has two ways of reading them");
#else
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const LocalFlow flow{AF_INET, IPPROTO_TCP, server.serverPort(), QStringLiteral("127.0.0.1")};

    ProcessLookup viaNetlink;
    viaNetlink.setWatchList(watchSelf());
    const AppIdentity fromNetlink = viaNetlink.resolve(flow);
    const ProcessLookup::ScanReport netlinkScan = viaNetlink.lastScan();

    // Everything that depends on the environment happens between these two
    // lines, and the assertions come after it is restored: a QVERIFY that fails
    // inside would otherwise leave every later test reading the text tables.
    qputenv("FT_TEST_NO_SOCKET_NETLINK", "1");
    ProcessLookup viaProcNet;
    viaProcNet.setWatchList(watchSelf());
    const AppIdentity fromProcNet = viaProcNet.resolve(flow);
    const ProcessLookup::ScanReport procScan = viaProcNet.lastScan();

    // And the other half of the answer, which decides most connections: ports
    // that are open and belong to nobody named must be recorded by this path
    // too, or every such connection would buy a walk of its own.
    ProcessLookup unnamed;
    unnamed.setWatchList({QStringLiteral("a-program-that-is-not-running")});
    unnamed.resolve(flow);
    const qint64 walksBefore = unnamed.walksTaken();
    for (int i = 0; i < 5; ++i)
        unnamed.resolve(flow);
    const qint64 unnamedWalks = unnamed.walksTaken() - walksBefore;
    qunsetenv("FT_TEST_NO_SOCKET_NETLINK");

    QVERIFY2(netlinkScan.netlink, "this kernel answers sock_diag, so that is what should be used");
    QVERIFY2(!procScan.netlink, "the hook must actually have forced the text tables");
    QVERIFY2(procScan.ok, "and that path must complete");
    QVERIFY2(!fromNetlink.executablePath.isEmpty(), "the socket is open, so it must be attributed");
    QCOMPARE(fromProcNet.executablePath, fromNetlink.executablePath);
    QCOMPARE(fromProcNet.name, fromNetlink.name);
    QVERIFY2(unnamedWalks == 0,
             qPrintable(QStringLiteral("the text path cost %1 walks for ports it had already seen")
                                .arg(unnamedWalks)));
#endif
}

// A socket the system files under IPv6 has to be findable, and findable whether
// or not the family the core reports agrees with the one the table used.
//
// The two are not always the same thing. A dual-stack socket — one IPv6 socket
// bound to every address, which takes IPv4 as well — is filed under IPv6 as
// [::], while a connection on it is, by address, IPv4.
void TestProcessLookup::anIpv6SocketIsFoundWhicheverFamilyTheFlowClaims()
{
    QTcpServer six;
    if (!six.listen(QHostAddress::LocalHostIPv6, 0))
        QSKIP("no IPv6 loopback on this machine");
    QTcpServer both; // QHostAddress::Any is Qt's dual-stack address
    QVERIFY(both.listen(QHostAddress::Any, 0));
    const quint16 port = both.serverPort();

    // Both open before the first question, so all three are answered by one walk.
    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    QVERIFY2(!lookup.resolve(LocalFlow{AF_INET6, IPPROTO_TCP, six.serverPort(), QStringLiteral("::1")})
                      .executablePath.isEmpty(),
             "an IPv6 socket, asked about as IPv6");
    QVERIFY2(!lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, port, QStringLiteral("127.0.0.1")})
                      .executablePath.isEmpty(),
             "an IPv4 flow on a dual-stack socket");
    QVERIFY2(!lookup.resolve(LocalFlow{AF_INET6, IPPROTO_TCP, port, QStringLiteral("::1")})
                      .executablePath.isEmpty(),
             "and an IPv6 one on the same socket");
}

// The other way an IPv6 socket carries IPv4: a connection to an IPv4 address
// from an IPv6 socket, which the system files as ::ffff:a.b.c.d while the core
// reports the flow as IPv4 from a.b.c.d.
void TestProcessLookup::aConnectionFromAnIpv6SocketToAnIpv4AddressIsFound()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QTcpSocket mapped;
    // Bound first, to Qt's dual-stack address. A port connect() picks may be
    // shared with a live connection elsewhere on the machine, to a different
    // destination — which the kernel allows and a table without destinations
    // cannot tell apart. A port bind() picks is never shared with a live socket;
    // the connection then fills in ::ffff:127.0.0.1 as the local address.
    QVERIFY2(mapped.bind(QHostAddress::Any, 0), qPrintable(mapped.errorString()));
    mapped.connectToHost(QHostAddress(QStringLiteral("::ffff:127.0.0.1")), server.serverPort());
    if (!mapped.waitForConnected(5000))
        QSKIP("this system will not connect an IPv6 socket to an IPv4 address");
    QVERIFY(server.waitForNewConnection(5000));

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    QVERIFY2(!lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, mapped.localPort(), QStringLiteral("127.0.0.1")})
                      .executablePath.isEmpty(),
             qPrintable(QStringLiteral("the connection from local %1 is ours")
                                .arg(mapped.localAddress().toString())));
}

// A port number is not a socket. The kernel lets two programs hold the same
// number at once, on different local addresses or in the two families, and on a
// real desktop they do: a service bound to the LAN address, a container bridge,
// an IPv6 link-local listener. So a table that remembers only the number, and
// holds "seen, belongs to nobody you named" as a final answer, answers a new
// socket with a row about somebody else's — found on this machine one question
// in three hundred, as this suite's intermittent failure.
//
// Both halves: our new socket must not be answered by theirs...
void TestProcessLookup::aNewSocketIsNotAnsweredByAnotherProgramsOnTheSamePort_data()
{
    QTest::addColumn<QHostAddress>("theirs");
    QTest::addColumn<QString>("asked");
    const QHostAddress six(QHostAddress::LocalHostIPv6);
    const QHostAddress lan(QStringLiteral("127.0.0.2"));
    QTest::newRow("same port, other family") << six << QStringLiteral("127.0.0.1");
    QTest::newRow("same port, other address") << lan << QStringLiteral("127.0.0.1");
    // Asked without the address, a flow can be matched only by port number, and
    // their row is the only one the old table has on it. It must buy a fresh
    // look instead of answering.
    QTest::newRow("other family, asked without an address") << six << QString();
    QTest::newRow("other address, asked without an address") << lan << QString();
}

void TestProcessLookup::aNewSocketIsNotAnsweredByAnotherProgramsOnTheSamePort()
{
    QFETCH(QHostAddress, theirs);
    QFETCH(QString, asked);
    AnotherProgram other;
    const quint16 port = other.start(theirs);
    if (port == 0)
        QSKIP("this machine cannot listen on that address");

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    QTcpServer first;
    QVERIFY(first.listen(QHostAddress::LocalHost, 0));
    lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, first.serverPort(), QStringLiteral("127.0.0.1")});
    QVERIFY(lookup.lastScan().ok); // a table that has seen their socket, and not ours

    QTcpServer ours;
    if (!ours.listen(QHostAddress::LocalHost, port))
        QSKIP("something else holds 127.0.0.1 on that port");
    letItAffordAnotherWalk(lookup);
    const qint64 walks = lookup.walksTaken();
    bool skipped = false;
    const AppIdentity id = lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, port, asked}, &skipped);
    QVERIFY2(!skipped, "the walk this needs was refused for want of budget");
#ifndef Q_OS_LINUX
    // By port number alone, on the same family, two programs' sockets are one row,
    // and which of them it names is only Linux's to settle: it records sockets no
    // watched program holds as nobody's, so ours wins; macOS and Windows record
    // every socket's owner, and keep whichever the system listed first. What every
    // platform owes is that the old row was not trusted, and the table looked again.
    if (asked.isEmpty() && theirs.protocol() == QAbstractSocket::IPv4Protocol) {
        QVERIFY2(lookup.walksTaken() > walks, "a row found by port number alone, from before the question, answered");
        return;
    }
#else
    Q_UNUSED(walks)
#endif
    QVERIFY2(!id.executablePath.isEmpty(), "our socket, answered by another program's on the same number");
    QCOMPARE(canonical(id.executablePath), canonical(QCoreApplication::applicationFilePath()));
}

// ...and theirs must not be answered by ours: that would route another program's
// connection by a rule written about this one.
void TestProcessLookup::anotherProgramsNewSocketIsNotAnsweredByOurs_data()
{
    QTest::addColumn<QHostAddress>("theirs");
    QTest::addColumn<int>("family");
    QTest::newRow("same port, other family") << QHostAddress(QHostAddress::LocalHostIPv6) << int(AF_INET6);
    QTest::newRow("same port, other address") << QHostAddress(QStringLiteral("127.0.0.2")) << int(AF_INET);
}

void TestProcessLookup::anotherProgramsNewSocketIsNotAnsweredByOurs()
{
    QFETCH(QHostAddress, theirs);
    QFETCH(int, family);
    QTcpServer ours;
    QVERIFY(ours.listen(QHostAddress::LocalHost, 0));
    const quint16 port = ours.serverPort();

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    QVERIFY(!lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, port, QStringLiteral("127.0.0.1")})
                     .executablePath.isEmpty());

    AnotherProgram other;
    if (other.start(theirs, port) != port)
        QSKIP("this machine cannot listen on that address and port");
    letItAffordAnotherWalk(lookup);
    bool skipped = false;
    const AppIdentity id = lookup.resolve(LocalFlow{family, IPPROTO_TCP, port, theirs.toString()}, &skipped);
    QVERIFY2(!skipped, "the walk this needs was refused for want of budget");
    QVERIFY2(id.executablePath.isEmpty(),
             qPrintable(QStringLiteral("another program's socket was answered as %1").arg(id.executablePath)));
}

// Which row answers which flow, on every platform and without a live socket:
// the rules resolve() lives by, one at a time.
void TestProcessLookup::theTableMatchesAFlowToItsOwnSocket_data()
{
    QTest::addColumn<int>("rowFamily");
    QTest::addColumn<QString>("rowAddress");
    QTest::addColumn<int>("flowFamily");
    QTest::addColumn<QString>("flowAddress");
    QTest::addColumn<bool>("matches");

    QTest::newRow("its own address") << int(AF_INET) << "127.0.0.1" << int(AF_INET) << "127.0.0.1" << true;
    QTest::newRow("bound to every address") << int(AF_INET) << "0.0.0.0" << int(AF_INET) << "10.1.2.3" << true;
    QTest::newRow("another address, same port") << int(AF_INET) << "192.168.1.119" << int(AF_INET)
                                                << "127.0.0.1" << false;
    QTest::newRow("IPv4 on a dual-stack socket") << int(AF_INET6) << "::" << int(AF_INET) << "127.0.0.1" << true;
    QTest::newRow("IPv4 from an IPv6 socket") << int(AF_INET6) << "::ffff:10.1.2.3" << int(AF_INET)
                                              << "10.1.2.3" << true;
    QTest::newRow("reported as mapped IPv6") << int(AF_INET) << "10.1.2.3" << int(AF_INET6)
                                             << "::ffff:10.1.2.3" << true;
    QTest::newRow("IPv4 on an IPv6 socket bound to ::ffff:0.0.0.0") << int(AF_INET6) << "::ffff:0.0.0.0"
                                                                    << int(AF_INET) << "10.1.2.3" << true;
    QTest::newRow("another program's IPv6, same port") << int(AF_INET6) << "fe80::1" << int(AF_INET)
                                                       << "127.0.0.1" << false;
    QTest::newRow("IPv6 loopback is not IPv4 loopback") << int(AF_INET) << "127.0.0.1" << int(AF_INET6)
                                                        << "::1" << false;
    QTest::newRow("IPv6 is not taken by an IPv4 wildcard") << int(AF_INET) << "0.0.0.0" << int(AF_INET6)
                                                           << "::1" << false;
}

void TestProcessLookup::theTableMatchesAFlowToItsOwnSocket()
{
    QFETCH(int, rowFamily);
    QFETCH(QString, rowAddress);
    QFETCH(int, flowFamily);
    QFETCH(QString, flowAddress);
    QFETCH(bool, matches);

    SocketOwnerTable table;
    table.record(rowFamily, IPPROTO_TCP, 4242, addressOf(qPrintable(rowAddress)), 77);
    // The same address and number under the other protocol is another socket.
    table.record(flowFamily, IPPROTO_UDP, 4242, addressOf(qPrintable(flowAddress)), 88);
    const SocketOwnerTable::Match m = table.find(LocalFlow{flowFamily, IPPROTO_TCP, 4242, flowAddress});
    QCOMPARE(m.found, matches);
    if (matches) {
        QCOMPARE(m.pid, qint64(77));
        QVERIFY(m.byAddress);
    }
}

// Several sockets can share one address: a listener and every connection
// accepted on it. The first owner stays, unless the first was nobody watched.
void TestProcessLookup::aSocketsFirstOwnerIsKeptUnlessItWasNobody()
{
    const SocketAddress lo = addressOf("127.0.0.1");
    const LocalFlow flow{AF_INET, IPPROTO_TCP, 4242, QStringLiteral("127.0.0.1")};

    SocketOwnerTable table;
    table.record(AF_INET, IPPROTO_TCP, 4242, lo, freetunnel::kUnattributed);
    QCOMPARE(table.find(flow).pid, freetunnel::kUnattributed);
    table.record(AF_INET, IPPROTO_TCP, 4242, lo, 10);
    QCOMPARE(table.find(flow).pid, qint64(10)); // the watched program wins over nobody
    table.record(AF_INET, IPPROTO_TCP, 4242, lo, 20);
    table.record(AF_INET, IPPROTO_TCP, 4242, lo, freetunnel::kUnattributed);
    QCOMPARE(table.find(flow).pid, qint64(10)); // and keeps it
    QCOMPARE(table.size(), 1);
    QCOMPARE(table.distinctPids(), 1);

    // An exact address is preferred to a socket bound to every address.
    table.record(AF_INET, IPPROTO_TCP, 5000, SocketAddress{}, 30);
    table.record(AF_INET, IPPROTO_TCP, 5000, lo, freetunnel::kUnattributed);
    QCOMPARE(table.find(LocalFlow{AF_INET, IPPROTO_TCP, 5000, QStringLiteral("127.0.0.1")}).pid,
             freetunnel::kUnattributed);
    QCOMPARE(table.distinctPids(), 2);

    table.clear();
    QCOMPARE(table.size(), 0);
    QVERIFY(!table.find(flow).found);
}

// The core always gives the address; a flow without one can only be matched by
// port number, its own family first, and says so — resolve() trusts a match
// like that only from a table built after it was asked.
void TestProcessLookup::aFlowWithoutAnAddressIsMatchedByPortAlone()
{
    SocketOwnerTable table;
    table.record(AF_INET6, IPPROTO_TCP, 4242, addressOf("fe80::1"), 60);
    SocketOwnerTable::Match m = table.find(LocalFlow{AF_INET, IPPROTO_TCP, 4242, QString()});
    QVERIFY(m.found);
    QCOMPARE(m.pid, qint64(60)); // the other family, when its own has nothing
    QVERIFY(!m.byAddress);

    table.record(AF_INET, IPPROTO_TCP, 4242, addressOf("192.168.1.119"), 70);
    m = table.find(LocalFlow{AF_INET, IPPROTO_TCP, 4242, QString()});
    QCOMPARE(m.pid, qint64(70)); // its own family first
    QVERIFY(!m.byAddress);
    QVERIFY(!table.find(LocalFlow{AF_INET, IPPROTO_UDP, 4242, QString()}).found);
}

// A connection that has been closed stays in the system's tables for a minute,
// in TIME_WAIT, holding its address and port and belonging to no process. The
// next socket to take that address and port — which the kernel allows — must not
// be answered by it.
void TestProcessLookup::aClosedConnectionDoesNotAnswerForTheNextSocketOnItsAddress()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QTcpSocket client;
    // SO_REUSEADDR on the connection, which the socket in TIME_WAIT keeps, is what
    // lets the listener below take its address and port while it lingers.
    QVERIFY(client.bind(QHostAddress::LocalHost, 0, QAbstractSocket::ShareAddress));
    const quint16 port = client.localPort();
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(5000));
    QVERIFY(server.waitForNewConnection(5000));
    QTcpSocket *accepted = server.nextPendingConnection();
    client.disconnectFromHost(); // the side that closes first is the one left in TIME_WAIT
    QVERIFY(accepted->state() == QAbstractSocket::UnconnectedState || accepted->waitForDisconnected(5000));
    delete accepted;
    client.abort();

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    QTcpServer first;
    QVERIFY(first.listen(QHostAddress::LocalHost, 0));
    lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, first.serverPort(), QStringLiteral("127.0.0.1")});
    QVERIFY(lookup.lastScan().ok); // a table taken while the closed connection lingers

    QTcpServer next;
    if (!next.listen(QHostAddress::LocalHost, port))
        QSKIP("this system would not let a new socket take the closed one's address");
    letItAffordAnotherWalk(lookup);
    bool skipped = false;
    const AppIdentity id =
            lookup.resolve(LocalFlow{AF_INET, IPPROTO_TCP, port, QStringLiteral("127.0.0.1")}, &skipped);
    QVERIFY2(!skipped, "the walk this needs was refused for want of budget");
    QVERIFY2(!id.executablePath.isEmpty(), "the new socket, answered by the closed one's row");
}

// An IPv6-only socket on [::] cannot carry IPv4, and the kernel lets an IPv4
// socket take the same port number beside it; where the system says which
// sockets are IPv6-only, their rows do not answer for IPv4.
void TestProcessLookup::anIpv6OnlyRowDoesNotAnswerForIpv4()
{
    SocketOwnerTable table;
    table.record(AF_INET6, IPPROTO_UDP, 4242, SocketAddress{}, 77, true);
    QVERIFY(!table.find(LocalFlow{AF_INET, IPPROTO_UDP, 4242, QStringLiteral("127.0.0.1")}).found);
    QCOMPARE(table.find(LocalFlow{AF_INET6, IPPROTO_UDP, 4242, QStringLiteral("::1")}).pid, qint64(77));

    table.record(AF_INET6, IPPROTO_UDP, 5000, SocketAddress{}, 88, false); // dual-stack
    QCOMPARE(table.find(LocalFlow{AF_INET, IPPROTO_UDP, 5000, QStringLiteral("127.0.0.1")}).pid, qint64(88));
    table.clear();
    table.record(AF_INET6, IPPROTO_UDP, 4242, SocketAddress{}, 99, false);
    QCOMPARE(table.find(LocalFlow{AF_INET, IPPROTO_UDP, 4242, QStringLiteral("127.0.0.1")}).pid, qint64(99));
}

// An IPv6 socket bound to the IPv4 wildcard, ::ffff:0.0.0.0, carries IPv4 from
// every local address. .NET's dual-mode sockets are bound that way when given
// IPAddress.Any, and a UDP one never gets a more specific address.
void TestProcessLookup::anIpv4FlowOnASocketBoundToTheMappedWildcardIsFound()
{
#ifdef Q_OS_WIN
    QSKIP("bound with the POSIX socket calls");
#else
    const int fd = ::socket(AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0)
        QSKIP("no IPv6 sockets on this machine");
    int off = 0;
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    sockaddr_in6 bound{};
    bound.sin6_family = AF_INET6;
    bound.sin6_addr.s6_addr[10] = 0xff;
    bound.sin6_addr.s6_addr[11] = 0xff;
    socklen_t length = sizeof(bound);
    const bool ok = ::bind(fd, reinterpret_cast<const sockaddr *>(&bound), sizeof(bound)) == 0
            && ::getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &length) == 0;
    const quint16 port = ntohs(bound.sin6_port);

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    const AppIdentity id = ok ? lookup.resolve(LocalFlow{AF_INET, IPPROTO_UDP, port, QStringLiteral("10.1.2.3")})
                              : AppIdentity{};
    ::close(fd);
    if (!ok)
        QSKIP("this system will not bind an IPv6 socket to ::ffff:0.0.0.0");
    QVERIFY2(!id.executablePath.isEmpty(), "an IPv4 flow on the socket it arrived at");
#endif
}

// The live half: another program's IPv6-only socket on [::] and our IPv4 socket on
// the same port number, which the kernel allows side by side.
void TestProcessLookup::anotherProgramsIpv6OnlySocketDoesNotAnswerForOurIpv4()
{
#ifdef Q_OS_WIN
    QSKIP("Windows does not say which sockets are IPv6-only; see SocketOwnerTable");
#else
    AnotherProgram other;
    const quint16 port = other.start(QHostAddress(QStringLiteral("::")), 0, "udp");
    if (port == 0)
        QSKIP("no IPv6 on this machine");

    ProcessLookup lookup;
    lookup.setWatchList(watchSelf());
    QUdpSocket first;
    QVERIFY(first.bind(QHostAddress::LocalHost, 0));
    lookup.resolve(LocalFlow{AF_INET, IPPROTO_UDP, first.localPort(), QStringLiteral("127.0.0.1")});
    QVERIFY(lookup.lastScan().ok); // a table that has their socket and not ours

    QUdpSocket ours;
    QVERIFY2(ours.bind(QHostAddress::LocalHost, port),
             "their socket should be IPv6-only, which leaves the IPv4 port number free");
    letItAffordAnotherWalk(lookup);
    bool skipped = false;
    const AppIdentity id =
            lookup.resolve(LocalFlow{AF_INET, IPPROTO_UDP, port, QStringLiteral("127.0.0.1")}, &skipped);
    QVERIFY2(!skipped, "the walk this needs was refused for want of budget");
    QVERIFY2(!id.executablePath.isEmpty(), "our IPv4 socket, answered by their IPv6-only one");
#endif
}

int main(int argc, char *argv[])
{
    if (argc == 5 && std::strcmp(argv[1], "--hold") == 0)
        return holdASocket(argc, argv);
    QCoreApplication app(argc, argv);
    TestProcessLookup tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "test_processlookup.moc"
