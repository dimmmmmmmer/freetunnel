// cppcheck-suppress-file missingIncludeSystem
#include "vpn/vpn_helper_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QTimer>

#include "core/AppUiUtils.h" // shellEscape / appleScriptEscape (macOS)
#include "vpn/vpn_helper_protocol.h"

namespace {

const QHostAddress kLoopback = QHostAddress(QStringLiteral("127.0.0.1"));

} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
namespace {

QStringList linuxHelperCommand(const QString &exe, quint16 port, const QString &tokenPath)
{
    QStringList cmd;
    const QByteArray appImage = qgetenv("APPIMAGE");
    if (!appImage.isEmpty()) {
        cmd << QStringLiteral("env") << QStringLiteral("APPIMAGE_EXTRACT_AND_RUN=1")
            << QString::fromLocal8Bit(appImage);
    } else {
        cmd << exe;
    }
    cmd << QStringLiteral("--helper") << QStringLiteral("--port") << QString::number(port)
        << QStringLiteral("--token-file") << tokenPath;
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
    shutdown();
}

void VpnHelperClient::shutdown() {
    if (m_sock && m_helloAcked) {
        if (m_state != State::Disconnected && m_state != State::Error) {
            QJsonObject c; c["cmd"] = "disconnect"; send(c);
            m_sock->flush();
            m_sock->waitForBytesWritten(500);
        }
        QJsonObject q; q["cmd"] = "quit"; send(q);
        m_sock->flush();
        m_sock->waitForBytesWritten(500);
    }
    abortStartup();
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
    m_configToml.clear();
    return !path.isEmpty();
}

bool VpnHelperClient::loadConfigFromToml(const QString &toml) {
    m_configToml = toml;
    m_configPath.clear();
    return !toml.isEmpty();
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
    QJsonObject c;
    c["cmd"] = "connect";
    if (!m_configToml.isEmpty())
        c["configToml"] = m_configToml;
    else
        c["configPath"] = m_configPath;
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

void VpnHelperClient::resetHelperTransport()
{
    if (m_sock) {
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    if (m_proc) {
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

bool VpnHelperClient::configureProductionHelper()
{
    m_tcpPort = static_cast<quint16>(49152 + QRandomGenerator::system()->bounded(16383));
    m_token = QString::number(QRandomGenerator::system()->generate64(), 16)
            + QString::number(QRandomGenerator::system()->generate64(), 16);
    clearTokenFile();
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
    QFile::setPermissions(tf.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    m_tokenPath = tf.fileName();

    QString err;
    if (!spawnElevatedHelper(m_tcpPort, m_tokenPath, &err)) {
        fail(err.isEmpty() ? tr("Could not start the VPN helper") : err);
        return false;
    }
    return true;
}

bool VpnHelperClient::configureTestHelper()
{
#ifdef FT_ENABLE_TEST_HOOKS
    m_tcpPort = static_cast<quint16>(qEnvironmentVariableIntValue("FT_TEST_HELPER_PORT"));
    m_token = QString::fromUtf8(qgetenv("FT_TEST_HELPER_TOKEN"));
    if (m_token.isEmpty()) {
        fail(tr("FT_TEST_HELPER_TOKEN is required when FT_TEST_HELPER_PORT is set"));
        return false;
    }
    return true;
#else
    return false;
#endif
}

void VpnHelperClient::wireHelperSocket(bool testHelper)
{
    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::connected, this, &VpnHelperClient::onSocketConnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &VpnHelperClient::onReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
        m_helloAcked = false;
        m_starting = false;
        if (m_state != State::Disconnected)
            setState(State::Disconnected);
    });
    if (testHelper) {
        m_sock->connectToHost(kLoopback, m_tcpPort);
        return;
    }
    startHelperConnectRetry();
}

void VpnHelperClient::startHelperConnectRetry()
{
    if (m_attempt) {
        m_attempt->stop();
        m_attempt->deleteLater();
    }
    m_attempt = new QTimer(this);
    m_tries = 0;
    connect(m_attempt, &QTimer::timeout, this, [this]() {
        if (!m_sock) {
            if (m_attempt) {
                m_attempt->stop();
                m_attempt->deleteLater();
                m_attempt = nullptr;
            }
            return;
        }
        if (m_sock->state() == QAbstractSocket::ConnectedState) {
            m_attempt->stop();
            m_attempt->deleteLater();
            m_attempt = nullptr;
            return;
        }
        if (++m_tries > 80) {
            m_attempt->stop();
            m_attempt->deleteLater();
            m_attempt = nullptr;
            const QString detail = m_sock->errorString();
            fail(tr("The VPN helper didn't start — authorization may have been declined, "
                    "or the elevated helper could not be reached.%1")
                         .arg(detail.isEmpty() ? QString()
                                               : QStringLiteral(" (") + detail + QLatin1Char(')')));
            return;
        }
        m_sock->abort();
        m_sock->connectToHost(kLoopback, m_tcpPort);
    });
    m_attempt->start(250);
    m_sock->connectToHost(kLoopback, m_tcpPort);
}

bool VpnHelperClient::ensureHelper() {
    if (m_sock && m_sock->state() == QAbstractSocket::ConnectedState)
        return true;
    if (m_starting)
        return true;
    m_starting = true;
    resetHelperTransport();

#if defined(FT_ENABLE_TEST_HOOKS)
    if (qEnvironmentVariableIsSet("FT_TEST_HELPER_PORT")) {
        if (!configureTestHelper())
            return false;
        wireHelperSocket(true);
        return true;
    }
#endif
    if (!configureProductionHelper())
        return false;
    wireHelperSocket(false);
    return true;
}

#if defined(Q_OS_MACOS)
static bool launchMacElevatedHelper(QProcess **procOut, QObject *parent, const QString &exe,
                                    quint16 port, const QString &tokenPath, QString *err)
{
    const QString inner =
            QStringLiteral("logf=$(mktemp \"${TMPDIR:-/tmp}/freetunnel-helper.XXXXXX\") || logf=/dev/null; "
                           "exec %1 --helper --port %2 --token-file %3 "
                           ">\"$logf\" 2>&1 &")
                    .arg(shellEscape(exe), QString::number(port), shellEscape(tokenPath));
    const QString script = QStringLiteral("do shell script \"%1\" with administrator privileges")
                                   .arg(appleScriptEscape(inner));
    auto *proc = new QProcess(parent);
    proc->start(QStringLiteral("osascript"), {QStringLiteral("-e"), script});
    if (!proc->waitForStarted(5000)) {
        if (err)
            *err = QObject::tr("Could not launch osascript");
        proc->deleteLater();
        return false;
    }
    *procOut = proc;
    return true;
}
#endif

#if defined(Q_OS_WIN)
static bool launchWinElevatedHelper(const QString &exe, quint16 port, const QString &tokenPath,
                                    QString *err)
{
    const QString exeDir = QFileInfo(exe).absolutePath();
    const QString args = QStringLiteral("--helper --port %1 --token-file \"%2\"")
                                 .arg(QString::number(port), tokenPath);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    const std::wstring wexe = exe.toStdWString();
    const std::wstring wargs = args.toStdWString();
    const std::wstring wdir = exeDir.toStdWString();
    sei.lpFile = wexe.c_str();
    sei.lpParameters = wargs.c_str();
    sei.lpDirectory = wdir.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        if (err)
            *err = QObject::tr("Elevation was cancelled or failed");
        return false;
    }
    return true;
}
#endif

bool VpnHelperClient::spawnElevatedHelper(quint16 port, const QString &tokenPath, QString *err) {
    const QString exe = QCoreApplication::applicationFilePath();

#if defined(Q_OS_MACOS)
    return launchMacElevatedHelper(&m_proc, this, exe, port, tokenPath, err);
#elif defined(Q_OS_WIN)
    return launchWinElevatedHelper(exe, port, tokenPath, err);
#else
    m_proc = new QProcess(this);
    const QStringList helperCmd = linuxHelperCommand(exe, port, tokenPath);

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
    if (m_buf.size() > vpn_helper::kMaxIpcLineBytes) {
        fail(tr("VPN helper sent too much data"));
        return;
    }
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        const auto doc = QJsonDocument::fromJson(line);
        if (doc.isObject()) handleEvent(doc.object());
    }
}

void VpnHelperClient::handleReadyEvent()
{
    m_helloAcked = true;
    clearTokenFile();
    setVpnMode(m_selective);
    setKillSwitch(m_killSwitch);
    setExtraExclusions(m_exclusions);
    setExcludedRoutes(m_excludedRoutes);
    if (!m_connectPending)
        return;
    m_connectPending = false;
    connectVpn();
}

void VpnHelperClient::handleEvent(const QJsonObject &ev) {
    const QString type = ev.value("ev").toString();
    if (type == "ready") {
        handleReadyEvent();
        return;
    }
    if (type == "state") {
        setState(stateFromString(ev.value("state").toString()));
        return;
    }
    if (type == "stats") {
        emit tunnelStats(static_cast<quint64>(ev.value("up").toDouble()),
                         static_cast<quint64>(ev.value("down").toDouble()));
        return;
    }
    if (type == "info") {
        emit connectionInfo(ev.value("msg").toString());
        return;
    }
    if (type == "progress") {
        emit connectProgress(ev.value("msg").toString());
        return;
    }
    if (type == "log") {
        emit coreLogLine(ev.value("msg").toString());
        return;
    }
    if (type == "error") {
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
