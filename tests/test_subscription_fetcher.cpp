// cppcheck-suppress-file missingIncludeSystem
// The subscription fetcher against a TLS server of the test's own, on the TLS
// backend FT_TEST_TLS_BACKEND names: tests/CMakeLists.txt registers this once
// for each. The backends differ most in what they make of a pinned certificate,
// and Windows users get schannel, which the CI runner's Qt does not pick by
// itself since it has OpenSSL as well. Windows CI shows none of Qt's messages,
// so every check says in its own words what the fetch gave, and on which
// backend.
#include <QtTest>

#include <QElapsedTimer>
#include <QNetworkInterface>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTcpServer>

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

#include "core/ConfigToml.h"
#include "core/Subscription.h"
#include "core/SubscriptionFetcher.h"
#include "tls_test_certs.h"
#include "tls_test_server.h"

using namespace freetunnel;

Q_DECLARE_METATYPE(freetunnel::SubscriptionFetchError)
Q_DECLARE_METATYPE(freetunnel::SubscriptionDocError)
Q_DECLARE_METATYPE(TlsTestServer::Refusal)

namespace {

// A document as the endpoint serves one, less the fields it fixes.
const QByteArray kDocument(
        R"({"version":1,"hostname":"vpn.test","address":"127.0.0.1:443","username":"alice","password":"s3cret"})");

const QString kUserInfo = QStringLiteral("alice:s3cret@");

// The backend this system's users get, whose tests may never be skipped.
QString nativeBackend()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("schannel");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("securetransport");
#else
    return QStringLiteral("openssl");
#endif
}

TlsTestServer::Response json(const QByteArray &body = kDocument)
{
    TlsTestServer::Response response;
    response.body = body;
    return response;
}

TlsTestServer::Response status(int code, const QList<TlsTestServer::Header> &headers = {})
{
    TlsTestServer::Response response;
    response.status = code;
    response.contentType.clear();
    response.headers = headers;
    return response;
}

TlsTestServer::Response redirect(int code, const QByteArray &location)
{
    return status(code, {{"Location", location}});
}

QString pemOf(std::initializer_list<const TlsTestIdentity *> identities)
{
    QByteArray pem;
    for (const TlsTestIdentity *identity : identities)
        pem += identity->pem;
    return QString::fromLatin1(pem);
}

bool listen(TlsTestServer &server, const TlsTestIdentity &identity, const TlsTestIdentity *issuer = nullptr)
{
    QList<QSslCertificate> chain{identity.certificate};
    if (issuer)
        chain.append(issuer->certificate);
    return server.listen(chain, identity.key);
}

QByteArray withPort(QByteArray text, quint16 port, quint16 otherPort = 0)
{
    return text.replace("{port}", QByteArray::number(port)).replace("{port2}", QByteArray::number(otherPort));
}

QStringList *g_captured = nullptr;

void capture(QtMsgType, const QMessageLogContext &, const QString &message)
{
    g_captured->append(message);
}

} // namespace

// The fetch ended as `expected`, or the check says how it did end.
#define QVERIFY_FETCH(result, expected)                                                                      \
    QVERIFY2((result) && (result)->error == (expected),                                                      \
             qPrintable(QStringLiteral("expected %1, got %2")                                                \
                                .arg(subscriptionFetchCode((expected), SubscriptionDocError::Invalid),       \
                                     describe(result))))

class TestSubscriptionFetcher : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    // What a fetch trusts, and where it connects.
    void aPinnedRequestNeedsTheConfigsHostnameAndACertificate();
    void aPinnedRequestConnectsToTheFirstAddress_data();
    void aPinnedRequestConnectsToTheFirstAddress();
    void anUnreadablePinIsBadPinNotTheSystemsCas_data();
    void anUnreadablePinIsBadPinNotTheSystemsCas();

    // Trust.
    void aPinnedFetchReachesANameThatDoesNotResolve();
    void aWrongPinIsRefusedBeforeTheRequest();
    void aPinnedCertificateThatACaSignedIsTaken_data();
    void aPinnedCertificateThatACaSignedIsTaken();
    void aPinnedChainIsTaken();
    void pinningOnlyTheCaIsNotEnough();
    void aCaTheSystemTrustsDoesNotStandInForThePin();
    void aPinnedCertificateForAnotherNameIsRefused();
    void aPinnedCertificateOutOfDateIsRefused_data();
    void aPinnedCertificateOutOfDateIsRefused();
    void withoutAPinASelfSignedCertificateIsRefused();
    void withoutAPinTheSystemsCasDecide();
    void skipVerificationNeverWeakensAFetch();
    void onlyHttpsIsFetched();

    // Redirects.
    void aSameOriginRedirectIsFollowed_data();
    void aSameOriginRedirectIsFollowed();
    void aRedirectElsewhereIsRefused_data();
    void aRedirectElsewhereIsRefused();
    void aRedirectLoopIsRefused();
    void threeRedirectsAreTheMost();
    void theDefaultPortWrittenOutIsTheSameOrigin();
    void noCookieGoesBack();

    // The reply.
    void theCapHoldsAtItsEdge_data();
    void theCapHoldsAtItsEdge();
    void anAnnouncedBodyOverTheCapIsRefusedBeforeItArrives();
    void aBodyThatKeepsComingIsRefusedAtTheCap();
    void aReplyCutShortIsANetworkError();
    void onlyJsonIsTaken_data();
    void onlyJsonIsTaken();
    void eachStatusMeansSomething_data();
    void eachStatusMeansSomething();
    void aDocumentTheParserRefusesSaysWhy_data();
    void aDocumentTheParserRefusesSaysWhy();

    // Failures, and what is never done.
    void aTrickleHitsTheDeadline();
    void aHandshakeTheServerEndsIsRefusedByServer_data();
    void aHandshakeTheServerEndsIsRefusedByServer();
    void aServerThatHangsUpIsANetworkError();
    void nothingListeningIsANetworkError();
    void noLogLineHoldsTheCredentials();
    void anApplicationProxyIsNotUsed();
    void aResultNeverComesFromInsideStart();
    void anAbortedFetchNeverFinishes();
    void everyOutcomeHasACodeAPolicyAndWords();

private:
    // A pinned fetch of https://<userInfo>vpn.test:<port><path>, as a config for
    // vpn.test at 127.0.0.1:<port> that pins `pins` makes one.
    static SubscriptionFetchRequest pinned(quint16 port, const QString &pins,
                                           const QString &path = QStringLiteral("/sub"),
                                           const QString &userInfo = kUserInfo);
    std::optional<SubscriptionFetchResult> fetch(const SubscriptionFetchRequest &request);
    QString describe(const std::optional<SubscriptionFetchResult> &result) const;
    void trustTheTestCa();

    QString m_backend;
    // Made in initTestCase(), once the backend is set: a certificate, a key or a
    // configuration, even an empty one, makes Qt settle on its default backend
    // for good, and on the Windows runner that is OpenSSL.
    std::unique_ptr<TlsTestCerts> m_certs;
    std::optional<QSslConfiguration> m_defaultTls;
};

SubscriptionFetchRequest TestSubscriptionFetcher::pinned(quint16 port, const QString &pins, const QString &path,
                                                         const QString &userInfo)
{
    ConfigToml config;
    config.hostname = QStringLiteral("vpn.test");
    config.addresses = QStringLiteral("127.0.0.1:") + QString::number(port);
    config.certificate = pins;
    return subscriptionRequestFor(QStringLiteral("https://") + userInfo + QStringLiteral("vpn.test:")
                                          + QString::number(port) + path,
                                  config);
}

std::optional<SubscriptionFetchResult> TestSubscriptionFetcher::fetch(const SubscriptionFetchRequest &request)
{
    // Before the fetcher, so it outlives it.
    std::optional<SubscriptionFetchResult> result;
    SubscriptionFetcher fetcher(QStringLiteral("9.8.7"));
    connect(&fetcher, &SubscriptionFetcher::finished, this,
            [&result](const SubscriptionFetchResult &r) { result = r; });
    fetcher.start(request);
    QElapsedTimer waited;
    waited.start();
    while (!result && waited.elapsed() < 30000)
        QTest::qWait(5);
    return result;
}

QString TestSubscriptionFetcher::describe(const std::optional<SubscriptionFetchResult> &result) const
{
    if (!result)
        return QStringLiteral("no result in 30 s, on %1").arg(m_backend);
    return QStringLiteral("%1, HTTP %2, field '%3', on %4")
            .arg(subscriptionFetchCode(result->error, result->documentError.kind))
            .arg(result->httpStatus)
            .arg(result->documentError.field, m_backend);
}

// As a CA a system trusts. Schannel never takes one from Qt's configuration.
void TestSubscriptionFetcher::trustTheTestCa()
{
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.addCaCertificate(m_certs->ca.certificate);
    QSslConfiguration::setDefaultConfiguration(tls);
}

void TestSubscriptionFetcher::initTestCase()
{
    // Before anything else uses TLS: the backend cannot change afterwards.
    const QString wanted = qEnvironmentVariable("FT_TEST_TLS_BACKEND");
    if (!wanted.isEmpty() && !QSslSocket::availableBackends().contains(wanted)) {
        QVERIFY2(wanted != nativeBackend(),
                 qPrintable(QStringLiteral("Qt has no %1 TLS backend, which this system's users get; it has %2")
                                    .arg(wanted, QSslSocket::availableBackends().join(QStringLiteral(", ")))));
        QSKIP(qPrintable(QStringLiteral("Qt has no %1 TLS backend on this system").arg(wanted)));
    }
    if (!wanted.isEmpty())
        QVERIFY2(QSslSocket::setActiveBackend(wanted),
                 qPrintable(QStringLiteral("cannot use %1: %2 is in use").arg(wanted, QSslSocket::activeBackend())));
    m_backend = QSslSocket::activeBackend();
    m_certs = std::make_unique<TlsTestCerts>();
    QString error;
    QVERIFY2(makeTlsTestCerts(QStringLiteral(FT_TEST_OPENSSL_CLI), m_certs.get(), &error), qPrintable(error));
    m_defaultTls = QSslConfiguration::defaultConfiguration();
}

void TestSubscriptionFetcher::init()
{
    QSslConfiguration::setDefaultConfiguration(*m_defaultTls);
    // Well under the 30 s fetch() waits, so a fetch that hangs says so.
    qputenv("FT_SUBSCRIPTION_TIMEOUT_MS", "10000");
}

void TestSubscriptionFetcher::aPinnedRequestNeedsTheConfigsHostnameAndACertificate()
{
    ConfigToml config;
    config.hostname = QStringLiteral("VPN.Test");
    config.addresses = QStringLiteral("127.0.0.1:8443, 10.0.0.1:443");
    config.certificate = pemOf({&m_certs->selfSigned});
    const QString url = QStringLiteral("https://alice:s3cret@vpn.test/sub");

    const SubscriptionFetchRequest request = subscriptionRequestFor(url, config);
    QCOMPARE(request.url, url);
    QVERIFY(request.pinned);
    QCOMPARE(request.pins.size(), 1);
    QVERIFY(request.pins.constFirst() == m_certs->selfSigned.certificate);
    QCOMPARE(request.peerVerifyName, config.hostname);
    QCOMPARE(request.connectHost, QStringLiteral("127.0.0.1"));
    QCOMPARE(request.connectPort, 8443);

    // Another host, even one under the hostname: the system's CAs, and DNS.
    const SubscriptionFetchRequest other =
            subscriptionRequestFor(QStringLiteral("https://alice:s3cret@sub.vpn.test/sub"), config);
    QVERIFY(!other.pinned);
    QVERIFY(other.pins.isEmpty());
    QVERIFY(other.peerVerifyName.isEmpty());
    QVERIFY(other.connectHost.isEmpty());

    // No certificate, or blank lines for one: nothing to pin.
    config.certificate = QStringLiteral("\n  \n");
    QVERIFY(!subscriptionRequestFor(url, config).pinned);
    config.certificate.clear();
    QVERIFY(!subscriptionRequestFor(url, config).pinned);

    // An IDN is the same host whichever form either side writes it in.
    config.certificate = pemOf({&m_certs->selfSigned});
    config.hostname = QStringLiteral(u"пример.test");
    const QString ace = QString::fromLatin1(QUrl::toAce(config.hostname));
    QVERIFY(subscriptionRequestFor(QStringLiteral("https://") + ace + QStringLiteral("/s"), config).pinned);
}

void TestSubscriptionFetcher::aPinnedRequestConnectsToTheFirstAddress_data()
{
    QTest::addColumn<QString>("addresses");
    QTest::addColumn<QString>("host");
    QTest::addColumn<int>("port");
    QTest::newRow("IPv4, of two") << "1.2.3.4:443, 5.6.7.8:443" << "1.2.3.4" << 443;
    QTest::newRow("IPv6") << "[2001:db8::1]:8443" << "2001:db8::1" << 8443;
    QTest::newRow("a name, with spaces") << " vpn.example.com:9443 " << "vpn.example.com" << 9443;
    // Not one the core could use: the address's own host and port, still pinned.
    QTest::newRow("no port") << "1.2.3.4" << "" << -1;
    QTest::newRow("none") << "" << "" << -1;
}

// The address wins over the subscription address's port, as for the core.
void TestSubscriptionFetcher::aPinnedRequestConnectsToTheFirstAddress()
{
    QFETCH(QString, addresses);
    QFETCH(QString, host);
    QFETCH(int, port);
    ConfigToml config;
    config.hostname = QStringLiteral("vpn.test");
    config.addresses = addresses;
    config.certificate = pemOf({&m_certs->selfSigned});
    const SubscriptionFetchRequest request =
            subscriptionRequestFor(QStringLiteral("https://alice:s3cret@vpn.test:1111/sub"), config);
    QVERIFY(request.pinned);
    QCOMPARE(request.connectHost, host);
    QCOMPARE(request.connectPort, port);
}

void TestSubscriptionFetcher::anUnreadablePinIsBadPinNotTheSystemsCas_data()
{
    QTest::addColumn<QByteArray>("unreadable");
    QTest::addColumn<bool>("thenACertificate");
    // 00 00 00, which certificatesDer() does not keep.
    QTest::newRow("not a certificate") << QByteArray(3, '\0') << false;
    // A certificate's outer shape, which certificatesDer() keeps and Qt cannot read.
    const QByteArray shape = QByteArray::fromHex("300b3002050030020500030100");
    QTest::newRow("a certificate's shape") << shape << false;
    QTest::newRow("a certificate's shape, then a certificate") << shape << true;
}

void TestSubscriptionFetcher::anUnreadablePinIsBadPinNotTheSystemsCas()
{
    QFETCH(QByteArray, unreadable);
    QFETCH(bool, thenACertificate);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json());
    QString pem = QStringLiteral("-----BEGIN CERTIFICATE-----\n") + QString::fromLatin1(unreadable.toBase64())
            + QStringLiteral("\n-----END CERTIFICATE-----\n");
    if (thenACertificate)
        pem += pemOf({&m_certs->selfSigned});
    const SubscriptionFetchRequest request = pinned(server.port(), pem);
    QVERIFY(request.pinned);
    const std::optional<SubscriptionFetchResult> result = fetch(request);
    if (thenACertificate) {
        QCOMPARE(request.pins.size(), 1);
        QVERIFY(request.pins.constFirst() == m_certs->selfSigned.certificate);
        QVERIFY_FETCH(result, SubscriptionFetchError::None);
        return;
    }
    QVERIFY(request.pins.isEmpty());
    QVERIFY_FETCH(result, SubscriptionFetchError::BadPin);
    QCOMPARE(server.connections(), 0);
}

// The endpoint's name need not resolve: the fetch goes to the config's address
// and still sends the name, as the SNI and as the Host header.
void TestSubscriptionFetcher::aPinnedFetchReachesANameThatDoesNotResolve()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json());
    // %40, %3A and UTF-8 in the userinfo: what they decode to is what is sent.
    const std::optional<SubscriptionFetchResult> result = fetch(
            pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/sub"),
                   QStringLiteral("al%40ice:p%3Ass%C3%A9@")));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    const QByteArray port = QByteArray::number(server.port());
    QVERIFY(result->pinned);
    QCOMPARE(result->httpStatus, 200);
    QVERIFY(result->document);
    QCOMPARE(result->document->hostname, QStringLiteral("vpn.test"));
    QCOMPARE(result->origin, QStringLiteral("https://vpn.test:") + QString::fromLatin1(port));
    QCOMPARE(server.serverNames(), QStringList{QStringLiteral("vpn.test")});
    QCOMPARE(server.requests().size(), 1);
    const TlsTestServer::Request &request = server.requests().constFirst();
    QCOMPARE(request.method, QByteArray("GET"));
    QCOMPARE(request.target, QByteArray("/sub"));
    QCOMPARE(request.header("Host"), "vpn.test:" + port);
    const auto isHost = [](const TlsTestServer::Header &h) {
        return h.first.compare("Host", Qt::CaseInsensitive) == 0;
    };
    QCOMPARE(std::count_if(request.headers.cbegin(), request.headers.cend(), isHost), 1);
    QCOMPARE(request.header("Authorization"), "Basic " + QStringLiteral(u"al@ice:p:ssé").toUtf8().toBase64());
    QCOMPARE(request.header("User-Agent"), QByteArray("FreeTunnel/9.8.7"));
    QCOMPARE(request.header("Accept"), QByteArray("application/json"));
    QCOMPARE(request.header("Accept-Encoding"), QByteArray("identity"));
    QVERIFY(!request.hasHeader("Cookie"));
    // HTTP/1.1, which the server would have given up for HTTP/2 had it been asked.
    QCOMPARE(server.protocols(), QStringList{QString()});
}

void TestSubscriptionFetcher::aWrongPinIsRefusedBeforeTheRequest()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json());
    // The same names, another key: what a server that took the address would have.
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->otherKey})));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QVERIFY(result->pinned);
    QCOMPARE(server.requests().size(), 0);
    // Refused at the handshake, not after it. Secure Transport finishes its side
    // before it judges the certificate, so the server cannot tell there.
    if (m_backend != QLatin1String("securetransport")) {
        QTest::qWait(200);
        QVERIFY2(server.handshakesDone() == 0, qPrintable(m_backend));
    }
}

void TestSubscriptionFetcher::aPinnedCertificateThatACaSignedIsTaken_data()
{
    QTest::addColumn<bool>("withItsCa");
    QTest::newRow("served alone") << false;
    QTest::newRow("served with its CA") << true;
}

// The errors that only say no CA vouches for it are the pin's to answer. They
// differ with what the server sends: alone, OpenSSL finds no issuer; with its
// CA, it finds a self-signed one it does not trust.
void TestSubscriptionFetcher::aPinnedCertificateThatACaSignedIsTaken()
{
    QFETCH(bool, withItsCa);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->caSigned, withItsCa ? &m_certs->ca : nullptr));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->caSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QCOMPARE(server.requests().size(), 1);
}

// As the endpoint serves its certificate file: the server's own, then its CA.
void TestSubscriptionFetcher::aPinnedChainIsTaken()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->caSigned, &m_certs->ca));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result =
            fetch(pinned(server.port(), pemOf({&m_certs->caSigned, &m_certs->ca})));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
}

// OpenSSL takes the pinned CA as the only one, and the handshake passes: the
// check that the server's own certificate is a pin is what refuses it.
void TestSubscriptionFetcher::pinningOnlyTheCaIsNotEnough()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->caSigned, &m_certs->ca));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->ca})));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QCOMPARE(server.requests().size(), 0);
}

// Pinned, the pins are all that is trusted, whatever the system trusts besides.
void TestSubscriptionFetcher::aCaTheSystemTrustsDoesNotStandInForThePin()
{
    trustTheTestCa();
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->caSigned, &m_certs->ca));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->otherKey})));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QCOMPARE(server.requests().size(), 0);
}

// A pin vouches for the certificate, never for a name it does not carry.
void TestSubscriptionFetcher::aPinnedCertificateForAnotherNameIsRefused()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->otherName));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->otherName})));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QCOMPARE(server.requests().size(), 0);
}

void TestSubscriptionFetcher::aPinnedCertificateOutOfDateIsRefused_data()
{
    QTest::addColumn<bool>("expired");
    QTest::newRow("expired") << true;
    QTest::newRow("not yet valid") << false;
}

// A pin vouches for the certificate, never for a time it is not valid at, on
// every backend: Secure Transport calls such a certificate only untrusted, as it
// does a pinned one that is in date.
void TestSubscriptionFetcher::aPinnedCertificateOutOfDateIsRefused()
{
    QFETCH(bool, expired);
    const TlsTestIdentity &identity = expired ? m_certs->expired : m_certs->notYetValid;
    TlsTestServer server;
    QVERIFY(listen(server, identity));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&identity})));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QCOMPARE(server.requests().size(), 0);
}

void TestSubscriptionFetcher::withoutAPinASelfSignedCertificateIsRefused()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(subscriptionRequestFor(
            QStringLiteral("https://alice:s3cret@127.0.0.1:%1/sub").arg(server.port()), ConfigToml()));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QVERIFY(!result->pinned);
    QCOMPARE(server.requests().size(), 0);
}

void TestSubscriptionFetcher::withoutAPinTheSystemsCasDecide()
{
    if (m_backend == QLatin1String("schannel"))
        QSKIP("schannel trusts the Windows certificate store alone, never a CA added to Qt's configuration");
    trustTheTestCa();
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->caSigned, &m_certs->ca));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result = fetch(subscriptionRequestFor(
            QStringLiteral("https://alice:s3cret@127.0.0.1:%1/sub").arg(server.port()), ConfigToml()));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QVERIFY(!result->pinned);
    QCOMPARE(server.requests().size(), 1);
}

void TestSubscriptionFetcher::skipVerificationNeverWeakensAFetch()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json());
    ConfigToml config;
    config.skipVerification = true;
    config.addresses = QStringLiteral("127.0.0.1:%1").arg(server.port());

    // Without a certificate: the system's CAs, which do not know this one.
    config.hostname = QStringLiteral("127.0.0.1");
    std::optional<SubscriptionFetchResult> result = fetch(subscriptionRequestFor(
            QStringLiteral("https://alice:s3cret@127.0.0.1:%1/sub").arg(server.port()), config));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);

    // With one: that one, which is not this.
    config.hostname = QStringLiteral("vpn.test");
    config.certificate = pemOf({&m_certs->otherKey});
    result = fetch(subscriptionRequestFor(QStringLiteral("https://alice:s3cret@vpn.test:%1/sub").arg(server.port()),
                                          config));
    QVERIFY_FETCH(result, SubscriptionFetchError::CertificateUntrusted);
    QCOMPARE(server.requests().size(), 0);
}

void TestSubscriptionFetcher::onlyHttpsIsFetched()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    const std::optional<SubscriptionFetchResult> result = fetch(subscriptionRequestFor(
            QStringLiteral("http://alice:s3cret@127.0.0.1:%1/sub").arg(server.port()), ConfigToml()));
    QVERIFY_FETCH(result, SubscriptionFetchError::BadUrl);
    QCOMPARE(server.connections(), 0);
    QVERIFY(!result->origin.contains(QLatin1Char('@')));
}

void TestSubscriptionFetcher::aSameOriginRedirectIsFollowed_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<QByteArray>("location");
    QTest::addColumn<QByteArray>("target");
    QTest::newRow("301, a path") << 301 << QByteArray("/sub") << QByteArray("/sub");
    QTest::newRow("302, absolute") << 302 << QByteArray("https://vpn.test:{port}/sub") << QByteArray("/sub");
    QTest::newRow("303, relative") << 303 << QByteArray("sub") << QByteArray("/sub");
    QTest::newRow("307, with a query") << 307 << QByteArray("/sub?token=1") << QByteArray("/sub?token=1");
    QTest::newRow("308, in capitals") << 308 << QByteArray("https://VPN.TEST:{port}/sub") << QByteArray("/sub");
    QTest::newRow("302, scheme-relative") << 302 << QByteArray("//vpn.test:{port}/sub") << QByteArray("/sub");
}

// The credentials go with it, to the same server, under the same name.
void TestSubscriptionFetcher::aSameOriginRedirectIsFollowed()
{
    QFETCH(int, code);
    QFETCH(QByteArray, location);
    QFETCH(QByteArray, target);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/start", redirect(code, withPort(location, server.port())));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result =
            fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/start")));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QCOMPARE(server.requests().size(), 2);
    const TlsTestServer::Request &second = server.requests().at(1);
    QCOMPARE(second.target, target);
    QCOMPARE(second.header("Host"), "vpn.test:" + QByteArray::number(server.port()));
    QVERIFY(!second.header("Authorization").isEmpty());
    QCOMPARE(second.header("Authorization"), server.requests().at(0).header("Authorization"));
    QCOMPARE(server.serverNames(), (QStringList{QStringLiteral("vpn.test"), QStringLiteral("vpn.test")}));
}

void TestSubscriptionFetcher::aRedirectElsewhereIsRefused_data()
{
    QTest::addColumn<QByteArray>("location");
    QTest::newRow("another host") << QByteArray("https://other.test:{port}/sub");
    // The very server the fetch is connected to, by the address it connects to.
    QTest::newRow("the address") << QByteArray("https://127.0.0.1:{port}/sub");
    QTest::newRow("another port") << QByteArray("https://vpn.test:{port2}/sub");
    QTest::newRow("the default port") << QByteArray("https://vpn.test/sub");
    QTest::newRow("http") << QByteArray("http://vpn.test:{port}/sub");
    QTest::newRow("a userinfo") << QByteArray("https://alice:s3cret@vpn.test:{port}/sub");
    QTest::newRow("scheme-relative, elsewhere") << QByteArray("//other.test:{port}/sub");
    QTest::newRow("unreadable") << QByteArray("https://vpn.test:{port}/s ub");
    QTest::newRow("empty") << QByteArray();
}

// Never followed: the other server sees nothing, and neither does this one again.
void TestSubscriptionFetcher::aRedirectElsewhereIsRefused()
{
    QFETCH(QByteArray, location);
    TlsTestServer server;
    TlsTestServer other;
    QVERIFY(listen(server, m_certs->selfSigned));
    QVERIFY(listen(other, m_certs->selfSigned));
    server.setRoute("/start", redirect(302, withPort(location, server.port(), other.port())));
    server.setRoute("/sub", json());
    other.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result =
            fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/start")));
    QVERIFY_FETCH(result, SubscriptionFetchError::BadRedirect);
    QCOMPARE(result->httpStatus, 302);
    QCOMPARE(server.requests().size(), 1);
    QCOMPARE(other.connections(), 0);
}

void TestSubscriptionFetcher::aRedirectLoopIsRefused()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/a", redirect(302, "/b"));
    server.setRoute("/b", redirect(302, "/a"));
    server.setRoute("/c", redirect(302, "/c#again"));
    server.setRoute("/x", redirect(302, "/y"));
    server.setRoute("/y", redirect(302, "/z"));
    server.setRoute("/z", redirect(302, "/y"));
    std::optional<SubscriptionFetchResult> result =
            fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/a")));
    QVERIFY_FETCH(result, SubscriptionFetchError::BadRedirect);
    QCOMPARE(server.requests().size(), 2);
    // To itself, by another fragment, which is never sent.
    result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/c")));
    QVERIFY_FETCH(result, SubscriptionFetchError::BadRedirect);
    QCOMPARE(server.requests().size(), 3);
    // Between later steps: refused when it comes round, not one request later at
    // the limit.
    result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/x")));
    QVERIFY_FETCH(result, SubscriptionFetchError::BadRedirect);
    QCOMPARE(server.requests().size(), 6);
}

void TestSubscriptionFetcher::threeRedirectsAreTheMost()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/h1", redirect(302, "/h2"));
    server.setRoute("/h2", redirect(302, "/h3"));
    server.setRoute("/h3", redirect(302, "/sub"));
    server.setRoute("/r1", redirect(302, "/r2"));
    server.setRoute("/r2", redirect(302, "/r3"));
    server.setRoute("/r3", redirect(302, "/r4"));
    server.setRoute("/r4", redirect(302, "/sub"));
    server.setRoute("/sub", json());
    std::optional<SubscriptionFetchResult> result =
            fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/h1")));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QCOMPARE(server.requests().size(), 4);
    result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/r1")));
    QVERIFY_FETCH(result, SubscriptionFetchError::BadRedirect);
    QCOMPARE(server.requests().size(), 8);
    QCOMPARE(server.requests().constLast().target, QByteArray("/r4"));
}

// https://vpn.test and https://vpn.test:443 are one origin. A pinned fetch
// connects to the config's address whatever the port, so a test server serves
// both.
void TestSubscriptionFetcher::theDefaultPortWrittenOutIsTheSameOrigin()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/start", redirect(302, "https://vpn.test:443/sub"));
    server.setRoute("/sub", json());
    ConfigToml config;
    config.hostname = QStringLiteral("vpn.test");
    config.addresses = QStringLiteral("127.0.0.1:%1").arg(server.port());
    config.certificate = pemOf({&m_certs->selfSigned});
    const std::optional<SubscriptionFetchResult> result =
            fetch(subscriptionRequestFor(QStringLiteral("https://alice:s3cret@vpn.test/start"), config));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QCOMPARE(server.requests().size(), 2);
    QCOMPARE(server.requests().at(0).header("Host"), QByteArray("vpn.test"));
    QCOMPARE(server.requests().at(1).header("Host"), QByteArray("vpn.test:443"));
}

void TestSubscriptionFetcher::noCookieGoesBack()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/start", status(302, {{"Set-Cookie", "session=1; Path=/"}, {"Location", "/sub"}}));
    server.setRoute("/sub", json());
    const std::optional<SubscriptionFetchResult> result =
            fetch(pinned(server.port(), pemOf({&m_certs->selfSigned}), QStringLiteral("/start")));
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QCOMPARE(server.requests().size(), 2);
    QVERIFY(!server.requests().at(1).hasHeader("Cookie"));
}

void TestSubscriptionFetcher::theCapHoldsAtItsEdge_data()
{
    QTest::addColumn<int>("size");
    QTest::addColumn<bool>("chunked");
    QTest::addColumn<SubscriptionFetchError>("expected");
    const int cap = int(kMaxSubscriptionBodyBytes);
    QTest::newRow("the cap, with a length") << cap << false << SubscriptionFetchError::None;
    QTest::newRow("the cap, chunked") << cap << true << SubscriptionFetchError::None;
    QTest::newRow("a byte more, with a length") << cap + 1 << false << SubscriptionFetchError::TooLarge;
    QTest::newRow("a byte more, chunked") << cap + 1 << true << SubscriptionFetchError::TooLarge;
}

// JSON may end in white space, so the document grows to any size.
void TestSubscriptionFetcher::theCapHoldsAtItsEdge()
{
    QFETCH(int, size);
    QFETCH(bool, chunked);
    QFETCH(SubscriptionFetchError, expected);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response = json(kDocument + QByteArray(size - kDocument.size(), ' '));
    response.chunked = chunked;
    server.setRoute("/sub", response);
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, expected);
}

void TestSubscriptionFetcher::anAnnouncedBodyOverTheCapIsRefusedBeforeItArrives()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response = json(QByteArray(int(kMaxSubscriptionBodyBytes) + 1, ' '));
    response.holdBody = true;
    server.setRoute("/sub", response);
    qputenv("FT_SUBSCRIPTION_TIMEOUT_MS", "5000");
    QElapsedTimer elapsed;
    elapsed.start();
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::TooLarge);
    QVERIFY2(elapsed.elapsed() < 4000, qPrintable(QString::number(elapsed.elapsed())));
}

// Refused as it passes the cap, not when the server stops, which this one never
// does: the read is bounded, and so is what is held.
void TestSubscriptionFetcher::aBodyThatKeepsComingIsRefusedAtTheCap()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response = json(QByteArray(int(kMaxSubscriptionBodyBytes) + 4096, ' '));
    response.chunked = true;
    response.endless = true;
    server.setRoute("/sub", response);
    qputenv("FT_SUBSCRIPTION_TIMEOUT_MS", "5000");
    QElapsedTimer elapsed;
    elapsed.start();
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::TooLarge);
    QVERIFY2(elapsed.elapsed() < 4000, qPrintable(QString::number(elapsed.elapsed())));
}

// A 200 whose body stops short is not read as a document.
void TestSubscriptionFetcher::aReplyCutShortIsANetworkError()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response = json(kDocument + QByteArray(100, ' '));
    response.cutAfter = int(kDocument.size());
    server.setRoute("/sub", response);
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::Network);
    QCOMPARE(result->httpStatus, 200);
}

void TestSubscriptionFetcher::onlyJsonIsTaken_data()
{
    QTest::addColumn<QByteArray>("contentType");
    QTest::addColumn<SubscriptionFetchError>("expected");
    QTest::newRow("JSON") << QByteArray("application/json") << SubscriptionFetchError::None;
    QTest::newRow("with a charset") << QByteArray("application/json; charset=utf-8") << SubscriptionFetchError::None;
    QTest::newRow("in capitals") << QByteArray("Application/JSON") << SubscriptionFetchError::None;
    QTest::newRow("HTML") << QByteArray("text/html") << SubscriptionFetchError::BadContentType;
    QTest::newRow("plain text") << QByteArray("text/plain") << SubscriptionFetchError::BadContentType;
    QTest::newRow("a longer name") << QByteArray("application/jsonp") << SubscriptionFetchError::BadContentType;
    QTest::newRow("none") << QByteArray() << SubscriptionFetchError::BadContentType;
}

void TestSubscriptionFetcher::onlyJsonIsTaken()
{
    QFETCH(QByteArray, contentType);
    QFETCH(SubscriptionFetchError, expected);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response = json();
    response.contentType = contentType;
    server.setRoute("/sub", response);
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, expected);
    QCOMPARE(result->document.has_value(), expected == SubscriptionFetchError::None);
}

void TestSubscriptionFetcher::eachStatusMeansSomething_data()
{
    QTest::addColumn<int>("code");
    QTest::addColumn<SubscriptionFetchError>("expected");
    QTest::newRow("401") << 401 << SubscriptionFetchError::Http401;
    QTest::newRow("403") << 403 << SubscriptionFetchError::Http403;
    QTest::newRow("404") << 404 << SubscriptionFetchError::HttpNoSubscription;
    QTest::newRow("405") << 405 << SubscriptionFetchError::HttpNoSubscription;
    QTest::newRow("407") << 407 << SubscriptionFetchError::HttpNoSubscription;
    QTest::newRow("500") << 500 << SubscriptionFetchError::HttpServer;
    QTest::newRow("503") << 503 << SubscriptionFetchError::HttpServer;
    QTest::newRow("418") << 418 << SubscriptionFetchError::HttpOther;
    QTest::newRow("204") << 204 << SubscriptionFetchError::HttpOther;
    // A redirect without a Location is no redirect.
    QTest::newRow("302, nowhere") << 302 << SubscriptionFetchError::HttpOther;
}

void TestSubscriptionFetcher::eachStatusMeansSomething()
{
    QFETCH(int, code);
    QFETCH(SubscriptionFetchError, expected);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    // As the endpoint answers a 401.
    server.setRoute("/sub", status(code, {{"WWW-Authenticate", "Basic realm=\"Subscription\""}}));
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, expected);
    QCOMPARE(result->httpStatus, code);
    QCOMPARE(server.requests().size(), 1);
}

void TestSubscriptionFetcher::aDocumentTheParserRefusesSaysWhy_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<SubscriptionDocError>("kind");
    QTest::addColumn<QString>("field");
    QTest::newRow("not JSON") << QByteArray("{") << SubscriptionDocError::Invalid << QString();
    QTest::newRow("no password") << QByteArray(R"({"version":1,"hostname":"vpn.test","address":"1.2.3.4:443",)"
                                               R"("username":"alice"})")
                                 << SubscriptionDocError::Invalid << QStringLiteral("password");
    QTest::newRow("newer") << QByteArray(R"({"version":2})") << SubscriptionDocError::UnsupportedVersion
                           << QStringLiteral("version");
    QTest::newRow("skip verification") << QByteArray(kDocument).replace("}", R"(,"skip_verification":true})")
                                       << SubscriptionDocError::SkipVerification
                                       << QStringLiteral("skip_verification");
}

void TestSubscriptionFetcher::aDocumentTheParserRefusesSaysWhy()
{
    QFETCH(QByteArray, body);
    QFETCH(SubscriptionDocError, kind);
    QFETCH(QString, field);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json(body));
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::Document);
    QCOMPARE(result->documentError.kind, kind);
    QCOMPARE(result->documentError.field, field);
    QVERIFY(!result->document);
    QCOMPARE(result->httpStatus, 200);
}

// Bytes keep coming, so a timeout that restarts on progress would never end
// this; the deadline is for the whole fetch.
void TestSubscriptionFetcher::aTrickleHitsTheDeadline()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response = json();
    response.trickleMs = 100;
    server.setRoute("/sub", response);
    qputenv("FT_SUBSCRIPTION_TIMEOUT_MS", "1500");
    QElapsedTimer elapsed;
    elapsed.start();
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::Timeout);
    QVERIFY2(elapsed.elapsed() >= 1400 && elapsed.elapsed() < 6000, qPrintable(QString::number(elapsed.elapsed())));
    QCOMPARE(server.requests().size(), 1);
}

void TestSubscriptionFetcher::aHandshakeTheServerEndsIsRefusedByServer_data()
{
    QTest::addColumn<TlsTestServer::Refusal>("refusal");
    QTest::newRow("closed") << TlsTestServer::Refusal::Close;
    QTest::newRow("an alert") << TlsTestServer::Refusal::Alert;
}

// As an endpoint whose rules deny the client: the connection ends during the
// TLS handshake, with no certificate to blame. How each backend reports that
// is its own, and is what this records.
void TestSubscriptionFetcher::aHandshakeTheServerEndsIsRefusedByServer()
{
    QFETCH(TlsTestServer::Refusal, refusal);
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRefusal(refusal);
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::RefusedByServer);
    QCOMPARE(server.serverNames().value(0), QStringLiteral("vpn.test"));
}

// After the handshake it is the network's, whatever Qt tries again.
void TestSubscriptionFetcher::aServerThatHangsUpIsANetworkError()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response response;
    response.hangUp = true;
    server.setRoute("/sub", response);
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(server.port(), pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::Network);
    QVERIFY(!server.requests().isEmpty());
}

void TestSubscriptionFetcher::nothingListeningIsANetworkError()
{
    QTcpServer closed;
    closed.setProxy(QNetworkProxy::NoProxy);
    QVERIFY(closed.listen(QHostAddress::LocalHost));
    const quint16 port = closed.serverPort();
    closed.close();
    const std::optional<SubscriptionFetchResult> result = fetch(pinned(port, pemOf({&m_certs->selfSigned})));
    QVERIFY_FETCH(result, SubscriptionFetchError::Network);
}

// Neither the user, the password, the credentials as sent, nor an '@' of a
// userinfo, whatever the fetch comes to; nor a query, which may hold a token.
void TestSubscriptionFetcher::noLogLineHoldsTheCredentials()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    server.setRoute("/sub", json());
    server.setRoute("/denied", status(401));
    server.setRoute("/away", redirect(302, "https://alice:s3cret@other.test/sub"));
    server.setRoute("/bad", json("{"));

    QStringList lines;
    g_captured = &lines;
    const QtMessageHandler previous = qInstallMessageHandler(capture);
    auto restore = qScopeGuard([previous]() { qInstallMessageHandler(previous); });
    const QString pins = pemOf({&m_certs->selfSigned});
    const QList<std::pair<SubscriptionFetchRequest, SubscriptionFetchError>> fetches = {
            {pinned(server.port(), pins, QStringLiteral("/sub?token=s3cret")), SubscriptionFetchError::None},
            {pinned(server.port(), pins, QStringLiteral("/denied")), SubscriptionFetchError::Http401},
            {pinned(server.port(), pins, QStringLiteral("/away")), SubscriptionFetchError::BadRedirect},
            {pinned(server.port(), pins, QStringLiteral("/bad")), SubscriptionFetchError::Document},
            {pinned(server.port(), pemOf({&m_certs->otherKey})), SubscriptionFetchError::CertificateUntrusted},
            {subscriptionRequestFor(QStringLiteral("http://alice:s3cret@vpn.test/sub"), ConfigToml()),
             SubscriptionFetchError::BadUrl},
    };
    for (const auto &[request, expected] : fetches) {
        const std::optional<SubscriptionFetchResult> result = fetch(request);
        QVERIFY_FETCH(result, expected);
    }
    restore.dismiss();
    qInstallMessageHandler(previous);
    g_captured = nullptr;

    QVERIFY2(lines.filter(QStringLiteral("SubscriptionFetcher: ")).size() == fetches.size(),
             qPrintable(lines.join(QLatin1Char('\n'))));
    const QString basic = QString::fromLatin1(QByteArray("alice:s3cret").toBase64());
    for (const QString &line : std::as_const(lines)) {
        QVERIFY2(!line.contains(QLatin1String("alice")) && !line.contains(QLatin1String("s3cret"))
                         && !line.contains(basic) && !line.contains(QLatin1Char('@')),
                 qPrintable(line));
    }
}

// The fetch goes direct, as the core's connection does: a proxy would see it,
// and one that is down would stop every refresh. Qt never takes a loopback
// address through a proxy, so the server is reached at an address of this
// machine's own instead.
void TestSubscriptionFetcher::anApplicationProxyIsNotUsed()
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    const auto own = std::find_if(addresses.cbegin(), addresses.cend(), [](const QHostAddress &a) {
        return a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal();
    });
    if (own == addresses.cend())
        QSKIP("This machine has no IPv4 address but loopback, which Qt never sends through a proxy");
    TlsTestServer server;
    QVERIFY(server.listen({m_certs->selfSigned.certificate}, m_certs->selfSigned.key, *own));
    server.setRoute("/sub", json());
    QTcpSocket probe;
    probe.setProxy(QNetworkProxy::NoProxy);
    probe.connectToHost(*own, server.port());
    if (!probe.waitForConnected(3000))
        QSKIP("This machine cannot reach its own address here, a firewall's doing");
    probe.abort();
    ConfigToml config;
    config.hostname = QStringLiteral("vpn.test");
    config.addresses = own->toString() + QLatin1Char(':') + QString::number(server.port());
    config.certificate = pemOf({&m_certs->selfSigned});
    const SubscriptionFetchRequest request =
            subscriptionRequestFor(QStringLiteral("https://alice:s3cret@vpn.test/sub"), config);

    QTcpServer dead;
    dead.setProxy(QNetworkProxy::NoProxy);
    QVERIFY(dead.listen(QHostAddress::LocalHost));
    const quint16 deadPort = dead.serverPort();
    dead.close();
    const QNetworkProxy previous = QNetworkProxy::applicationProxy();
    QNetworkProxy::setApplicationProxy(
            QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), deadPort));
    auto restore = qScopeGuard([previous]() { QNetworkProxy::setApplicationProxy(previous); });
    const std::optional<SubscriptionFetchResult> result = fetch(request);
    QVERIFY_FETCH(result, SubscriptionFetchError::None);
    QCOMPARE(server.requests().size(), 1);
}

void TestSubscriptionFetcher::aResultNeverComesFromInsideStart()
{
    int results = 0;
    SubscriptionFetchError error = SubscriptionFetchError::None;
    SubscriptionFetcher fetcher(QStringLiteral("9.8.7"));
    connect(&fetcher, &SubscriptionFetcher::finished, this, [&](const SubscriptionFetchResult &r) {
        ++results;
        error = r.error;
    });
    fetcher.start(subscriptionRequestFor(QStringLiteral("http://vpn.test/sub"), ConfigToml()));
    QCOMPARE(results, 0);
    QVERIFY(!fetcher.isRunning());
    QTRY_COMPARE(results, 1);
    QCOMPARE(error, SubscriptionFetchError::BadUrl);
}

void TestSubscriptionFetcher::anAbortedFetchNeverFinishes()
{
    TlsTestServer server;
    QVERIFY(listen(server, m_certs->selfSigned));
    TlsTestServer::Response held = json();
    held.holdBody = true;
    server.setRoute("/held", held);
    server.setRoute("/sub", json());
    const QString pins = pemOf({&m_certs->selfSigned});
    int results = 0;
    SubscriptionFetchError error = SubscriptionFetchError::None;
    SubscriptionFetcher fetcher(QStringLiteral("9.8.7"));
    connect(&fetcher, &SubscriptionFetcher::finished, this, [&](const SubscriptionFetchResult &r) {
        ++results;
        error = r.error;
    });

    fetcher.start(pinned(server.port(), pins, QStringLiteral("/held")));
    QVERIFY(fetcher.isRunning());
    QTRY_COMPARE(server.requests().size(), 1);
    fetcher.abort();
    QVERIFY(!fetcher.isRunning());
    // A result already on its way is dropped as well.
    fetcher.start(subscriptionRequestFor(QStringLiteral("http://vpn.test/sub"), ConfigToml()));
    fetcher.abort();
    QTest::qWait(300);
    QCOMPARE(results, 0);

    // A fetch started over another one ends that one without its result, whether
    // it is still running or its result is on its way.
    fetcher.start(pinned(server.port(), pins, QStringLiteral("/held")));
    QTRY_COMPARE(server.requests().size(), 2);
    fetcher.start(subscriptionRequestFor(QStringLiteral("http://vpn.test/sub"), ConfigToml()));
    fetcher.start(pinned(server.port(), pins));
    QTRY_COMPARE(results, 1);
    QCOMPARE(error, SubscriptionFetchError::None);
    QTest::qWait(300);
    QCOMPARE(results, 1);
}

// Every outcome has a row: a code, a policy, and words for the user, but
// success. A 403 is the operator turning updates off, so nothing automatic
// asks again; a 404 is asked again weekly; what only a new link or a newer
// FreeTunnel can fix waits for the user.
void TestSubscriptionFetcher::everyOutcomeHasACodeAPolicyAndWords()
{
    using E = SubscriptionFetchError;
    using D = SubscriptionDocError;
    using P = SubscriptionRefreshPolicy;
    const QList<std::pair<E, P>> fetchOutcomes = {
            {E::None, P::Normal},           {E::BadUrl, P::ManualOnly},         {E::BadPin, P::ManualOnly},
            {E::Network, P::Backoff},       {E::Timeout, P::Backoff},           {E::RefusedByServer, P::Backoff},
            {E::CertificateUntrusted, P::ManualOnly}, {E::BadRedirect, P::ManualOnly}, {E::Http401, P::ManualOnly},
            {E::Http403, P::ManualOnly},    {E::HttpNoSubscription, P::Weekly}, {E::HttpServer, P::Backoff},
            {E::HttpOther, P::Backoff},     {E::BadContentType, P::Backoff},    {E::TooLarge, P::Backoff},
    };
    const QList<std::pair<D, P>> documentOutcomes = {
            {D::Invalid, P::Backoff},
            {D::TooLarge, P::Backoff},
            {D::UnsupportedVersion, P::ManualOnly},
            {D::SkipVerification, P::ManualOnly},
            {D::NeedsNewerCore, P::ManualOnly},
    };
    QSet<QString> codes;
    for (const auto &[error, policy] : fetchOutcomes) {
        const QString code = subscriptionFetchCode(error, D::Invalid);
        QVERIFY2(!code.isEmpty() && code != QLatin1String("unknown"), qPrintable(QString::number(int(error))));
        QVERIFY2(refreshPolicyFor(error, D::Invalid) == policy, qPrintable(code));
        QCOMPARE(subscriptionFailureText(error, D::Invalid).isEmpty(), error == E::None);
        codes.insert(code);
    }
    QCOMPARE(codes.size(), fetchOutcomes.size());
    QCOMPARE(subscriptionFetchCode(E::None, D::Invalid), QStringLiteral("ok"));
    codes.clear();
    for (const auto &[kind, policy] : documentOutcomes) {
        const QString code = subscriptionFetchCode(E::Document, kind);
        QVERIFY2(!code.isEmpty() && code != QLatin1String("unknown"), qPrintable(QString::number(int(kind))));
        QVERIFY2(refreshPolicyFor(E::Document, kind) == policy, qPrintable(code));
        QVERIFY(!subscriptionFailureText(E::Document, kind).isEmpty());
        codes.insert(code);
    }
    QCOMPARE(codes.size(), documentOutcomes.size());
}

QTEST_MAIN(TestSubscriptionFetcher)
#include "test_subscription_fetcher.moc"
