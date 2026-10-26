// cppcheck-suppress-file missingIncludeSystem
#include "tls_test_server.h"

#include <QNetworkProxy>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <optional>

namespace {

int be16(const QByteArray &bytes, qsizetype at)
{
    return (uchar(bytes.at(at)) << 8) | uchar(bytes.at(at + 1));
}

// The host_name of a ClientHello's server_name extension, "" when it sends none,
// or nullopt until the whole first TLS record has arrived. Only as much of the
// format as a ClientHello from Qt needs, and never a read past the record.
std::optional<QString> serverNameIn(const QByteArray &bytes)
{
    if (bytes.size() < 5)
        return std::nullopt;
    const qsizetype recordEnd = 5 + be16(bytes, 3);
    if (bytes.size() < recordEnd)
        return std::nullopt;
    // The record's header, the handshake's, the version and the random.
    qsizetype at = 5 + 4 + 2 + 32;
    if (uchar(bytes.at(0)) != 0x16 || at >= recordEnd)
        return QString();
    const auto skip = [&bytes, &at, recordEnd](int lengthBytes) {
        if (at + lengthBytes > recordEnd) {
            at = recordEnd;
            return;
        }
        at += lengthBytes + (lengthBytes == 1 ? uchar(bytes.at(at)) : be16(bytes, at));
    };
    skip(1); // the session id
    skip(2); // the cipher suites
    skip(1); // the compression methods
    if (at + 2 > recordEnd)
        return QString();
    const qsizetype extensionsEnd = qMin(recordEnd, at + 2 + be16(bytes, at));
    at += 2;
    while (at + 4 <= extensionsEnd) {
        const int type = be16(bytes, at);
        const int length = be16(bytes, at + 2);
        at += 4;
        // The list's length, the name's type and the name's length, then the name.
        if (type == 0 && at + 5 <= extensionsEnd)
            return QString::fromLatin1(bytes.mid(at + 5, qMin<qsizetype>(be16(bytes, at + 3), extensionsEnd - at - 5)));
        at += length;
    }
    return QString();
}

} // namespace

QByteArray TlsTestServer::Request::header(const QByteArray &name) const
{
    for (const Header &h : headers) {
        if (h.first.compare(name, Qt::CaseInsensitive) == 0)
            return h.second;
    }
    return QByteArray();
}

bool TlsTestServer::Request::hasHeader(const QByteArray &name) const
{
    return std::any_of(headers.cbegin(), headers.cend(),
                       [&name](const Header &h) { return h.first.compare(name, Qt::CaseInsensitive) == 0; });
}

TlsTestServer::TlsTestServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_front, &QTcpServer::newConnection, this, &TlsTestServer::onFrontConnection);
    connect(&m_tls, &QSslServer::pendingConnectionAvailable, this, &TlsTestServer::onTlsConnection);
}

bool TlsTestServer::listen(const QList<QSslCertificate> &chain, const QSslKey &key, const QHostAddress &address)
{
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setLocalCertificateChain(chain);
    tls.setPrivateKey(key);
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    tls.setAllowedNextProtocols({QSslConfiguration::ALPNProtocolHTTP2, QSslConfiguration::NextProtocolHttp1_1});
    m_tls.setSslConfiguration(tls);
    // Never through a proxy a test sets for the whole application.
    m_tls.setProxy(QNetworkProxy::NoProxy);
    m_front.setProxy(QNetworkProxy::NoProxy);
    return m_tls.listen(QHostAddress::LocalHost) && m_front.listen(address);
}

void TlsTestServer::onFrontConnection()
{
    while (QTcpSocket *client = m_front.nextPendingConnection()) {
        ++m_connections;
        QTcpSocket *upstream = nullptr;
        if (m_refusal == Refusal::None) {
            // Written to before it connects, which it sends once it has.
            upstream = new QTcpSocket(client);
            upstream->setProxy(QNetworkProxy::NoProxy);
            connect(upstream, &QTcpSocket::readyRead, client,
                    [client, upstream]() { client->write(upstream->readAll()); });
            connect(upstream, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
            upstream->connectToHost(QHostAddress::LocalHost, m_tls.serverPort());
        }
        connect(client, &QTcpSocket::readyRead, this,
                [this, client, upstream]() { noteClientHello(client, upstream); });
        connect(client, &QTcpSocket::disconnected, this, [this, client]() {
            m_hellos.remove(client);
            client->deleteLater();
        });
    }
}

void TlsTestServer::noteClientHello(QTcpSocket *client, QTcpSocket *upstream)
{
    const QByteArray data = client->readAll();
    if (!client->property("helloNoted").toBool()) {
        const std::optional<QString> name = serverNameIn(m_hellos[client] += data);
        if (name) {
            client->setProperty("helloNoted", true);
            m_hellos.remove(client);
            m_serverNames.append(*name);
            if (m_refusal != Refusal::None) {
                // A TLS 1.2 record: alert, fatal, handshake_failure.
                if (m_refusal == Refusal::Alert)
                    client->write(QByteArray::fromHex("15030300020228"));
                client->disconnectFromHost();
                return;
            }
        }
    }
    if (upstream)
        upstream->write(data);
}

void TlsTestServer::onTlsConnection()
{
    while (auto *socket = qobject_cast<QSslSocket *>(m_tls.nextPendingConnection())) {
        ++m_handshakesDone;
        m_protocols.append(QString::fromLatin1(socket->sslConfiguration().nextNegotiatedProtocol()));
        connect(socket, &QSslSocket::readyRead, this, [this, socket]() { onRequestData(socket); });
        connect(socket, &QSslSocket::disconnected, this, [this, socket]() {
            m_incoming.remove(socket);
            socket->deleteLater();
        });
        if (socket->bytesAvailable() > 0)
            onRequestData(socket);
    }
}

void TlsTestServer::onRequestData(QSslSocket *socket)
{
    QByteArray &incoming = m_incoming[socket];
    incoming += socket->readAll();
    const qsizetype headEnd = incoming.indexOf("\r\n\r\n");
    if (headEnd < 0 || socket->property("answered").toBool())
        return;
    socket->setProperty("answered", true);
    const QList<QByteArray> lines = incoming.left(headEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    Request request;
    request.method = requestLine.value(0);
    request.target = requestLine.value(1);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon > 0)
            request.headers.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
    }
    m_requests.append(request);
    const qsizetype query = request.target.indexOf('?');
    const QByteArray path = query < 0 ? request.target : request.target.left(query);
    Response notFound;
    notFound.status = 404;
    notFound.contentType.clear();
    respond(socket, m_routes.value(path, notFound));
}

void TlsTestServer::respond(QSslSocket *socket, const Response &response)
{
    if (response.hangUp) {
        socket->disconnectFromHost();
        return;
    }
    QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status)
            + (response.status == 200 ? " OK\r\n" : " Status\r\n");
    if (!response.contentType.isEmpty())
        head += "Content-Type: " + response.contentType + "\r\n";
    for (const Header &h : response.headers)
        head += h.first + ": " + h.second + "\r\n";
    head += response.chunked ? QByteArray("Transfer-Encoding: chunked\r\n")
                             : "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    socket->write(head + "Connection: close\r\n\r\n");
    if (response.holdBody)
        return;
    if (response.trickleMs > 0) {
        auto *timer = new QTimer(socket);
        auto sent = std::make_shared<qsizetype>(0);
        connect(timer, &QTimer::timeout, socket, [socket, timer, body = response.body, sent]() {
            if (*sent >= body.size()) {
                timer->stop();
                socket->disconnectFromHost();
                return;
            }
            socket->write(body.mid((*sent)++, 1));
        });
        timer->start(response.trickleMs);
        return;
    }
    sendBody(socket, response);
}

void TlsTestServer::sendBody(QSslSocket *socket, const Response &response)
{
    if (response.cutAfter >= 0) {
        socket->write(response.body.left(response.cutAfter));
    } else if (response.chunked) {
        for (qsizetype at = 0; at < response.body.size(); at += 4096) {
            const QByteArray chunk = response.body.mid(at, 4096);
            socket->write(QByteArray::number(chunk.size(), 16) + "\r\n" + chunk + "\r\n");
        }
        if (response.endless)
            return;
        socket->write("0\r\n\r\n");
    } else {
        socket->write(response.body);
    }
    socket->disconnectFromHost();
}
