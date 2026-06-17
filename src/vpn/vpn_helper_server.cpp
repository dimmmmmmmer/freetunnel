#include "vpn/vpn_helper_server.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>
#include <vector>

#include "vpn/qt_trusttunnel_client.h"

namespace {

bool tokensEqual(const QString &a, const QString &b)
{
    const QByteArray ba = a.toUtf8();
    const QByteArray bb = b.toUtf8();
    if (ba.size() != bb.size())
        return false;
    char diff = 0;
    for (int i = 0; i < ba.size(); ++i)
        diff |= static_cast<char>(ba[i] ^ bb[i]);
    return diff == 0;
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
    HelperServer(QString socketName, QString token)
        : m_socketName(std::move(socketName)), m_token(std::move(token)) {
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
        m_server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!m_server.listen(m_socketName))
            return false;
        connect(&m_server, &QLocalServer::newConnection, this, &HelperServer::onConnection);
        return true;
    }

    bool authed() const { return m_authed; }

private:
    void onConnection() {
        QLocalSocket *s = m_server.nextPendingConnection();
        if (!s) return;
        if (m_sock) { s->close(); s->deleteLater(); return; } // single GUI client
        m_sock = s;
        connect(m_sock, &QLocalSocket::readyRead, this, &HelperServer::onReadyRead);
        connect(m_sock, &QLocalSocket::disconnected, this, [this]() {
            // Only the authenticated GUI dropping tears the privileged helper
            // down. An unauthenticated/stray local connection going away must
            // not kill it (trivial DoS) — just free the slot for the real GUI.
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
        // Commands are tiny single-line JSON objects. Bound the buffer so a local
        // client can't exhaust memory in the privileged process by streaming a
        // huge line without a newline.
        if (m_buf.size() > 65536) {
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
            if (cmd == "hello" && tokensEqual(c.value("token").toString(), m_token)) {
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
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args[i] == QLatin1String("--socket")) socketName = args[i + 1];
        else if (args[i] == QLatin1String("--port")) { /* legacy TCP mode ignored */ }
        else if (args[i] == QLatin1String("--token")) token = args[i + 1]; // legacy/fallback
        else if (args[i] == QLatin1String("--token-file")) tokenFile = args[i + 1];
    }
    // Preferred path: the GUI passes the token in a 0600 file (kept off argv so
    // other local users can't read it via /proc/<pid>/cmdline). Read it once and
    // delete it immediately so the secret doesn't linger on disk.
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

    HelperServer server(std::move(socketName), token);
    if (!server.listen())
        return 3;

    // Safety net: if no GUI authenticates within 60s, don't linger as root.
    QTimer::singleShot(60000, &app, [&server]() {
        if (!server.authed())
            QCoreApplication::quit();
    });
    return app.exec();
}
