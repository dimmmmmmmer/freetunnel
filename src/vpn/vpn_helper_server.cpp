// cppcheck-suppress-file missingIncludeSystem
#include "vpn/vpn_helper_server.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include "vpn/qt_trusttunnel_client.h"
#include "vpn/vpn_helper_launch.h"
#include "vpn/vpn_helper_protocol.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

#if defined(Q_OS_WIN)
static bool prepareWindowsHelperRuntime(QString *errOut)
{
    const QString dir = QCoreApplication::applicationDirPath();
    QDir::setCurrent(dir);
    SetDllDirectoryW(reinterpret_cast<LPCWSTR>(dir.utf16()));
    if (!QFile::exists(dir + QStringLiteral("/wintun.dll"))) {
        if (errOut) {
            *errOut = QObject::tr("wintun.dll is missing next to FreeTunnel.exe (%1). "
                                  "Reinstall from the official installer.")
                              .arg(dir);
        }
        return false;
    }
    return true;
}
#endif

namespace {

// Always bind/connect on IPv4 loopback — QHostAddress::LocalHost can prefer
// ::1 on some hosts while the peer listens on 127.0.0.1 only.
const QHostAddress kLoopback = QHostAddress(QStringLiteral("127.0.0.1"));

// Whether this (helper) process is actually running elevated. A failed
// "create listener" from the core almost always means it is not, so we report
// it to the GUI log to make the cause obvious.
bool helperIsElevated() {
#if defined(Q_OS_WIN)
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev{};
    DWORD len = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &len)
            && elev.TokenIsElevated != 0;
    CloseHandle(token);
    return ok;
#else
    return ::geteuid() == 0;
#endif
}

QString stateName(QtTrustTunnelClient::State s) {
    switch (s) {
    case QtTrustTunnelClient::State::Connecting: return QStringLiteral("Connecting");
    case QtTrustTunnelClient::State::Connected: return QStringLiteral("Connected");
    case QtTrustTunnelClient::State::Reconnecting: return QStringLiteral("Reconnecting");
    case QtTrustTunnelClient::State::WaitingForNetwork: return QStringLiteral("WaitingForNetwork");
    case QtTrustTunnelClient::State::Disconnecting: return QStringLiteral("Disconnecting");
    case QtTrustTunnelClient::State::Error: return QStringLiteral("Error");
    default: return QStringLiteral("Disconnected");
    }
}

// Bridges one GUI connection to the VPN core. Quits the process when the GUI
// disconnects, so the elevated helper never lingers.
class HelperServer : public QObject {
public:
    HelperServer(quint16 port, QString token)
        : m_port(port), m_token(std::move(token)) {
        m_client.moveToThread(&m_vpnThread);
        m_vpnThread.start();
        connect(&m_client, &QtTrustTunnelClient::stateChanged, this,
                [this](QtTrustTunnelClient::State s) {
                    QJsonObject e; e["ev"] = "state"; e["state"] = stateName(s); send(e);
                }, Qt::QueuedConnection);
        connect(&m_client, &QtTrustTunnelClient::tunnelStats, this,
                [this](quint64 up, quint64 down) {
                    QJsonObject e; e["ev"] = "stats";
                    e["up"] = double(up); e["down"] = double(down); send(e);
                }, Qt::QueuedConnection);
        connect(&m_client, &QtTrustTunnelClient::connectionInfo, this,
                [this](const QString &m) { QJsonObject e; e["ev"]="info"; e["msg"]=m; send(e); },
                Qt::QueuedConnection);
        connect(&m_client, &QtTrustTunnelClient::connectProgress, this,
                [this](const QString &m) { QJsonObject e; e["ev"]="progress"; e["msg"]=m; send(e); },
                Qt::QueuedConnection);
        connect(&m_client, &QtTrustTunnelClient::coreLogLine, this,
                [this](const QString &line) {
                    QJsonObject e;
                    e["ev"] = "log";
                    e["msg"] = line;
                    send(e);
                },
                Qt::QueuedConnection);
        connect(&m_client, &QtTrustTunnelClient::vpnError, this,
                [this](const QString &m) { QJsonObject e; e["ev"]="error"; e["msg"]=m; send(e); },
                Qt::QueuedConnection);
    }

    ~HelperServer() override {
        if (m_vpnThread.isRunning()) {
            QMetaObject::invokeMethod(&m_client, "disconnectVpn", Qt::BlockingQueuedConnection);
            m_vpnThread.quit();
            m_vpnThread.wait();
        }
    }

    bool listen() {
        // Loopback TCP works across uid boundaries (GUI user ↔ elevated helper).
        // Authentication is enforced by the one-time token handshake.
        connect(&m_server, &QTcpServer::newConnection, this, &HelperServer::onConnection);
        if (m_server.listen(kLoopback, m_port))
            return true;
        qWarning("HelperServer: listen(127.0.0.1:%u) failed: %s",
                 m_port, qPrintable(m_server.errorString()));
        return false;
    }

    bool authed() const { return m_authed; }

private:
    void onConnection() {
        QTcpSocket *s = m_server.nextPendingConnection();
        if (!s) return;
        if (m_sock) { s->close(); s->deleteLater(); return; } // single GUI client
        m_sock = s;
        connect(m_sock, &QTcpSocket::readyRead, this, &HelperServer::onReadyRead);
        connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
            if (m_authed) {
                QCoreApplication::quit();
                return;
            }
            if (m_sock) { m_sock->deleteLater(); m_sock = nullptr; }
            m_buf.clear();
        });
    }

    void send(const QJsonObject &e) {
        if (m_authed && m_sock)
            m_sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n');
    }

    void onReadyRead() {
        m_buf += m_sock->readAll();
        if (m_buf.size() > vpn_helper::kMaxIpcLineBytes) {
            m_sock->close();
            return;
        }
        int nl;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            const QByteArray line = m_buf.left(nl);
            m_buf.remove(0, nl + 1);
            const auto doc = QJsonDocument::fromJson(line);
            if (doc.isObject()) handle(doc.object());
        }
    }

    void handle(const QJsonObject &c) {
        if (!m_authed)
            return handlePreAuth(c);
        handleAuthed(c);
    }

    void handlePreAuth(const QJsonObject &c) {
        const QString cmd = c.value("cmd").toString();
        if (cmd == "hello" && vpn_helper::tokensEqual(c.value("token").toString(), m_token)) {
            m_authed = true;
            QJsonObject e;
            e["ev"] = "ready";
            m_sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n');
            QJsonObject pe;
            pe["ev"] = "info";
            pe["msg"] = helperIsElevated()
                    ? QStringLiteral("VPN helper started with admin privileges")
                    : QStringLiteral("VPN helper started WITHOUT admin privileges — connecting will fail");
            m_sock->write(QJsonDocument(pe).toJson(QJsonDocument::Compact) + '\n');
            return;
        }
        m_sock->close();
    }

    void handleAuthed(const QJsonObject &c) {
        const QString cmd = c.value("cmd").toString();
        if (cmd == "setExclusions")
            return applyExclusions(c);
        if (cmd == "setRoutes")
            return applyRoutes(c);
        if (cmd == "setMode") {
            QMetaObject::invokeMethod(&m_client, "setVpnMode", Qt::QueuedConnection,
                                      Q_ARG(bool, c.value("selective").toBool()));
            return;
        }
        if (cmd == "setKillSwitch") {
            QMetaObject::invokeMethod(&m_client, "setKillSwitch", Qt::QueuedConnection,
                                      Q_ARG(bool, c.value("enabled").toBool()));
            return;
        }
        if (cmd == "connect") {
            handleConnect(c);
            return;
        }
        if (cmd == "disconnect") {
            QMetaObject::invokeMethod(&m_client, "disconnectVpn", Qt::QueuedConnection);
            return;
        }
        if (cmd == "quit")
            QCoreApplication::quit();
    }

    void applyExclusions(const QJsonObject &c) {
        QStringList domains;
        for (const QJsonValue &v : c.value(QStringLiteral("domains")).toArray())
            domains.append(v.toString());
        QMetaObject::invokeMethod(&m_client, "setExtraExclusionDomains", Qt::QueuedConnection,
                                  Q_ARG(QStringList, domains));
    }

    void applyRoutes(const QJsonObject &c) {
        QStringList routes;
        for (const QJsonValue &v : c.value(QStringLiteral("excluded")).toArray())
            routes.append(v.toString());
        QMetaObject::invokeMethod(&m_client, "setExcludedRouteStrings", Qt::QueuedConnection,
                                  Q_ARG(QStringList, routes));
    }

    void handleConnect(const QJsonObject &c) {
#if defined(Q_OS_WIN)
        QString wintunErr;
        if (!prepareWindowsHelperRuntime(&wintunErr)) {
            QJsonObject e;
            e["ev"] = "error";
            e["msg"] = wintunErr;
            send(e);
            return;
        }
#endif
        const QString toml = c.value(QStringLiteral("configToml")).toString();
        const QString path = c.value(QStringLiteral("configPath")).toString();
        const QString logPath = c.value(QStringLiteral("logPath")).toString();
        const bool loggingEnabled = c.value(QStringLiteral("loggingEnabled")).toBool(true);
        QMetaObject::invokeMethod(&m_client, "setSessionLogging", Qt::QueuedConnection,
                                  Q_ARG(QString, logPath), Q_ARG(bool, loggingEnabled));
        QMetaObject::invokeMethod(&m_client, "beginConnect", Qt::QueuedConnection,
                                  Q_ARG(QString, toml), Q_ARG(QString, path));
    }

    quint16 m_port = 0;
    QString m_token;
    QTcpServer m_server;
    QTcpSocket *m_sock = nullptr;
    QByteArray m_buf;
    bool m_authed = false;
    QThread m_vpnThread;
    QtTrustTunnelClient m_client;
};

} // namespace

int runVpnHelper(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#if defined(Q_OS_WIN)
    QString wintunErr;
    if (!prepareWindowsHelperRuntime(&wintunErr)) {
        qWarning("%s", qPrintable(wintunErr));
        return 4;
    }
#endif
    const freetunnel::HelperLaunchConfig cfg =
            freetunnel::parseHelperLaunchArgs(QCoreApplication::arguments());
    if (!cfg.ok())
        return 2;

    HelperServer server(cfg.port, cfg.token);
    if (!server.listen())
        return 3;

    QTimer::singleShot(60000, &app, [&server]() {
        if (!server.authed())
            QCoreApplication::quit();
    });
    return app.exec();
}
