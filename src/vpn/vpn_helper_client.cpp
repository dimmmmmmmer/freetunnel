#include "vpn/vpn_helper_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

#include "core/AppUiUtils.h" // shellEscape / appleScriptEscape (macOS)

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h> // getuid
#endif

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
namespace {

QStringList linuxHelperCommand(const QString &exe, const QString &socketName, const QString &tokenPath)
{
    QStringList cmd;
    const QByteArray appImage = qgetenv("APPIMAGE");
    if (!appImage.isEmpty()) {
        cmd << QStringLiteral("env") << QStringLiteral("APPIMAGE_EXTRACT_AND_RUN=1")
            << QString::fromLocal8Bit(appImage);
    } else {
        cmd << exe;
    }
    cmd << QStringLiteral("--helper") << QStringLiteral("--socket") << socketName
        << QStringLiteral("--token-file") << tokenPath
        << QStringLiteral("--peer-uid") << QString::number(static_cast<uint>(::getuid()));
    return cmd;
}

bool startLinuxElevation(QProcess *proc, const QString &elevator, const QStringList &helperCmd)
{
    QStringList args;
    if (elevator == QLatin1String("sudo"))
        args << QStringLiteral("--");
    args += helperCmd;
    proc->start(elevator, args);
    if (!proc->waitForStarted(5000))
        return false;
    if (elevator == QLatin1String("pkexec") && proc->waitForFinished(1000))
        return false;
    return true;
}

} // namespace
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
    return true;
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
        m_connectPending = true;
        return;
    }
    QJsonObject c; c["cmd"] = "connect"; c["configPath"] = m_configPath;
    send(c);
}

void VpnHelperClient::disconnectVpn() {
    if (!m_helloAcked) {
        abortStartup();
        if (m_state != State::Disconnected) setState(State::Disconnected);
        return;
    }
    if (!m_sock) return;
    QJsonObject c; c["cmd"] = "disconnect"; send(c);
}

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
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState)
        return true;
    if (m_starting)
        return true;
    m_starting = true;
    if (m_sock) { m_sock->deleteLater(); m_sock = nullptr; }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }

#ifdef FT_ENABLE_TEST_HOOKS
    const bool testHelper = qEnvironmentVariableIsSet("FT_TEST_HELPER_SOCKET");
#else
    const bool testHelper = false;
#endif
    if (!testHelper) {
        const QString rnd = QString::number(QRandomGenerator::system()->generate64(), 16)
                + QString::number(QRandomGenerator::system()->generate64(), 16);
        m_token = QString::number(QRandomGenerator::system()->generate64(), 16)
                + QString::number(QRandomGenerator::system()->generate64(), 16);

        clearTokenFile();
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir().mkpath(dir);
        {
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

        // Local socket the elevated helper will create and we connect to. On
        // Unix it's an absolute path in the user's temp dir (root can create
        // and chown it to us + 0600); on Windows it's a named-pipe name.
#if defined(Q_OS_WIN)
        m_socketName = QStringLiteral("freetunnel-helper-%1").arg(rnd);
#else
        m_socketName = QDir::tempPath() + QStringLiteral("/.fthelper-%1.sock").arg(rnd);
        QLocalServer::removeServer(m_socketName); // clear any stale socket file
#endif

        QString err;
        if (!spawnElevatedHelper(m_socketName, m_tokenPath, &err)) {
            fail(err.isEmpty() ? tr("Could not start the VPN helper") : err);
            return false;
        }
    } else {
#ifdef FT_ENABLE_TEST_HOOKS
        m_socketName = QString::fromUtf8(qgetenv("FT_TEST_HELPER_SOCKET"));
        m_token = QString::fromUtf8(qgetenv("FT_TEST_HELPER_TOKEN"));
        if (m_token.isEmpty()) {
            fail(tr("FT_TEST_HELPER_TOKEN is required when FT_TEST_HELPER_SOCKET is set"));
            return false;
        }
#endif
    }

    m_sock = new QLocalSocket(this);
    connect(m_sock, &QLocalSocket::connected, this, &VpnHelperClient::onSocketConnected);
    connect(m_sock, &QLocalSocket::readyRead, this, &VpnHelperClient::onReadyRead);
    connect(m_sock, &QLocalSocket::disconnected, this, [this]() {
        m_helloAcked = false;
        m_starting = false;
        if (m_state != State::Disconnected) setState(State::Disconnected);
    });

    if (testHelper) {
        m_sock->connectToServer(m_socketName);
        return true;
    }

    if (m_attempt) { m_attempt->stop(); m_attempt->deleteLater(); }
    m_attempt = new QTimer(this);
    m_tries = 0;
    connect(m_attempt, &QTimer::timeout, this, [this]() {
        if (!m_sock) { if (m_attempt) { m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr; } return; }
        if (m_sock->state() == QLocalSocket::ConnectedState) {
            m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr; return;
        }
        if (++m_tries > 40) {
            m_attempt->stop(); m_attempt->deleteLater(); m_attempt = nullptr;
            fail(tr("The VPN helper didn't start — authorization may have been declined, "
                    "or the elevated helper could not be reached."));
            return;
        }
        m_sock->abort();
        m_sock->connectToServer(m_socketName);
    });
    m_attempt->start(250);
    m_sock->connectToServer(m_socketName);
    return true;
}

bool VpnHelperClient::spawnElevatedHelper(const QString &socketName, const QString &tokenPath, QString *err) {
    const QString exe = QCoreApplication::applicationFilePath();

#if defined(Q_OS_MACOS)
    // Redirect the helper log to a freshly mktemp'd file (atomic O_EXCL create,
    // root-owned 0600, unpredictable name) instead of a predictable
    // "$$.log" in a world-writable dir — that was a symlink-attack vector
    // where root could be tricked into truncating an attacker-chosen file.
    const QString inner =
            QStringLiteral("logf=$(mktemp \"${TMPDIR:-/tmp}/freetunnel-helper.XXXXXX\") || logf=/dev/null; "
                           "exec %1 --helper --socket %2 --token-file %3 --peer-uid %4 "
                           ">\"$logf\" 2>&1 &")
                    .arg(shellEscape(exe), shellEscape(socketName), shellEscape(tokenPath),
                         QString::number(static_cast<uint>(::getuid())));
    const QString script = QStringLiteral("do shell script \"%1\" with administrator privileges")
                                   .arg(appleScriptEscape(inner));
    m_proc = new QProcess(this);
    m_proc->start(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
    if (!m_proc->waitForStarted(5000)) { if (err) *err = tr("Could not launch osascript"); return false; }
    return true;
#elif defined(Q_OS_WIN)
    const QString args = QStringLiteral("--helper --socket \"%1\" --token-file \"%2\"")
                                 .arg(socketName, tokenPath);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
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
#else
    m_proc = new QProcess(this);
    const QStringList helperCmd = linuxHelperCommand(exe, socketName, tokenPath);

    if (startLinuxElevation(m_proc, QStringLiteral("pkexec"), helperCmd))
        return true;

    m_proc->deleteLater();
    m_proc = new QProcess(this);

    if (startLinuxElevation(m_proc, QStringLiteral("sudo"), helperCmd))
        return true;

    if (err) {
        *err = tr("Could not start the VPN helper — authorization may have been "
                  "declined, or pkexec/sudo elevation failed.");
    }
    return false;
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
        clearTokenFile();
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
    abortStartup();
    emit vpnError(msg);
    setState(State::Error);
    setState(State::Disconnected);
}
