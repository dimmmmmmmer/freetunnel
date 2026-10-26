// cppcheck-suppress-file missingIncludeSystem
// The subscription document: what a reply has to hold to be taken, what taking
// it changes in a config, and the subscription address itself. A reply is the
// one thing here a server writes, and whatever passes these rules goes into a
// config the core connects with, so each rule is held to its edge.
#include <QtTest>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include "certificate_vectors.h"
#include "core/ConfigToml.h"
#include "core/DeepLink.h"
#include "core/Subscription.h"
#include "subscription_vectors.h"

using namespace freetunnel;

Q_DECLARE_METATYPE(freetunnel::SubscriptionDocError)

class TestSubscription : public QObject {
    Q_OBJECT

private slots:
    // The document.
    void readsTheMinimalDocument();
    void readsTheFullDocument();
    void readsASelfSignedDocument();
    void eachRequiredFieldIsChecked_data();
    void eachRequiredFieldIsChecked();
    void theVersionIsReadFirst_data();
    void theVersionIsReadFirst();
    void aNewerDocumentIsNeverCalledDamaged();
    void theProtocolIsHttp2OrHttp3();
    void theFlagsAreBooleans();
    void aSubscriptionNeverSkipsVerification();
    void anAuthKeyNeedsANewerCore();
    void eachLimitHoldsAtItsEdge_data();
    void eachLimitHoldsAtItsEdge();
    void theBodyHasALimit();
    void anAddressIsOneHostAndPort_data();
    void anAddressIsOneHostAndPort();
    void noStringHoldsAControlCharacter_data();
    void noStringHoldsAControlCharacter();
    void theClientRandomIsTakenAsTheCoreReadsIt();
    void anEmptySniOrNameIsTaken();
    void aDnsEntryIsOneServer();
    void unknownFieldsAreIgnored();
    void aBodyThatIsNotOneObjectIsInvalid_data();
    void aBodyThatIsNotOneObjectIsInvalid();
    void aDuplicateKeyKeepsItsLastValue();

    // Its certificate.
    void anEmptyCertificatePinsNothing();
    void aKeyBesideTheCertificateIsLeftOut();
    void aFieldWithNoCertificateIsInvalid_data();
    void aFieldWithNoCertificateIsInvalid();
    void atMostEightCertificates();
    void aCertificateIsKeptAsWritten();
    void aDamagedBlockBesideACertificateIsLeftOut();

    // What it changes.
    void aRefreshOverwritesWhatTheSubscriptionOwns();
    void aRefreshCollapsesTheAddressList();
    void aRefreshClearsWhatTheDocumentLeavesOut();
    void aRefreshKeepsWhatTheUserOwns();
    void creationTakesTheDocumentsSettings();
    void anUnchangedReplyChangesNothing();
    void theCredentialsAndCertificateChangeWithoutValues();
    void theSameHostnameInOtherLettersIsTheSameServer();

    // The address and the rest.
    void aSubscriptionAddressMustBeHttps_data();
    void aSubscriptionAddressMustBeHttps();
    void aSubscriptionAddressHasALengthLimit();
    void theUserinfoBecomesABasicHeader();
    void userinfoThatCannotBeSentIsRefused_data();
    void userinfoThatCannotBeSentIsRefused();
    void theRequestUrlHasNoUserinfoOrFragment();
    void theIdentityIsTheSubscriptionNotThePassword();
    void redactUrlKeepsNothingSecret();
    void theRefreshIntervalIsClamped();
    void noRefreshHintIsReadYet();
    void theAddressRuleTheEditorShares_data();
    void theAddressRuleTheEditorShares();

    void mutatedDocumentsNeverBreakTheRules();
    void randomFieldValuesNeverBreakTheRules();
};

namespace {

// A value that takes the field out of the document instead.
const QJsonValue kAbsent(QJsonValue::Undefined);

QJsonObject objectOf(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

QByteArray jsonOf(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

// The document with one field set to `value`, or taken out for kAbsent. Qt writes
// the keys in an order of its own, which no rule here depends on.
QByteArray withField(const QByteArray &json, const QString &key, const QJsonValue &value)
{
    QJsonObject object = objectOf(json);
    if (value.isUndefined())
        object.remove(key);
    else
        object.insert(key, value);
    return jsonOf(object);
}

std::optional<SubscriptionDocument> parse(const QByteArray &json, SubscriptionParseError *error = nullptr)
{
    return parseSubscriptionJson(json, error);
}

SubscriptionDocument parsed(const QByteArray &json)
{
    SubscriptionParseError error;
    const auto doc = parseSubscriptionJson(json, &error);
    if (!doc)
        qWarning("refused, field '%s': %s", qPrintable(error.field), json.constData());
    return doc.value_or(SubscriptionDocument());
}

// Refused for `kind`, naming `field`.
bool refusedFor(const QByteArray &json, SubscriptionDocError kind, const QString &field)
{
    SubscriptionParseError error;
    error.kind = kind == SubscriptionDocError::Invalid ? SubscriptionDocError::TooLarge
                                                        : SubscriptionDocError::Invalid;
    error.field = QStringLiteral("unset");
    if (parseSubscriptionJson(json, &error))
        return false;
    if (error.kind != kind || error.field != field) {
        qWarning("refused as %d for '%s', not %d for '%s'", int(error.kind), qPrintable(error.field), int(kind),
                 qPrintable(field));
        return false;
    }
    return true;
}

// A config as one made from kMinimal is stored, before a refresh.
ConfigToml storedConfig()
{
    ConfigToml c;
    c.hostname = QStringLiteral("vpn.example.com");
    c.addresses = QStringLiteral("1.2.3.4:443");
    c.username = QStringLiteral("alice");
    return c;
}

int certificateCountOf(const QString &pem)
{
    const QByteArray der = certificatesDer(pem);
    int count = 0;
    for (int pos = 0; pos < der.size(); pos = derElementEnd(der, pos))
        ++count;
    return count;
}

const SubscriptionChange *changeOf(const SubscriptionApplied &applied, SubscriptionField field)
{
    for (const SubscriptionChange &change : applied.changes) {
        if (change.field == field)
            return &change;
    }
    return nullptr;
}

} // namespace

void TestSubscription::readsTheMinimalDocument()
{
    SubscriptionParseError error;
    const auto doc = parse(sub::kMinimal, &error);
    QVERIFY2(doc.has_value(), qPrintable(error.field));
    QCOMPARE(doc->hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(doc->address, QStringLiteral("1.2.3.4:443"));
    QCOMPARE(doc->username, QStringLiteral("alice"));
    QCOMPARE(doc->password, QStringLiteral("s3cret"));
    QVERIFY(doc->hasIpv6);
    QCOMPARE(doc->upstreamProtocol, QStringLiteral("http2"));
    QVERIFY(!doc->antiDpi);
    QVERIFY(doc->certificatePem.isEmpty());
    QVERIFY(!doc->extraPemIgnored);
    QVERIFY(doc->customSni.isEmpty());
    QVERIFY(doc->clientRandomPrefix.isEmpty());
    QVERIFY(!doc->name.has_value());
    QVERIFY(!doc->dnsUpstreams.has_value());

    // The four the endpoint fixes may be left out, as TrustTunnel's description
    // of the format has them, and are then their defaults.
    QByteArray bare = sub::kMinimal;
    for (const char *key : {"has_ipv6", "upstream_protocol", "anti_dpi", "skip_verification"})
        bare = withField(bare, QString::fromLatin1(key), kAbsent);
    const auto defaults = parse(bare);
    QVERIFY(defaults.has_value());
    QVERIFY(defaults->hasIpv6);
    QCOMPARE(defaults->upstreamProtocol, QStringLiteral("http2"));
    QVERIFY(!defaults->antiDpi);
}

void TestSubscription::readsTheFullDocument()
{
    const SubscriptionDocument doc = parsed(sub::kFull);
    QCOMPARE(doc.hostname, QStringLiteral("vpn.example.com"));
    QVERIFY(!doc.hasIpv6);
    QCOMPARE(doc.upstreamProtocol, QStringLiteral("http3"));
    QVERIFY(doc.antiDpi);
    QCOMPARE(doc.customSni, QStringLiteral("example.org"));
    QCOMPARE(doc.clientRandomPrefix, QStringLiteral("aabbcc/ff00ff"));
    QCOMPARE(doc.name, std::optional<QString>(QStringLiteral("Example")));
    QCOMPARE(doc.dnsUpstreams,
             std::optional<QStringList>(
                     QStringList{QStringLiteral("1.1.1.1"), QStringLiteral("tls://dns.example.com")}));
}

void TestSubscription::readsASelfSignedDocument()
{
    const SubscriptionDocument doc = parsed(sub::kSelfSigned);
    QCOMPARE(doc.certificatePem, certs::kCaPem);
    QVERIFY(!doc.extraPemIgnored);
}

void TestSubscription::eachRequiredFieldIsChecked_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");
    for (const char *key : {"hostname", "address", "username", "password"}) {
        const QString k = QString::fromLatin1(key);
        QTest::addRow("%s missing", key) << k << kAbsent;
        QTest::addRow("%s empty", key) << k << QJsonValue(QString());
        QTest::addRow("%s null", key) << k << QJsonValue(QJsonValue::Null);
        QTest::addRow("%s a number", key) << k << QJsonValue(443);
        QTest::addRow("%s a list", key) << k << QJsonValue(QJsonArray{QStringLiteral("1.2.3.4:443")});
    }
}

void TestSubscription::eachRequiredFieldIsChecked()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);
    QVERIFY(refusedFor(withField(sub::kMinimal, key, value), SubscriptionDocError::Invalid, key));
}

void TestSubscription::theVersionIsReadFirst_data()
{
    QTest::addColumn<QByteArray>("version"); // as the body writes it; empty: left out
    QTest::addColumn<bool>("taken");
    QTest::addColumn<SubscriptionDocError>("kind");
    const auto invalid = SubscriptionDocError::Invalid;
    const auto newer = SubscriptionDocError::UnsupportedVersion;
    QTest::newRow("1") << QByteArray("1") << true << invalid;
    QTest::newRow("1.0, a whole number") << QByteArray("1.0") << true << invalid;
    QTest::newRow("left out") << QByteArray() << false << invalid;
    QTest::newRow("\"1\"") << QByteArray("\"1\"") << false << invalid;
    QTest::newRow("0") << QByteArray("0") << false << invalid;
    QTest::newRow("-1") << QByteArray("-1") << false << invalid;
    QTest::newRow("1.5") << QByteArray("1.5") << false << invalid;
    QTest::newRow("true") << QByteArray("true") << false << invalid;
    QTest::newRow("null") << QByteArray("null") << false << invalid;
    QTest::newRow("2") << QByteArray("2") << false << newer;
    QTest::newRow("2^60") << QByteArray("1152921504606846976") << false << newer;
    QTest::newRow("2^64 + 1") << QByteArray("18446744073709551617") << false << newer;
    QTest::newRow("1e300") << QByteArray("1e300") << false << newer;
    QTest::newRow("2.5") << QByteArray("2.5") << false << invalid;
}

void TestSubscription::theVersionIsReadFirst()
{
    QFETCH(QByteArray, version);
    QFETCH(bool, taken);
    QFETCH(SubscriptionDocError, kind);
    QByteArray body = sub::kMinimal;
    QVERIFY(body.startsWith("{\"version\":1,"));
    body.replace(0, int(qstrlen("{\"version\":1,")),
                 version.isEmpty() ? QByteArray("{") : "{\"version\":" + version + ",");
    if (taken)
        QVERIFY2(parse(body).has_value(), body.constData());
    else
        QVERIFY(refusedFor(body, kind, QStringLiteral("version")));
}

// A newer format may name its fields otherwise, and its document is refused as
// newer, never as damaged, which would say something else to the user.
void TestSubscription::aNewerDocumentIsNeverCalledDamaged()
{
    QVERIFY(refusedFor(sub::kVersion2, SubscriptionDocError::UnsupportedVersion, QStringLiteral("version")));
    const QByteArray damaged = withField(withField(sub::kMinimal, QStringLiteral("version"), 2),
                                         QStringLiteral("hostname"), QJsonValue(42));
    QVERIFY(refusedFor(damaged, SubscriptionDocError::UnsupportedVersion, QStringLiteral("version")));
    const QByteArray skipping = withField(withField(sub::kMinimal, QStringLiteral("version"), 3),
                                          QStringLiteral("skip_verification"), true);
    QVERIFY(refusedFor(skipping, SubscriptionDocError::UnsupportedVersion, QStringLiteral("version")));
}

void TestSubscription::theProtocolIsHttp2OrHttp3()
{
    const QString key = QStringLiteral("upstream_protocol");
    QCOMPARE(parsed(withField(sub::kMinimal, key, QStringLiteral("http3"))).upstreamProtocol, QStringLiteral("http3"));
    QCOMPARE(parsed(withField(sub::kMinimal, key, kAbsent)).upstreamProtocol, QStringLiteral("http2"));
    for (const QJsonValue &bad : {QJsonValue(QStringLiteral("http9")), QJsonValue(QStringLiteral("HTTP2")),
                                  QJsonValue(QString()), QJsonValue(2), QJsonValue(QJsonValue::Null)})
        QVERIFY(refusedFor(withField(sub::kMinimal, key, bad), SubscriptionDocError::Invalid, key));
}

void TestSubscription::theFlagsAreBooleans()
{
    QVERIFY(!parsed(withField(sub::kMinimal, QStringLiteral("has_ipv6"), false)).hasIpv6);
    QVERIFY(parsed(withField(sub::kMinimal, QStringLiteral("anti_dpi"), true)).antiDpi);
    for (const char *key : {"has_ipv6", "anti_dpi", "skip_verification"}) {
        const QString k = QString::fromLatin1(key);
        for (const QJsonValue &bad : {QJsonValue(QStringLiteral("true")), QJsonValue(1), QJsonValue(0),
                                      QJsonValue(QJsonValue::Null), QJsonValue(QJsonObject())})
            QVERIFY2(refusedFor(withField(sub::kMinimal, k, bad), SubscriptionDocError::Invalid, k), key);
    }
}

// Whatever else the reply holds: nothing would then check that the next one came
// from the same server.
void TestSubscription::aSubscriptionNeverSkipsVerification()
{
    const QString key = QStringLiteral("skip_verification");
    QVERIFY(refusedFor(withField(sub::kMinimal, key, true), SubscriptionDocError::SkipVerification, key));
    QVERIFY(refusedFor(withField(sub::kSelfSigned, key, true), SubscriptionDocError::SkipVerification, key));
    QVERIFY(parse(withField(sub::kMinimal, key, false)).has_value());
}

void TestSubscription::anAuthKeyNeedsANewerCore()
{
    const QString key = QStringLiteral("client_random_auth_key");
    QVERIFY(refusedFor(sub::kAuthKey, SubscriptionDocError::NeedsNewerCore, key));
    QVERIFY(parse(withField(sub::kMinimal, key, QString())).has_value());
    QVERIFY(refusedFor(withField(sub::kMinimal, key, 5), SubscriptionDocError::Invalid, key));
    QVERIFY(refusedFor(withField(sub::kMinimal, key, QJsonValue(QJsonValue::Null)), SubscriptionDocError::Invalid,
                       key));
}

void TestSubscription::eachLimitHoldsAtItsEdge_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("atLimit");
    QTest::addColumn<QJsonValue>("pastIt");
    const auto text = [](int n, char c = 'a') { return QJsonValue(QString(n, QLatin1Char(c))); };
    const auto host = [](int n) { return QJsonValue(QString(n - 4, QLatin1Char('h')) + QStringLiteral(".org")); };
    const auto address = [](int n) { return QJsonValue(QString(n - 4, QLatin1Char('h')) + QStringLiteral(":443")); };
    const auto servers = [](int n) {
        QJsonArray list;
        for (int i = 0; i < n; ++i)
            list.append(QStringLiteral("10.0.0.%1").arg(i));
        return QJsonValue(list);
    };
    const auto server = [](int n) {
        return QJsonValue(QJsonArray{QStringLiteral("https://dns.example/") + QString(n - 20, QLatin1Char('q'))});
    };
    QTest::newRow("hostname") << QStringLiteral("hostname") << host(253) << host(254);
    QTest::newRow("address") << QStringLiteral("address") << address(300) << address(301);
    QTest::newRow("username") << QStringLiteral("username") << text(1024) << text(1025);
    QTest::newRow("password") << QStringLiteral("password") << text(1024) << text(1025);
    QTest::newRow("custom_sni") << QStringLiteral("custom_sni") << host(253) << host(254);
    QTest::newRow("name") << QStringLiteral("name") << text(256) << text(257);
    QTest::newRow("dns entries") << QStringLiteral("dns_upstreams") << servers(16) << servers(17);
    QTest::newRow("dns entry length") << QStringLiteral("dns_upstreams") << server(512) << server(513);
}

void TestSubscription::eachLimitHoldsAtItsEdge()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, atLimit);
    QFETCH(QJsonValue, pastIt);
    QVERIFY(parse(withField(sub::kMinimal, key, atLimit)).has_value());
    QVERIFY(refusedFor(withField(sub::kMinimal, key, pastIt), SubscriptionDocError::Invalid, key));
}

// Counted before anything is read, in bytes, as the fetch will bound its read.
void TestSubscription::theBodyHasALimit()
{
    const QByteArray head = sub::kMinimal.left(sub::kMinimal.size() - 1) + ",\"padding\":\"";
    const QByteArray atLimit = head + QByteArray(kMaxSubscriptionBodyBytes - head.size() - 2, 'a') + "\"}";
    QCOMPARE(atLimit.size(), kMaxSubscriptionBodyBytes);
    QCOMPARE(parsed(atLimit).hostname, QStringLiteral("vpn.example.com"));
    const QByteArray pastIt = head + QByteArray(kMaxSubscriptionBodyBytes - head.size() - 1, 'a') + "\"}";
    QVERIFY(refusedFor(pastIt, SubscriptionDocError::TooLarge, QString()));
    // Even when what is past the limit is only white space after a whole document.
    QVERIFY(refusedFor(atLimit + ' ', SubscriptionDocError::TooLarge, QString()));
}

void TestSubscription::anAddressIsOneHostAndPort_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("taken");
    QTest::newRow("ipv4") << QStringLiteral("1.2.3.4:443") << true;
    QTest::newRow("ipv6") << QStringLiteral("[2001:db8::1]:443") << true;
    QTest::newRow("a name") << QStringLiteral("vpn.example.com:8443") << true;
    QTest::newRow("port 1") << QStringLiteral("1.2.3.4:1") << true;
    QTest::newRow("port 65535") << QStringLiteral("1.2.3.4:65535") << true;
    QTest::newRow("two, with a comma") << QStringLiteral("1.2.3.4:443,5.6.7.8:443") << false;
    QTest::newRow("a comma at the end") << QStringLiteral("1.2.3.4:443,") << false;
    QTest::newRow("no port") << QStringLiteral("1.2.3.4") << false;
    QTest::newRow("no host") << QStringLiteral(":443") << false;
    QTest::newRow("port 0") << QStringLiteral("1.2.3.4:0") << false;
    QTest::newRow("port 65536") << QStringLiteral("1.2.3.4:65536") << false;
    QTest::newRow("a port that is no number") << QStringLiteral("1.2.3.4:https") << false;
    QTest::newRow("a space in front") << QStringLiteral(" 1.2.3.4:443") << false;
    QTest::newRow("a space behind") << QStringLiteral("1.2.3.4:443 ") << false;
    QTest::newRow("a space inside") << QStringLiteral("1.2.3.4 :443") << false;
}

void TestSubscription::anAddressIsOneHostAndPort()
{
    QFETCH(QString, address);
    QFETCH(bool, taken);
    const QByteArray body = withField(sub::kMinimal, QStringLiteral("address"), address);
    if (taken)
        QCOMPARE(parsed(body).address, address);
    else
        QVERIFY(refusedFor(body, SubscriptionDocError::Invalid, QStringLiteral("address")));
}

void TestSubscription::noStringHoldsAControlCharacter_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value");
    // Each value is fine but for the character put into it.
    const QList<QPair<const char *, QString>> fields{
            {"hostname", QStringLiteral("vpn.ex%1ample.com")}, {"address", QStringLiteral("1.2.3.4%1:443")},
            {"username", QStringLiteral("al%1ice")},          {"password", QStringLiteral("s3%1cret")},
            {"custom_sni", QStringLiteral("example%1.org")},  {"name", QStringLiteral("Exa%1mple")},
            {"client_random_prefix", QStringLiteral("aa%1bb")}};
    const QList<QPair<const char *, QString>> characters{
            {"NUL", QString(QChar(0))}, {"a newline", QStringLiteral("\n")}, {"a tab", QStringLiteral("\t")},
            {"DEL", QString(QChar(0x7F))}, {"half a surrogate pair", QString(QChar(0xD800))}};
    for (const auto &field : fields) {
        for (const auto &character : characters) {
            QTest::addRow("%s, %s", field.first, character.first)
                    << QString::fromLatin1(field.first) << QJsonValue(field.second.arg(character.second));
        }
        // A server name never holds a format character either, nor a control
        // character past ASCII, which is no password's business to check.
        const QString key = QString::fromLatin1(field.first);
        if (key == QLatin1String("hostname") || key == QLatin1String("custom_sni")) {
            QTest::addRow("%s, a zero-width space", field.first) << key << QJsonValue(field.second.arg(QChar(0x200B)));
            QTest::addRow("%s, a byte order mark", field.first) << key << QJsonValue(field.second.arg(QChar(0xFEFF)));
            QTest::addRow("%s, a space", field.first) << key << QJsonValue(field.second.arg(QStringLiteral(" ")));
        }
        if (key != QLatin1String("username") && key != QLatin1String("password"))
            QTest::addRow("%s, U+0085", field.first) << key << QJsonValue(field.second.arg(QChar(0x85)));
    }
    for (const auto &character : characters) {
        QTest::addRow("dns_upstreams, %s", character.first)
                << QStringLiteral("dns_upstreams")
                << QJsonValue(QJsonArray{QStringLiteral("1.1.1.1"),
                                         QStringLiteral("tls://dns.ex%1ample.com").arg(character.second)});
    }
}

void TestSubscription::noStringHoldsAControlCharacter()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);
    QVERIFY(refusedFor(withField(sub::kMinimal, key, value), SubscriptionDocError::Invalid, key));
}

void TestSubscription::theClientRandomIsTakenAsTheCoreReadsIt()
{
    const QString key = QStringLiteral("client_random_prefix");
    QCOMPARE(parsed(withField(sub::kMinimal, key, QStringLiteral("aabb//"))).clientRandomPrefix,
             QStringLiteral("aabb"));
    QCOMPARE(parsed(withField(sub::kMinimal, key, QStringLiteral("/ff"))).clientRandomPrefix, QString());
    QCOMPARE(parsed(withField(sub::kMinimal, key, QString())).clientRandomPrefix, QString());
    for (const QString &bad : {QStringLiteral("abc"), QStringLiteral("xyz"), QStringLiteral("aa/fff"),
                               QStringLiteral("aa/bb/cc"), QString(66, QLatin1Char('a'))})
        QVERIFY2(refusedFor(withField(sub::kMinimal, key, bad), SubscriptionDocError::Invalid, key), qPrintable(bad));
}

// An operator who sets one of these to nothing has the endpoint serve "", which
// TrustTunnel's own client reads as no SNI. Neither is damage: an empty SNI is
// none, and an empty name is no name.
void TestSubscription::anEmptySniOrNameIsTaken()
{
    const auto noSni = parse(withField(sub::kFull, QStringLiteral("custom_sni"), QString()));
    QVERIFY(noSni.has_value());
    QVERIFY(noSni->customSni.isEmpty());
    const auto noName = parse(withField(sub::kFull, QStringLiteral("name"), QString()));
    QVERIFY(noName.has_value());
    QCOMPARE(noName->name, std::optional<QString>(QString()));
}

// The editor keeps the list as one string, its servers separated by commas,
// semicolons or spaces, so an entry holding one would come back as two servers,
// and an empty one as none.
void TestSubscription::aDnsEntryIsOneServer()
{
    const QString key = QStringLiteral("dns_upstreams");
    QCOMPARE(parsed(withField(sub::kMinimal, key, QJsonArray())).dnsUpstreams,
             std::optional<QStringList>(QStringList()));
    for (const QString &bad : {QStringLiteral("1.1.1.1,8.8.8.8"), QStringLiteral("1.1.1.1 8.8.8.8"),
                               QStringLiteral("1.1.1.1;8.8.8.8"), QString(), QStringLiteral(" 1.1.1.1")})
        QVERIFY2(refusedFor(withField(sub::kMinimal, key, QJsonArray{bad}), SubscriptionDocError::Invalid, key),
                 qPrintable(bad));
    for (const QJsonValue &bad : {QJsonValue(QStringLiteral("1.1.1.1")), QJsonValue(QJsonArray{1}),
                                  QJsonValue(QJsonArray{QJsonValue::Null}), QJsonValue(QJsonObject())})
        QVERIFY(refusedFor(withField(sub::kMinimal, key, bad), SubscriptionDocError::Invalid, key));
}

void TestSubscription::unknownFieldsAreIgnored()
{
    QJsonObject object = objectOf(sub::kFull);
    object.insert(QStringLiteral("tls_profile"), QStringLiteral("chrome"));
    object.insert(QStringLiteral("addresses"), QJsonArray{QStringLiteral("not checked")});
    const QJsonArray nested{1, QJsonObject{{QStringLiteral("a"), QJsonValue()}}};
    object.insert(QStringLiteral("future"), QJsonObject{{QStringLiteral("nested"), nested}});
    const SubscriptionDocument doc = parsed(jsonOf(object));
    QCOMPARE(doc.address, QStringLiteral("1.2.3.4:443"));
    QCOMPARE(doc.name, std::optional<QString>(QStringLiteral("Example")));
}

void TestSubscription::aBodyThatIsNotOneObjectIsInvalid_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::newRow("not JSON") << QByteArray("{not json");
    QTest::newRow("a list") << QByteArray("[1,2,3]");
    QTest::newRow("a list of the document") << "[" + sub::kMinimal + "]";
    QTest::newRow("text after it") << sub::kMinimal + " x";
    QTest::newRow("a second object") << sub::kMinimal + "{}";
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("white space") << QByteArray("  \n");
    QTest::newRow("a byte order mark") << "\xEF\xBB\xBF" + sub::kMinimal;
    QTest::newRow("null") << QByteArray("null");
    QTest::newRow("a string") << QByteArray("\"vpn.example.com\"");
    QTest::newRow("a number") << QByteArray("1");
    QTest::newRow("not UTF-8") << QByteArray(sub::kMinimal).replace("alice", "al\xFF" "ce");
    QTest::newRow("cut short") << sub::kMinimal.left(sub::kMinimal.size() - 1);
}

void TestSubscription::aBodyThatIsNotOneObjectIsInvalid()
{
    QFETCH(QByteArray, body);
    QVERIFY(refusedFor(body, SubscriptionDocError::Invalid, QString()));
}

// serde, the endpoint's own parser, refuses a document that names a field twice.
// Qt keeps the last value, and this is where to see that it still does: whichever
// it keeps, that value is checked like any other.
void TestSubscription::aDuplicateKeyKeepsItsLastValue()
{
    QByteArray twice = sub::kMinimal;
    twice.replace("\"hostname\":\"vpn.example.com\"",
                  "\"hostname\":\"first.example.com\",\"hostname\":\"second.example.com\"");
    QCOMPARE(parsed(twice).hostname, QStringLiteral("second.example.com"));

    QByteArray badLast = sub::kMinimal;
    badLast.replace("\"skip_verification\":false", "\"skip_verification\":false,\"skip_verification\":true");
    QVERIFY(refusedFor(badLast, SubscriptionDocError::SkipVerification, QStringLiteral("skip_verification")));
}

void TestSubscription::anEmptyCertificatePinsNothing()
{
    const SubscriptionDocument doc = parsed(withField(sub::kMinimal, QStringLiteral("certificate"), QString()));
    QVERIFY(doc.certificatePem.isEmpty());
    QVERIFY(!doc.extraPemIgnored);
    for (const QJsonValue &bad : {QJsonValue(1), QJsonValue(QJsonValue::Null), QJsonValue(QJsonArray{certs::kCaPem})})
        QVERIFY(refusedFor(withField(sub::kMinimal, QStringLiteral("certificate"), bad), SubscriptionDocError::Invalid,
                           QStringLiteral("certificate")));
}

// The endpoint serves its whole certificate file, and one that holds the key as
// well is a set-up its own tests use.
void TestSubscription::aKeyBesideTheCertificateIsLeftOut()
{
    const QByteArray body = sub::selfSignedWithKey();
    QVERIFY(body.contains(certs::keyLabel().toLatin1()));
    const SubscriptionDocument doc = parsed(body);
    QCOMPARE(doc.certificatePem, certs::kCaPem);
    QVERIFY(doc.extraPemIgnored);
    QVERIFY(!doc.certificatePem.contains(QStringLiteral("PRIVATE")));

    // In front of it, and in any of a key's spellings, too.
    for (const QString &kind :
         {QString(), QStringLiteral("RSA "), QStringLiteral("EC "), QStringLiteral("ENCRYPTED ")}) {
        const QString pem = certs::pemBlock(certs::keyLabel(kind), certs::kPkcs8Der) + certs::kLeafPem;
        const SubscriptionDocument keyFirst = parsed(withField(sub::kMinimal, QStringLiteral("certificate"), pem));
        QCOMPARE(keyFirst.certificatePem, certs::kLeafPem);
        QVERIFY(keyFirst.extraPemIgnored);
    }
}

void TestSubscription::aFieldWithNoCertificateIsInvalid_data()
{
    QTest::addColumn<QString>("certificate");
    QTest::newRow("a key alone") << certs::keyPem();
    // One whole SEQUENCE, which a key is as well: the rule every path keeps
    // certificates by looks at a certificate's shape, and so does this one.
    QTest::newRow("a key under the certificate label")
            << certs::pemBlock(QStringLiteral("CERTIFICATE"), certs::kPkcs8Der);
    QTest::newRow("an RSA key under the certificate label")
            << certs::pemBlock(QStringLiteral("CERTIFICATE"), certs::kPkcs1Der);
    QTest::newRow("another label")
            << certs::pemBlock(QStringLiteral("TRUSTED CERTIFICATE"), certs::derOf(certs::kCaPem));
    QTest::newRow("base64 that is not")
            << QStringLiteral("-----BEGIN CERTIFICATE-----\nMIIB!xjCC\n-----END CERTIFICATE-----\n");
    QTest::newRow("no BEGIN line") << certs::kCaPem.mid(certs::kCaPem.indexOf(QLatin1Char('\n')) + 1);
    QTest::newRow("text") << QStringLiteral("not a certificate");
    QTest::newRow("white space") << QStringLiteral(" \n ");
}

// Whatever the file import does with such a field: taken, a refresh would clear
// the pin, and the next fetch would trust whatever the system trusts. Refused,
// the last good config stays.
void TestSubscription::aFieldWithNoCertificateIsInvalid()
{
    QFETCH(QString, certificate);
    QVERIFY(refusedFor(withField(sub::kMinimal, QStringLiteral("certificate"), certificate),
                       SubscriptionDocError::Invalid, QStringLiteral("certificate")));
}

// Counted one by one, however many blocks hold them: FreeTunnel 1.1.9 wrote a
// whole chain into one.
void TestSubscription::atMostEightCertificates()
{
    QString eight;
    QByteArray eightDer;
    for (int i = 0; i < 4; ++i) {
        eight += certs::kLeafPem + certs::kCaPem;
        eightDer += certs::derOf(certs::kLeafPem) + certs::derOf(certs::kCaPem);
    }
    const QString key = QStringLiteral("certificate");
    QCOMPARE(certificateCountOf(parsed(withField(sub::kMinimal, key, eight)).certificatePem), 8);
    QVERIFY(refusedFor(withField(sub::kMinimal, key, eight + certs::kCaPem), SubscriptionDocError::Invalid, key));

    const QString eightInOne = certs::pemBlock(QStringLiteral("CERTIFICATE"), eightDer);
    QCOMPARE(parsed(withField(sub::kMinimal, key, eightInOne)).certificatePem, eightInOne);
    const QString nineInOne =
            certs::pemBlock(QStringLiteral("CERTIFICATE"), eightDer + certs::derOf(certs::kCaPem));
    QVERIFY(refusedFor(withField(sub::kMinimal, key, nineInOne), SubscriptionDocError::Invalid, key));
}

// As written, and compared by its DER (see anUnchangedReplyChangesNothing), so a
// server that writes its file another way does not change the config each time.
void TestSubscription::aCertificateIsKeptAsWritten()
{
    QString written = QStringLiteral("  ") + certs::kChainPem + QStringLiteral("\n\n");
    written.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const SubscriptionDocument doc = parsed(withField(sub::kMinimal, QStringLiteral("certificate"), written));
    QCOMPARE(doc.certificatePem, written);
    QVERIFY(!doc.extraPemIgnored);
    QCOMPARE(certificatesDer(doc.certificatePem), certificatesDer(certs::kChainPem));

    const QString legacy = certs::pemBlock(QStringLiteral("X509 CERTIFICATE"), certs::derOf(certs::kCaPem));
    QCOMPARE(parsed(withField(sub::kMinimal, QStringLiteral("certificate"), legacy)).certificatePem, legacy);
}

void TestSubscription::aDamagedBlockBesideACertificateIsLeftOut()
{
    const QString damaged = QStringLiteral("-----BEGIN CERTIFICATE-----\nMIIB!xjCC\n-----END CERTIFICATE-----\n");
    const SubscriptionDocument doc =
            parsed(withField(sub::kMinimal, QStringLiteral("certificate"), damaged + certs::kCaPem));
    QCOMPARE(doc.certificatePem, certs::kCaPem);
    QVERIFY(doc.extraPemIgnored);
}

void TestSubscription::aRefreshOverwritesWhatTheSubscriptionOwns()
{
    ConfigToml current = storedConfig();
    current.certificate = certs::kLeafPem;
    current.customSni = QStringLiteral("old.example.org");
    current.clientRandom = QStringLiteral("0011");
    SubscriptionDocument doc = parsed(sub::kFull);
    doc.hostname = QStringLiteral("new.example.com");
    doc.address = QStringLiteral("5.6.7.8:8443");
    doc.username = QStringLiteral("bob");
    doc.password = QStringLiteral("hunter2");
    doc.certificatePem = certs::kCaPem;

    const SubscriptionApplied applied =
            applySubscription(current, QStringLiteral("old"), doc, SubscriptionApplyMode::Refresh);
    QCOMPARE(applied.config.hostname, QStringLiteral("new.example.com"));
    QCOMPARE(applied.config.addresses, QStringLiteral("5.6.7.8:8443"));
    QCOMPARE(applied.config.username, QStringLiteral("bob"));
    QCOMPARE(applied.password, QStringLiteral("hunter2"));
    QCOMPARE(applied.config.certificate, certs::kCaPem);
    QCOMPARE(applied.config.customSni, QStringLiteral("example.org"));
    QCOMPARE(applied.config.clientRandom, QStringLiteral("aabbcc/ff00ff"));
    QVERIFY(applied.hostnameChanged);
    QCOMPARE(applied.changes.size(), 7);
}

// As TrustTunnel's own client does: the document has one address, and it is the
// list.
void TestSubscription::aRefreshCollapsesTheAddressList()
{
    ConfigToml current = storedConfig();
    current.addresses = QStringLiteral("1.2.3.4:443, 5.6.7.8:443");
    const SubscriptionApplied applied =
            applySubscription(current, QStringLiteral("s3cret"), parsed(sub::kMinimal), SubscriptionApplyMode::Refresh);
    QCOMPARE(applied.config.addresses, QStringLiteral("1.2.3.4:443"));
    QCOMPARE(applied.changes.size(), 1);
    QCOMPARE(applied.changes.first().field, SubscriptionField::Address);
    QCOMPARE(applied.changes.first().oldValue, QStringLiteral("1.2.3.4:443, 5.6.7.8:443"));
    QCOMPARE(applied.changes.first().newValue, QStringLiteral("1.2.3.4:443"));
}

// As TrustTunnel's own client does (omitted_optionals_reset_to_default): once
// the pin is gone, the next fetch trusts the system's authorities.
void TestSubscription::aRefreshClearsWhatTheDocumentLeavesOut()
{
    ConfigToml current = storedConfig();
    current.certificate = certs::kCaPem;
    current.customSni = QStringLiteral("example.org");
    current.clientRandom = QStringLiteral("aabb");
    const SubscriptionApplied applied =
            applySubscription(current, QStringLiteral("s3cret"), parsed(sub::kMinimal), SubscriptionApplyMode::Refresh);
    QVERIFY(applied.config.certificate.isEmpty());
    QVERIFY(applied.config.customSni.isEmpty());
    QVERIFY(applied.config.clientRandom.isEmpty());
    QCOMPARE(applied.changes.size(), 3);
    QVERIFY(!applied.hostnameChanged);
}

void TestSubscription::aRefreshKeepsWhatTheUserOwns()
{
    ConfigToml current = storedConfig();
    current.name = QStringLiteral("Mine");
    current.protocol = QStringLiteral("http3");
    current.allowIpv6 = false;
    current.antiDpi = true;
    current.dns = QStringLiteral("9.9.9.9");
    current.skipVerification = true;
    current.postQuantum = false;
    current.tunSection = QStringLiteral("mtu_size = 1280\n");
    current.extraRootKeys = QStringLiteral("exclusions = [\"example.org\"]\n");
    current.extraEndpointKeys = QStringLiteral("future_key = 1\n");
    current.extraSections = QStringLiteral("\n[other]\nkey = 1\n");

    SubscriptionDocument doc = parsed(sub::kFull);
    doc.upstreamProtocol = QStringLiteral("http2");
    doc.hasIpv6 = true;
    doc.antiDpi = false;
    const ConfigToml next = applySubscription(current, QString(), doc, SubscriptionApplyMode::Refresh).config;
    QCOMPARE(next.name, current.name);
    QCOMPARE(next.protocol, current.protocol);
    QCOMPARE(next.allowIpv6, current.allowIpv6);
    QCOMPARE(next.antiDpi, current.antiDpi);
    QCOMPARE(next.dns, current.dns);
    QCOMPARE(next.skipVerification, current.skipVerification);
    QCOMPARE(next.postQuantum, current.postQuantum);
    QCOMPARE(next.tunSection, current.tunSection);
    QCOMPARE(next.extraRootKeys, current.extraRootKeys);
    QCOMPARE(next.extraEndpointKeys, current.extraEndpointKeys);
    QCOMPARE(next.extraSections, current.extraSections);
}

void TestSubscription::creationTakesTheDocumentsSettings()
{
    ConfigToml ignored = storedConfig();
    ignored.dns = QStringLiteral("9.9.9.9");
    ignored.skipVerification = true;
    ignored.extraSections = QStringLiteral("\n[other]\nkey = 1\n");
    const SubscriptionApplied applied =
            applySubscription(ignored, QStringLiteral("other"), parsed(sub::kFull), SubscriptionApplyMode::Creation);
    const ConfigToml &c = applied.config;
    QCOMPARE(c.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(c.addresses, QStringLiteral("1.2.3.4:443"));
    QCOMPARE(c.protocol, QStringLiteral("http3"));
    QVERIFY(!c.allowIpv6);
    QVERIFY(c.antiDpi);
    QCOMPARE(c.name, QStringLiteral("Example"));
    QCOMPARE(splitDnsList(c.dns), (QStringList{QStringLiteral("1.1.1.1"), QStringLiteral("tls://dns.example.com")}));
    QCOMPARE(c.customSni, QStringLiteral("example.org"));
    QCOMPARE(c.clientRandom, QStringLiteral("aabbcc/ff00ff"));
    QVERIFY(!c.skipVerification);
    QVERIFY(c.extraSections.isEmpty());
    QVERIFY(c.password.isEmpty());
    QCOMPARE(applied.password, QStringLiteral("s3cret"));
    QVERIFY(applied.changes.isEmpty());
    QVERIFY(!applied.hostnameChanged);

    const ConfigToml bare =
            applySubscription(ignored, QString(), parsed(sub::kMinimal), SubscriptionApplyMode::Creation).config;
    QVERIFY(bare.name.isEmpty());
    QVERIFY(bare.dns.isEmpty());
    QCOMPARE(bare.protocol, QStringLiteral("http2"));
    QVERIFY(bare.allowIpv6);
}

// Through the file, as the next refresh reads the config back: the certificate
// written with other line breaks, the client random spelled another way and the
// address list with spaces in it are all the same config.
void TestSubscription::anUnchangedReplyChangesNothing()
{
    QString certificate = certs::kChainPem;
    certificate.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const QByteArray body = withField(withField(sub::kFull, QStringLiteral("certificate"), certificate),
                                      QStringLiteral("client_random_prefix"), QStringLiteral("aabbcc/ff00ff//"));
    const SubscriptionDocument doc = parsed(body);
    const SubscriptionApplied created =
            applySubscription(ConfigToml(), QString(), doc, SubscriptionApplyMode::Creation);
    const ConfigToml stored = parseConfigToml(buildConfigToml(created.config));
    const SubscriptionApplied again = applySubscription(stored, created.password, doc, SubscriptionApplyMode::Refresh);
    QVERIFY2(again.changes.isEmpty(), qPrintable(buildConfigToml(again.config)));
    QVERIFY(!again.hostnameChanged);

    ConfigToml spaced = stored;
    spaced.addresses = QStringLiteral(" 1.2.3.4:443 ,");
    spaced.clientRandom = QStringLiteral("aabbcc/ff00ff/");
    QVERIFY(applySubscription(spaced, created.password, doc, SubscriptionApplyMode::Refresh).changes.isEmpty());

    // And a key a stored copy may still hold beside the certificate is no change
    // either: the startup sweep takes it out, not a refresh.
    ConfigToml withKey = stored;
    withKey.certificate = certs::kChainPem + certs::keyPem();
    QVERIFY(applySubscription(withKey, created.password, doc, SubscriptionApplyMode::Refresh).changes.isEmpty());
}

void TestSubscription::theCredentialsAndCertificateChangeWithoutValues()
{
    ConfigToml current = storedConfig();
    current.certificate = certs::kLeafPem;
    SubscriptionDocument doc = parsed(sub::kSelfSigned);
    doc.username = QStringLiteral("bob");
    doc.password = QStringLiteral("hunter2");
    doc.customSni = QStringLiteral("example.org");
    doc.clientRandomPrefix = QStringLiteral("aabb");
    const SubscriptionApplied applied =
            applySubscription(current, QStringLiteral("s3cret"), doc, SubscriptionApplyMode::Refresh);
    QCOMPARE(applied.changes.size(), 5);
    for (const SubscriptionField field :
         {SubscriptionField::Username, SubscriptionField::Password, SubscriptionField::Certificate}) {
        const SubscriptionChange *change = changeOf(applied, field);
        QVERIFY(change);
        QVERIFY(change->oldValue.isEmpty());
        QVERIFY(change->newValue.isEmpty());
    }
    const SubscriptionChange *sni = changeOf(applied, SubscriptionField::CustomSni);
    QVERIFY(sni);
    QCOMPARE(sni->newValue, QStringLiteral("example.org"));
    const SubscriptionChange *random = changeOf(applied, SubscriptionField::ClientRandom);
    QVERIFY(random);
    QCOMPARE(random->newValue, QStringLiteral("aabb"));

    // The password alone, the change a rotation makes.
    const SubscriptionApplied rotated =
            applySubscription(storedConfig(), QStringLiteral("old"), parsed(sub::kMinimal),
                              SubscriptionApplyMode::Refresh);
    QCOMPARE(rotated.changes.size(), 1);
    QCOMPARE(rotated.changes.first().field, SubscriptionField::Password);
    QVERIFY(rotated.changes.first().oldValue.isEmpty() && rotated.changes.first().newValue.isEmpty());
}

void TestSubscription::theSameHostnameInOtherLettersIsTheSameServer()
{
    ConfigToml current = storedConfig();
    current.hostname = QStringLiteral("VPN.Example.COM");
    const SubscriptionApplied recased =
            applySubscription(current, QStringLiteral("s3cret"), parsed(sub::kMinimal), SubscriptionApplyMode::Refresh);
    QVERIFY(!recased.hostnameChanged);
    // Still written as the server spells it now.
    QCOMPARE(recased.config.hostname, QStringLiteral("vpn.example.com"));
    QCOMPARE(recased.changes.size(), 1);
    QCOMPARE(recased.changes.first().field, SubscriptionField::Hostname);

    const auto changed = [](const QString &before, const QString &after) {
        ConfigToml stored = storedConfig();
        stored.hostname = before;
        SubscriptionDocument doc = parsed(sub::kMinimal);
        doc.hostname = after;
        return applySubscription(stored, QStringLiteral("s3cret"), doc, SubscriptionApplyMode::Refresh).hostnameChanged;
    };
    // "ПРИМЕР.рф", escaped so that no compiler reads the source in another
    // code page.
    QVERIFY(!changed(QStringLiteral("xn--e1afmkfd.xn--p1ai"),
                     QStringLiteral("ПРИМЕР.рф")));
    QVERIFY(changed(QStringLiteral("xn--e1afmkfd.xn--p1ai"), QStringLiteral("other.example.com")));
    // A name that has no ACE form, such as an IPv6 address, is compared in lower
    // case all the same.
    QVERIFY(!changed(QStringLiteral("2001:DB8::1"), QStringLiteral("2001:db8::1")));
    QVERIFY(changed(QStringLiteral("2001:DB8::1"), QStringLiteral("2001:db8::2")));
}

void TestSubscription::aSubscriptionAddressMustBeHttps_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("taken");
    QTest::newRow("https") << QStringLiteral("https://alice:s3cret@vpn.example.com/subscription") << true;
    QTest::newRow("HTTPS") << QStringLiteral("HTTPS://vpn.example.com/subscription") << true;
    QTest::newRow("another port") << QStringLiteral("https://vpn.example.com:8443/subscription") << true;
    QTest::newRow("an IPv6 host") << QStringLiteral("https://[2001:db8::1]/subscription") << true;
    QTest::newRow("http") << QStringLiteral("http://alice:s3cret@vpn.example.com/subscription") << false;
    QTest::newRow("ftp") << QStringLiteral("ftp://vpn.example.com/subscription") << false;
    QTest::newRow("no scheme") << QStringLiteral("vpn.example.com/subscription") << false;
    QTest::newRow("no host") << QStringLiteral("https:///subscription") << false;
    QTest::newRow("userinfo and no host") << QStringLiteral("https://alice@/subscription") << false;
    QTest::newRow("a space") << QStringLiteral("https://vpn.example.com/sub scription") << false;
    QTest::newRow("a space behind") << QStringLiteral("https://vpn.example.com/subscription ") << false;
    QTest::newRow("a tab") << QStringLiteral("https://vpn.example.com/\tsubscription") << false;
    QTest::newRow("a newline") << QStringLiteral("https://vpn.example.com/subscription\n") << false;
    QTest::newRow("a control character") << QStringLiteral("https://vpn.example.com/\x01") << false;
    QTest::newRow("a zero-width space") << QStringLiteral("https://vpn.example.com/sub\u200Bscription") << false;
    QTest::newRow("half a surrogate pair")
            << QStringLiteral("https://vpn.example.com/") + QChar(0xD800) << false;
    QTest::newRow("a bad escape") << QStringLiteral("https://vpn.example.com/%zz") << false;
    QTest::newRow("empty") << QString() << false;
}

void TestSubscription::aSubscriptionAddressMustBeHttps()
{
    QFETCH(QString, url);
    QFETCH(bool, taken);
    QCOMPARE(parseSubscriptionUrl(url).has_value(), taken);
}

void TestSubscription::aSubscriptionAddressHasALengthLimit()
{
    const QString base = QStringLiteral("https://vpn.example.com/");
    const QString atLimit = base + QString(kMaxSubscriptionUrlBytes - base.size(), QLatin1Char('a'));
    QCOMPARE(int(atLimit.toUtf8().size()), kMaxSubscriptionUrlBytes);
    QVERIFY(parseSubscriptionUrl(atLimit).has_value());
    QVERIFY(!parseSubscriptionUrl(atLimit + QLatin1Char('a')).has_value());
    // In bytes, as a link's address is counted: a letter of two bytes counts two.
    const QString wide = base + QString((kMaxSubscriptionUrlBytes - base.size()) / 2 + 1, QChar(0x0416));
    QVERIFY(wide.size() < kMaxSubscriptionUrlBytes);
    QVERIFY(!parseSubscriptionUrl(wide).has_value());
}

// Worked out by hand, not with the code under test: the bytes of user ":" password
// in UTF-8, in base64.
void TestSubscription::theUserinfoBecomesABasicHeader()
{
    const auto header = [](const QString &url) {
        const auto parsedUrl = parseSubscriptionUrl(url);
        return parsedUrl ? parsedUrl->authorization : QByteArray("refused");
    };
    // "alice:s3cret"
    QCOMPARE(header(QStringLiteral("https://alice:s3cret@vpn.example.com/subscription")),
             QByteArray("Basic YWxpY2U6czNjcmV0"));
    // "a@b:p:q": an @ and a colon, escaped in the address, are the user's and the
    // password's own, and a colon in a password is no harm.
    QCOMPARE(header(QStringLiteral("https://a%40b:p%3Aq@vpn.example.com/s")), QByteArray("Basic YUBiOnA6cQ=="));
    // "п:р", as UTF-8 escaped and as it is.
    QCOMPARE(header(QStringLiteral("https://%D0%BF:%D1%80@vpn.example.com/s")), QByteArray("Basic 0L860YA="));
    QCOMPARE(header(QStringLiteral("https://\u043F:\u0440@vpn.example.com/s")), QByteArray("Basic 0L860YA="));
    // "alice:"
    QCOMPARE(header(QStringLiteral("https://alice@vpn.example.com/s")), QByteArray("Basic YWxpY2U6"));
    QCOMPARE(header(QStringLiteral("https://vpn.example.com/subscription")), QByteArray());
}

void TestSubscription::userinfoThatCannotBeSentIsRefused_data()
{
    QTest::addColumn<QString>("url");
    // Basic credentials are user ":" password, so the server would read another
    // user and another password.
    QTest::newRow("a colon in the user") << QStringLiteral("https://a%3Ab:p@vpn.example.com/s");
    // Bytes that are not UTF-8, which Qt decodes to a replacement character.
    QTest::newRow("a stray byte in the user") << QStringLiteral("https://%FF:p@vpn.example.com/s");
    QTest::newRow("a stray byte in the password") << QStringLiteral("https://alice:%C3%28@vpn.example.com/s");
    QTest::newRow("half a character") << QStringLiteral("https://alice:%D0@vpn.example.com/s");
    QTest::newRow("a bad escape") << QStringLiteral("https://al%zzice:p@vpn.example.com/s");
}

void TestSubscription::userinfoThatCannotBeSentIsRefused()
{
    QFETCH(QString, url);
    QVERIFY(!parseSubscriptionUrl(url).has_value());
}

void TestSubscription::theRequestUrlHasNoUserinfoOrFragment()
{
    const auto url = parseSubscriptionUrl(
            QStringLiteral("https://alice:s3cret@vpn.example.com:8443/subscription?token=1#fragment"));
    QVERIFY(url.has_value());
    QCOMPARE(url->requestUrl.toString(QUrl::FullyEncoded),
             QStringLiteral("https://vpn.example.com:8443/subscription?token=1"));
    QVERIFY(url->requestUrl.userInfo().isEmpty());
}

void TestSubscription::theIdentityIsTheSubscriptionNotThePassword()
{
    const auto identity = [](const QString &url) {
        const auto parsedUrl = parseSubscriptionUrl(url);
        return parsedUrl ? parsedUrl->identity : QString();
    };
    const QString one = identity(QStringLiteral("https://alice:s3cret@vpn.example.com/subscription"));
    // The origin, the path, a newline and the user, hashed.
    const QByteArray hashed = "https://vpn.example.com/subscription\nalice";
    QCOMPARE(one, QString::fromLatin1(QCryptographicHash::hash(hashed, QCryptographicHash::Sha256).toHex()));
    QCOMPARE(identity(QStringLiteral("https://alice:rotated@vpn.example.com/subscription")), one);
    QCOMPARE(identity(QStringLiteral("https://alice:s3cret@VPN.Example.com:443/subscription#x")), one);
    QVERIFY(identity(QStringLiteral("https://bob:s3cret@vpn.example.com/subscription")) != one);
    QVERIFY(identity(QStringLiteral("https://alice:s3cret@vpn.example.com/other")) != one);
    QVERIFY(identity(QStringLiteral("https://alice:s3cret@vpn.example.com:8443/subscription")) != one);
    QVERIFY(identity(QStringLiteral("https://alice:s3cret@vpn.example.com/Subscription")) != one);
    QCOMPARE(identity(QStringLiteral("https://alice:s3cret@vpn.example.com")),
             identity(QStringLiteral("https://alice:s3cret@vpn.example.com/")));
}

void TestSubscription::redactUrlKeepsNothingSecret()
{
    const QUrl url(QStringLiteral("https://alice:s3cret@vpn.example.com:8443/sub/token?key=k3y#frag"));
    const QString origin = redactUrl(url);
    QCOMPARE(origin, QStringLiteral("https://vpn.example.com:8443"));
    const QString withPath = redactUrl(url, RedactKeep::OriginAndPath);
    QCOMPARE(withPath, QStringLiteral("https://vpn.example.com:8443/sub/token"));
    for (const QString &shown : {origin, withPath}) {
        for (const QString &secret : {QStringLiteral("@"), QStringLiteral("alice"), QStringLiteral("s3cret"),
                                      QStringLiteral("k3y"), QStringLiteral("frag"), QStringLiteral("?")})
            QVERIFY2(!shown.contains(secret), qPrintable(shown));
    }
    QCOMPARE(redactUrl(QUrl(QStringLiteral("https://u:p@[2001:db8::1]:8443/s"))),
             QStringLiteral("https://[2001:db8::1]:8443"));
    QCOMPARE(redactUrl(QUrl(QStringLiteral("https://\u043F\u0440\u0438\u043C\u0435\u0440.\u0440\u0444/s"))),
             QStringLiteral("https://xn--e1afmkfd.xn--p1ai"));
    QCOMPARE(redactUrl(QUrl(QStringLiteral("https://VPN.Example.com:443/s"))),
             QStringLiteral("https://vpn.example.com"));
    // A redirect may point elsewhere, and is logged the same way.
    QCOMPARE(redactUrl(QUrl(QStringLiteral("http://u:p@vpn.example.com:443/s"))),
             QStringLiteral("http://vpn.example.com:443"));
}

void TestSubscription::theRefreshIntervalIsClamped()
{
    const qint64 hour = 3600;
    QCOMPARE(subscriptionRefreshInterval(std::nullopt), 24 * hour);
    QCOMPARE(subscriptionRefreshInterval(hour / 2), hour);
    QCOMPARE(subscriptionRefreshInterval(0), hour);
    QCOMPARE(subscriptionRefreshInterval(-5), hour);
    QCOMPARE(subscriptionRefreshInterval(hour), hour);
    QCOMPARE(subscriptionRefreshInterval(6 * hour), 6 * hour);
    QCOMPARE(subscriptionRefreshInterval(7 * 24 * hour), 7 * 24 * hour);
    QCOMPARE(subscriptionRefreshInterval(10 * 24 * hour), 7 * 24 * hour);
}

// Until a version of the format defines one, nothing a server sends is taken for
// a hint, so every config refreshes once a day.
void TestSubscription::noRefreshHintIsReadYet()
{
    QJsonObject document = objectOf(sub::kMinimal);
    document.insert(QStringLiteral("refresh_interval"), 3600);
    document.insert(QStringLiteral("update_interval"), 3600);
    QVERIFY(!subscriptionRefreshHint(document, QByteArray("6")).has_value());
    QVERIFY(!subscriptionRefreshHint(QJsonObject(), QByteArray()).has_value());
}

void TestSubscription::theAddressRuleTheEditorShares_data()
{
    QTest::addColumn<QString>("address");
    QTest::addColumn<bool>("valid");
    QTest::newRow("ipv4") << QStringLiteral("1.2.3.4:443") << true;
    QTest::newRow("ipv6") << QStringLiteral("[2001:db8::1]:443") << true;
    QTest::newRow("a name") << QStringLiteral("vpn.example.com:1") << true;
    QTest::newRow("spaces around, which the editor trims") << QStringLiteral(" 1.2.3.4:65535 ") << true;
    QTest::newRow("no port") << QStringLiteral("1.2.3.4") << false;
    QTest::newRow("an empty port") << QStringLiteral("1.2.3.4:") << false;
    QTest::newRow("no host") << QStringLiteral(":443") << false;
    // The editor's list is split at its commas alone, so "a:443, :443" has this
    // entry, and a space is no host once trimmed.
    QTest::newRow("a space for a host") << QStringLiteral(" :443") << false;
    QTest::newRow("port 0") << QStringLiteral("1.2.3.4:0") << false;
    QTest::newRow("port 65536") << QStringLiteral("1.2.3.4:65536") << false;
    QTest::newRow("a port that is no number") << QStringLiteral("1.2.3.4:x") << false;
    QTest::newRow("empty") << QString() << false;
}

void TestSubscription::theAddressRuleTheEditorShares()
{
    QFETCH(QString, address);
    QFETCH(bool, valid);
    QCOMPARE(isValidEndpointAddress(address), valid);
}

// The seed of the random loops below, as test_deeplink's: fixed, so a failing
// input comes back on every run, or FT_FUZZ_SEED for others, and printed either way.
static quint32 fuzzSeed(const char *loop, quint32 fixed)
{
    bool fromEnv = false;
    const quint32 env = qEnvironmentVariable("FT_FUZZ_SEED").toUInt(&fromEnv);
    const quint32 seed = fromEnv ? env : fixed;
    qInfo("%s: seed %u (FT_FUZZ_SEED=%u repeats it)", loop, seed, seed);
    return seed;
}

static bool hasControl(const QString &s)
{
    return !s.isValidUtf16()
            || std::any_of(s.cbegin(), s.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}

// A name sent on the wire: no space and no format character either.
static bool isNoWireName(const QString &s)
{
    return hasControl(s) || std::any_of(s.cbegin(), s.cend(), [](QChar c) {
               return c.isSpace() || c.category() == QChar::Other_Format;
           });
}

// What tomlEsc() would drop.
static bool hasC0OrDel(const QString &s)
{
    return !s.isValidUtf16()
            || std::any_of(s.cbegin(), s.cend(), [](QChar c) { return c < QChar(0x20) || c == QChar(0x7F); });
}

// What every document that parses has to be, whatever came in: nothing past a
// limit, no control character in what reaches the config, an address the editor
// would take, and certificates alone.
static QString brokenRule(const SubscriptionDocument &doc)
{
    if (doc.hostname.isEmpty() || doc.hostname.size() > 253 || isNoWireName(doc.hostname))
        return QStringLiteral("hostname");
    if (doc.address.size() > 300 || doc.address.contains(QLatin1Char(',')) || isNoWireName(doc.address)
            || !isValidEndpointAddress(doc.address))
        return QStringLiteral("address");
    for (const QString &credential : {doc.username, doc.password}) {
        if (credential.isEmpty() || credential.size() > 1024 || hasC0OrDel(credential))
            return QStringLiteral("credentials");
    }
    if (doc.customSni.size() > 253 || isNoWireName(doc.customSni) || !isValidClientRandom(doc.clientRandomPrefix))
        return QStringLiteral("sni or client random");
    if (doc.name && (doc.name->size() > 256 || hasControl(*doc.name)))
        return QStringLiteral("name");
    if (doc.dnsUpstreams && doc.dnsUpstreams->size() > 16)
        return QStringLiteral("dns");
    for (const QString &server : doc.dnsUpstreams.value_or(QStringList())) {
        if (server.size() > 512 || hasControl(server) || splitDnsList(server) != QStringList{server})
            return QStringLiteral("dns");
    }
    if (!doc.certificatePem.isEmpty()
            && (certificateCountOf(doc.certificatePem) > 8 || certificateCountOf(doc.certificatePem) == 0
                || certificatesOnly(doc.certificatePem) != doc.certificatePem))
        return QStringLiteral("certificate");
    if (doc.upstreamProtocol != QLatin1String("http2") && doc.upstreamProtocol != QLatin1String("http3"))
        return QStringLiteral("protocol");
    return QString();
}

void TestSubscription::mutatedDocumentsNeverBreakTheRules()
{
    const QList<QByteArray> seeds{sub::kMinimal, sub::kFull, sub::kSelfSigned, sub::selfSignedWithKey(),
                                  sub::kAuthKey};
    QRandomGenerator rng(fuzzSeed("mutatedDocumentsNeverBreakTheRules", 20261026u));
    int taken = 0;
    for (int i = 0; i < 1000 * seeds.size(); ++i) {
        QByteArray body = seeds.at(i % seeds.size());
        const int n = rng.bounded(1, 6);
        for (int j = 0; j < n; ++j) {
            const int pos = rng.bounded(int(body.size()) + 1);
            // Mostly printable, which keeps more of the mutants JSON.
            const char byte = rng.bounded(4) == 0 ? static_cast<char>(rng.generate() & 0xFF)
                                                  : static_cast<char>(rng.bounded(0x20, 0x7F));
            if (pos == body.size() || rng.bounded(3) == 0)
                body.insert(pos, byte);
            else
                body[pos] = byte;
        }
        const auto doc = parseSubscriptionJson(body);
        if (!doc)
            continue;
        ++taken;
        const QString broken = brokenRule(*doc);
        const QString shown = QString::fromLatin1(body.toPercentEncoding(" {}\":,[]"));
        QVERIFY2(broken.isEmpty(), qPrintable(broken + QStringLiteral(": ") + shown));
    }
    // Most single changes to a document leave one it takes, so a loop that took
    // none would be testing nothing.
    QVERIFY2(taken > 100, qPrintable(QString::number(taken)));
}

// Each field set to values of every type and of random text, so that every check
// meets values it refuses and values it takes.
void TestSubscription::randomFieldValuesNeverBreakTheRules()
{
    const QStringList keys{QStringLiteral("version"), QStringLiteral("hostname"), QStringLiteral("address"),
                           QStringLiteral("username"), QStringLiteral("password"), QStringLiteral("has_ipv6"),
                           QStringLiteral("upstream_protocol"), QStringLiteral("anti_dpi"),
                           QStringLiteral("skip_verification"), QStringLiteral("certificate"),
                           QStringLiteral("custom_sni"), QStringLiteral("client_random_prefix"), QStringLiteral("name"),
                           QStringLiteral("dns_upstreams"), QStringLiteral("client_random_auth_key")};
    const QString alphabet = QStringLiteral("aZ09.:,;/[] -_\t\n") + QChar(0) + QChar(0x7F) + QChar(0x85) + QChar(0x200B)
            + QChar(0xD800) + QChar(0x0416);
    QRandomGenerator rng(fuzzSeed("randomFieldValuesNeverBreakTheRules", 20261027u));
    const auto randomText = [&] {
        QString s;
        const int n = rng.bounded(0, 12);
        for (int i = 0; i < n; ++i)
            s += alphabet.at(rng.bounded(alphabet.size()));
        return s;
    };
    const auto randomValue = [&]() -> QJsonValue {
        switch (rng.bounded(9)) {
        case 0: return kAbsent;
        case 1: return QJsonValue(QJsonValue::Null);
        case 2: return QJsonValue(rng.bounded(2) == 0);
        case 3: return QJsonValue(rng.bounded(-2, 4));
        case 4: return QJsonValue(rng.generateDouble() * 4);
        case 5: return QJsonValue(QJsonArray{randomText(), randomText()});
        case 6: return QJsonValue(QJsonObject{{randomText(), randomText()}});
        case 7: return QJsonValue(certs::kChainPem + (rng.bounded(2) == 0 ? certs::keyPem() : QString()));
        default: return QJsonValue(randomText() + QStringLiteral("1.2.3.4:443").left(rng.bounded(12)));
        }
    };
    int taken = 0;
    for (int i = 0; i < 20000; ++i) {
        QByteArray body = sub::kFull;
        const int n = rng.bounded(1, 3);
        for (int j = 0; j < n; ++j)
            body = withField(body, keys.at(rng.bounded(keys.size())), randomValue());
        const auto doc = parseSubscriptionJson(body);
        if (!doc)
            continue;
        ++taken;
        const QString broken = brokenRule(*doc);
        QVERIFY2(broken.isEmpty(), qPrintable(broken + QStringLiteral(": ") + QString::fromUtf8(body)));
    }
    QVERIFY2(taken > 500, qPrintable(QString::number(taken)));
}

QTEST_GUILESS_MAIN(TestSubscription)
#include "test_subscription.moc"
