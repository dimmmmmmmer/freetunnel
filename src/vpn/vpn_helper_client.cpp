#include "vpn/vpn_helper_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTimer>

#include "core/AppUiUtils.h" // shellEscape / appleScriptEscape (macOS)

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

VpnHelperClient::VpnHelperClient(QObject *parent) : QObject(parent) {}

VpnHelperClient::~VpnHelperClient() {
    if (m_sock) {
        QJsonObject q; q["cmd"] = "quit"; send(q);
        m_sock->flush();
    }
    if (m_proc)
        m_proc->kill();
    clearTokenFile();
}

void VpnHelperClient::clearTokenFile() {
    if (m_tokenPath.isEmpty())
        return;
    QFile::remove(m_tokenPath);
    m_tokenPath.clear();
}

static VpnHelperClient::State stateFromString(const QString &s) {
    using S = VpnHelperClient::State;
    if (s == "Connecting") return S::Connecting;
    if (s == "Connected") return S::Connected;
    if (s == "Reconnecting") return S::Reconnecting;
    if (s == "WaitingForNetwork") return S::WaitingForNetwork;
    if (s == "Disconnecting") return S::Disconnecting;
    if (s == "Error") return S::Error;
    return S::Disconnected;
}

bool VpnHelperClient::loadConfigFromFile(const QString &path) {
    m_configPath = path;
    return true; // helper validates; errors surface via vpnError
}

void VpnHelperClient::setExtraExclusions(const std::vector<std::string> &exclusions) {
    m_exclusions = exclusions;
    if (m_helloAcked) {
        QJsonObject c; c["cmd"] = "setExclusions";
        QJsonArray arr;
        for (const auto &d : m_exclusions) arr.append(QString::fromStdString(d));
        c["domains"] = arr;
        send(c);
    }
}

void VpnHelperClient::setExcludedRoutes(const std::vector<std::string> &routes) {
    m_excludedRoutes = routes;
    if (m_helloAcked) {
        QJsonObject c; c["cmd"] = "setRoutes";
        QJsonArray arr;
        for (const auto &r : m_excludedRoutes) arr.append(QString::fromStdString(r));
        c["excluded"] = arr;
        send(c);
    }
}

void VpnHelperClient::setVpnMode(bool selective) {
    m_selective = selective;
    if (m_helloAcked) {
        QJsonObject c; c["cmd"] = "setMode"; c["selective"] = selective;
        send(c);
    }
}

void VpnHelperClient::setKillSwitch(bool enabled) {
    m_killSwitch = enabled;
    if (m_helloAcked) {
        QJsonObject c; c["cmd"] = "setKillSwitch"; c["enabled"] = enabled;
        send(c);
    }
}

void VpnHelperClient::connectVpn() {
    if (!ensureHelper())
        return;
    if (!m_helloAcked) {
        m_connectPending = true; // sent once the helper handshake completes
        return;
    }
    QJsonObject c; c["cmd"] = "connect"; c["configPath"] = m_configPath;
    send(c);
}

void VpnHelperClient::disconnectVpn() {
    // Cancelling before the helper has finished coming up (elevation prompt /
    // handshake still pending): tear the startup down instead of waiting it out.
    if (!m_helloAcked) {
        abortStartup();
        if (m_state != State::Disconnected) setState(State::Disconnected);
        return;
    }
    if (!m_sock) return;
    QJsonObject c; c["cmd"] = "disconnect"; send(c);
}

// Stop a half-finished connect: kill the retry timer, drop the socket, and kill
// the (possibly still-prompting) elevated helper process.
void VpnHelperClient::abortStartup() {
    m_connectPending = false;
    m_starting = false;
    m_helloAcked = false;
    if (m_attempt) { m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr; }
    if (m_sock) { m_sock->abort(); m_sock->deleteLater(); m_sock = nullptr; }
    if (m_proc) { m_proc->kill(); m_proc->deleteLater(); m_proc = nullptr; }
    clearTokenFile();
}

bool VpnHelperClient::ensureHelper() {
    if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState)
        return true;
    if (m_starting) // spawn already in progress — don't prompt again
        return true;
    m_starting = true;
    if (m_sock) { m_sock->deleteLater(); m_sock = nullptr; }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }

    // Pick a free loopback port and a random auth token.
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0)) {
            fail(tr("Could not allocate a local port for the VPN helper"));
            return false;
        }
        m_port = probe.serverPort();
    }
    m_token = QString::number(QRandomGenerator::system()->generate64(), 16)
            + QString::number(QRandomGenerator::system()->generate64(), 16);

    // Hand the token to the elevated helper via a 0600 file rather than argv, so
    // it can't be read by other local users through /proc/<pid>/cmdline (Linux)
    // or `ps`. QTemporaryFile creates it atomically (O_EXCL) with owner-only
    // permissions; the helper reads it once and deletes it.
    clearTokenFile();
    {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        QTemporaryFile tf(dir + QStringLiteral("/.fthelper-XXXXXX"));
        tf.setAutoRemove(false);
        if (!tf.open()) {
            fail(tr("Could not create the VPN helper token file"));
            return false;
        }
        tf.write(m_token.toUtf8());
        tf.close();
        m_tokenPath = tf.fileName();
    }

    QString err;
    if (!spawnElevatedHelper(m_port, m_tokenPath, &err)) {
        fail(err.isEmpty() ? tr("Could not start the VPN helper") : err);
        return false;
    }

    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::connected, this, &VpnHelperClient::onSocketConnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &VpnHelperClient::onReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
        m_helloAcked = false;
        m_starting = false;
        if (m_state != State::Disconnected) setState(State::Disconnected);
    });

    // The elevated helper needs a moment to come up; retry the connect. The
    // timer is a member so disconnectVpn() can cancel a pending attempt.
    if (m_attempt) { m_attempt->stop(); m_attempt->deleteLater(); }
    m_attempt = new QTimer(this);
    m_tries = 0;
    connect(m_attempt, &QTimer::timeout, this, [this]() {
        if (!m_sock) { if (m_attempt) { m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr; } return; }
        if (m_sock->state() == QAbstractSocket::ConnectedState) {
            m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr; return;
        }
        if (++m_tries > 40) { // ~10s
            m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr;
            fail(tr("The VPN helper didn't start — authorization may have been "
                    "declined, or polkit (pkexec) isn't available."));
            return;
        }
        m_sock->abort();
        m_sock->connectToHost(QHostAddress::LocalHost, m_port);
    });
    m_attempt->start(250);
    m_sock->connectToHost(QHostAddress::LocalHost, m_port);
    return true;
}

bool VpnHelperClient::spawnElevatedHelper(quint16 port, const QString &tokenPath, QString *err) {
    const QString exe = QCoreApplication::applicationFilePath();
    const QString p = QString::number(port);

#if defined(Q_OS_MACOS)
    // Log into the (per-user, non-world-writable) temp dir with an unpredictable
    // name instead of a fixed /tmp path, so a local attacker can't pre-plant a
    // symlink there and have the elevated (root) helper clobber an arbitrary file.
    // The token path is shell-escaped (it lives under "Application Support", which
    // contains a space).
    const QString inner = QStringLiteral("exec %1 --helper --port %2 --token-file %3 "
                                         ">\"${TMPDIR:-/tmp}/freetunnel-helper.$$.log\" 2>&1 &")
                                  .arg(shellEscape(exe), p, shellEscape(tokenPath));
    const QString script = QStringLiteral("do shell script \"%1\" with administrator privileges")
                                   .arg(appleScriptEscape(inner));
    m_proc = new QProcess(this);
    m_proc->start(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
    if (!m_proc->waitForStarted(5000)) { if (err) *err = tr("Could not launch osascript"); return false; }
    return true;
#elif defined(Q_OS_WIN)
    const QString args = QStringLiteral("--helper --port %1 --token-file \"%2\"").arg(p, tokenPath);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas"; // UAC elevation
    std::wstring wexe = exe.toStdWString();
    std::wstring wargs = args.toStdWString();
    sei.lpFile = wexe.c_str();
    sei.lpParameters = wargs.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        if (err) *err = tr("Elevation was cancelled or failed");
        return false;
    }
    return true;
#else // Linux + others: prefer pkexec (graphical polkit prompt)
    m_proc = new QProcess(this);
    QStringList args;
    const QByteArray appImage = qgetenv("APPIMAGE");
    if (!appImage.isEmpty()) {
        // Launched from an AppImage: applicationFilePath() points inside the
        // per-user FUSE mount, which root (via pkexec) cannot read. Run the
        // .AppImage file itself (root can read it) and force extract-and-run,
        // since the elevated session may not have FUSE available.
        args << QStringLiteral("env") << QStringLiteral("APPIMAGE_EXTRACT_AND_RUN=1")
             << QString::fromLocal8Bit(appImage)
             << QStringLiteral("--helper") << QStringLiteral("--port") << p
             << QStringLiteral("--token-file") << tokenPath;
    } else {
        args << exe << QStringLiteral("--helper") << QStringLiteral("--port") << p
             << QStringLiteral("--token-file") << tokenPath;
    }
    m_proc->start(QStringLiteral("pkexec"), args);
    if (!m_proc->waitForStarted(5000)) {
        if (err) *err = tr("Could not run pkexec (is polkit installed?)");
        return false;
    }
    return true;
#endif
}

void VpnHelperClient::onSocketConnected() {
    m_starting = false;
    QJsonObject hello; hello["cmd"] = "hello"; hello["token"] = m_token;
    send(hello);
}

void VpnHelperClient::send(const QJsonObject &obj) {
    if (!m_sock) return;
    m_sock->write(QJsonDocument(obj).toJson(QJsonDocument::Compact) + '\n');
}

void VpnHelperClient::onReadyRead() {
    m_buf += m_sock->readAll();
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        const auto doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) handleEvent(doc.object());
    }
}

void VpnHelperClient::handleEvent(const QJsonObject &ev) {
    const QString type = ev.value("ev").toString();
    if (type == "ready") {
        m_helloAcked = true;
        // The helper has read (and deleted) the token file; drop our reference
        // and remove it too if anything lingered.
        clearTokenFile();
        // Push any pending routing config, then connect if requested.
        setVpnMode(m_selective);
        setKillSwitch(m_killSwitch);
        setExtraExclusions(m_exclusions);
        setExcludedRoutes(m_excludedRoutes);
        if (m_connectPending) { m_connectPending = false; connectVpn(); }
    } else if (type == "state") {
        setState(stateFromString(ev.value("state").toString()));
    } else if (type == "stats") {
        emit tunnelStats(static_cast<quint64>(ev.value("up").toDouble()),
                         static_cast<quint64>(ev.value("down").toDouble()));
    } else if (type == "info") {
        emit connectionInfo(ev.value("msg").toString());
    } else if (type == "error") {
        emit vpnError(ev.value("msg").toString());
    }
}

void VpnHelperClient::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(m_state);
}

void VpnHelperClient::fail(const QString &msg) {
    m_starting = false;
    clearTokenFile(); // startup failed — don't leave the token on disk
    emit vpnError(msg);
    setState(State::Error);
    setState(State::Disconnected);
}
