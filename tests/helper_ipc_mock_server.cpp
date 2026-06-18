#include "helper_ipc_mock_server.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>

#include "vpn/vpn_helper_protocol.h"

MockHelperServer::MockHelperServer(const QString &token, QObject *parent)
    : QObject(parent)
    , m_token(token)
{
    m_server.setParent(this);
    connect(&m_server, &QLocalServer::newConnection, this, [this]() {
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
    m_name = QStringLiteral("ft-test-helper-%1-%2")
                     .arg(QCoreApplication::applicationPid())
                     .arg(QRandomGenerator::global()->generate(), 0, 16);
    QLocalServer::removeServer(m_name);
    return m_server.listen(m_name);
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

void MockHelperServer::adoptSocket(QLocalSocket *s)
{
    if (!s)
        return;
    ++m_connectionCount;
    if (m_sock) {
        s->close();
        s->deleteLater();
        return;
    }
    s->setParent(this);
    m_sock = s;
    connect(m_sock, &QLocalSocket::readyRead, this, [this]() { onReadyRead(); });
    connect(m_sock, &QLocalSocket::disconnected, this, [this]() {
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
    if (m_buf.size() > vpn_helper::kMaxReadBuffer) {
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
        send(QJsonObject{{"ev", "state"}, {"state", "Connecting"}});
        send(QJsonObject{{"ev", "state"}, {"state", "Connected"}});
        send(QJsonObject{{"ev", "stats"}, {"up", 1024.0}, {"down", 2048.0}});
    } else if (cmd == QLatin1String("disconnect")) {
        send(QJsonObject{{"ev", "state"}, {"state", "Disconnecting"}});
        send(QJsonObject{{"ev", "state"}, {"state", "Disconnected"}});
    } else if (cmd == QLatin1String("quit")) {
        emit quitRequested();
    }
}
