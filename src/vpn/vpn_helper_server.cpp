#include "vpn/vpn_helper_server.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <vector>

#include "vpn/qt_trusttunnel_client.h"

namespace {

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
        if (!m_server.listen(QHostAddress::LocalHost, m_port))
            return false;
        connect(&m_server, &QTcpServer::newConnection, this, &HelperServer::onConnection);
        return true;
    }

    bool authed() const { return m_authed; }

private:
    void onConnection() {
        QTcpSocket *s = m_server.nextPendingConnection();
        if (!s) return;
        if (m_sock) { s->close(); s->deleteLater(); return; } // single GUI client
        m_sock = s;
        connect(m_sock, &QTcpSocket::readyRead, this, &HelperServer::onReadyRead);
        connect(m_sock, &QTcpSocket::disconnected, this, []() {
            QCoreApplication::quit(); // GUI gone -> tear down the privileged helper
        });
    }

    void send(const QJsonObject &e) {
        if (m_authed && m_sock)
            m_sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n');
    }

    void onReadyRead() {
        m_buf += m_sock->readAll();
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
            if (cmd == "hello" && c.value("token").toString() == m_token) {
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

    quint16 m_port;
    QString m_token;
    QTcpServer m_server;
    QTcpSocket *m_sock = nullptr;
    QByteArray m_buf;
    bool m_authed = false;
    QtTrustTunnelClient m_client;
};

} // namespace

int runVpnHelper(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    quint16 port = 0;
    QString token;
    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args[i] == QLatin1String("--port")) port = args[i + 1].toUShort();
        else if (args[i] == QLatin1String("--token")) token = args[i + 1];
    }
    if (port == 0 || token.isEmpty())
        return 2;

    HelperServer server(port, token);
    if (!server.listen())
        return 3;

    // Safety net: if no GUI authenticates within 60s, don't linger as root.
    QTimer::singleShot(60000, &app, [&server]() {
        if (!server.authed())
            QCoreApplication::quit();
    });
    return app.exec();
}
