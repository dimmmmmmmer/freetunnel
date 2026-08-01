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
        // Per-connection handshake state must not leak into the next socket, or
        // the second client would be answering the first one's challenge.
        m_challenge.clear();
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
    // Mirror the real server: drain complete lines first and cap the LINE, not
    // the accumulated buffer. Capping the buffer killed legitimate pipelined
    // bursts, and a mock with the old policy would keep passing after production
    // was fixed.
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
    if (m_sock && m_buf.size() > vpn_helper::kMaxIpcLineBytes) {
        m_buf.clear();
        m_sock->abort();
    }
}

void MockHelperServer::handle(const QJsonObject &c)
{
    const QString cmd = c.value("cmd").toString();
    if (!m_authed) {
        // Mirrors the production handshake in vpn_helper_server.cpp: the client
        // opens with a nonce only, we prove we hold the token over it and send a
        // nonce of our own, and only a correct answer authenticates. Keep the two
        // in step — this double is what the client-side tests talk to.
        if (cmd == QLatin1String("hello") && !c.value("nonce").toString().isEmpty()
            && m_challenge.isEmpty()) {
            m_challenge = QStringLiteral("mock-nonce-0123456789abcdef");
            QJsonObject ch;
            ch["ev"] = "challenge";
            ch["proof"] = vpn_helper::authProof(
                    m_token, QString::fromLatin1(vpn_helper::kHelperRole),
                    c.value("nonce").toString());
            ch["nonce"] = m_challenge;
            m_sock->write(QJsonDocument(ch).toJson(QJsonDocument::Compact) + '\n');
            m_sock->flush();
            return;
        }
        if (cmd == QLatin1String("auth") && !m_challenge.isEmpty()
            && vpn_helper::tokensEqual(
                    c.value("proof").toString(),
                    vpn_helper::authProof(m_token, QString::fromLatin1(vpn_helper::kGuiRole),
                                          m_challenge))) {
            m_authed = true;
            QJsonObject ready;
            ready["ev"] = "ready";
            m_sock->write(QJsonDocument(ready).toJson(QJsonDocument::Compact) + '\n');
            m_sock->flush();
            return;
        }
        m_sock->abort();
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

bool mockHelperHandshake(MockHelperServer &server, QTcpSocket &client, const QString &token)
{
    const QString nonce = QStringLiteral("test-gui-nonce-abcdef0123456789");
    QJsonObject hello;
    hello["cmd"] = QStringLiteral("hello");
    hello["nonce"] = nonce;
    client.write(QJsonDocument(hello).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    if (!server.waitForClientData(3000) || !client.waitForReadyRead(3000))
        return false;
    const auto challenge = QJsonDocument::fromJson(client.readLine()).object();
    if (challenge.value("ev").toString() != QLatin1String("challenge"))
        return false;
    // The client half of the contract: never answer a peer that cannot prove it
    // holds the token.
    if (!vpn_helper::tokensEqual(
                challenge.value("proof").toString(),
                vpn_helper::authProof(token, QString::fromLatin1(vpn_helper::kHelperRole), nonce)))
        return false;
    QJsonObject auth;
    auth["cmd"] = QStringLiteral("auth");
    auth["proof"] = vpn_helper::authProof(token, QString::fromLatin1(vpn_helper::kGuiRole),
                                          challenge.value("nonce").toString());
    client.write(QJsonDocument(auth).toJson(QJsonDocument::Compact) + '\n');
    client.flush();
    if (!server.waitForClientData(3000) || !client.waitForReadyRead(3000))
        return false;
    return QJsonDocument::fromJson(client.readLine()).object().value("ev").toString()
            == QLatin1String("ready");
}
