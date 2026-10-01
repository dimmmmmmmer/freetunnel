// cppcheck-suppress-file missingIncludeSystem
// End-to-end single-instance control IPC, exercising PRODUCTION code: the
// second launch (forwardToRunningInstance) and the token/framing checks the
// listener applies to whatever arrives.
//
// The previous version of this file created a bare QLocalServer, wrote bytes
// into it and read them back — it verified that Qt delivers bytes, contained no
// project code at all, and could not fail. The auth boundary it was named after
// (an unauthenticated local peer must not be able to drive the running app) was
// never touched.
#include <QtTest>

#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <thread>
#include <vector>

#if defined(Q_OS_UNIX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

#include "core/InstanceControl.h"

using freetunnel::ForwardResult;

class TestIntegrationSingleInstance : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void forwardDeliversAuthenticatedCommand();
    void listenerRejectsWrongToken();
    void forwardRefusesWhenNoTokenExists();
    void listenerRejectsUnframedMessage();
    void forwardFailsWhenNoInstanceIsListening();
    void aStaleSocketIsNoInstanceAndOnlyItIsCleared();
    void forwardFindsAnOlderInstanceOnTheSharedName();
    void aBusyListenerOnTheSharedNameIsNoInstance();
    void aBusyListenerOfOursIsUnreachable();
    void aNameAnotherAccountHoldsIsNoInstance();
    void aLinkAtTheNameIsNotOurs();
    void aPipeThatStaysBusyIsNoInstance();
    void aLinkToAListenerOfOursIsNotHandedTheLink();
    void aRefusalWhileOursIsRunningIsUnreachable();
    void aBusyInstanceOfOursThatCatchesUpIsHandedTheLink();
    void theRuntimeDirectoryNameIsListenedOnAndFound();
    void aStartAsksNoPipeWhetherItIsStale();
    void cleanupTestCase();

private:
    struct Received {
        bool got = false;
        QString token;
        QString payload;
    };
    Received readOne(QLocalServer &server, int timeoutMs = 3000);
    void wireCollector(QLocalServer &server);

    QByteArray m_received;
    QString m_socketName;
    QTemporaryDir m_configHome;
};

void TestIntegrationSingleInstance::initTestCase()
{
    QVERIFY(m_configHome.isValid());
    // Keep the auth token out of the real user's store: on Linux the file
    // fallback follows XDG_CONFIG_HOME, and the credential store resolves to a
    // test-only service because test targets are built with FT_ENABLE_TEST_HOOKS
    // (see credentialServiceName()).
    qputenv("XDG_CONFIG_HOME", m_configHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SingleInstanceTest"));

    m_socketName = QStringLiteral("freetunnel-si-test-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(m_socketName);
}

void TestIntegrationSingleInstance::cleanupTestCase()
{
    freetunnel::removeInstanceAuthToken();
    QLocalServer::removeServer(m_socketName);
}

// Collect asynchronously rather than polling after the fact: the real sender
// writes, flushes and disconnects, and on Windows named pipes a peer that has
// already gone leaves nothing to accept — the message has to be picked up as it
// arrives, which is also how the production listener works.
TestIntegrationSingleInstance::Received
TestIntegrationSingleInstance::readOne(QLocalServer &server, int timeoutMs)
{
    Received out;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs && !out.got) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (m_received.isEmpty())
            continue;
        if (freetunnel::parseInstanceMessage(m_received, &out.token, &out.payload))
            out.got = true;
    }
    Q_UNUSED(server);
    return out;
}

void TestIntegrationSingleInstance::wireCollector(QLocalServer &server)
{
    m_received.clear();
    connect(&server, &QLocalServer::newConnection, this, [this, &server]() {
        while (QLocalSocket *peer = server.nextPendingConnection()) {
            connect(peer, &QLocalSocket::readyRead, this,
                    [this, peer]() { m_received += peer->readAll(); });
            connect(peer, &QLocalSocket::disconnected, this, [this, peer]() {
                m_received += peer->readAll();
                peer->deleteLater();
            });
            m_received += peer->readAll();
        }
    });
}

// The real second-launch path: find the session token, verify the listener
// belongs to the same user, deliver the control command verbatim.
void TestIntegrationSingleInstance::forwardDeliversAuthenticatedCommand()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QVERIFY(!token.isEmpty());

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(m_socketName));
    wireCollector(server);

    // Send from another thread so the listener is spinning its event loop while
    // the client writes — which is what happens in production (the running
    // instance is a separate process). Doing it inline blocks the loop until the
    // sender has already disconnected, and a Windows named pipe discards data
    // the server never read.
    ForwardResult sent = ForwardResult::NoInstance;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance({m_socketName},
                                                    QStringLiteral("freetunnel://toggle"));
    });
    const Received got = readOne(server);
    sender.join();
    QCOMPARE(sent, ForwardResult::Forwarded);
    QVERIFY(got.got);
    QCOMPARE(got.payload, QStringLiteral("freetunnel://toggle"));
    // It really is the session token — a sender that shipped anything at all
    // would satisfy the payload check above just the same.
    QVERIFY(freetunnel::instanceTokensEqual(got.token, token));
    QVERIFY(!freetunnel::instanceTokensEqual(got.token, token + QStringLiteral("x")));
}

// The gates a forged control message has to clear, checked one at a time over a
// real socket: the peer really is this user, its framing really does parse, and
// the token comparison is therefore the only thing left that can reject it. That
// ordering is the point — it stops a future green run from meaning "the uid check
// happened to refuse", which would leave token auth measured by nothing.
//
// Read this as a characterisation of the individual gates, NOT as coverage of the
// listener: it stands up a bare QLocalServer and calls the gate functions itself,
// so a bug in how handleInstanceConnection() *composes* them is invisible here.
// The listener proper is covered in test_app_startup.cpp
// (wireInstanceServerIgnoresWrongToken and wireInstanceServerForwardsCommand),
// which wires the production wireInstanceServer() and asserts on the Backend.
void TestIntegrationSingleInstance::listenerRejectsWrongToken()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QVERIFY(!token.isEmpty());

    const QString forgedSocket = m_socketName + QStringLiteral("-forged");
    QLocalServer::removeServer(forgedSocket);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(forgedSocket));

    // "freetunnel://disconnect" is the payload with teeth: accepted, it drops a
    // live tunnel and puts the user back on the open network without a prompt.
    const QByteArray forged = freetunnel::formatInstanceMessage(
            QStringLiteral("not-the-token"), QStringLiteral("freetunnel://disconnect"));
    QLocalSocket forger;
    forger.connectToServer(forgedSocket);
    QVERIFY(forger.waitForConnected(3000));
    QCOMPARE(forger.write(forged), static_cast<qint64>(forged.size()));
    // Best effort, result ignored, exactly as forwardToRunningInstance() does it:
    // on Windows the named-pipe write is completed asynchronously and is still
    // queued at this point, so waitForBytesWritten() answers false for a message
    // that does arrive. Whether it arrived is asserted below, by reading it.
    forger.flush();
    forger.waitForBytesWritten(3000);

    QVERIFY(server.waitForNewConnection(3000));
    QLocalSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);
    QByteArray received = peer->readAll();
    while (received.size() < forged.size() && peer->waitForReadyRead(3000))
        received += peer->readAll();
    QCOMPARE(received, forged);

    // Gate 1 lets it through, and is supposed to: the uid check only keeps out
    // OTHER users, and this attacker is us.
    QVERIFY(freetunnel::localSocketPeerIsSameUser(peer, freetunnel::SocketEnd::WeAccepted));
    // Gate 2 lets it through too: the message is well-formed and the command is
    // one the running instance would happily execute.
    QString parsedToken;
    QString payload;
    QVERIFY(freetunnel::parseInstanceMessage(received, &parsedToken, &payload));
    QCOMPARE(payload, QStringLiteral("freetunnel://disconnect"));
    // Gate 3 is therefore the only one refusing.
    QVERIFY(!freetunnel::instanceTokensEqual(parsedToken, token));
    // ...and it refuses because of the token, not because it refuses everything:
    // the same bytes carrying the session token compare equal.
    const QByteArray genuine = freetunnel::formatInstanceMessage(
            token, QStringLiteral("freetunnel://disconnect"));
    QString genuineToken;
    QString genuinePayload;
    QVERIFY(freetunnel::parseInstanceMessage(genuine, &genuineToken, &genuinePayload));
    QCOMPARE(genuinePayload, payload);
    QVERIFY(freetunnel::instanceTokensEqual(genuineToken, token));

    peer->disconnectFromServer();
    delete peer;
    server.close();
    QLocalServer::removeServer(forgedSocket);
}

// The session token is not decoration: with no token on file the second launch
// must not send a command at all. This is the degraded state
// startSingleInstanceServer() leaves behind when it cannot persist a token, and
// treating "no token" as "no check needed" would turn every such run into an
// unauthenticated control channel.
//
// Nor is it "no instance". The instance is right there, and a launch that took
// the missing token to mean otherwise started a full second copy beside it,
// which took the socket name over. It connects to learn that much, sends
// nothing, and reports the instance unreachable.
void TestIntegrationSingleInstance::forwardRefusesWhenNoTokenExists()
{
    const QString socketName = m_socketName + QStringLiteral("-untokened");
    QLocalServer::removeServer(socketName);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(socketName));
    wireCollector(server);

    freetunnel::removeInstanceAuthToken();
    QCOMPARE(freetunnel::forwardToRunningInstance({socketName},
                                                  QStringLiteral("freetunnel://disconnect")),
             ForwardResult::Unreachable);
    // Nothing may reach the listener — a sender that shipped an empty token
    // would be probing the very equality case the listener has to reject.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    QVERIFY(m_received.isEmpty());

    // Control: restore the token and the very same call now succeeds, so the
    // refusal above is about the missing token rather than about this listener
    // being unreachable.
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    ForwardResult sent = ForwardResult::NoInstance;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance({socketName},
                                                    QStringLiteral("freetunnel://disconnect"));
    });
    const Received got = readOne(server);
    sender.join();
    QCOMPARE(sent, ForwardResult::Forwarded);
    QVERIFY(got.got);
    QVERIFY(freetunnel::instanceTokensEqual(got.token, token));

    server.close();
    QLocalServer::removeServer(socketName);
}

void TestIntegrationSingleInstance::listenerRejectsUnframedMessage()
{
    QString token;
    QString payload;
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("freetunnel://toggle"), &token, &payload));
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray(), &token, &payload));
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("\npayload-without-token"), &token,
                                              &payload));
}

// No listener: the launch must fall through to starting a new instance rather
// than reporting success, which would make the app silently fail to start.
void TestIntegrationSingleInstance::forwardFailsWhenNoInstanceIsListening()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    const QString dead = m_socketName + QStringLiteral("-nobody-home");
    QLocalServer::removeServer(dead);
    QCOMPARE(freetunnel::forwardToRunningInstance({dead}, QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
}

#if defined(Q_OS_UNIX)
static bool unixSocketAddress(const QString &path, sockaddr_un *addr)
{
    const QByteArray encoded = QFile::encodeName(path);
    *addr = sockaddr_un{};
    addr->sun_family = AF_UNIX;
    if (encoded.size() >= static_cast<qsizetype>(sizeof(addr->sun_path)))
        return false;
    memcpy(addr->sun_path, encoded.constData(), static_cast<size_t>(encoded.size()));
    return true;
}

// What a crashed instance leaves behind on Unix: a socket file that nothing
// listens on any more.
static bool leaveStaleSocketFile(const QString &path)
{
    sockaddr_un addr{};
    if (!unixSocketAddress(path, &addr))
        return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    const bool listened = ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0
            && ::listen(fd, 1) == 0;
    ::close(fd);
    return listened && QFileInfo::exists(path);
}

// A listener that lets no one in: connections it never accepts fill its
// backlog. Anyone can put one on a name in /tmp, and it is what an instance of
// ours would look like had it stopped taking connections. How many connections
// a backlog lets wait differs: a backlog of 0 held one on Linux but left room on
// macOS, where the forward under test simply went through. So the backlog is 1,
// and it is filled until the system turns a connection away.
class BusySocket
{
public:
    explicit BusySocket(const QString &path) : m_path(path)
    {
        sockaddr_un addr{};
        if (!unixSocketAddress(path, &addr))
            return;
        const auto *a = reinterpret_cast<const sockaddr *>(&addr);
        m_listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
        m_bound = m_listener >= 0 && ::bind(m_listener, a, sizeof(addr)) == 0;
        if (!m_bound || ::listen(m_listener, 1) != 0)
            return;
        // Non-blocking, so that a full backlog answers at once instead of
        // holding connect() until something is accepted, which on Linux is never.
        for (int i = 0; i < 200; ++i) {
            const int filler = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (filler < 0)
                return;
            ::fcntl(filler, F_SETFL, ::fcntl(filler, F_GETFL) | O_NONBLOCK);
            if (::connect(filler, a, sizeof(addr)) == 0) {
                m_fillers.push_back(filler);
                continue;
            }
            m_lastError = errno;
            ::close(filler);
            m_busy = !m_fillers.empty()
                    && (m_lastError == EAGAIN || m_lastError == EWOULDBLOCK
                        || m_lastError == ECONNREFUSED);
            return;
        }
    }
    ~BusySocket()
    {
        for (const int filler : m_fillers)
            ::close(filler);
        if (m_listener >= 0)
            ::close(m_listener);
        if (m_bound)
            QFile::remove(m_path);
    }
    BusySocket(const BusySocket &) = delete;
    BusySocket &operator=(const BusySocket &) = delete;
    bool isBusy() const { return m_busy; }
    // For a failed isBusy(): how far filling got, and what stopped it.
    QString describe() const
    {
        return QStringLiteral("%1: %2 connection(s) waiting, then errno %3 (%4)")
                .arg(m_path)
                .arg(m_fillers.size())
                .arg(m_lastError)
                .arg(QString::fromLocal8Bit(std::strerror(m_lastError)));
    }

private:
    QString m_path;
    int m_listener = -1;
    int m_lastError = 0;
    std::vector<int> m_fillers;
    bool m_bound = false;
    bool m_busy = false;
};

// Whether connecting to @p name fails without one of the answers that say
// nothing is there (no such name, refused, not allowed). That is how a busy
// listener fails on Linux. A system that refuses a full backlog outright has
// already said "nothing here", and leaves the test nothing to tell apart.
static bool connectFailsWithoutSayingWhy(const QString &name)
{
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(250))
        return false;
    switch (probe.error()) {
    case QLocalSocket::ServerNotFoundError:
    case QLocalSocket::ConnectionRefusedError:
    case QLocalSocket::SocketAccessError:
        return false;
    default:
        return true;
    }
}
#endif

// A crash leaves the socket file behind on Unix, and that is no instance: read
// as one that merely did not answer, it would keep FreeTunnel from starting at
// all after a crash. It is also the only name a start may clear. Clearing used
// to be unconditional, and on Unix that unlinked a running instance's socket and
// left it reachable by nothing for the rest of its session.
void TestIntegrationSingleInstance::aStaleSocketIsNoInstanceAndOnlyItIsCleared()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));

#if defined(Q_OS_UNIX)
    // Short: a socket path has to fit in about a hundred bytes, and macOS's
    // temporary directory takes half of that already.
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString stale = dir.filePath(QStringLiteral("stale"));
    QVERIFY(leaveStaleSocketFile(stale));
    QCOMPARE(freetunnel::forwardToRunningInstance({stale}, QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
    QVERIFY(freetunnel::removeStaleInstanceServer(stale));
    QVERIFY(!QFileInfo::exists(stale));
#endif

    const QString live = m_socketName + QStringLiteral("-live");
    QLocalServer::removeServer(live);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(live));
    QVERIFY(!freetunnel::removeStaleInstanceServer(live));
    QLocalSocket client;
    client.connectToServer(live);
    QVERIFY2(client.waitForConnected(3000), "a live instance's socket name was removed");
    client.abort();
    server.close();
    QLocalServer::removeServer(live);
}

// An update installed while an older FreeTunnel keeps running: the older one
// listens on the name every build up to 1.2.2 shared, and this user's own name
// has nobody on it. The launch has to find the older one there, not start a
// second copy beside it.
void TestIntegrationSingleInstance::forwardFindsAnOlderInstanceOnTheSharedName()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    const QString own = m_socketName + QStringLiteral("-own");
    const QString shared = m_socketName + QStringLiteral("-shared");
    QLocalServer::removeServer(own);
    QLocalServer::removeServer(shared);
    QLocalServer older;
    older.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(older.listen(shared));
    wireCollector(older);

    ForwardResult sent = ForwardResult::NoInstance;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance({own, shared},
                                                    QStringLiteral("freetunnel://toggle"));
    });
    const Received got = readOne(older);
    sender.join();
    QCOMPARE(sent, ForwardResult::Forwarded);
    QVERIFY(got.got);
    QCOMPARE(got.payload, QStringLiteral("freetunnel://toggle"));

    older.close();
    QLocalServer::removeServer(shared);
}

// A listener on the shared name that lets no one in is not ours to give way to.
// Every user's launch still tries that name after its own, and any account on
// the computer can hold it. Taken for a busy instance of ours, it made the launch
// exit, and FreeTunnel did not start at all for anyone who had none running.
void TestIntegrationSingleInstance::aBusyListenerOnTheSharedNameIsNoInstance()
{
#if defined(Q_OS_UNIX)
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString own = dir.filePath(QStringLiteral("own"));
    const QString shared = dir.filePath(QStringLiteral("shared"));
    const BusySocket squatter(shared);
    QVERIFY2(squatter.isBusy(), qPrintable(squatter.describe()));
    if (!connectFailsWithoutSayingWhy(shared))
        QSKIP("this system refuses a connection to a full backlog outright");

    QCOMPARE(freetunnel::forwardToRunningInstance({own, shared},
                                                  QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
#else
    QSKIP("a pipe that stays busy is aPipeThatStaysBusyIsNoInstance");
#endif
}

// The other side of it: a listener of this user's own that lets no one in is an
// instance of ours too busy to answer, and a launch must not start beside it.
// Told by who owns the socket file, wherever the name puts it: in the temporary
// directory for a bare name, as FreeTunnel's own is, or at a path.
void TestIntegrationSingleInstance::aBusyListenerOfOursIsUnreachable()
{
#if defined(Q_OS_UNIX)
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString bare = m_socketName + QStringLiteral("-busy");
    const QList<std::pair<QString, QString>> nameAndFile = {
        {bare, QDir::tempPath() + QLatin1Char('/') + bare},
        {dir.filePath(QStringLiteral("busy")), dir.filePath(QStringLiteral("busy"))},
    };
    for (const auto &[name, file] : nameAndFile) {
        QFile::remove(file);
        const BusySocket ours(file);
        QVERIFY2(ours.isBusy(), qPrintable(ours.describe()));
        // macOS refuses a full backlog outright, as it does a socket file nothing
        // listens on; there the claim a running instance holds tells them apart.
        QObject instance;
        if (!connectFailsWithoutSayingWhy(name))
            QVERIFY(freetunnel::claimInstanceName(&instance, name));
        QCOMPARE(freetunnel::forwardToRunningInstance({name}, QStringLiteral("freetunnel://toggle")),
                 ForwardResult::Unreachable);
    }
#else
    QSKIP("Unix socket files only");
#endif
}

// A name another account holds and that turns us away is no instance of ours,
// even as this user's own name, which in /tmp another account can take first.
// A test cannot make another account's busy listener without root; the system
// log's socket stands in. Root owns it, and as a datagram socket it refuses a
// stream connection with an error a FreeTunnel listener would never give: the
// same unrecognised kind of failure as a full backlog.
void TestIntegrationSingleInstance::aNameAnotherAccountHoldsIsNoInstance()
{
#if defined(Q_OS_UNIX)
    QString theirs;
    for (const char *candidate : {"/run/systemd/journal/dev-log", "/var/run/syslog"}) {
        const QFileInfo socket(QString::fromLatin1(candidate));
        if (socket.exists() && socket.ownerId() != ::getuid()) {
            theirs = socket.filePath();
            break;
        }
    }
    if (theirs.isEmpty() || !connectFailsWithoutSayingWhy(theirs))
        QSKIP("no socket of another account here that turns a connection away like that");
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));

    QCOMPARE(freetunnel::forwardToRunningInstance({theirs}, QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
#else
    QSKIP("Unix socket files only");
#endif
}

// A link at the name is not ours, whatever it points at. Another account can
// leave one in /tmp, and pointed at a socket of this user's that turns a
// connection away, it would lend the name that socket's owner.
void TestIntegrationSingleInstance::aLinkAtTheNameIsNotOurs()
{
#if defined(Q_OS_UNIX)
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("busy"));
    const QString link = dir.filePath(QStringLiteral("link"));
    const BusySocket ours(target);
    QVERIFY2(ours.isBusy(), qPrintable(ours.describe()));
    QVERIFY(QFile::link(target, link));
    // Where a full backlog is refused outright (macOS), not even with a running
    // instance's claim on the name.
    QObject instance;
    if (!connectFailsWithoutSayingWhy(link))
        QVERIFY(freetunnel::claimInstanceName(&instance, link));

    QCOMPARE(freetunnel::forwardToRunningInstance({link}, QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
#else
    QSKIP("Unix socket files only");
#endif
}

// Windows: a pipe that stays busy is someone else's. Another account can create
// this user's pipe name first and keep its one instance taken, and Qt waits five
// seconds for a free one before it gives up. A listener of FreeTunnel's keeps
// fifty instances of its pipe waiting and opens another for each it takes, so it
// is not the one that stays busy.
void TestIntegrationSingleInstance::aPipeThatStaysBusyIsNoInstance()
{
#if defined(Q_OS_WIN)
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    const QString name = m_socketName + QStringLiteral("-busy");
    const std::wstring path = (QStringLiteral("\\\\.\\pipe\\") + name).toStdWString();
    HANDLE pipe = ::CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT,
                                     1, 0, 0, 0, nullptr);
    QVERIFY(pipe != INVALID_HANDLE_VALUE);
    HANDLE taken = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    const auto release = qScopeGuard([&] {
        if (taken != INVALID_HANDLE_VALUE)
            ::CloseHandle(taken);
        ::CloseHandle(pipe);
    });
    QVERIFY(taken != INVALID_HANDLE_VALUE);

    QCOMPARE(freetunnel::forwardToRunningInstance({name}, QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
#else
    QSKIP("Windows named pipes only");
#endif
}

// Connected through a link, a launch could reach any socket of this user's.
// connect() follows a link, and another account can leave one in /tmp at a name
// a launch tries, pointing at, say, this user's session bus: that passes the
// peer check, and was handed the token and the link while this launch exited
// with FreeTunnel never started. A listener of this user's stands in for it.
void TestIntegrationSingleInstance::aLinkToAListenerOfOursIsNotHandedTheLink()
{
#if defined(Q_OS_UNIX)
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString target = dir.filePath(QStringLiteral("live"));
    const QString link = dir.filePath(QStringLiteral("link"));
    QLocalServer other;
    other.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(other.listen(target));
    wireCollector(other);
    QVERIFY(QFile::link(target, link));

    ForwardResult sent = ForwardResult::Forwarded;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance({link}, QStringLiteral("freetunnel://toggle"));
    });
    const Received got = readOne(other, 1000);
    sender.join();
    QCOMPARE(sent, ForwardResult::NoInstance);
    QVERIFY2(!got.got, qPrintable(got.payload));
#else
    QSKIP("Unix socket files only");
#endif
}

#if defined(Q_OS_UNIX)
// Linux never refuses a connection to a listener that is there, so it takes the
// macOS path only when told to.
static auto refusalCanBeBusyForThisTest()
{
    qputenv("FT_TEST_REFUSAL_CAN_BE_BUSY", "1");
    return qScopeGuard([]() { qunsetenv("FT_TEST_REFUSAL_CAN_BE_BUSY"); });
}
#endif

// macOS refuses a connection to a full backlog just as it does one to a socket
// file nothing listens on. Taken for the second, a busy instance of ours was
// taken over: this launch started, cleared its socket and listened on its name,
// and the running one was left reachable by nothing. A running instance's claim
// on the name tells the two apart; on the shared name it is still nothing to
// give way to, and a crashed instance leaves no claim behind.
void TestIntegrationSingleInstance::aRefusalWhileOursIsRunningIsUnreachable()
{
#if defined(Q_OS_UNIX)
    const auto hook = refusalCanBeBusyForThisTest();
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString own = dir.filePath(QStringLiteral("own"));
    const QString fallback = dir.filePath(QStringLiteral("fallback"));
    QVERIFY(leaveStaleSocketFile(own));
    QVERIFY(leaveStaleSocketFile(fallback));

    {
        QObject instance;
        QVERIFY(freetunnel::claimInstanceName(&instance, own));
        QVERIFY(freetunnel::claimInstanceName(&instance, fallback));
        QCOMPARE(freetunnel::forwardToRunningInstance({own}, QStringLiteral("freetunnel://toggle")),
                 ForwardResult::Unreachable);
        QVERIFY(!freetunnel::removeStaleInstanceServer(own));
        QVERIFY(QFileInfo::exists(own));
        QCOMPARE(freetunnel::forwardToRunningInstance({dir.filePath(QStringLiteral("none")), fallback},
                                                      QStringLiteral("freetunnel://toggle")),
                 ForwardResult::NoInstance);
        // Nor through a link, even to a socket file of ours, with a claim on it.
        const QString link = dir.filePath(QStringLiteral("link"));
        QVERIFY(QFile::link(own, link));
        QVERIFY(freetunnel::claimInstanceName(&instance, link));
        QCOMPARE(freetunnel::forwardToRunningInstance({link}, QStringLiteral("freetunnel://toggle")),
                 ForwardResult::NoInstance);

        freetunnel::releaseInstanceName(&instance);
        QCOMPARE(freetunnel::forwardToRunningInstance({own}, QStringLiteral("freetunnel://toggle")),
                 ForwardResult::NoInstance);
        QVERIFY(freetunnel::claimInstanceName(&instance, own));
    }
    // Gone with the instance that held it.
    QCOMPARE(freetunnel::forwardToRunningInstance({own}, QStringLiteral("freetunnel://toggle")),
             ForwardResult::NoInstance);
    QVERIFY(freetunnel::removeStaleInstanceServer(own));
    QVERIFY(!QFileInfo::exists(own));
#else
    QSKIP("Unix socket files only");
#endif
}

// A full backlog drains once the instance's event loop turns again, and a link
// sent while it was busy is still handed over then, not dropped by a launch that
// gave up at the first refusal.
void TestIntegrationSingleInstance::aBusyInstanceOfOursThatCatchesUpIsHandedTheLink()
{
#if defined(Q_OS_UNIX)
    const auto hook = refusalCanBeBusyForThisTest();
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QTemporaryDir dir(QDir::tempPath() + QStringLiteral("/ftsi-XXXXXX"));
    QVERIFY(dir.isValid());
    const QString own = dir.filePath(QStringLiteral("own"));
    QVERIFY(leaveStaleSocketFile(own));
    QObject instance;
    QVERIFY(freetunnel::claimInstanceName(&instance, own));

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    wireCollector(server);
    QTimer::singleShot(300, this, [&server, own]() {
        QFile::remove(own);
        QVERIFY(server.listen(own));
    });
    ForwardResult sent = ForwardResult::NoInstance;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance({own}, QStringLiteral("freetunnel://toggle"));
    });
    const Received got = readOne(server, 3000);
    sender.join();
    QCOMPARE(sent, ForwardResult::Forwarded);
    QVERIFY(got.got);
    QCOMPARE(got.payload, QStringLiteral("freetunnel://toggle"));
#else
    QSKIP("Unix socket files only");
#endif
}

// Linux: this user's name in $XDG_RUNTIME_DIR is a path, which a listener takes
// and a launch finds like any other name. Only the own name is tried here: the
// ones after it are where the developer's own FreeTunnel listens.
void TestIntegrationSingleInstance::theRuntimeDirectoryNameIsListenedOnAndFound()
{
#if defined(Q_OS_LINUX)
    const QByteArray previousName = qgetenv("FT_TEST_INSTANCE_NAME");
    const bool hadRuntime = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
    const QByteArray previousRuntime = qgetenv("XDG_RUNTIME_DIR");
    const auto putBack = qScopeGuard([&]() {
        if (!previousName.isEmpty())
            qputenv("FT_TEST_INSTANCE_NAME", previousName);
        if (hadRuntime)
            qputenv("XDG_RUNTIME_DIR", previousRuntime);
        else
            qunsetenv("XDG_RUNTIME_DIR");
    });
    QTemporaryDir runtime(QDir::tempPath() + QStringLiteral("/ftrt-XXXXXX"));
    QVERIFY(runtime.isValid());
    QVERIFY(QFile::setPermissions(runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                          | QFileDevice::ExeOwner));
    qunsetenv("FT_TEST_INSTANCE_NAME");
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
    const QString own = freetunnel::instanceServerName();
    QCOMPARE(own, runtime.filePath(QStringLiteral("FreeTunnelInstance")));

    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY2(server.listen(own), qPrintable(server.errorString()));
    wireCollector(server);
    ForwardResult sent = ForwardResult::NoInstance;
    std::thread sender([&]() {
        sent = freetunnel::forwardToRunningInstance({own}, QStringLiteral("freetunnel://toggle"));
    });
    const Received got = readOne(server);
    sender.join();
    QCOMPARE(sent, ForwardResult::Forwarded);
    QVERIFY(got.got);
    QCOMPARE(got.payload, QStringLiteral("freetunnel://toggle"));
#else
    QSKIP("$XDG_RUNTIME_DIR is used on Linux only");
#endif
}

// Windows: a start has nothing to clear, since a pipe goes away with its last
// handle, and asking cost five more seconds whenever another account kept this
// user's pipe busy: Qt waits that long for a free instance, and the launch has
// waited for it once already.
void TestIntegrationSingleInstance::aStartAsksNoPipeWhetherItIsStale()
{
#if defined(Q_OS_WIN)
    const QString name = m_socketName + QStringLiteral("-stale-check");
    const std::wstring path = (QStringLiteral("\\\\.\\pipe\\") + name).toStdWString();
    HANDLE pipe = ::CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT,
                                     1, 0, 0, 0, nullptr);
    QVERIFY(pipe != INVALID_HANDLE_VALUE);
    HANDLE taken = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    const auto release = qScopeGuard([&] {
        if (taken != INVALID_HANDLE_VALUE)
            ::CloseHandle(taken);
        ::CloseHandle(pipe);
    });
    QVERIFY(taken != INVALID_HANDLE_VALUE);

    QElapsedTimer timer;
    timer.start();
    QVERIFY(!freetunnel::removeStaleInstanceServer(name));
    QVERIFY2(timer.elapsed() < 2000, qPrintable(QString::number(timer.elapsed())));
#else
    QSKIP("Windows named pipes only");
#endif
}

QTEST_MAIN(TestIntegrationSingleInstance)
#include "test_integration_single_instance.moc"
