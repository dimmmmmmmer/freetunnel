// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Fetching a TrustTunnel subscription document (endpoint 1.3.0 and later) over
// HTTPS, and what a failed fetch means for the next one. Nothing in the app
// starts a fetch yet: the code that asks the user first, stores the address and
// applies the reply comes later.

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSslCertificate>
#include <QSslError>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <optional>

#include "core/ConfigToml.h"
#include "core/Subscription.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace freetunnel {

// Why a fetch gave no document. Each is a different answer to the user, and
// decides when an automatic refresh tries again (refreshPolicyFor).
enum class SubscriptionFetchError {
    None,
    BadUrl,               // not a subscription address (parseSubscriptionUrl)
    BadPin,               // the config pins certificates, and none of them can be read
    Network,              // no connection, or one lost after the TLS handshake
    Timeout,              // no whole answer within the deadline, redirects included
    RefusedByServer,      // closed during the TLS handshake, as an endpoint's rules turn a client away
    CertificateUntrusted, // not the pinned certificate, or one the system does not trust
    BadRedirect,          // to another origin, to http, with a userinfo, in a loop, or a fourth one
    Http401,              // the credentials are refused
    Http403,              // the server has turned updates off
    HttpNoSubscription,   // 404, 405 or 407: no subscription here
    HttpServer,           // 5xx
    HttpOther,            // any other status
    BadContentType,       // a 200 that is not application/json
    TooLarge,             // a body over kMaxSubscriptionBodyBytes
    Document,             // a reply parseSubscriptionJson() refused; see documentError
};

struct SubscriptionFetchResult {
    SubscriptionFetchError error = SubscriptionFetchError::None;
    int httpStatus = 0; // of the last request's reply; 0 when it got none
    std::optional<SubscriptionDocument> document;
    SubscriptionParseError documentError; // for Document
    std::optional<qint64> hint;           // the operator's refresh hint (subscriptionRefreshHint)
    QString origin;                       // redactUrl() of the address, for logs and errors
    bool pinned = false;
};

// What one fetch needs besides the address: what it trusts, and where it
// connects. Without pins, the system's CAs, and the address's own host.
struct SubscriptionFetchRequest {
    QString url; // with its userinfo, which never reaches QNetworkAccessManager
    bool pinned = false;
    QList<QSslCertificate> pins;
    QString peerVerifyName; // the name the certificate has to carry, and the SNI
    QString connectHost;    // empty: the address's own host and port
    int connectPort = -1;
};

// A fetch for `url` with what `trustSource`, the config it came with or updates,
// trusts. When the address's host is the config's hostname (in ACE form, in any
// case) and the config pins a certificate, the fetch trusts that certificate
// alone and connects to the config's first address, as the core does and as
// TrustTunnel's own client fetches: the endpoint's name need not resolve, and its
// certificate is often self-signed. The pins are the certificates every other path
// keeps (certificatesDer); a config whose certificate holds none that Qt can read
// gives BadPin, never the system's CAs. Otherwise the system's CAs, and DNS. The
// config's skip_verification never weakens a fetch.
SubscriptionFetchRequest subscriptionRequestFor(const QString &url, const ConfigToml &trustSource);

// When an automatic refresh tries again after a fetch ends this way.
enum class SubscriptionRefreshPolicy {
    Normal,     // after the refresh interval
    Backoff,    // sooner at first, then after the interval
    Weekly,     // in a week
    ManualOnly, // not until the user asks for one that then succeeds
};

SubscriptionRefreshPolicy refreshPolicyFor(SubscriptionFetchError error, SubscriptionDocError documentError);

// What to tell the user, translated; empty for None.
QString subscriptionFailureText(SubscriptionFetchError error, SubscriptionDocError documentError);

// A short code that names the outcome, the same in every language: for logs,
// and for storing what the last fetch gave.
QString subscriptionFetchCode(SubscriptionFetchError error, SubscriptionDocError documentError);

// One fetch at a time. The result always comes through finished(), from the
// event loop, never from inside start(). Each fetch has a manager of its own,
// with no proxy, cache, cookies or stored credentials, and nothing of it is kept
// once the fetch ends. Over HTTP/1.1, as TrustTunnel's own client fetches: in
// pinned mode the connection goes to an IP address, and HTTP/2 would name that
// address where HTTP/1.1 sends the Host header with the real name.
class SubscriptionFetcher : public QObject {
    Q_OBJECT
public:
    explicit SubscriptionFetcher(const QString &appVersion, QObject *parent = nullptr);
    ~SubscriptionFetcher() override;

    // Starts a fetch, ending any fetch already running without its finished().
    virtual void start(const SubscriptionFetchRequest &request);
    // Ends the fetch in flight, if any. Its finished() never comes.
    virtual void abort();
    bool isRunning() const { return m_nam != nullptr; }

signals:
    void finished(const freetunnel::SubscriptionFetchResult &result);

private:
    void stop();
    void sendRequest();
    void watch(QNetworkReply *reply);
    void onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors);
    void onEncrypted(QNetworkReply *reply);
    void onMetaDataChanged(QNetworkReply *reply);
    void onReadyRead(QNetworkReply *reply);
    void onFinished(QNetworkReply *reply);
    void onHttpReply(QNetworkReply *reply, int status);
    void onDocument(QNetworkReply *reply);
    void followRedirect(QNetworkReply *reply);
    bool isPinned(const QSslCertificate &peer) const;
    bool readBounded(QNetworkReply *reply);
    void cancel(QNetworkReply *reply, bool *flag);
    void finish(SubscriptionFetchError error);

    QString m_appVersion;
    SubscriptionFetchRequest m_request;
    SubscriptionFetchResult m_result;
    QByteArray m_authorization;
    QUrl m_url;           // the address of this hop, with its own host: what a redirect resolves against
    QList<QUrl> m_seen;   // every address asked for, to refuse a loop
    int m_redirects = 0;
    QByteArray m_body;
    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer m_deadline;
    quint64 m_generation = 0; // which start() a queued finished() belongs to
    bool m_timedOut = false;
    bool m_tooLarge = false;
    bool m_untrusted = false;
    bool m_handshakeDone = false;
};

} // namespace freetunnel
