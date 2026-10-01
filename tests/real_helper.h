// cppcheck-suppress-file missingIncludeSystem
// The REAL privileged helper as a child process, for tests that drive it from the
// outside: helper_server_main.cpp around src/vpn/vpn_helper_server.cpp, built
// against tests/mock_core as freetunnel_test_helper, so it needs neither root nor
// the VPN core. What it does at the boundary — what it accepts, what it refuses,
// when it leaves — is production code either way.
//
// The same start-up as test_helper_server.cpp's own, kept here for the suites
// that came after it (test_helper_server_lifecycle, _settings and _fuzz).
#pragma once

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "vpn/vpn_helper_protocol.h"

namespace realhelper {

inline QByteArray line(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

// runVpnHelper() refuses to start on Windows unless wintun.dll sits next to the
// executable. That is a production requirement with nothing to do with the IPC
// under test, so a placeholder goes next to the test helper; it is a build-tree
// file and is never packaged.
inline bool ensureWintunPlaceholder()
{
#if defined(Q_OS_WIN)
    const QString stub = QFileInfo(QStringLiteral(FT_TEST_HELPER_BINARY)).absolutePath()
            + QStringLiteral("/wintun.dll");
    if (QFile::exists(stub))
        return true;
    QFile f(stub);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write("placeholder for tests");
#endif
    return true;
}

// One whole line from the helper, or an empty one if none came in time.
inline QByteArray readLineWithin(QTcpSocket &sock, int timeoutMs = 5000)
{
    QElapsedTimer t;
    t.start();
    while (!sock.canReadLine() && t.elapsed() < timeoutMs) {
        if (!sock.waitForReadyRead(100) && sock.state() != QAbstractSocket::ConnectedState)
            break;
    }
    return sock.canReadLine() ? sock.readLine() : QByteArray();
}

// The client half of the handshake, on a raw socket: hello with a nonce, check
// the helper's proof, answer its nonce. True once the helper says "ready".
// Anything the helper sent after "ready" is left unread on the socket.
inline bool authenticate(QTcpSocket &sock, const QString &token)
{
    const QString nonce = QStringLiteral("raw-client-nonce-0123456789");
    sock.write(line({{QStringLiteral("cmd"), QStringLiteral("hello")},
                     {QStringLiteral("nonce"), nonce}}));
    sock.flush();
    const QJsonObject challenge = QJsonDocument::fromJson(readLineWithin(sock)).object();
    if (challenge.value(QStringLiteral("ev")).toString() != QLatin1String("challenge")
        || !vpn_helper::tokensEqual(
                challenge.value(QStringLiteral("proof")).toString(),
                vpn_helper::authProof(token, QString::fromLatin1(vpn_helper::kHelperRole), nonce)))
        return false;
    sock.write(line({{QStringLiteral("cmd"), QStringLiteral("auth")},
                     {QStringLiteral("proof"),
                      vpn_helper::authProof(token, QString::fromLatin1(vpn_helper::kGuiRole),
                                            challenge.value(QStringLiteral("nonce")).toString())}}));
    sock.flush();
    return QJsonDocument::fromJson(readLineWithin(sock)).object().value(QStringLiteral("ev")).toString()
            == QLatin1String("ready");
}

// Wait for an event of type `ev` on an authenticated socket and return it, empty
// if none came within `timeoutMs`. Events of other types are read and dropped.
inline QJsonObject waitForEvent(QTcpSocket &sock, const QString &ev, int timeoutMs = 5000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        while (sock.canReadLine()) {
            const QJsonObject e = QJsonDocument::fromJson(sock.readLine()).object();
            if (e.value(QStringLiteral("ev")).toString() == ev)
                return e;
        }
        if (sock.state() != QAbstractSocket::ConnectedState)
            break;
        sock.waitForReadyRead(100);
    }
    return {};
}

// How many of the lines in `bytes` the helper refuses itself, at once and in
// order: a connect with no inline config. Only complete lines count, split where
// the helper splits them, so random bytes with a newline inside are two lines.
inline int refusalsOwedFor(const QByteArray &bytes)
{
    const QList<QByteArray> lines = bytes.split('\n');
    int owed = 0;
    for (qsizetype i = 0; i + 1 < lines.size(); ++i) { // the last piece has no newline
        const QJsonDocument doc = QJsonDocument::fromJson(lines.at(i));
        const QJsonObject c = doc.object();
        if (doc.isObject() && c.value(QStringLiteral("cmd")).toString() == QLatin1String("connect")
            && c.value(QStringLiteral("configToml")).toString().isEmpty())
            ++owed;
    }
    return owed;
}

// Something the helper answers at once, without a core behind it: a connect with
// no inline config is refused by the server itself. Used as a ping, to show that
// a session is still the helper's after whatever a test put it through.
//
// Counted, not merely awaited: the core inside the helper reports its own errors
// on the same channel, and lines sent earlier may have been refused the same way,
// so the first refusal to arrive is not necessarily this one. `owed` is how many
// refusals the lines sent since the last ping are due (refusalsOwedFor).
inline bool answersOnSession(QTcpSocket &sock, int owed = 0, int timeoutMs = 5000)
{
    sock.write(line({{QStringLiteral("cmd"), QStringLiteral("connect")}}));
    sock.flush();
    QElapsedTimer t;
    t.start();
    int refused = 0;
    while (refused <= owed && t.elapsed() < timeoutMs) {
        const QJsonObject e = waitForEvent(sock, QStringLiteral("error"), timeoutMs - int(t.elapsed()));
        if (e.isEmpty())
            return false;
        // The core's own errors come this way too; only the server's refusals count.
        if (e.value(QStringLiteral("msg")).toString().contains(QStringLiteral("inline configToml")))
            ++refused;
    }
    return refused > owed;
}

// A config the helper's core accepts: the one field every real config has.
inline QString minimalConfigToml()
{
    return QStringLiteral("[endpoint]\nhostname = \"vpn.example\"\n");
}

inline QProcessEnvironment envWith(const QString &name, const QString &value)
{
    QProcessEnvironment env;
    env.insert(name, value);
    return env;
}

// A file the helper's mock core writes, one line per record; empty if none yet.
inline QStringList fileLines(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// Every VpnHelperClient started while the returned guard lives talks to the
// helper on `port` instead of spawning one (its FT_TEST_HELPER_PORT test hook).
[[nodiscard]] inline auto pointClientsAt(quint16 port, const QString &token)
{
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(port));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());
    return qScopeGuard([]() {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
    });
}

class Process {
public:
    explicit Process(QString dir) : m_dir(std::move(dir)) {}
    ~Process() { stop(); }
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;

    // Start the helper with `token` on a port that is still free when it binds,
    // trying another when someone took the first in between. `extra` is added to
    // the environment the CHILD starts with; setting a variable in this process
    // afterwards would never reach it.
    bool start(const QString &token, const QProcessEnvironment &extra = QProcessEnvironment())
    {
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (startOn(freePort(), token, extra))
                return true;
            stop();
        }
        return false;
    }

    // Ask, then insist: SIGTERM runs the helper's teardown and, under coverage,
    // is the only moment its counters are written.
    void stop()
    {
        if (!m_proc)
            return;
        if (m_proc->state() != QProcess::NotRunning) {
            m_proc->terminate();
            if (!m_proc->waitForFinished(3000)) {
                m_proc->kill();
                m_proc->waitForFinished(3000);
            }
        }
        delete m_proc;
        m_proc = nullptr;
    }

    quint16 port() const { return m_port; }
    QProcess *process() const { return m_proc; }
    // Up right now. QProcess learns of an exit from its event loop, which a test
    // busy on a socket may not have run since the helper died; asking with a zero
    // wait settles it on the spot.
    bool running() const
    {
        return m_proc && m_proc->state() == QProcess::Running && !m_proc->waitForFinished(0);
    }
    // Where the helper's mock core leaves the config it was built with, and the
    // log of its lifecycle (tests/mock_core/mock_core_controller.h).
    QString coreConfigDump() const { return m_configDump; }
    QString coreEventLog() const { return m_eventLog; }

    bool connectTo(QTcpSocket &sock) const
    {
        sock.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), m_port);
        return sock.waitForConnected(3000);
    }

private:
    static quint16 freePort()
    {
        QTcpServer probe;
        probe.listen(QHostAddress(QStringLiteral("127.0.0.1")), 0);
        return probe.serverPort();
    }

    bool startOn(quint16 port, const QString &token, const QProcessEnvironment &extra)
    {
        static int seq = 0;
        ++seq;
        // Named as the GUI names its token files.
        const QString tokenPath = QDir(m_dir).filePath(QStringLiteral(".fthelper-%1").arg(seq));
        QFile f(tokenPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(token.toUtf8());
        f.close();
        m_configDump = QDir(m_dir).filePath(QStringLiteral("core-config-%1").arg(seq));
        m_eventLog = QDir(m_dir).filePath(QStringLiteral("core-events-%1").arg(seq));
        m_port = port;

        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("FT_TEST_SKIP_PRIVILEGE_CHECK"), QStringLiteral("1"));
        env.insert(QStringLiteral("FT_TEST_CORE_CONFIG_DUMP"), m_configDump);
        env.insert(QStringLiteral("FT_TEST_CORE_EVENT_LOG"), m_eventLog);
        env.insert(extra);
        m_proc = new QProcess;
        m_proc->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_proc->setProcessEnvironment(env);
        m_proc->start(QStringLiteral(FT_TEST_HELPER_BINARY),
                      {QStringLiteral("--helper"), QStringLiteral("--port"), QString::number(port),
                       QStringLiteral("--token-file"), tokenPath});
        if (!m_proc->waitForStarted(5000))
            return false;
        // Up once it accepts a connection; gone means it could not bind (the port
        // was taken in between) or died on start, and waiting longer proves nothing.
        for (int i = 0; i < 100; ++i) {
            if (m_proc->state() == QProcess::NotRunning)
                return false;
            QTcpSocket probe;
            probe.connectToHost(QHostAddress(QStringLiteral("127.0.0.1")), port);
            if (probe.waitForConnected(100)) {
                probe.abort();
                return true;
            }
            QTest::qWait(50);
        }
        return false;
    }

    QString m_dir;
    QProcess *m_proc = nullptr;
    quint16 m_port = 0;
    QString m_configDump;
    QString m_eventLog;
};

} // namespace realhelper
