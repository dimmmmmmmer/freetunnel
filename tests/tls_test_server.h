// cppcheck-suppress-file missingIncludeSystem
#pragma once

// A small HTTPS/1.1 server on loopback for the subscription fetcher's tests,
// behind a TCP proxy of its own that reads the name each ClientHello sends (the
// SNI), which a QSslServer does not tell. Clients connect to the proxy's port.
// Every response closes its connection, so each request is a TLS handshake of
// its own. It offers HTTP/2 as well, where the backend lets a server, but never
// speaks it: a client that asked for it would get it, and nothing it can read.

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslServer>
#include <QStringList>
#include <QTcpServer>

class QSslSocket;
class QTcpSocket;

class TlsTestServer : public QObject {
    Q_OBJECT
public:
    using Header = QPair<QByteArray, QByteArray>;

    struct Response {
        int status = 200;
        QByteArray contentType = QByteArrayLiteral("application/json"); // empty: no Content-Type
        QList<Header> headers; // more, such as Location or Set-Cookie
        QByteArray body;
        bool chunked = false;  // no Content-Length, and the body in chunks
        // No Content-Length and no chunks: the body ends where the connection does,
        // as an endpoint sends a 401, 403, 404 or 405.
        bool lengthless = false;
        bool endless = false;  // chunked: the chunks, then never the last one
        bool holdBody = false; // the head with the body's length, then nothing
        int trickleMs = 0;     // the body a byte at a time, this far apart
        int delayMs = 0;       // the whole answer only this long after the request
        int cutAfter = -1;     // only this many bytes of the body, then close
        bool hangUp = false;   // close the connection without an answer
    };

    // What the proxy does once it has read a ClientHello.
    enum class Refusal {
        None,  // pass it on
        Close, // close the connection, as an endpoint does when its rules deny the client
        Alert, // answer with a fatal handshake_failure alert, then close
    };

    struct Request {
        QByteArray method;
        QByteArray target; // the path and any query
        QList<Header> headers;
        // The first header by this name, in any case; empty when there is none.
        QByteArray header(const QByteArray &name) const;
        bool hasHeader(const QByteArray &name) const;
    };

    explicit TlsTestServer(QObject *parent = nullptr);

    // Serves `chain`, the server's own certificate first, with its `key`, to
    // clients that connect to `address`.
    bool listen(const QList<QSslCertificate> &chain, const QSslKey &key,
                const QHostAddress &address = QHostAddress(QHostAddress::LocalHost));
    quint16 port() const { return m_front.serverPort(); }

    // What a request for `path` (without a query) gets; any other path gets 404.
    void setRoute(const QByteArray &path, const Response &response) { m_routes.insert(path, response); }
    void setRefusal(Refusal refusal) { m_refusal = refusal; }

    int connections() const { return m_connections; }
    // The SNI of each connection, in order: "" for one that sent none.
    const QStringList &serverNames() const { return m_serverNames; }
    // Handshakes the server finished. Secure Transport, as a client, finishes its
    // side before it judges the certificate, so this is no guide there.
    int handshakesDone() const { return m_handshakesDone; }
    // The protocol each finished handshake agreed on (ALPN), "" for none.
    const QStringList &protocols() const { return m_protocols; }
    const QList<Request> &requests() const { return m_requests; }

private:
    void onFrontConnection();
    void onTlsConnection();
    void onRequestData(QSslSocket *socket);
    void respond(QSslSocket *socket, const Response &response);
    static void sendBody(QSslSocket *socket, const Response &response);
    void noteClientHello(QTcpSocket *client, QTcpSocket *upstream);

    QHash<QByteArray, Response> m_routes;
    QHash<QTcpSocket *, QByteArray> m_hellos;   // what each client sent before its ClientHello was whole
    QHash<QSslSocket *, QByteArray> m_incoming; // each request until its head is whole
    QStringList m_serverNames;
    QStringList m_protocols;
    QList<Request> m_requests;
    int m_connections = 0;
    int m_handshakesDone = 0;
    Refusal m_refusal = Refusal::None;
    // Last, so they go first: a connection still open when this goes says it is
    // closed as its server takes it down, and the handlers above it must still
    // be there to hear it.
    QSslServer m_tls;
    QTcpServer m_front;
};
