// cppcheck-suppress-file missingIncludeSystem
#include "core/Subscription.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

#include "core/DeepLink.h"

namespace freetunnel {
namespace {

// The longest each field may be: far past anything an endpoint serves, and short
// enough that nothing unreasonable reaches a config.
constexpr qsizetype kMaxHostnameLength = 253; // also a custom SNI
constexpr qsizetype kMaxAddressLength = 300;
constexpr qsizetype kMaxCredentialLength = 1024;
constexpr qsizetype kMaxNameLength = 256;
constexpr qsizetype kMaxDnsUpstreams = 16;
constexpr qsizetype kMaxDnsUpstreamLength = 512;

constexpr qint64 kHour = 3600;
constexpr qint64 kDefaultRefreshSeconds = 24 * kHour;
constexpr qint64 kMinRefreshSeconds = kHour;
constexpr qint64 kMaxRefreshSeconds = 7 * 24 * kHour;

// No space, control or format character, by code point, so one outside the BMP is
// not let through as a pair of surrogates: what the deep link takes in a
// subscription address (applyDeepLinkSubscriptionTlv), and what a name sent on
// the wire, a hostname or an SNI, never holds.
bool isWireText(const QString &s)
{
    const QList<uint> codePoints = s.toUcs4();
    return std::none_of(codePoints.cbegin(), codePoints.cend(), [](char32_t c) {
        const QChar::Category category = QChar::category(c);
        return QChar::isSpace(c) || category == QChar::Other_Control || category == QChar::Other_Format;
    });
}

// What tomlEsc() drops without a word: the file, and the config the core is
// given, would hold other credentials than the server sent.
bool hasNoC0OrDel(const QString &s)
{
    return std::none_of(s.cbegin(), s.cend(), [](QChar c) { return c < QChar(0x20) || c == QChar(0x7F); });
}

bool hasNoControl(const QString &s)
{
    return std::none_of(s.cbegin(), s.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}

// Whether Qt takes `s` as the host of a URL, and nothing more: a name, an IDN or
// an IP address, with no label longer than DNS allows, and nothing a URL reads as
// a path, a userinfo or a port, such as "/", "@" or ":443". No substitute for
// isWireText(): Qt drops a zero-width space or a soft hyphen before it checks.
bool isUrlHost(const QString &s)
{
    QUrl url;
    url.setHost(s, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty();
}

// The name the core sends as the SNI and checks the certificate against, and the
// one the user is asked about when it changes, so "vpn.example.com/x" or
// "vpn.example.com@x", which read like the old name, is refused. Qt also takes a
// whole "[v6]" for a host, which a hostname is never written as.
bool isHostname(const QString &s)
{
    return !s.isEmpty() && s.size() <= kMaxHostnameLength && isWireText(s) && !s.startsWith(QLatin1Char('['))
            && isUrlHost(s);
}

bool isPortDigits(const QString &s)
{
    const auto isDigit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); };
    return !s.isEmpty() && std::all_of(s.cbegin(), s.cend(), isDigit);
}

// "host:port" or "[v6]:port", as the core resolves an address: an IPv6 address
// only in brackets, a name or an IPv4 address with no colon of its own, and a
// port in digits alone. The editor's rule, which this adds to, also takes
// "[1.2.3.4:443", "2001:db8::1:443" (an IPv6 address with no port, to the core)
// and "a/b:443", which the core cannot connect to, and "1.2.3.4:+443", which no
// endpoint writes. A refresh that took one would replace an address that works.
bool isHostAndPort(const QString &s)
{
    const qsizetype colon = s.lastIndexOf(QLatin1Char(':'));
    if (colon < 0 || !isPortDigits(s.mid(colon + 1)))
        return false;
    const QString host = s.left(colon);
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']'))) {
        const QString v6 = host.mid(1, host.size() - 2);
        return v6.contains(QLatin1Char(':')) && isUrlHost(v6);
    }
    return !host.contains(QLatin1Char(':')) && isUrlHost(host);
}

// No comma: a config keeps its addresses as one comma-separated list, and the
// one address would be written as two.
bool isAddress(const QString &s)
{
    return s.size() <= kMaxAddressLength && !s.contains(QLatin1Char(',')) && isWireText(s)
            && isValidEndpointAddress(s) && isHostAndPort(s);
}

bool isCredential(const QString &s)
{
    return !s.isEmpty() && s.size() <= kMaxCredentialLength && hasNoC0OrDel(s);
}

bool isUpstreamProtocol(const QString &s)
{
    return s == QLatin1String("http2") || s == QLatin1String("http3");
}

bool isCustomSni(const QString &s)
{
    return s.size() <= kMaxHostnameLength && isWireText(s);
}

// As the deep link takes one (applyDeepLinkClientRandomTlv): what the core
// reads as no mask, or no prefix, is repaired, and anything else is refused.
bool isClientRandom(const QString &s)
{
    return isValidClientRandom(clientRandomForCore(s));
}

bool isName(const QString &s)
{
    return s.size() <= kMaxNameLength && hasNoControl(s);
}

// One server, and one only once the list is written as the editor keeps it:
// separated by commas, semicolons or spaces (splitDnsList), so an entry holding
// one of those, or nothing at all, would come back as other servers.
bool isDnsUpstream(const QString &s)
{
    return s.size() <= kMaxDnsUpstreamLength && s.isValidUtf16() && hasNoControl(s)
            && splitDnsList(s) == QStringList{s};
}

bool isAnyText(const QString &)
{
    return true;
}

using TextRule = bool (*)(const QString &);

// The fields of one document, with the first thing wrong among them.
class DocumentReader {
public:
    DocumentReader(const QJsonObject &object, SubscriptionParseError *error) : m_object(object), m_error(error) {}

    QJsonValue value(const char *key) const { return m_object.value(QLatin1String(key)); }

    bool refuse(const char *key, SubscriptionDocError kind = SubscriptionDocError::Invalid) const
    {
        if (m_error) {
            m_error->kind = kind;
            m_error->field = QString::fromLatin1(key);
        }
        return false;
    }

    // A string the document has to have, which `rule` takes.
    bool required(const char *key, TextRule rule, QString *out) const
    {
        return value(key).isUndefined() ? refuse(key) : text(key, rule, out);
    }

    // One it may leave out, which leaves *out as it was. Present, it has to be a
    // string all the same: null, or a value of another type, is damage, not an
    // omission.
    bool optional(const char *key, TextRule rule, QString *out) const
    {
        return value(key).isUndefined() || text(key, rule, out);
    }

    bool optional(const char *key, TextRule rule, std::optional<QString> *out) const
    {
        QString s;
        if (value(key).isUndefined())
            return true;
        if (!text(key, rule, &s))
            return false;
        *out = s;
        return true;
    }

    bool flag(const char *key, bool *out) const
    {
        const QJsonValue v = value(key);
        if (v.isUndefined())
            return true;
        if (!v.isBool())
            return refuse(key);
        *out = v.toBool();
        return true;
    }

private:
    // Well-formed UTF-16 as well: a JSON escape can name half a surrogate pair,
    // which UTF-8 cannot carry into the config.
    bool text(const char *key, TextRule rule, QString *out) const
    {
        const QJsonValue v = value(key);
        const QString s = v.toString();
        if (!v.isString() || !s.isValidUtf16() || !rule(s))
            return refuse(key);
        *out = s;
        return true;
    }

    const QJsonObject &m_object;
    SubscriptionParseError *m_error;
};

void setError(SubscriptionParseError *error, SubscriptionDocError kind)
{
    if (error) {
        error->kind = kind;
        error->field.clear();
    }
}

// The body's one object. A byte order mark is refused, which Qt would read past:
// RFC 8259 forbids sending one, and serde, which the endpoint and its own client
// use, refuses one too.
std::optional<QJsonObject> documentObject(const QByteArray &body, SubscriptionParseError *error)
{
    if (body.size() > kMaxSubscriptionBodyBytes) {
        setError(error, SubscriptionDocError::TooLarge);
        return std::nullopt;
    }
    const QJsonDocument document =
            body.startsWith("\xEF\xBB\xBF") ? QJsonDocument() : QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        setError(error, SubscriptionDocError::Invalid);
        return std::nullopt;
    }
    return document.object();
}

// Before any other field, as readDeepLinkVersion() reads a link's: a newer format
// may mean something else by a field, so a document in one has to be refused as
// newer, not as damaged by what that field holds. A whole number, compared as the
// double JSON gives, so a version past what a qint64 holds is not cut down to one
// this client reads.
bool readVersion(const DocumentReader &in)
{
    const QJsonValue v = in.value("version");
    const double version = v.toDouble();
    if (!v.isDouble() || version < 1 || std::floor(version) != version)
        return in.refuse("version");
    return version <= kSubscriptionMaxVersion || in.refuse("version", SubscriptionDocError::UnsupportedVersion);
}

bool readServer(const DocumentReader &in, SubscriptionDocument *doc)
{
    return in.required("hostname", isHostname, &doc->hostname) && in.required("address", isAddress, &doc->address)
            && in.required("username", isCredential, &doc->username)
            && in.required("password", isCredential, &doc->password);
}

// skip_verification is never raised by a subscription: a reply that asks for it
// is refused, whatever else it holds, since nothing would then check that the
// next one came from the same server.
bool readSettings(const DocumentReader &in, SubscriptionDocument *doc)
{
    bool skipVerification = false;
    if (!in.flag("has_ipv6", &doc->hasIpv6) || !in.flag("anti_dpi", &doc->antiDpi)
            || !in.flag("skip_verification", &skipVerification))
        return false;
    if (skipVerification)
        return in.refuse("skip_verification", SubscriptionDocError::SkipVerification);
    return in.optional("upstream_protocol", isUpstreamProtocol, &doc->upstreamProtocol);
}

// certificatesDer() gives whole certificates, one after another, so every step
// lands on the next one. Should it ever give less, the count stops there rather
// than read from -1, the DER reader's "not one".
int certificateCount(const QByteArray &der)
{
    int count = 0;
    int pos = 0;
    while (pos < der.size()) {
        const int end = derElementEnd(der, pos);
        if (end <= pos)
            break;
        ++count;
        pos = end;
    }
    return count;
}

// The certificates by the rule every other path keeps them by (certificatesOnly),
// and no more than kMaxSubscriptionCertificates. Anything else beside them, a
// private key above all, is noted and never stored.
//
// A field with no certificate left in it is a damaged reply, whatever a file
// import does with one. Taken, a refresh would clear the pin, and the next fetch
// would trust whatever the system trusts; refused, the last good config stays.
// An empty field pins nothing, as one left out does.
bool readCertificate(const DocumentReader &in, SubscriptionDocument *doc)
{
    QString pem;
    if (!in.optional("certificate", isAnyText, &pem))
        return false;
    if (pem.isEmpty())
        return true;
    bool dropped = false;
    const QByteArray der = certificatesDer(pem, &dropped);
    if (der.isEmpty() || certificateCount(der) > kMaxSubscriptionCertificates)
        return in.refuse("certificate");
    doc->certificatePem = certificatesOnly(pem);
    doc->extraPemIgnored = dropped;
    return true;
}

bool readDnsUpstreams(const DocumentReader &in, SubscriptionDocument *doc)
{
    const QJsonValue v = in.value("dns_upstreams");
    if (v.isUndefined())
        return true;
    const QJsonArray list = v.toArray();
    if (!v.isArray() || list.size() > kMaxDnsUpstreams)
        return in.refuse("dns_upstreams");
    QStringList servers;
    for (const QJsonValue &entry : list) {
        if (!entry.isString() || !isDnsUpstream(entry.toString()))
            return in.refuse("dns_upstreams");
        servers << entry.toString();
    }
    doc->dnsUpstreams = servers;
    return true;
}

// The key the server checks the client random against, as a link's tag 0x0F
// takes it: whole bytes of hex, or none. A key the core cannot decode would be
// dropped by it, silently, and the server would turn the client away.
bool isAuthKey(const QString &s)
{
    return isValidClientRandomAuthKey(s);
}

bool readOptionalFields(const DocumentReader &in, SubscriptionDocument *doc)
{
    QString clientRandom;
    if (!in.optional("custom_sni", isCustomSni, &doc->customSni)
            || !in.optional("client_random_prefix", isClientRandom, &clientRandom)
            || !in.optional("client_random_auth_key", isAuthKey, &doc->clientRandomAuthKey)
            || !in.optional("name", isName, &doc->name))
        return false;
    doc->clientRandomPrefix = clientRandomForCore(clientRandom);
    return readDnsUpstreams(in, doc);
}

QStringList addressList(const QString &addresses)
{
    QStringList out;
    for (const QString &address : addresses.split(QLatin1Char(','), Qt::SkipEmptyParts))
        out << address.trimmed();
    return out;
}

// A hostname as a server is named: in ACE form, in any case. Should Qt find no
// ACE form, the name in lower case.
QString hostIdentity(const QString &hostname)
{
    const QByteArray ace = QUrl::toAce(hostname);
    return (ace.isEmpty() ? hostname : QString::fromLatin1(ace)).toLower();
}

// The certificates compared as DER, so the same ones written another way, with
// other line breaks or beside a key a sweep has not taken out yet, are no change.
QList<SubscriptionChange> changesBetween(const ConfigToml &before, const QString &beforePassword,
                                         const ConfigToml &after, const QString &password,
                                         const QString &authKey)
{
    QList<SubscriptionChange> out;
    const auto withValues = [&out](SubscriptionField field, const QString &from, const QString &to) {
        if (from != to)
            out.append({field, from, to});
    };
    const auto withoutValues = [&out](SubscriptionField field, bool changed) {
        if (changed)
            out.append({field, QString(), QString()});
    };
    withValues(SubscriptionField::Hostname, before.hostname, after.hostname);
    if (addressList(before.addresses) != addressList(after.addresses))
        out.append({SubscriptionField::Address, before.addresses, after.addresses});
    withoutValues(SubscriptionField::Username, before.username != after.username);
    withoutValues(SubscriptionField::Password, beforePassword != password);
    withoutValues(SubscriptionField::AuthKey, before.clientRandomAuthKey != authKey);
    withoutValues(SubscriptionField::Certificate,
                  certificatesDer(before.certificate) != certificatesDer(after.certificate));
    withValues(SubscriptionField::CustomSni, before.customSni, after.customSni);
    withValues(SubscriptionField::ClientRandom, clientRandomForCore(before.clientRandom), after.clientRandom);
    return out;
}

// One part of the userinfo, decoded, or nothing when its bytes are not UTF-8.
// QUrl puts a replacement character where they are not, and the server would be
// sent other credentials than the address holds.
std::optional<QString> userInfoPart(const QString &decoded, const QString &encoded)
{
    if (decoded.toUtf8() != QByteArray::fromPercentEncoding(encoded.toLatin1()))
        return std::nullopt;
    return decoded;
}

// A path left out is "/", as a request sends it, and so the same subscription.
QString subscriptionIdentity(const QUrl &url, const QString &user)
{
    QString path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
        path = QStringLiteral("/");
    const QString text = redactUrl(url).toLower() + path + QLatin1Char('\n') + user;
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

} // namespace

std::optional<SubscriptionDocument> parseSubscriptionJson(const QByteArray &body, SubscriptionParseError *error)
{
    const std::optional<QJsonObject> object = documentObject(body, error);
    if (!object)
        return std::nullopt;
    const DocumentReader in(*object, error);
    SubscriptionDocument doc;
    if (!readVersion(in) || !readServer(in, &doc) || !readSettings(in, &doc) || !readCertificate(in, &doc)
            || !readOptionalFields(in, &doc))
        return std::nullopt;
    return doc;
}

SubscriptionApplied applySubscription(const ConfigToml &current, const QString &currentPassword,
                                      const SubscriptionDocument &document, SubscriptionApplyMode mode)
{
    const bool creation = mode == SubscriptionApplyMode::Creation;
    SubscriptionApplied out;
    ConfigToml &next = out.config;
    next = creation ? ConfigToml() : current;
    next.hostname = document.hostname;
    next.addresses = document.address;
    next.username = document.username;
    next.password.clear();
    next.clientRandomAuthKey.clear();
    next.certificate = document.certificatePem;
    next.customSni = document.customSni;
    next.clientRandom = document.clientRandomPrefix;
    out.password = document.password;
    // A document that does not mention the key leaves it as it is (see the header).
    out.clientRandomAuthKey =
            document.clientRandomAuthKey.value_or(creation ? QString() : current.clientRandomAuthKey);
    if (creation) {
        next.protocol = document.upstreamProtocol;
        next.allowIpv6 = document.hasIpv6;
        next.antiDpi = document.antiDpi;
        next.name = document.name.value_or(QString());
        next.dns = document.dnsUpstreams.value_or(QStringList()).join(QStringLiteral(", "));
        return out;
    }
    out.changes = changesBetween(current, currentPassword, next, out.password, out.clientRandomAuthKey);
    out.hostnameChanged = !sameSubscriptionHost(current.hostname, next.hostname);
    return out;
}

bool sameSubscriptionHost(const QString &a, const QString &b)
{
    return hostIdentity(a) == hostIdentity(b);
}

std::optional<SubscriptionUrl> parseSubscriptionUrl(const QString &url)
{
    // As a link's subscription address is taken, which also keeps it under the
    // 2560 bytes Windows Credential Manager holds.
    if (url.toUtf8().size() > kMaxSubscriptionUrlBytes || !url.isValidUtf16() || !isWireText(url))
        return std::nullopt;
    const QUrl parsed(url, QUrl::StrictMode);
    // Port 0 is no port a server listens on: refused here, it is a bad address,
    // not a fetch that fails and is tried again.
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https") || parsed.host().isEmpty()
            || parsed.port() == 0)
        return std::nullopt;
    const std::optional<QString> user =
            userInfoPart(parsed.userName(QUrl::FullyDecoded), parsed.userName(QUrl::FullyEncoded));
    const std::optional<QString> password =
            userInfoPart(parsed.password(QUrl::FullyDecoded), parsed.password(QUrl::FullyEncoded));
    // Basic credentials are the user, a colon and the password: a user with a
    // colon in it cannot be sent.
    if (!user || !password || user->contains(QLatin1Char(':')))
        return std::nullopt;
    SubscriptionUrl out;
    out.requestUrl = parsed.adjusted(QUrl::RemoveUserInfo | QUrl::RemoveFragment);
    if (!user->isEmpty() || !password->isEmpty())
        out.authorization = "Basic " + (*user + QLatin1Char(':') + *password).toUtf8().toBase64();
    out.identity = subscriptionIdentity(parsed, *user);
    return out;
}

// Built from the parts that may be shown, never by taking out the ones that may
// not, so nothing new in a URL can slip through. The default port is left out:
// an address with it and one without are the same server.
//
// Only a valid URL with a host has an origin. Without a host, Qt has read
// whatever came first as the scheme and the rest as the path, so
// "alice:s3cret@host/s", an address pasted without its "https://", has the user
// for its scheme and the password in its path. An invalid one keeps what Qt
// could make of it, which is not to be relied on. Nothing of either is shown.
QString redactUrl(const QUrl &url, RedactKeep keep)
{
    if (!url.isValid() || url.host().isEmpty())
        return QString();
    QUrl origin;
    origin.setScheme(url.scheme());
    origin.setHost(url.host());
    if (url.port() != 443 || url.scheme() != QLatin1String("https"))
        origin.setPort(url.port());
    QString out = QString::fromLatin1(origin.toEncoded());
    if (keep == RedactKeep::OriginAndPath)
        out += url.path(QUrl::FullyEncoded);
    return out;
}

// A hint of zero or less is no hint: a server may mean "never" or "unset" by it,
// and neither is a reason to fetch every hour, which would show the real
// hostname on the wire 24 times a day.
qint64 subscriptionRefreshInterval(std::optional<qint64> hintSeconds)
{
    if (!hintSeconds || *hintSeconds <= 0)
        return kDefaultRefreshSeconds;
    return std::clamp(*hintSeconds, kMinRefreshSeconds, kMaxRefreshSeconds);
}

// Nothing yet: endpoint 1.3 sends no hint, and no field or header is defined for
// one. When a version of the format defines it, it is read here, and every
// refresh takes it through subscriptionRefreshInterval().
std::optional<qint64> subscriptionRefreshHint(const QJsonObject &document,
                                              const QByteArray &profileUpdateIntervalHeader)
{
    Q_UNUSED(document);
    Q_UNUSED(profileUpdateIntervalHeader);
    return std::nullopt;
}

} // namespace freetunnel
