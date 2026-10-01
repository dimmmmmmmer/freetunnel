// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpSocket>

#include <QJsonArray>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStringList>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTemporaryDir>

#include <string>
#include <vector>

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
#include <unistd.h> // getuid
#endif
#if defined(Q_OS_UNIX)
#include <fcntl.h> // AT_FDCWD, AT_SYMLINK_NOFOLLOW
#include <sys/stat.h> // utimensat
#endif

#include "helper_ipc_mock_server.h"
#include "vpn/vpn_helper_client.h"
#include "vpn/vpn_helper_launch.h"
#include "vpn/vpn_helper_protocol.h"

namespace {

QStringList jsonStringArray(const QJsonObject &obj, const char *key)
{
    QStringList out;
    for (const QJsonValue &v : obj.value(QLatin1String(key)).toArray())
        out.append(v.toString());
    return out;
}

bool writeFile(const QString &path, const QByteArray &body)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(body) == body.size();
}

// Last changed @p secsAgo seconds ago. On POSIX a link is aged itself, not what
// it points to.
bool age(const QString &path, qint64 secsAgo)
{
    const QDateTime then = QDateTime::currentDateTimeUtc().addSecs(-secsAgo);
#if defined(Q_OS_UNIX)
    const struct timespec times[2] = {{static_cast<time_t>(then.toSecsSinceEpoch()), 0},
                                      {static_cast<time_t>(then.toSecsSinceEpoch()), 0}};
    return ::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, AT_SYMLINK_NOFOLLOW)
            == 0;
#else
    QFile f(path);
    return f.open(QIODevice::ReadWrite) && f.setFileTime(then, QFileDevice::FileModificationTime);
#endif
}

} // namespace

// GUI-side helper IPC client (no elevated process): mirrors VpnHelperClient handshake.
class TestIntegrationHelperClient : public QObject {
    Q_OBJECT

private slots:
    void clientHandshakeAndConnectFlow();
    void realClientRefusesPeerThatCannotProveTheToken();
    void realClientRefusesPeerThatSkipsTheChallenge();
    void realClientRefusesAChallengeCarryingNoNonce();
    void securitySettingsAreSentAsValuesNotJustCommandNames();
    void theElevatedArgvComesFromItsArgumentsAndNotTheEnvironment();
    void theAppImageIsUnpackedWhereNobodyElseCanWrite();
    void theHelpersOutputIsNotKeptInMemory();
    void aPeerThatNeverAnswersIsGivenUpOn();
    void aTokenFileAnEarlierRunLeftIsRemovedAtStartup();
    void onlyOldTokenFilesOfOursAreRemoved();
};

// linuxHelperCommand() builds the argv pkexec is asked to run AS ROOT, and until
// now it was file-local in vpn_helper_client.cpp with no test anywhere — the one
// function in this codebase whose output is executed with full privilege, and the
// only one nothing checked.
//
// docs/security-threats.md describes the bug this shape exists to prevent, and it
// is worth restating because it was real: naming the AppImage from $APPIMAGE and
// validating it against $APPDIR is not validation, since an attacker who can set
// the GUI's environment sets both sides — and $APPDIR only had to be a path
// prefix, so APPDIR=/usr passed for an ordinary /usr/bin/FreeTunnel install and
// $APPIMAGE was then run as root. The answer is that the path arrives as an
// argument, from the kernel via runningAppImagePath(), and this function reads no
// environment at all.
void TestIntegrationHelperClient::theElevatedArgvComesFromItsArgumentsAndNotTheEnvironment()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QSKIP("linuxHelperCommand() is the Linux elevation path");
#else
    const QString exe = QStringLiteral("/usr/bin/FreeTunnel");
    const QString tokenPath = QStringLiteral("/run/user/1000/ft.token");

    // An AppImage build re-execs the .AppImage file, because the running
    // executable sits in a FUSE mount root cannot read. It goes through a shell
    // that gives the AppImage runtime somewhere private to unpack it; what that
    // script does is checked by running it, in
    // theAppImageIsUnpackedWhereNobodyElseCanWrite below.
    const QString appImage = QStringLiteral("/home/u/FreeTunnel.AppImage");
    const QStringList viaAppImage =
            freetunnel::linuxHelperCommand(exe, appImage, 51820, tokenPath);
    QCOMPARE(viaAppImage.size(), 10);
    QCOMPARE(viaAppImage.at(0), QStringLiteral("/bin/sh"));
    QCOMPARE(viaAppImage.at(1), QStringLiteral("-c"));
    const QString script = viaAppImage.at(2);
    // The path is an argument to the script, never text inside it.
    QVERIFY(!script.contains(appImage));
    QVERIFY(!script.contains(tokenPath));
    const QStringList expectedAppImage{QStringLiteral("/bin/sh"),
                                       QStringLiteral("-c"),
                                       script,
                                       QStringLiteral("freetunnel-helper"),
                                       appImage,
                                       QStringLiteral("--helper"),
                                       QStringLiteral("--port"),
                                       QStringLiteral("51820"),
                                       QStringLiteral("--token-file"),
                                       tokenPath};
    QCOMPARE(viaAppImage, expectedAppImage);

    // An ordinary install re-execs itself and gains no env wrapper.
    const QStringList viaExe = freetunnel::linuxHelperCommand(exe, QString(), 51820, tokenPath);
    const QStringList expectedExe{QStringLiteral("/usr/bin/FreeTunnel"),
                                  QStringLiteral("--helper"),
                                  QStringLiteral("--port"),
                                  QStringLiteral("51820"),
                                  QStringLiteral("--token-file"),
                                  tokenPath};
    QCOMPARE(viaExe, expectedExe);

    // The point of the whole arrangement: a hostile environment changes nothing.
    // Both variables are set to paths an attacker would want root to run, and the
    // argv must be byte-for-byte what it was above.
    const QByteArray oldAppImage = qgetenv("APPIMAGE");
    const QByteArray oldAppDir = qgetenv("APPDIR");
    qputenv("APPIMAGE", "/tmp/evil.AppImage");
    qputenv("APPDIR", "/usr");
    const auto restore = qScopeGuard([&] {
        if (oldAppImage.isEmpty()) qunsetenv("APPIMAGE"); else qputenv("APPIMAGE", oldAppImage);
        if (oldAppDir.isEmpty()) qunsetenv("APPDIR"); else qputenv("APPDIR", oldAppDir);
    });

    QCOMPARE(freetunnel::linuxHelperCommand(exe, QString(), 51820, tokenPath), expectedExe);
    QCOMPARE(freetunnel::linuxHelperCommand(exe, appImage, 51820, tokenPath), expectedAppImage);
    for (const QStringList &cmd : {viaExe, expectedAppImage}) {
        for (const QString &arg : cmd)
            QVERIFY2(!arg.contains(QStringLiteral("evil")),
                     "the elevated argv must never pick anything up from the environment");
    }
#endif
}

// The script linuxHelperCommand() wraps an AppImage in, run for real — as this
// user rather than root, with a stand-in for the AppImage that reports what it
// was given instead of unpacking anything.
//
// It exists because of where the AppImage runtime unpacks: under $TMPDIR. Run as
// root with no TMPDIR, that was a fixed name in the shared /tmp, which another
// user of the machine could prepare in advance. The runtime must get a directory
// that root has just made for this run, that nobody else can write to, and that
// is gone afterwards.
//
// The script also brings its own PATH: under sudo without secure_path it would
// otherwise find mktemp, and hand the helper a PATH, in the user's directories.
// A mktemp of the user's own is put first on the PATH it is started with here.
void TestIntegrationHelperClient::theAppImageIsUnpackedWhereNobodyElseCanWrite()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QSKIP("linuxHelperCommand() is the Linux elevation path");
#else
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Paths a careless script would split or expand.
    const QString fakeAppImage = dir.filePath(QStringLiteral("Free Tunnel $(id).AppImage"));
    const QString tokenPath = dir.filePath(QStringLiteral("token 'a' \"b\" $HOME;x"));
    const QString report = dir.filePath(QStringLiteral("report"));
    {
        QFile f(fakeAppImage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#!/bin/sh\n"
                "{\n"
                "  printf 'TMPDIR=%s\\n' \"$TMPDIR\"\n"
                "  printf 'EXTRACT=%s\\n' \"$APPIMAGE_EXTRACT_AND_RUN\"\n"
                "  printf 'OWNER_MODE=%s\\n' \"$(stat -c '%u %a' \"$TMPDIR\")\"\n"
                "  printf 'PATH=%s\\n' \"$PATH\"\n"
                "  for a in \"$@\"; do printf 'ARG=%s\\n' \"$a\"; done\n"
                "} > \"$(dirname \"$0\")/report\"\n"
                "exit 7\n");
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                 | QFileDevice::ExeOwner));
    }

    const QString userBin = dir.filePath(QStringLiteral("bin"));
    const QString decoyRan = dir.filePath(QStringLiteral("decoy-ran"));
    QVERIFY(QDir().mkpath(userBin));
    {
        QFile f(userBin + QStringLiteral("/mktemp"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QStringLiteral("#!/bin/sh\n: > '%1'\nexit 1\n").arg(decoyRan).toUtf8());
        f.close();
        QVERIFY(f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                 | QFileDevice::ExeOwner));
    }
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PATH"),
               userBin + QLatin1Char(':') + env.value(QStringLiteral("PATH")));

    QStringList dirsSeen;
    for (int run = 0; run < 2; ++run) {
        QFile::remove(report);
        const QStringList cmd = freetunnel::linuxHelperCommand(
                QStringLiteral("/usr/bin/FreeTunnel"), fakeAppImage, 51820, tokenPath);
        QProcess proc;
        proc.setProcessEnvironment(env);
        proc.start(cmd.first(), cmd.mid(1));
        QVERIFY(proc.waitForFinished(10000));
        QVERIFY2(!QFileInfo::exists(decoyRan), "root would have run a mktemp from the user's PATH");
        // The helper's own exit is what pkexec reports, and what tells the GUI
        // the elevation ended.
        QCOMPARE(proc.exitStatus(), QProcess::NormalExit);
        QCOMPARE(proc.exitCode(), 7);

        QFile r(report);
        QVERIFY2(r.open(QIODevice::ReadOnly | QIODevice::Text), "the stand-in AppImage never ran");
        QString tmpDir;
        QString extract;
        QString ownerMode;
        QString path;
        QStringList args;
        for (const QString &line : QString::fromUtf8(r.readAll()).split(QLatin1Char('\n'))) {
            if (line.startsWith(QLatin1String("TMPDIR=")))
                tmpDir = line.mid(7);
            else if (line.startsWith(QLatin1String("EXTRACT=")))
                extract = line.mid(8);
            else if (line.startsWith(QLatin1String("OWNER_MODE=")))
                ownerMode = line.mid(11);
            else if (line.startsWith(QLatin1String("PATH=")))
                path = line.mid(5);
            else if (line.startsWith(QLatin1String("ARG=")))
                args << line.mid(4);
        }

        QVERIFY2(tmpDir.startsWith(QLatin1String("/tmp/freetunnel-helper.")),
                 qPrintable(QStringLiteral("unpacked under \"%1\"").arg(tmpDir)));
        QCOMPARE(extract, QStringLiteral("1"));
        // Its owner — root, when this runs elevated — and nobody else.
        QCOMPARE(ownerMode, QStringLiteral("%1 700").arg(::getuid()));
        QCOMPARE(path, QStringLiteral("/usr/sbin:/usr/bin:/sbin:/bin"));
        QCOMPARE(args,
                 QStringList({QStringLiteral("--helper"), QStringLiteral("--port"),
                              QStringLiteral("51820"), QStringLiteral("--token-file"), tokenPath}));
        QVERIFY2(!QFileInfo::exists(tmpDir), "the unpacking directory outlived the helper");
        dirsSeen << tmpDir;
    }
    // A new one every time, so nothing anyone prepared in advance is ever it.
    QVERIFY(dirsSeen.at(0) != dirsSeen.at(1));
#endif
}

// pkexec and sudo become the helper, so the QProcess that starts them holds the
// helper's stdout and stderr for its whole life — and with session logging off,
// the core's log goes to stderr. As pipes, QProcess read every byte of it into
// the GUI's memory and nothing ever read it back out. A shell stands in for the
// elevator here; what matters is what is left in the QProcess afterwards.
void TestIntegrationHelperClient::theHelpersOutputIsNotKeptInMemory()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QSKIP("startLinuxElevation() is the Linux elevation path");
#else
    QProcess proc;
    QVERIFY(freetunnel::startLinuxElevation(
            &proc, QStringLiteral("/bin/sh"),
            {QStringLiteral("-c"),
             QStringLiteral("head -c 4000000 /dev/zero; head -c 4000000 /dev/zero >&2")}));
    QVERIFY(proc.waitForFinished(10000));
    QCOMPARE(proc.exitCode(), 0);
    // bytesAvailable() rather than readAll*(): with nothing to read the QProcess
    // is write-only, and reading from it only produces a warning.
    proc.setReadChannel(QProcess::StandardOutput);
    QCOMPARE(proc.bytesAvailable(), 0);
    proc.setReadChannel(QProcess::StandardError);
    QCOMPARE(proc.bytesAvailable(), 0);
#endif
}

void TestIntegrationHelperClient::clientHandshakeAndConnectFlow()
{
    const QString token = QStringLiteral("client-integration-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    QTcpSocket sock;
    sock.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), server.port());
    QVERIFY(sock.waitForConnected(3000));
    server.acceptPending();

    QVERIFY(mockHelperHandshake(server, sock, token));
    QJsonObject hello;
    hello[QStringLiteral("cmd")] = QStringLiteral("noop");
    sock.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(3000));
    QVERIFY(server.authed());

    QJsonObject setMode;
    setMode[QStringLiteral("cmd")] = QStringLiteral("setMode");
    setMode[QStringLiteral("selective")] = false;
    sock.write(QJsonDocument(setMode).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(1000));
    QCOMPARE(server.lastCmd(), QStringLiteral("setMode"));

    QJsonObject connectCmd;
    connectCmd[QStringLiteral("cmd")] = QStringLiteral("connect");
    connectCmd[QStringLiteral("configPath")] = QStringLiteral("/tmp/test.toml");
    sock.write(QJsonDocument(connectCmd).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(3000));
    QCOMPARE(server.lastCmd(), QStringLiteral("connect"));

    QString lastState;
    for (int i = 0; i < 6 && lastState != QLatin1String("Connected"); ++i) {
        if (sock.bytesAvailable() == 0 && !sock.waitForReadyRead(3000))
            break;
        while (sock.canReadLine()) {
            const auto doc = QJsonDocument::fromJson(sock.readLine());
            if (!doc.isObject())
                continue;
            const QJsonObject ev = doc.object();
            if (ev.value(QStringLiteral("ev")).toString() == QLatin1String("state"))
                lastState = ev.value(QStringLiteral("state")).toString();
        }
    }
    QCOMPARE(lastState, QStringLiteral("Connected"));

    QJsonObject disconnectCmd;
    disconnectCmd[QStringLiteral("cmd")] = QStringLiteral("disconnect");
    sock.write(QJsonDocument(disconnectCmd).toJson(QJsonDocument::Compact) + '\n');
    sock.flush();
    QVERIFY(server.waitForClientData(3000));
    QCOMPARE(server.lastCmd(), QStringLiteral("disconnect"));
}

// The helper only starts listening after the elevation prompt is answered, so a
// local process can own that port in the meantime. It must not be able to talk
// the GUI into handing over the config: that payload carries the VPN password,
// and an answer of "Connected" from it would show a protected tunnel where there
// is none. Drive the REAL VpnHelperClient against a peer that cannot produce the
// proof and assert it never sends anything sensitive.
void TestIntegrationHelperClient::realClientRefusesPeerThatCannotProveTheToken()
{
    QTcpServer rogue;
    QVERIFY(rogue.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    connect(&rogue, &QTcpServer::newConnection, this, [&]() {
        peer = rogue.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, this, [&]() {
            received += peer->readAll();
            // Answer the hello like a helper would, but with a proof we cannot
            // actually compute — we do not know the token.
            if (received.contains("\"cmd\":\"hello\"")) {
                QJsonObject ch;
                ch[QStringLiteral("ev")] = QStringLiteral("challenge");
                ch[QStringLiteral("proof")] = QString(64, QLatin1Char('0'));
                ch[QStringLiteral("nonce")] = QStringLiteral("rogue-nonce");
                peer->write(QJsonDocument(ch).toJson(QJsonDocument::Compact) + '\n');
                peer->flush();
            }
        });
    });

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(rogue.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", "the-real-token");

    VpnHelperClient client;
    QSignalSpy errors(&client, &VpnHelperClient::vpnError);
    client.loadConfigFromToml(QStringLiteral("password = \"super-secret\"\n"));
    client.connectVpn();

    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    // Guard against passing for the wrong reason: the negative assertions below
    // are only meaningful if the client actually reached this peer and opened
    // the handshake with it.
    QVERIFY(received.contains("\"cmd\":\"hello\""));
    QVERIFY(!received.contains("super-secret"));
    QVERIFY(!received.contains("\"cmd\":\"auth\""));
    QVERIFY(!received.contains("\"cmd\":\"connect\""));

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

// The sibling above gives a wrong proof. This one gives a RIGHT proof and no
// nonce, which is the more interesting case: the peer has satisfied the only
// check most readers think about, and the client still has to refuse — with no
// server nonce there is nothing for its own proof to answer, so whatever it
// sent back would be a constant a listener could replay.
//
// Found by mutation: deleting the empty-nonce check broke nothing in the suite.
void TestIntegrationHelperClient::realClientRefusesAChallengeCarryingNoNonce()
{
    const QString token = QStringLiteral("no-nonce-challenge-token");

    QTcpServer rogue;
    QVERIFY(rogue.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    connect(&rogue, &QTcpServer::newConnection, this, [&]() {
        peer = rogue.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, this, [&]() {
            received += peer->readAll();
            const int nl = received.indexOf('\n');
            if (nl < 0 || !received.contains("\"cmd\":\"hello\""))
                return;
            const QJsonObject hello =
                    QJsonDocument::fromJson(received.left(nl)).object();
            QJsonObject ch;
            ch[QStringLiteral("ev")] = QStringLiteral("challenge");
            // A genuine proof: this peer really does hold the token.
            ch[QStringLiteral("proof")] = vpn_helper::authProof(
                    token, QString::fromLatin1(vpn_helper::kHelperRole),
                    hello.value(QStringLiteral("nonce")).toString());
            ch[QStringLiteral("nonce")] = QString();  // ... but no nonce of its own
            peer->write(QJsonDocument(ch).toJson(QJsonDocument::Compact) + '\n');
            peer->flush();
        });
    });

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(rogue.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    VpnHelperClient client;
    QSignalSpy errors(&client, &VpnHelperClient::vpnError);
    client.loadConfigFromToml(QStringLiteral("password = \"super-secret\"\n"));
    client.connectVpn();

    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    // Same guard as the sibling: the negative assertions mean nothing unless the
    // client actually talked to this peer.
    QVERIFY(received.contains("\"cmd\":\"hello\""));
    QVERIFY2(!received.contains("\"cmd\":\"auth\""),
             "the client must not answer a challenge that carries no nonce");
    QVERIFY2(!received.contains("super-secret"),
             "the config, and the VPN password in it, must never reach this peer");
    QVERIFY(!received.contains("\"cmd\":\"connect\""));

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

// Verifying the challenge is not sufficient on its own: the client must also
// refuse to act on ANYTHING from a peer that never proved itself. A rogue that
// simply skipped the challenge and announced {"ev":"ready"} was handed the
// config TOML — password included — and could then report a tunnel that did not
// exist. Everything said before a peer is proven has to be inert.
void TestIntegrationHelperClient::realClientRefusesPeerThatSkipsTheChallenge()
{
    QTcpServer rogue;
    QVERIFY(rogue.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    connect(&rogue, &QTcpServer::newConnection, this, [&]() {
        peer = rogue.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, this, [&]() {
            const bool first = received.isEmpty();
            received += peer->readAll();
            if (first) {
                // No challenge, no proof — just claim the handshake is done.
                peer->write(QByteArrayLiteral("{\"ev\":\"ready\"}\n"));
                peer->flush();
            }
        });
    });

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(rogue.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", "the-real-token");

    VpnHelperClient client;
    QSignalSpy errors(&client, &VpnHelperClient::vpnError);
    client.loadConfigFromToml(QStringLiteral("password = \"super-secret\"\n"));
    client.connectVpn();

    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    // The client did talk to this peer, so the negative assertions mean something.
    QVERIFY(received.contains("\"cmd\":\"hello\""));
    QVERIFY(!received.contains("super-secret"));
    QVERIFY(!received.contains("\"cmd\":\"connect\""));
    QVERIFY(!received.contains("\"cmd\":\"auth\""));

    // A fabricated state from it must not move the client either.
    if (peer) {
        peer->write(QByteArrayLiteral("{\"ev\":\"state\",\"state\":\"Connected\"}\n"));
        peer->flush();
    }
    QTest::qWait(200);
    QVERIFY(client.state() != VpnHelperClient::State::Connected);

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

// The GUI half of the kill-switch chain: whatever Backend asks for has to leave
// this process as a JSON value the elevated helper can read back. Asserting that
// a "setKillSwitch" line was sent proves nothing — the helper reads
// c.value("enabled").toBool(), and QJsonValue::toBool() answers false for a key
// that is absent or misspelled, so a rename on either side silently disarms the
// kill switch in every session while the toggle in the GUI still reads ON.
// Assert the key is PRESENT and carries the value the caller asked for.
void TestIntegrationHelperClient::securitySettingsAreSentAsValuesNotJustCommandNames()
{
    const QString token = QStringLiteral("settings-value-token");
    MockHelperServer server(token);
    QVERIFY(server.listen());

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(server.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());

    VpnHelperClient client;
    client.setKillSwitch(true);
    client.setVpnMode(true);
    client.setExcludedRoutes(std::vector<std::string>{"10.66.0.0/16"});
    client.setExtraExclusions(std::vector<std::string>{"intranet.example"});
    client.loadConfigFromToml(QStringLiteral("loglevel = \"warn\"\n"
                                             "[endpoint]\n"
                                             "hostname = \"vpn.example\"\n"));
    client.connectVpn();

    // The settings are pushed the moment the handshake completes, before connect.
    QTRY_VERIFY_WITH_TIMEOUT(
            !server.lastMessageFor(QStringLiteral("setKillSwitch")).isEmpty(), 10000);

    const QJsonObject killSwitch = server.lastMessageFor(QStringLiteral("setKillSwitch"));
    QVERIFY2(killSwitch.contains(QStringLiteral("enabled")),
             "setKillSwitch carried no \"enabled\" key — the helper decodes a missing key as "
             "false and turns the kill switch off without anything reporting it");
    QCOMPARE(killSwitch.value(QStringLiteral("enabled")).toBool(), true);

    const QJsonObject mode = server.lastMessageFor(QStringLiteral("setMode"));
    QVERIFY2(mode.contains(QStringLiteral("selective")), "setMode carried no \"selective\" key");
    QCOMPARE(mode.value(QStringLiteral("selective")).toBool(), true);

    QCOMPARE(jsonStringArray(server.lastMessageFor(QStringLiteral("setRoutes")), "excluded"),
             QStringList{QStringLiteral("10.66.0.0/16")});
    QCOMPARE(jsonStringArray(server.lastMessageFor(QStringLiteral("setExclusions")), "domains"),
             QStringList{QStringLiteral("intranet.example")});

    // Turning them back off must travel just as faithfully: a sender that hardcodes
    // true is exactly as broken as one that drops the value, and only this half of
    // the assertion can tell them apart.
    client.setKillSwitch(false);
    client.setVpnMode(false);
    QTRY_VERIFY_WITH_TIMEOUT(
            server.lastMessageFor(QStringLiteral("setKillSwitch"))
                    .value(QStringLiteral("enabled"))
                    .toBool()
                    == false,
            10000);
    QTRY_VERIFY_WITH_TIMEOUT(server.lastMessageFor(QStringLiteral("setMode"))
                                     .value(QStringLiteral("selective"))
                                     .toBool()
                                     == false,
                             10000);
    // Still the right key, not merely a missing one decoding to false.
    QVERIFY(server.lastMessageFor(QStringLiteral("setKillSwitch"))
                    .contains(QStringLiteral("enabled")));

    qunsetenv("FT_TEST_HELPER_PORT");
    qunsetenv("FT_TEST_HELPER_TOKEN");
}

QTEST_MAIN(TestIntegrationHelperClient)
// A peer that accepts the connection and then says nothing at all.
//
// Connecting used to disarm every other failure detector: the retry budget that
// produces the only "could not reach the helper" message deletes itself the
// moment the socket reaches ConnectedState, and the elevation-outcome watcher
// returns early for the same reason. So this left the window on "Connecting…"
// with no error ever — after the user had already typed their administrator
// password. The helper port is a random pick that nothing reserves, so an
// unrelated local service answering there is enough to cause it by accident.
void TestIntegrationHelperClient::aPeerThatNeverAnswersIsGivenUpOn()
{
    QTcpServer mute;
    QVERIFY(mute.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0));

    QByteArray received;
    QTcpSocket *peer = nullptr;
    connect(&mute, &QTcpServer::newConnection, this, [&]() {
        peer = mute.nextPendingConnection();
        connect(peer, &QTcpSocket::readyRead, this, [&]() { received += peer->readAll(); });
    });

    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(mute.serverPort()));
    qputenv("FT_TEST_HELPER_TOKEN", "the-real-token");
    qputenv("FT_TEST_HANDSHAKE_MS", "300");
    const auto clear = qScopeGuard([]() {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
        qunsetenv("FT_TEST_HANDSHAKE_MS");
    });

    VpnHelperClient client;
    QSignalSpy errors(&client, &VpnHelperClient::vpnError);
    client.loadConfigFromToml(QStringLiteral("password = \"super-secret\"\n"));
    client.connectVpn();

    QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 5000);
    // It really did talk to this peer, so the silence is the thing being tested.
    QVERIFY(received.contains("\"cmd\":\"hello\""));
    QVERIFY2(!received.contains("super-secret"), "and it sent nothing else");
    // And it does not sit in a state the user cannot leave.
    QVERIFY(client.state() != VpnHelperClient::State::Connected);
}

// The elevated helper no longer deletes the token file it reads; the GUI removes
// its own once the helper has answered or the attempt is given up. A GUI that
// crashed or was killed in between left its 0600 file behind, and nothing would
// ever have removed it. The next start does.
void TestIntegrationHelperClient::aTokenFileAnEarlierRunLeftIsRemovedAtStartup()
{
    QStandardPaths::setTestModeEnabled(true);
    const auto testModeOff = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QVERIFY(QDir().mkpath(dir));
    const QString leftBehind = QDir(dir).filePath(QStringLiteral(".fthelper-Ab12Cd"));
    QVERIFY(writeFile(leftBehind, "0123456789abcdef0123456789abcdef"));
    const auto cleanUp = qScopeGuard([&] { QFile::remove(leftBehind); });
    QVERIFY(age(leftBehind, 10 * 60));

    VpnHelperClient client;
    QVERIFY2(!QFile::exists(leftBehind), "a token file from an earlier run outlived the next start");
}

// What the sweep may remove: a regular file named as the GUI names its token
// files, this user's, old enough that no attempt can still be using it.
void TestIntegrationHelperClient::onlyOldTokenFilesOfOursAreRemoved()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString old = dir.filePath(QStringLiteral(".fthelper-old111"));
    const QString fresh = dir.filePath(QStringLiteral(".fthelper-new222"));
    const QString otherName = dir.filePath(QStringLiteral("settings.ini"));
    for (const QString &path : {old, fresh, otherName})
        QVERIFY(writeFile(path, "x"));
    QVERIFY(age(old, 10 * 60));
    QVERIFY(age(otherName, 10 * 60));

#if defined(Q_OS_UNIX)
    // A link with the name is not a token file, nor is a folder, however old.
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString target = elsewhere.filePath(QStringLiteral("theirs"));
    QVERIFY(writeFile(target, "keep"));
    QVERIFY(age(target, 10 * 60));
    const QString link = dir.filePath(QStringLiteral(".fthelper-link33"));
    QVERIFY(QFile::link(target, link));
    QVERIFY(age(link, 10 * 60));
    const QString folder = dir.filePath(QStringLiteral(".fthelper-dir444"));
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(age(folder, 10 * 60));
#endif

    QCOMPARE(VpnHelperClient::removeStaleTokenFiles(dir.path(), 5 * 60), 1);
    QVERIFY(!QFile::exists(old));
    QVERIFY2(QFile::exists(fresh), "a token file young enough to be in use was removed");
    QVERIFY2(QFile::exists(otherName), "a file not named as a token file was removed");
#if defined(Q_OS_UNIX)
    QVERIFY2(QFileInfo(link).isSymLink(), "a link named as a token file was removed");
    QCOMPARE(QFileInfo(target).size(), qint64(4));
    QVERIFY(QFileInfo(folder).isDir());
#endif
}

#include "test_integration_helper_client.moc"
