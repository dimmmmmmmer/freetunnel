// cppcheck-suppress-file missingIncludeSystem
#include "helper_ipc_mock_server.h"

#include <QJsonArray>
#include <QJsonDocument>

#include "vpn/vpn_helper_protocol.h"

MockHelperServer::MockHelperServer(const QString &token, QObject *parent)
    : QObject(parent)
    , m_token(token)
{
    m_server.setParent(this);
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
        while (m_server.hasPendingConnections())
            adoptSocket(m_server.nextPendingConnection());
    });
}


MockHelperServer::~MockHelperServer()
{
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->abort();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_server.close();
}

bool MockHelperServer::listen()
{
    return m_server.listen(QHostAddress(QStringLiteral("127.0.0.1")));
}

quint16 MockHelperServer::port() const
{
    return m_server.serverPort();
}

void MockHelperServer::acceptPending(int timeoutMs)
{
    if (!m_server.waitForNewConnection(timeoutMs))
        return;
    while (m_server.hasPendingConnections())
        adoptSocket(m_server.nextPendingConnection());
}

bool MockHelperServer::processAvailable()
{
    if (!m_sock || m_sock->bytesAvailable() <= 0)
        return false;
    onReadyRead();
    return true;
}

bool MockHelperServer::waitForClientData(int timeoutMs)
{
    if (!m_sock)
        return false;
    if (m_sock->bytesAvailable() > 0) {
        onReadyRead();
        return true;
    }
    if (!m_sock->waitForReadyRead(timeoutMs))
        return false;
    onReadyRead();
    return true;
}

void MockHelperServer::adoptSocket(QTcpSocket *s)
{
    if (!s)
        return;
    ++m_connectionCount;
    // After a client has authenticated, the slot is taken — reject extra links.
    if (m_authed) {
        s->close();
        s->deleteLater();
        return;
    }
    // Pre-auth: the newest connection takes the slot. Dropping any earlier,
    // still-unauthenticated socket here is what stops a token-less squatter
    // from holding the slot and blocking the real client.
    if (m_sock) {
        m_sock->disconnect(this);
        m_sock->abort();
        m_sock->deleteLater();
        m_buf.clear();
    }
    s->setParent(this);
    m_sock = s;
    connect(m_sock, &QTcpSocket::readyRead, this, [this]() { onReadyRead(); });
    connect(m_sock, &QTcpSocket::disconnected, this, [this]() {
        if (m_sock) {
            m_sock->deleteLater();
            m_sock = nullptr;
        }
        m_buf.clear();
        m_authed = false;
    });
}

void MockHelperServer::send(const QJsonObject &e)
{
    if (!m_authed || !m_sock)
        return;
    m_sock->write(QJsonDocument(e).toJson(QJsonDocument::Compact) + '\n');
    m_sock->flush();
}

void MockHelperServer::onReadyRead()
{
    if (!m_sock)
        return;
    m_buf += m_sock->readAll();
    if (m_buf.size() > vpn_helper::kMaxIpcLineBytes) {
        m_sock->abort();
        return;
    }
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0) {
        const QByteArray line = m_buf.left(nl);
        m_buf.remove(0, nl + 1);
        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject())
            continue;
        handle(doc.object());
        if (!m_sock)
            break;
    }
}

void MockHelperServer::handle(const QJsonObject &c)
{
    const QString cmd = c.value("cmd").toString();
    if (!m_authed) {
        if (cmd == QLatin1String("hello")
            && vpn_helper::tokensEqual(c.value("token").toString(), m_token)) {
            m_authed = true;
            QJsonObject ready;
            ready["ev"] = "ready";
            m_sock->write(QJsonDocument(ready).toJson(QJsonDocument::Compact) + '\n');
            m_sock->flush();
        } else {
            m_sock->abort();
        }
        return;
    }

    m_lastCmd = cmd;
    if (cmd == QLatin1String("setExclusions") || cmd == QLatin1String("setRoutes")
        || cmd == QLatin1String("setMode") || cmd == QLatin1String("setKillSwitch")) {
        return;
    }
    if (cmd == QLatin1String("connect")) {
        if (m_connectCount++ > 0) {
            send(QJsonObject{{"ev", "error"}, {"msg", QStringLiteral("core disconnected")}});
        }
        send(QJsonObject{{"ev", "state"}, {"state", "Connecting"}});
        send(QJsonObject{{"ev", "state"}, {"state", "Connected"}});
        send(QJsonObject{{"ev", "stats"}, {"up", 1024.0}, {"down", 2048.0}});
        m_tunnelUp = true;
    } else if (cmd == QLatin1String("disconnect")) {
        send(QJsonObject{{"ev", "state"}, {"state", "Disconnecting"}});
        send(QJsonObject{{"ev", "state"}, {"state", "Disconnected"}});
        m_tunnelUp = false;
    } else if (cmd == QLatin1String("quit")) {
        emit quitRequested();
    }
}
