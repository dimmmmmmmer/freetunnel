// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QScopeGuard>
#include <QTemporaryDir>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

#include "core/InstanceControl.h"
#include "core/CredentialStore.h"

class TestInstanceControl : public QObject {
    Q_OBJECT

private slots:
    void roundTripMessage();
    void rejectsBadMessage();
    void tokenFileRoundTrip();
    void legacyInstanceAuthFileMigratesWhenSecure();
    void rejectsMismatchedToken();
    void peerCredentialCheckNeedsALiveSocket();
    void quittingDoesNotDeleteASuccessorsToken();
    void quittingRemovesATokenKeptInTheFallbackFile();
    void aRunningInstancePutsItsTokenBack();
    void theSocketNameIsPerUser();
    void linuxPutsTheNameInTheRuntimeDirectory();
    void linuxWithoutTheVariableUsesTheSessionsDirectory();
};

// XDG_RUNTIME_DIR as the test found it, put back by the guard this returns.
static auto keepRuntimeDir()
{
    const bool had = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
    const QByteArray value = qgetenv("XDG_RUNTIME_DIR");
    return qScopeGuard([had, value]() {
        if (had)
            qputenv("XDG_RUNTIME_DIR", value);
        else
            qunsetenv("XDG_RUNTIME_DIR");
    });
}

// What stands in for /run/user, where a launch without XDG_RUNTIME_DIR looks for
// this user's runtime directory; unset again by the guard this returns.
static auto runUserDirAt(const QString &dir)
{
    qputenv("FT_TEST_RUN_USER_DIR", QFile::encodeName(dir));
    return qScopeGuard([]() { qunsetenv("FT_TEST_RUN_USER_DIR"); });
}

void TestInstanceControl::roundTripMessage()
{
    const QByteArray raw =
            freetunnel::formatInstanceMessage(QStringLiteral("abc123"), QStringLiteral("freetunnel://toggle"));
    QString token;
    QString payload;
    QVERIFY(freetunnel::parseInstanceMessage(raw, &token, &payload));
    QCOMPARE(token, QStringLiteral("abc123"));
    QCOMPARE(payload, QStringLiteral("freetunnel://toggle"));
}

void TestInstanceControl::rejectsBadMessage()
{
    QString token;
    QString payload;
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("no-newline"), &token, &payload));
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("\nempty"), &token, &payload));
}

void TestInstanceControl::tokenFileRoundTrip()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    QString written;
    QVERIFY(freetunnel::writeInstanceAuthToken(&written));
    QString read;
    QVERIFY(freetunnel::readInstanceAuthToken(&read));
    QCOMPARE(read, written);

    freetunnel::removeInstanceAuthToken();
    QVERIFY(!freetunnel::readInstanceAuthToken(&read));
}

void TestInstanceControl::legacyInstanceAuthFileMigratesWhenSecure()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    if (!freetunnel::CredentialStore::secureStorageAvailable())
        QSKIP("Secure storage required to verify legacy instance-auth migration");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    const QString legacyToken = QStringLiteral("legacy-token-abc");
    const QString path = freetunnel::instanceAuthFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(legacyToken.toUtf8());
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    QString read;
    QVERIFY(freetunnel::readInstanceAuthToken(&read));
    QCOMPARE(read, legacyToken);
    QVERIFY(!QFileInfo::exists(path));

    freetunnel::removeInstanceAuthToken();
}

void TestInstanceControl::rejectsMismatchedToken()
{
    QVERIFY(freetunnel::instanceTokensEqual(QStringLiteral("same"), QStringLiteral("same")));
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("a"), QStringLiteral("b")));
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("short"), QStringLiteral("longer")));
    // A difference in the last byte must count as much as one in the first: the
    // compare folds every byte into a single accumulator precisely so it cannot
    // return early on the longest matching prefix, which is what would let a
    // local peer recover the token one byte at a time.
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("tokenA"), QStringLiteral("tokenB")));
    // Two empty tokens DO compare equal here — the emptiness of the stored token
    // is rejected by the callers (handleInstanceConnection refuses outright when
    // it holds no token, and parseInstanceMessage refuses a message with an empty
    // one), not by this primitive. Pinning that down so nobody "fixes" the
    // primitive and assumes the caller-side guards became redundant.
    QVERIFY(freetunnel::instanceTokensEqual(QString(), QString()));
}

// Both the sender and the listener gate on this before anything else, so what it
// returns for a socket that is not a live same-user peer is a security answer,
// not a convenience one: false must mean "cannot vouch for this peer". The
// connected case is asserted alongside because a function that simply returned
// false everywhere would also satisfy the negative cases while quietly breaking
// single-instance forwarding altogether.
void TestInstanceControl::peerCredentialCheckNeedsALiveSocket()
{
    QVERIFY(!freetunnel::localSocketPeerIsSameUser(nullptr, freetunnel::SocketEnd::WeConnected));
    QVERIFY(!freetunnel::localSocketPeerIsSameUser(nullptr, freetunnel::SocketEnd::WeAccepted));

    // Never connected: there is no peer to identify, so there is no one to trust.
    QLocalSocket unconnected;
    QVERIFY(!freetunnel::localSocketPeerIsSameUser(&unconnected, freetunnel::SocketEnd::WeConnected));

    const QString name =
            QStringLiteral("freetunnel-peercred-test-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(name);
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(3000));
    QVERIFY(server.waitForNewConnection(3000));
    QLocalSocket *peer = server.nextPendingConnection();
    QVERIFY(peer != nullptr);

    // This process is trivially the same user as itself, on both ends.
    //
    // On Windows this stopped being trivial: the check used to return true for
    // any connected socket, and now asks the pipe which process is on the other
    // end and compares that process's user against ours. The value of these two
    // lines there is that the real check still says yes to the legitimate case —
    // failing closed is the whole design, and a check that refuses everything
    // would silently stop a second launch from reaching the running instance.
    //
    // Each end is asked as itself. The two Windows APIs are not interchangeable:
    // asking who SERVES the pipe while holding the server end names this very
    // process, so the check compares us with ourselves and can never say no.
    QVERIFY(freetunnel::localSocketPeerIsSameUser(peer, freetunnel::SocketEnd::WeAccepted));
    QVERIFY(freetunnel::localSocketPeerIsSameUser(&client, freetunnel::SocketEnd::WeConnected));

    client.disconnectFromServer();
    delete peer;
    server.close();
    QLocalServer::removeServer(name);
}

QTEST_MAIN(TestInstanceControl)
// A quitting instance must not take a successor's token with it.
//
// The self-update path overlaps two processes on purpose: the replacement is
// started before the old one is gone, and it writes its own token at startup. If
// the old one's aboutToQuit lands afterwards and deletes whatever is stored, the
// new instance is left reachable by nothing — every later deep link starts a
// second copy instead of being forwarded, until the next restart.
void TestInstanceControl::quittingDoesNotDeleteASuccessorsToken()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    QString mine;
    QVERIFY(freetunnel::writeInstanceAuthToken(&mine));

    // The successor replaces it with its own.
    QString successor;
    QVERIFY(freetunnel::writeInstanceAuthToken(&successor));
    QVERIFY(successor != mine);

    // Now the old instance quits.
    freetunnel::removeInstanceAuthToken(mine);

    QString stored;
    QVERIFY2(freetunnel::readInstanceAuthToken(&stored), "the successor's token must survive");
    QCOMPARE(stored, successor);

    // And its own quit does remove it.
    freetunnel::removeInstanceAuthToken(successor);
    QVERIFY(!freetunnel::readInstanceAuthToken(&stored));
}

// Without a Secret Service the token lives in a 0600 file beside the config, and
// a quitting instance has to remove it from there too. The check that the token is
// still this instance's own asked the credential store alone — which is empty in
// exactly that case — so it never matched, and the file outlived every session.
//
// The file is written here by hand: writeInstanceAuthToken() falls back to it only
// when the store refuses, and the store a test build gets never does.
void TestInstanceControl::quittingRemovesATokenKeptInTheFallbackFile()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // Put back on the way out: every later test would otherwise look for its
    // config in this directory after it is gone.
    const QByteArray configHome = qgetenv("XDG_CONFIG_HOME");
    auto restoreConfigHome = qScopeGuard([configHome] {
        if (configHome.isEmpty())
            qunsetenv("XDG_CONFIG_HOME");
        else
            qputenv("XDG_CONFIG_HOME", configHome);
    });
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    const QString path = freetunnel::instanceAuthFilePath();
    const auto keepOnlyInTheFile = [&path](const QString &token) {
        freetunnel::removeInstanceAuthToken(); // the store holds nothing
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(f.write(token.toUtf8()), static_cast<qint64>(token.toUtf8().size()));
    };

    keepOnlyInTheFile(QStringLiteral("this-instances-token"));
    freetunnel::removeInstanceAuthToken(QStringLiteral("this-instances-token"));
    QVERIFY2(!QFileInfo::exists(path), "the instance's own token file outlived its quit");
    QString stored;
    QVERIFY(!freetunnel::readInstanceAuthToken(&stored));

    // A successor's token in the file is still not this instance's to remove.
    keepOnlyInTheFile(QStringLiteral("successors-token"));
    freetunnel::removeInstanceAuthToken(QStringLiteral("this-instances-token"));
    QVERIFY2(freetunnel::readInstanceAuthToken(&stored), "the successor's token must survive");
    QCOMPARE(stored, QStringLiteral("successors-token"));
    freetunnel::removeInstanceAuthToken();
}

// An older FreeTunnel quitting through an update compares the stored token with
// its own and then deletes it, in two calls to the store, so a token its
// successor writes between them is deleted all the same. The successor puts its
// own back: the same token, which is what its listener checks, and only when a
// launch would not read it already.
void TestInstanceControl::aRunningInstancePutsItsTokenBack()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QByteArray configHome = qgetenv("XDG_CONFIG_HOME");
    auto restoreConfigHome = qScopeGuard([configHome] {
        if (configHome.isEmpty())
            qunsetenv("XDG_CONFIG_HOME");
        else
            qputenv("XDG_CONFIG_HOME", configHome);
    });
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    QString mine;
    QVERIFY(freetunnel::writeInstanceAuthToken(&mine));
    freetunnel::removeInstanceAuthToken(); // the predecessor's deletion, after the write
    QString stored;
    QVERIFY(!freetunnel::readInstanceAuthToken(&stored));
    QVERIFY(freetunnel::restoreInstanceAuthToken(mine));
    QVERIFY2(freetunnel::readInstanceAuthToken(&stored), "the token was not put back");
    QCOMPARE(stored, mine);

    // A launch reads one token, so another one there is replaced as well.
    QString other;
    QVERIFY(freetunnel::writeInstanceAuthToken(&other));
    QVERIFY(freetunnel::restoreInstanceAuthToken(mine));
    QVERIFY(freetunnel::readInstanceAuthToken(&stored));
    QCOMPARE(stored, mine);
    QVERIFY(freetunnel::restoreInstanceAuthToken(mine)); // in place: nothing to do

    // An instance that never had a token has none to put back.
    freetunnel::removeInstanceAuthToken();
    QVERIFY(!freetunnel::restoreInstanceAuthToken(QString()));
    QVERIFY(!freetunnel::readInstanceAuthToken(&stored));
}

// One name per user. A single name for the machine belonged to whoever started
// FreeTunnel first — a Windows pipe name is one namespace for every session, and
// on Linux the socket sat in the shared /tmp — and every launch and link of any
// other user started another full copy. The shared name is still tried after the
// user's own, since an older FreeTunnel running through an update listens
// there; under a test override nothing but the override may be touched, or a
// test run would talk to the developer's own FreeTunnel.
void TestInstanceControl::theSocketNameIsPerUser()
{
    qunsetenv("FT_TEST_INSTANCE_NAME");
    const auto putBack = keepRuntimeDir();
    qunsetenv("XDG_RUNTIME_DIR");
    // Nor a runtime directory of a session to use in its place.
    const QTemporaryDir noSessions;
    QVERIFY(noSessions.isValid());
    const auto noRunUser = runUserDirAt(noSessions.filePath(QStringLiteral("missing")));
    const QString shared = QStringLiteral("FreeTunnelInstance");
    const QString own = freetunnel::instanceServerName();
#if defined(Q_OS_WIN)
    QVERIFY2(own.startsWith(shared + QStringLiteral("-S-1-")), qPrintable(own));
#else
    QCOMPARE(own, shared + QLatin1Char('-') + QString::number(::getuid()));
#endif
    QCOMPARE(freetunnel::instanceServerNames(), QStringList({own, shared}));

    qputenv("FT_TEST_INSTANCE_NAME", "FreeTunnelNameTest");
    QCOMPARE(freetunnel::instanceServerName(), QStringLiteral("FreeTunnelNameTest"));
    QCOMPARE(freetunnel::instanceServerNames(), QStringList({QStringLiteral("FreeTunnelNameTest")}));
    qunsetenv("FT_TEST_INSTANCE_NAME");
}

// On Linux the name goes in $XDG_RUNTIME_DIR, which no other account can enter:
// in /tmp another account can create even this user's own name first. Only a
// directory that is what the variable promises counts, and without one the name
// in /tmp is used as before. A launch still looks there, after its own name, for
// a FreeTunnel started without the variable, and on the shared name for one from
// before 1.2.3.
void TestInstanceControl::linuxPutsTheNameInTheRuntimeDirectory()
{
#if defined(Q_OS_LINUX)
    qunsetenv("FT_TEST_INSTANCE_NAME");
    const auto putBack = keepRuntimeDir();
    const QString shared = QStringLiteral("FreeTunnelInstance");
    const QString inTemp = shared + QLatin1Char('-') + QString::number(::getuid());
    QTemporaryDir runtime(QDir::tempPath() + QStringLiteral("/ftrt-XXXXXX"));
    QVERIFY(runtime.isValid());
    const QString inRuntime = runtime.path() + QStringLiteral("/FreeTunnelInstance");
    const auto nameWith = [](const QString &dir) {
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(dir));
        return freetunnel::instanceServerName();
    };

    QVERIFY(QFile::setPermissions(runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                          | QFileDevice::ExeOwner));
    QCOMPARE(nameWith(runtime.path()), inRuntime);
    QCOMPARE(freetunnel::instanceServerNames(), QStringList({inRuntime, inTemp, shared}));
    QCOMPARE(nameWith(runtime.path() + QLatin1Char('/')), inRuntime);

    // Not a directory of this user's that no one else may enter.
    QCOMPARE(nameWith(QStringLiteral("relative/dir")), inTemp);
    QCOMPARE(nameWith(runtime.filePath(QStringLiteral("missing"))), inTemp);
    const QString link = runtime.path() + QStringLiteral("-link");
    QVERIFY(QFile::link(runtime.path(), link));
    const auto dropLink = qScopeGuard([&link]() { QFile::remove(link); });
    QCOMPARE(nameWith(link), inTemp);
    const QFileInfo root(QStringLiteral("/root"));
    if (root.isDir() && root.ownerId() != ::getuid()
        && (root.permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                                  | QFileDevice::ReadOther | QFileDevice::WriteOther
                                  | QFileDevice::ExeOther)) == 0)
        QCOMPARE(nameWith(root.filePath()), inTemp);
    QVERIFY(QFile::setPermissions(runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                          | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                                          | QFileDevice::ExeGroup));
    QCOMPARE(nameWith(runtime.path()), inTemp);
    QCOMPARE(freetunnel::instanceServerNames(), QStringList({inTemp, shared}));
    QVERIFY(QFile::setPermissions(runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                          | QFileDevice::ExeOwner));

    // A socket path has to fit in sockaddr_un, 108 bytes.
    const QString deep = runtime.filePath(QString(90, QLatin1Char('d')));
    QVERIFY(QDir().mkdir(deep));
    QVERIFY(QFile::setPermissions(deep, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                | QFileDevice::ExeOwner));
    QCOMPARE(nameWith(deep), inTemp);
#else
    QSKIP("$XDG_RUNTIME_DIR is used on Linux only");
#endif
}

// A command run from cron, over ssh, after su - or by a hotkey daemon started
// outside the session has no $XDG_RUNTIME_DIR. It looked only in /tmp, missed
// the FreeTunnel the desktop had started in the runtime directory, and started
// a second one beside it; in 1.2.2 both used /tmp. Without the variable the
// directory it names in a session, /run/user/<uid>, is used, by a launch and by
// a FreeTunnel started that way alike, and only when it passes the checks the
// variable's directory does.
void TestInstanceControl::linuxWithoutTheVariableUsesTheSessionsDirectory()
{
#if defined(Q_OS_LINUX)
    qunsetenv("FT_TEST_INSTANCE_NAME");
    const auto putBack = keepRuntimeDir();
    const QString shared = QStringLiteral("FreeTunnelInstance");
    const QString inTemp = shared + QLatin1Char('-') + QString::number(::getuid());
    QTemporaryDir runUser(QDir::tempPath() + QStringLiteral("/ftru-XXXXXX"));
    QVERIFY(runUser.isValid());
    const auto standIn = runUserDirAt(runUser.path());
    const QString session = runUser.filePath(QString::number(::getuid()));
    QVERIFY(QDir().mkdir(session));
    const auto privateTo = [](const QString &dir, QFileDevice::Permissions more) {
        return QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner | more);
    };
    QVERIFY(privateTo(session, {}));
    const QString inSession = session + QStringLiteral("/FreeTunnelInstance");

    qunsetenv("XDG_RUNTIME_DIR");
    QCOMPARE(freetunnel::instanceServerName(), inSession);
    QCOMPARE(freetunnel::instanceServerNames(), QStringList({inSession, inTemp, shared}));
    qputenv("XDG_RUNTIME_DIR", QByteArray()); // set, and empty, is no directory either
    QCOMPARE(freetunnel::instanceServerName(), inSession);
    // The name a launch from the session gets, where the variable names it.
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(session));
    QCOMPARE(freetunnel::instanceServerName(), inSession);
    // The variable is not second-guessed: one that is set and fails the checks
    // leaves the name in /tmp, as before.
    qputenv("XDG_RUNTIME_DIR", "relative/dir");
    QCOMPARE(freetunnel::instanceServerName(), inTemp);
    qunsetenv("XDG_RUNTIME_DIR");

    // The same checks as for the variable's directory.
    QVERIFY(privateTo(session, QFileDevice::ReadGroup | QFileDevice::ExeGroup));
    QCOMPARE(freetunnel::instanceServerName(), inTemp);
    QCOMPARE(freetunnel::instanceServerNames(), QStringList({inTemp, shared}));
    QVERIFY(privateTo(session, {}));
    QVERIFY(QDir().rename(session, session + QStringLiteral("-real")));
    QVERIFY(QFile::link(session + QStringLiteral("-real"), session));
    QCOMPARE(freetunnel::instanceServerName(), inTemp); // a link to one of ours
    QVERIFY(QFile::remove(session));
    QCOMPARE(freetunnel::instanceServerName(), inTemp); // none at all
#else
    QSKIP("$XDG_RUNTIME_DIR and /run/user are used on Linux only");
#endif
}

#include "test_instance_control.moc"
