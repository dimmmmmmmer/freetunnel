// cppcheck-suppress-file missingIncludeSystem
#include "core/SubscriptionFetcher.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>

#include <algorithm>
#include <iterator>
#include <utility>

namespace freetunnel {
namespace {

// The whole fetch, redirects included. A reply of a few hundred bytes takes far
// less; this is for a server that accepts the connection and then stalls, or
// sends a byte now and then, which a timeout that restarts on progress, as Qt's
// transfer timeout does, would never end. So there is none of those as well:
// this one always comes first.
constexpr int kFetchTimeoutMs = 20000;

// The most same-origin redirects one fetch follows.
constexpr int kMaxRedirects = 3;

int fetchTimeoutMs()
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Test-only, as UpdateChecker's FT_UPDATE_TRANSFER_TIMEOUT_MS is: the deadline
    // waited out in milliseconds. Compiled out of a shipped build.
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("FT_SUBSCRIPTION_TIMEOUT_MS", &ok);
    if (ok && ms > 0)
        return ms;
#endif
    return kFetchTimeoutMs;
}

// A host as it names a server: in ACE form, in any case, as applySubscription()
// compares hostnames. One with no ACE form, such as an IP address, in lower case.
QString hostKey(const QString &host)
{
    const QByteArray ace = QUrl::toAce(host);
    return (ace.isEmpty() ? host : QString::fromLatin1(ace)).toLower();
}

// Each certificate certificatesDer() keeps that Qt can read. The DER comes as
// whole certificates one after another, so every step lands on the next one.
QList<QSslCertificate> pinsFrom(const QString &pem)
{
    const QByteArray der = certificatesDer(pem);
    QList<QSslCertificate> pins;
    int pos = 0;
    while (pos < der.size()) {
        const int end = derElementEnd(der, pos);
        if (end <= pos)
            break;
        const QSslCertificate certificate(der.mid(pos, end - pos), QSsl::Der);
        if (!certificate.isNull())
            pins.append(certificate);
        pos = end;
    }
    return pins;
}

// The config's first address, as the core connects to it: "host:port" or
// "[v6]:port". False when it is not one.
bool firstAddress(const QString &addresses, QString *host, int *port)
{
    const QString first = addresses.section(QLatin1Char(','), 0, 0).trimmed();
    if (!isValidEndpointAddress(first))
        return false;
    const qsizetype colon = first.lastIndexOf(QLatin1Char(':'));
    QString name = first.left(colon).trimmed();
    if (name.startsWith(QLatin1Char('[')) && name.endsWith(QLatin1Char(']')))
        name = name.mid(1, name.size() - 2);
    *host = name;
    *port = first.mid(colon + 1).toInt();
    return true;
}

// The errors that only say no CA the backend trusts vouches for the
// certificate. A pin vouches for it instead. Every backend takes the pins as its
// only CAs, and still reports one of these for some pinned certificates:
// schannel for a self-signed one and for one a CA signed, Secure Transport for a
// self-signed one, OpenSSL for one a CA signed. Nothing else is ever ignored:
// not a name that does not match, not a certificate out of date. Secure
// Transport reports that as untrusted too, so isPinned() checks the dates.
bool isChainTrustError(const QSslError &error)
{
    switch (error.error()) {
    case QSslError::SelfSignedCertificate:
    case QSslError::SelfSignedCertificateInChain:
    case QSslError::UnableToGetIssuerCertificate:
    case QSslError::UnableToGetLocalIssuerCertificate:
    case QSslError::UnableToVerifyFirstCertificate:
    case QSslError::CertificateUntrusted:
        return true;
    default:
        return false;
    }
}

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

SubscriptionFetchError statusError(int status)
{
    if (status == 401)
        return SubscriptionFetchError::Http401;
    if (status == 403)
        return SubscriptionFetchError::Http403;
    if (status == 404 || status == 405 || status == 407)
        return SubscriptionFetchError::HttpNoSubscription;
    if (status >= 500 && status <= 599)
        return SubscriptionFetchError::HttpServer;
    return SubscriptionFetchError::HttpOther;
}

// The media type before any parameter, in any case: "application/json;
// charset=utf-8" is JSON too.
bool isJson(const QByteArray &contentType)
{
    return contentType.split(';').constFirst().trimmed().toLower() == "application/json";
}

// What a reply with no HTTP status means. A connection closed during the TLS
// handshake, with no certificate at fault, is the server turning this client
// away: an endpoint does that before any HTTP when its rules deny the client.
// OpenSSL reports a reset there as RemoteHostClosedError, and an alert as
// SslHandshakeFailedError. Anything else is the network's.
SubscriptionFetchError transportError(QNetworkReply::NetworkError error, bool handshakeDone)
{
    const bool closed = error == QNetworkReply::RemoteHostClosedError
            || error == QNetworkReply::SslHandshakeFailedError;
    return closed && !handshakeDone ? SubscriptionFetchError::RefusedByServer : SubscriptionFetchError::Network;
}

// https, the same host in any case, and the same port, 443 when none is given.
bool isSameOrigin(const QUrl &url, const QUrl &origin)
{
    return url.scheme() == QLatin1String("https") && hostKey(url.host()) == hostKey(origin.host())
            && url.port(443) == origin.port(443);
}

// What one outcome means, kept in one row so the three never disagree: its code,
// when an automatic refresh tries again, and what the user is told.
struct Outcome {
    const char *code;
    SubscriptionRefreshPolicy policy;
    const char *text; // in the "SubscriptionFetcher" context; empty for success
};

using Policy = SubscriptionRefreshPolicy;
using Fetch = SubscriptionFetchError;
using Doc = SubscriptionDocError;

// What the user is told: few answers, since many outcomes mean the same to them.
constexpr const char *kBadAddress =
        QT_TRANSLATE_NOOP("SubscriptionFetcher", "The subscription address is not valid");
constexpr const char *kBadPin = QT_TRANSLATE_NOOP(
        "SubscriptionFetcher", "The config's certificate can't be read; ask your provider for a new link");
constexpr const char *kOffline = QT_TRANSLATE_NOOP("SubscriptionFetcher", "Couldn't update (offline?)");
constexpr const char *kRefused = QT_TRANSLATE_NOOP("SubscriptionFetcher", "The server refused the connection");
constexpr const char *kUntrusted =
        QT_TRANSLATE_NOOP("SubscriptionFetcher", "Certificate not trusted; ask your provider for a new link");
constexpr const char *kDenied =
        QT_TRANSLATE_NOOP("SubscriptionFetcher", "Access denied; import a new link from your provider");
constexpr const char *kNoUpdates = QT_TRANSLATE_NOOP("SubscriptionFetcher", "This server doesn't offer updates now");
constexpr const char *kInvalid = QT_TRANSLATE_NOOP("SubscriptionFetcher", "The server sent an invalid update");
constexpr const char *kNewerFormat =
        QT_TRANSLATE_NOOP("SubscriptionFetcher", "Update FreeTunnel to read this subscription");
constexpr const char *kNewerCore = QT_TRANSLATE_NOOP(
        "SubscriptionFetcher", "This subscription needs a newer TrustTunnel core than this version of FreeTunnel uses");

// A 403 is the operator turning updates off: nothing automatic tries again
// until the user does. A 404 may be a subscription not set up yet, so it is asked
// again weekly. What only a new link or a new FreeTunnel can fix waits for the
// user; what may pass, such as a lost network, backs off.
constexpr std::pair<Fetch, Outcome> kFetchOutcomes[] = {
    {Fetch::None, {"ok", Policy::Normal, ""}},
    {Fetch::BadUrl, {"bad_url", Policy::ManualOnly, kBadAddress}},
    {Fetch::BadPin, {"bad_pin", Policy::ManualOnly, kBadPin}},
    {Fetch::Network, {"network", Policy::Backoff, kOffline}},
    {Fetch::Timeout, {"timeout", Policy::Backoff, kOffline}},
    {Fetch::RefusedByServer, {"refused", Policy::Backoff, kRefused}},
    {Fetch::CertificateUntrusted, {"untrusted", Policy::ManualOnly, kUntrusted}},
    {Fetch::BadRedirect, {"bad_redirect", Policy::ManualOnly, kInvalid}},
    {Fetch::Http401, {"http_401", Policy::ManualOnly, kDenied}},
    {Fetch::Http403, {"http_403", Policy::ManualOnly, kNoUpdates}},
    {Fetch::HttpNoSubscription, {"no_subscription", Policy::Weekly, kNoUpdates}},
    {Fetch::HttpServer, {"http_server", Policy::Backoff, kOffline}},
    {Fetch::HttpOther, {"http_other", Policy::Backoff, kInvalid}},
    {Fetch::BadContentType, {"bad_content_type", Policy::Backoff, kInvalid}},
    {Fetch::TooLarge, {"too_large", Policy::Backoff, kInvalid}},
};

// A reply the parser refused (Fetch::Document), by why.
constexpr std::pair<Doc, Outcome> kDocumentOutcomes[] = {
    {Doc::Invalid, {"invalid_document", Policy::Backoff, kInvalid}},
    {Doc::TooLarge, {"too_large", Policy::Backoff, kInvalid}},
    {Doc::UnsupportedVersion, {"newer_version", Policy::ManualOnly, kNewerFormat}},
    {Doc::SkipVerification, {"skip_verification", Policy::ManualOnly, kInvalid}},
    {Doc::NeedsNewerCore, {"needs_newer_core", Policy::ManualOnly, kNewerCore}},
};

template <typename Key, std::size_t N>
const Outcome *findOutcome(const std::pair<Key, Outcome> (&table)[N], Key key)
{
    const auto row =
            std::find_if(std::cbegin(table), std::cend(table), [key](const auto &r) { return r.first == key; });
    return row == std::cend(table) ? nullptr : &row->second;
}

// Every member has a row; the fallback is for one added without its row, which
// the tests catch by its code.
const Outcome &outcomeFor(SubscriptionFetchError error, SubscriptionDocError documentError)
{
    static constexpr Outcome kUnknown{"unknown", Policy::Backoff, ""};
    const Outcome *outcome = error == Fetch::Document ? findOutcome(kDocumentOutcomes, documentError)
                                                      : findOutcome(kFetchOutcomes, error);
    return outcome ? *outcome : kUnknown;
}

} // namespace

SubscriptionFetchRequest subscriptionRequestFor(const QString &url, const ConfigToml &trustSource)
{
    SubscriptionFetchRequest request;
    request.url = url;
    const QString host = QUrl(url, QUrl::StrictMode).host();
    if (trustSource.certificate.trimmed().isEmpty() || host.isEmpty()
            || hostKey(host) != hostKey(trustSource.hostname))
        return request;
    request.pinned = true;
    request.pins = pinsFrom(trustSource.certificate);
    request.peerVerifyName = trustSource.hostname;
    // Without an address the core could use, the address's own host: still pinned.
    if (!firstAddress(trustSource.addresses, &request.connectHost, &request.connectPort)) {
        request.connectHost.clear();
        request.connectPort = -1;
    }
    return request;
}

SubscriptionRefreshPolicy refreshPolicyFor(SubscriptionFetchError error, SubscriptionDocError documentError)
{
    return outcomeFor(error, documentError).policy;
}

QString subscriptionFailureText(SubscriptionFetchError error, SubscriptionDocError documentError)
{
    const char *text = outcomeFor(error, documentError).text;
    return *text ? QCoreApplication::translate("SubscriptionFetcher", text) : QString();
}

QString subscriptionFetchCode(SubscriptionFetchError error, SubscriptionDocError documentError)
{
    return QString::fromLatin1(outcomeFor(error, documentError).code);
}

SubscriptionFetcher::SubscriptionFetcher(const QString &appVersion, QObject *parent)
    : QObject(parent)
    , m_appVersion(appVersion)
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this]() {
        if (m_reply)
            cancel(m_reply, &m_timedOut);
    });
}

SubscriptionFetcher::~SubscriptionFetcher()
{
    stop();
}

void SubscriptionFetcher::start(const SubscriptionFetchRequest &request)
{
    stop();
    ++m_generation;
    m_request = request;
    m_result = SubscriptionFetchResult();
    m_result.pinned = request.pinned;
    m_redirects = 0;
    m_timedOut = m_tooLarge = m_untrusted = m_handshakeDone = false;

    const std::optional<SubscriptionUrl> url = parseSubscriptionUrl(request.url);
    m_result.origin = redactUrl(url ? url->requestUrl : QUrl(request.url, QUrl::StrictMode));
    if (!url)
        return finish(SubscriptionFetchError::BadUrl);
    if (request.pinned && request.pins.isEmpty())
        return finish(SubscriptionFetchError::BadPin);
    m_authorization = url->authorization;
    m_url = url->requestUrl;
    m_seen = {m_url};

    // A manager of its own, gone when the fetch ends: no connection, session or
    // credential outlives it. No proxy, as the core uses none: a proxy would see
    // the request, and one that is down would stop every refresh.
    m_nam = new QNetworkAccessManager(this);
    m_nam->setProxy(QNetworkProxy::NoProxy);
    m_nam->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
    m_deadline.start(fetchTimeoutMs());
    sendRequest();
}

void SubscriptionFetcher::abort()
{
    stop();
    ++m_generation;
}

void SubscriptionFetcher::stop()
{
    m_deadline.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply = nullptr;
    }
    if (m_nam) {
        m_nam->deleteLater();
        m_nam = nullptr;
    }
}

// The credentials are sent as a header of our own, never as a URL's userinfo,
// which Qt would cache and offer again to whatever a redirect leads to. Nothing
// is taken from or kept in a cache or a cookie jar. Accept-Encoding: identity,
// or Qt would ask for a compressed body and inflate it past the cap unseen.
// Each request starts with no handshake seen and no status of its own: a
// redirect is followed on a new connection when the server closed the last one.
void SubscriptionFetcher::sendRequest()
{
    QUrl target = m_url;
    if (!m_request.connectHost.isEmpty()) {
        target.setHost(m_request.connectHost);
        target.setPort(m_request.connectPort);
    }
    QNetworkRequest request(target);
    if (!m_authorization.isEmpty())
        request.setRawHeader("Authorization", m_authorization);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_appVersion));
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    // Connected to an address, the request still names the server: the TLS
    // name and the SNI through the peer verify name, and the Host header,
    // which only HTTP/1.1 takes as given.
    if (!m_request.peerVerifyName.isEmpty())
        request.setPeerVerifyName(m_request.peerVerifyName);
    if (!m_request.connectHost.isEmpty())
        request.setRawHeader("Host", m_url.authority(QUrl::FullyEncoded).toLatin1());
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    if (m_request.pinned)
        tls.setCaCertificates(m_request.pins);
    request.setSslConfiguration(tls);

    m_body.clear();
    m_handshakeDone = false;
    m_result.httpStatus = 0;
    m_reply = m_nam->get(request);
    watch(m_reply);
}

void SubscriptionFetcher::watch(QNetworkReply *reply)
{
    connect(reply, &QNetworkReply::sslErrors, this,
            [this, reply](const QList<QSslError> &errors) { onSslErrors(reply, errors); });
    connect(reply, &QNetworkReply::encrypted, this, [this, reply]() { onEncrypted(reply); });
    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply]() { onMetaDataChanged(reply); });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { onReadyRead(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onFinished(reply); });
}

// The server's own certificate is one of the pins, and valid now: Secure
// Transport calls one out of date only untrusted, which a pin would answer.
bool SubscriptionFetcher::isPinned(const QSslCertificate &peer) const
{
    const QByteArray der = peer.toDer();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    return !der.isEmpty() && peer.effectiveDate() <= now && now <= peer.expiryDate()
            && std::any_of(m_request.pins.cbegin(), m_request.pins.cend(),
                           [&der](const QSslCertificate &pin) { return pin.toDer() == der; });
}

// Without pins, the system's CAs decide and nothing is ignored. With them, the
// errors that only say no CA vouches for the server's own certificate are
// ignored when that certificate is a pin, since the pin does.
void SubscriptionFetcher::onSslErrors(QNetworkReply *reply, const QList<QSslError> &errors)
{
    if (m_request.pinned && std::all_of(errors.cbegin(), errors.cend(), isChainTrustError)
            && isPinned(reply->sslConfiguration().peerCertificate()))
        reply->ignoreSslErrors(errors);
    else
        m_untrusted = true;
}

// Before the request goes out: in pinned mode the server's own certificate has
// to be a pin, whatever the backend made of the CA list. A CA that signed it, a
// pin or one the system trusts, is not enough.
void SubscriptionFetcher::onEncrypted(QNetworkReply *reply)
{
    m_handshakeDone = true;
    if (m_request.pinned && !isPinned(reply->sslConfiguration().peerCertificate()))
        cancel(reply, &m_untrusted);
}

// A body announced as too large is refused before it arrives.
void SubscriptionFetcher::onMetaDataChanged(QNetworkReply *reply)
{
    const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
    if (length.isValid() && length.toLongLong() > kMaxSubscriptionBodyBytes)
        cancel(reply, &m_tooLarge);
}

void SubscriptionFetcher::onReadyRead(QNetworkReply *reply)
{
    if (!readBounded(reply))
        cancel(reply, &m_tooLarge);
}

// Never more than one byte past the cap, so a body sent without a length, or
// with a false one, is refused as it passes it, and never held whole.
bool SubscriptionFetcher::readBounded(QNetworkReply *reply)
{
    m_body += reply->read(kMaxSubscriptionBodyBytes + 1 - m_body.size());
    return m_body.size() <= kMaxSubscriptionBodyBytes;
}

// Aborting the reply finishes it at once, so `reply` and this fetch are done
// with once this returns.
void SubscriptionFetcher::cancel(QNetworkReply *reply, bool *flag)
{
    *flag = true;
    reply->abort();
}

// The reply may outlive the fetch for a while, and Qt can still pass on what
// its connection does, such as a handshake that ends after the abort: that is no
// longer this fetch's, so nothing of it is heard.
void SubscriptionFetcher::onFinished(QNetworkReply *reply)
{
    reply->disconnect(this);
    m_reply = nullptr;
    reply->deleteLater();
    // What readyRead had not taken yet. A reply this fetch aborted is closed.
    if (!m_tooLarge && reply->isOpen() && !readBounded(reply))
        m_tooLarge = true;
    if (m_tooLarge)
        return finish(SubscriptionFetchError::TooLarge);
    if (m_untrusted)
        return finish(SubscriptionFetchError::CertificateUntrusted);
    if (m_timedOut)
        return finish(SubscriptionFetchError::Timeout);
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid())
        return finish(transportError(reply->error(), m_handshakeDone));
    onHttpReply(reply, status.toInt());
}

void SubscriptionFetcher::onHttpReply(QNetworkReply *reply, int status)
{
    m_result.httpStatus = status;
    if (isRedirect(status) && reply->hasRawHeader("Location"))
        return followRedirect(reply);
    if (status != 200)
        return finish(statusError(status));
    // A 200 whose body was cut short.
    if (reply->error() != QNetworkReply::NoError)
        return finish(SubscriptionFetchError::Network);
    if (!isJson(reply->rawHeader("Content-Type")))
        return finish(SubscriptionFetchError::BadContentType);
    onDocument(reply);
}

void SubscriptionFetcher::onDocument(QNetworkReply *reply)
{
    SubscriptionParseError error;
    m_result.document = parseSubscriptionJson(m_body, &error);
    if (!m_result.document) {
        m_result.documentError = error;
        return finish(SubscriptionFetchError::Document);
    }
    m_result.hint = subscriptionRefreshHint(QJsonDocument::fromJson(m_body).object(),
                                            reply->rawHeader("profile-update-interval"));
    finish(SubscriptionFetchError::None);
}

// Only within the subscription's own origin, compared on the address with the
// hostname in it, never on the IP a pinned fetch connects to: the credentials
// go with every request, and no other server may see them. A pinned fetch stays
// pinned. An address asked for before is a loop.
void SubscriptionFetcher::followRedirect(QNetworkReply *reply)
{
    const QUrl location = QUrl::fromEncoded(reply->rawHeader("Location"), QUrl::StrictMode);
    const QUrl next = m_url.resolved(location).adjusted(QUrl::RemoveFragment);
    if (m_redirects >= kMaxRedirects || !location.isValid() || !isSameOrigin(next, m_seen.constFirst())
            || !next.userInfo().isEmpty() || m_seen.contains(next))
        return finish(SubscriptionFetchError::BadRedirect);
    ++m_redirects;
    m_seen.append(next);
    m_url = next;
    sendRequest();
}

// One line per fetch, with nothing in it the address hides: the origin, what
// came of it, the status and, for a refused document, the field's name, which
// is never its value. Qt's own error text is never logged: it holds the whole
// request URL, path and query included.
void SubscriptionFetcher::finish(SubscriptionFetchError error)
{
    m_result.error = error;
    stop();
    QString line = QStringLiteral("SubscriptionFetcher: %1: %2")
                           .arg(m_result.origin.isEmpty() ? QStringLiteral("-") : m_result.origin,
                                subscriptionFetchCode(error, m_result.documentError.kind));
    if (m_result.httpStatus != 0)
        line += QStringLiteral(", HTTP %1").arg(m_result.httpStatus);
    if (error == SubscriptionFetchError::Document && !m_result.documentError.field.isEmpty())
        line += QStringLiteral(", field %1").arg(m_result.documentError.field);
    if (error == SubscriptionFetchError::None)
        qInfo("%s", qPrintable(line));
    else
        qWarning("%s", qPrintable(line));
    QMetaObject::invokeMethod(
            this,
            [this, result = m_result, generation = m_generation]() {
                if (generation == m_generation)
                    emit finished(result);
            },
            Qt::QueuedConnection);
}

} // namespace freetunnel
