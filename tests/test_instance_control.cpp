// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>

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
};

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

#include "test_instance_control.moc"
