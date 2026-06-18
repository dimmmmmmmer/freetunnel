#include "vpn/vpn_helper_server.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStringList>
#include <QTimer>
#include <vector>

#ifndef Q_OS_WIN
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

#include "vpn/qt_trusttunnel_client.h"
#include "vpn/vpn_helper_protocol.h"

namespace {

#ifndef Q_OS_WIN
// Effective uid of the process on the other end of a connected Unix-domain
// socket. Returns (uid_t)-1 if it can't be determined.
uid_t socketPeerUid(qintptr fd)
{
#if defined(SO_PEERCRED)
    // Linux: SO_PEERCRED returns {pid, uid, gid}. Use a local POD with the same
    // layout as struct ucred so we don't depend on _GNU_SOURCE being defined.
    struct PeerCred { pid_t pid; uid_t uid; gid_t gid; } cred{};
    socklen_t len = sizeof(cred);
    if (::getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
        return cred.uid;
    return static_cast<uid_t>(-1);
#else
    // macOS / BSD.
    uid_t euid = static_cast<uid_t>(-1);
    gid_t egid = 0;
    if (::getpeereid(static_cast<int>(fd), &euid, &egid) == 0)
        return euid;
    return static_cast<uid_t>(-1);
#endif
}
#endif

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
    HelperServer(QString socketName, QString token, int peerUid)
        : m_socketName(std::move(socketName)), m_token(std::move(token)), m_peerUid(peerUid) {
        connect(&m_client, &QtTrustTunnelClient::stateChanged, this,
                [this](QtTrustTunnelClient::State s) {
                    QJsonObject e; e["ev"] = "state"; e["state"] = stateName(s); send(e);
                });
        connect(&m_client, &QtTrustTunnelClient::tunnelStats, this,
                [this](quint64 up, quint64 down) {
                    QJsonObject e; e["ev"] = "stats";
                    e["up"] = double(up); e["down"] = double(down); send(e);
                });
        connect(&m_client, &QtTrustTunnelClient::connectionInfo, this,
                [this](const QString &m) { QJsonObject e; e["ev"]="info"; e["msg"]=m; send(e); });
        connect(&m_client, &QtTrustTunnelClient::vpnError, this,
                [this](const QString &m) { QJsonObject e; e["ev"]="error"; e["msg"]=m; send(e); });
    }

    bool listen() {
        connect(&m_server, &QLocalServer::newConnection, this, &HelperServer::onConnection);
#if defined(Q_OS_WIN)
        // Named pipe: the elevated (high-integrity) helper must be reachable by
        // the medium-integrity GUI, so allow the connection and gate it with the
        // one-time token (parity with the previous loopback behavior).
        m_server.setSocketOptions(QLocalServer::WorldAccessOption);
        return m_server.listen(m_socketName);
#else
        // Unix-domain socket: create it 0600 (owner-only), then hand ownership to
        // the launching user so that *only* that user — not other local accounts
        // or the loopback network — can connect to the root-owned helper.
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!m_server.listen(m_socketName)) {
            qWarning("HelperServer: listen(%s) failed", qPrintable(m_socketName));
            return false;
        }
        // Use the path we passed to listen(), not fullServerName() — on some
        // platforms the latter can differ or be empty right after listen().
        const QString path = m_socketName;
        if (m_peerUid >= 0 && !path.isEmpty()) {
            if (::chown(path.toLocal8Bit().constData(),
                        static_cast<uid_t>(m_peerUid), static_cast<gid_t>(-1)) != 0) {
                qWarning("HelperServer: chown(%s, uid=%d) failed: %s",
                         qPrintable(path), m_peerUid, std::strerror(errno));
                m_server.close();
                return false;
            }
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        }
        return true;
#endif
    }

    bool authed() const { return m_authed; }

private:
    void onConnection() {
        QLocalSocket *s = m_server.nextPendingConnection();
        if (!s) return;
        if (m_sock) { s->close(); s->deleteLater(); return; } // single GUI client
#ifndef Q_OS_WIN
        // Verify the connecting process really belongs to the launching user
        // (defense in depth on top of the 0600 socket ownership).
        if (m_peerUid >= 0) {
            const uid_t peer = socketPeerUid(s->socketDescriptor());
            // Only reject when peer identity is known and doesn't match. If
            // getpeereid/SO_PEERCRED fails (returns -1), rely on the chown'd
            // 0600 socket ownership instead of dropping the connection.
            if (peer != static_cast<uid_t>(-1)
                    && peer != static_cast<uid_t>(m_peerUid)) {
                qWarning("HelperServer: rejecting peer uid %u (expected %d)",
                         static_cast<unsigned>(peer), m_peerUid);
                s->close();
                s->deleteLater();
                return;
            }
        }
#endif
        m_sock = s;
        connect(m_sock, &QLocalSocket::readyRead, this, &HelperServer::onReadyRead);
        connect(m_sock, &QLocalSocket::disconnected, this, [this]() {
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
        if (m_buf.size() > vpn_helper::kMaxReadBuffer) {
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
        const QString cmd = c.value("cmd").toString();
        if (!m_authed) {
            if (cmd == "hello" && vpn_helper::tokensEqual(c.value("token").toString(), m_token)) {
                m_authed = true;
                QJsonObject e; e["ev"] = "ready"; m_sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n');
            } else {
                m_sock->close();
            }
            return;
        }
        if (cmd == "setExclusions") {
            std::vector<std::string> ex;
            for (const auto v : c.value("domains").toArray()) ex.push_back(v.toString().toStdString());
            m_client.setExtraExclusions(ex);
        } else if (cmd == "setRoutes") {
            std::vector<std::string> routes;
            for (const auto v : c.value("excluded").toArray()) routes.push_back(v.toString().toStdString());
            m_client.setRoutingRules({}, routes);
        } else if (cmd == "setMode") {
            m_client.setVpnMode(c.value("selective").toBool());
        } else if (cmd == "setKillSwitch") {
            m_client.setKillSwitch(c.value("enabled").toBool());
        } else if (cmd == "connect") {
            const QString path = c.value("configPath").toString();
            if (!m_client.loadConfigFromFile(path)) {
                QJsonObject e; e["ev"]="error"; e["msg"]=QStringLiteral("Failed to load config"); send(e);
                return;
            }
            m_client.connectVpn();
        } else if (cmd == "disconnect") {
            m_client.disconnectVpn();
        } else if (cmd == "quit") {
            QCoreApplication::quit();
        }
    }

    QString m_socketName;
    QString m_token;
    int m_peerUid = -1;
    QLocalServer m_server;
    QLocalSocket *m_sock = nullptr;
    QByteArray m_buf;
    bool m_authed = false;
    QtTrustTunnelClient m_client;
};

} // namespace

int runVpnHelper(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QString socketName;
    QString token;
    QString tokenFile;
    int peerUid = -1;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args[i] == QLatin1String("--socket")) socketName = args[i + 1];
        else if (args[i] == QLatin1String("--token-file")) tokenFile = args[i + 1];
        else if (args[i] == QLatin1String("--peer-uid")) peerUid = args[i + 1].toInt();
    }
    if (!tokenFile.isEmpty()) {
        QFile f(tokenFile);
        if (f.open(QIODevice::ReadOnly)) {
            token = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
        }
        QFile::remove(tokenFile);
    }
    if (socketName.isEmpty() || token.isEmpty())
        return 2;

    HelperServer server(socketName, token, peerUid);
    if (!server.listen())
        return 3;

    QTimer::singleShot(60000, &app, [&server]() {
        if (!server.authed())
            QCoreApplication::quit();
    });
    return app.exec();
}
