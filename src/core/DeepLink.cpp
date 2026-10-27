// cppcheck-suppress-file missingIncludeSystem
#include "core/DeepLink.h"
#include "core/ConfigToml.h"

#include <QCoreApplication>

#include <algorithm>
#include <limits>

namespace freetunnel {
namespace {

bool tlvLengthFits(const QByteArray &buf, int pos, quint64 len)
{
    if (len > static_cast<quint64>(std::numeric_limits<int>::max()))
        return false;
    if (pos < 0 || pos > buf.size())
        return false;
    return len <= static_cast<quint64>(buf.size() - pos);
}

// ---- QUIC/TLS variable-length integer (RFC 9000 §16) ----

bool readVarint(const QByteArray &buf, int &pos, quint64 &out) {
    if (pos >= buf.size()) {
        return false;
    }
    const quint8 first = static_cast<quint8>(buf.at(pos));
    const int len = 1 << (first >> 6); // 1, 2, 4 or 8 bytes
    if (pos + len > buf.size()) {
        return false;
    }
    quint64 v = first & 0x3F;
    for (int i = 1; i < len; ++i) {
        v = (v << 8) | static_cast<quint8>(buf.at(pos + i));
    }
    pos += len;
    out = v;
    return true;
}

void writeVarint(QByteArray &buf, quint64 v) {
    if (v <= 63) {
        buf.append(static_cast<char>(v));
    } else if (v <= 16383) {
        buf.append(static_cast<char>(0x40 | (v >> 8)));
        buf.append(static_cast<char>(v & 0xFF));
    } else if (v <= 1073741823ULL) {
        buf.append(static_cast<char>(0x80 | (v >> 24)));
        buf.append(static_cast<char>((v >> 16) & 0xFF));
        buf.append(static_cast<char>((v >> 8) & 0xFF));
        buf.append(static_cast<char>(v & 0xFF));
    } else {
        for (int shift = 56; shift >= 0; shift -= 8) {
            if (shift == 56)
                buf.append(static_cast<char>(0xC0 | ((v >> shift) & 0x3F)));
            else
                buf.append(static_cast<char>((v >> shift) & 0xFF));
        }
    }
}

void writeTlv(QByteArray &buf, quint64 tag, const QByteArray &value) {
    writeVarint(buf, tag);
    writeVarint(buf, static_cast<quint64>(value.size()));
    buf.append(value);
}

QByteArray varintBytes(quint64 v) {
    QByteArray b;
    writeVarint(b, v);
    return b;
}

// String[] value: concatenation of (varint length + UTF-8 bytes).
QByteArray encodeStringList(const QStringList &list) {
    QByteArray out;
    for (const QString &s : list) {
        const QByteArray u = s.toUtf8();
        writeVarint(out, static_cast<quint64>(u.size()));
        out.append(u);
    }
    return out;
}

QStringList decodeStringList(const QByteArray &value, bool *ok) {
    QStringList list;
    int pos = 0;
    while (pos < value.size()) {
        quint64 len = 0;
        if (!readVarint(value, pos, len) || !tlvLengthFits(value, pos, len)) {
            *ok = false;
            return {};
        }
        list << QString::fromUtf8(value.mid(pos, static_cast<int>(len)));
        pos += static_cast<int>(len);
    }
    *ok = true;
    return list;
}

QString tomlEscape(const QString &s) {
    // Escape backslash/quote AND drop C0 control characters (newlines, tabs, …)
    // and DEL. TOML basic strings forbid raw control chars, and leaving them in
    // would let a crafted deep-link field (e.g. a username containing "\n key =
    // value") break out of its quoted value and inject arbitrary TOML keys into
    // the generated config.
    QString out;
    out.reserve(s.size());
    for (const QChar &c : s) {
        if (c < QChar(0x20) || c == QChar(0x7F))
            continue;
        if (c == QLatin1Char('\\') || c == QLatin1Char('"'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

// One PEM block per line of 64 base64 characters, which is what every tool that
// reads PEM expects.
QString onePemBlock(const QByteArray &der)
{
    const QByteArray b64 = der.toBase64();
    QString body;
    for (int i = 0; i < b64.size(); i += 64)
        body += QString::fromLatin1(b64.mid(i, 64)) + '\n';
    return QStringLiteral("-----BEGIN CERTIFICATE-----\n") + body
            + QStringLiteral("-----END CERTIFICATE-----\n");
}

// The DER elements of a link's certificate field (tag 0x08), one after another,
// as TrustTunnel's der_to_pem splits it (derElementEnd, in ConfigTomlValues.cpp).
// *whole says whether the bytes split into them with nothing left over; the
// elements before whatever is not one come back either way.
QList<QByteArray> derElements(const QByteArray &der, bool *whole)
{
    QList<QByteArray> out;
    int pos = 0;
    while (pos < der.size()) {
        const int end = derElementEnd(der, pos);
        if (end <= pos) {
            *whole = false;
            return out;
        }
        out << der.mid(pos, end - pos);
        pos = end;
    }
    *whole = true;
    return out;
}

// The inverse of the export side, which concatenates the DER of every certificate
// it finds (certificatesDer) — tag 0x08 is a chain, and the header and
// DEEP_LINK.md both say so.
//
// This used to base64 the whole blob into a single BEGIN/END pair, so a config
// pinning a leaf and its intermediate came back through its own share link as
// one block that is not a certificate at all: the export split the chain
// correctly and the import had no way to put it back. Only certificates are
// written (deepLinkCertificates). An element of another kind, such as a private
// key, has no place in a config, and neither have bytes that are not elements at
// all, which were once emitted whole as one "certificate".
QString derToPem(const QByteArray &der) {
    QString out;
    bool whole = false;
    for (const QByteArray &certificate : derElements(deepLinkCertificates(der), &whole))
        out += onePemBlock(certificate);
    return out;
}

bool isAsciiHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

struct DeepLinkFieldFlags {
    bool hostname = false;
    bool user = false;
    bool pass = false;
};

bool applyDeepLinkStringTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg, DeepLinkFieldFlags &flags)
{
    switch (tag) {
    case 0x01: cfg.hostname = QString::fromUtf8(value); flags.hostname = true; return true;
    case 0x02: cfg.addresses << QString::fromUtf8(value); return true;
    case 0x03: cfg.customSni = QString::fromUtf8(value); return true;
    case 0x05: cfg.username = QString::fromUtf8(value); flags.user = true; return true;
    case 0x06: cfg.password = QString::fromUtf8(value); flags.pass = true; return true;
    case 0x0C: cfg.name = QString::fromUtf8(value); return true;
    default: return false;
    }
}

bool applyDeepLinkBoolTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg)
{
    const bool flag = value.isEmpty() ? false : (value.at(0) != 0);
    switch (tag) {
    case 0x04: cfg.hasIpv6 = value.isEmpty() ? true : flag; return true;
    case 0x07: cfg.skipVerification = flag; return true;
    case 0x0A: cfg.antiDpi = flag; return true;
    default: return false;
    }
}

bool applyDeepLinkScalarTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg)
{
    switch (tag) {
    case 0x00: return true; // read before any field, by readDeepLinkVersion()
    case 0x04:
    case 0x07:
    case 0x0A: return applyDeepLinkBoolTlv(tag, value, cfg);
    case 0x09: {
        int p = 0;
        quint64 v = 1;
        readVarint(value, p, v);
        cfg.upstreamProtocol = v == 2 ? UpstreamProtocol::Http3 : UpstreamProtocol::Http2;
        return true;
    }
    default: return false;
    }
}

// The client random, "prefix[/mask]" in hex. It was taken as it came, so a value
// the core cannot use reached the config all the same - the core quietly does
// without a part it cannot decode - and with it the editor, which then refused
// to save the config until it was changed. What the core reads as no mask, or no
// prefix, is repaired as clientRandomForCore() does; anything else means the link
// was damaged, and it is refused like any other damage.
bool applyDeepLinkClientRandomTlv(const QByteArray &value, DeepLinkConfig &cfg, QString *error)
{
    cfg.clientRandomPrefix = QString::fromUtf8(value);
    if (isValidClientRandom(clientRandomForCore(cfg.clientRandomPrefix)))
        return true;
    if (error)
        *error = QCoreApplication::translate("DeepLink", "malformed client_random value");
    return false;
}

// The certificates (tag 0x08): DER elements, one after another. Bytes that do not
// split into whole elements are damage, as TrustTunnel's der_to_pem and the
// Android client take them; here they used to be wrapped, whatever they were, in
// one BEGIN CERTIFICATE block. Which of the elements are certificates is for the
// import to say (deepLinkCertificates), so the value is kept as the link has it.
bool applyDeepLinkCertificateTlv(const QByteArray &value, DeepLinkConfig &cfg, QString *error)
{
    bool whole = false;
    derElements(value, &whole);
    if (whole) {
        cfg.certificate = value;
        return true;
    }
    if (error)
        *error = QCoreApplication::translate("DeepLink", "malformed certificate");
    return false;
}

// The subscription address (tag 0x0E, version 2). The format asks for https://
// and nothing more; the rest is what no address holds. A control character or a
// space is not part of one, nor is a byte that is not UTF-8, and nothing a server
// hands out runs past kMaxSubscriptionUrlBytes. The address carries the VPN
// password in plain text, so the error says what is wrong and never quotes it.
bool applyDeepLinkSubscriptionTlv(const QByteArray &value, DeepLinkConfig &cfg, QString *error)
{
    const QString url = QString::fromUtf8(value);
    bool ok = value.size() <= kMaxSubscriptionUrlBytes && value.startsWith("https://")
            && url.toUtf8() == value; // fromUtf8() quietly replaces what is not UTF-8
    // By code point, so a format character outside the BMP is not let through as
    // a pair of surrogates.
    for (const char32_t c : url.toUcs4()) {
        const QChar::Category category = QChar::category(c);
        if (QChar::isSpace(c) || category == QChar::Other_Control || category == QChar::Other_Format)
            ok = false;
    }
    if (ok) {
        cfg.subscriptionUrl = url;
        return true;
    }
    if (error)
        *error = QCoreApplication::translate("DeepLink", "subscription_url is not a valid https:// address");
    return false;
}

// The client random auth key (tag 0x0F, version 2): whole bytes of hex, as
// TrustTunnel's own decoder takes it. Empty is no key at all, which is what the
// endpoint means by one.
bool applyDeepLinkAuthKeyTlv(const QByteArray &value, DeepLinkConfig &cfg, QString *error)
{
    bool ok = value.size() % 2 == 0;
    for (const char c : value) {
        if (!isAsciiHexDigit(c))
            ok = false;
    }
    if (ok) {
        cfg.clientRandomAuthKey = QString::fromLatin1(value);
        return true;
    }
    if (error)
        *error = QCoreApplication::translate("DeepLink", "malformed client_random_auth_key value");
    return false;
}

bool applyDeepLinkTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg,
                      DeepLinkFieldFlags &flags, QString *error)
{
    if (applyDeepLinkStringTlv(tag, value, cfg, flags))
        return true;
    if (applyDeepLinkScalarTlv(tag, value, cfg))
        return true;
    if (tag == 0x08)
        return applyDeepLinkCertificateTlv(value, cfg, error);
    if (tag == 0x0B)
        return applyDeepLinkClientRandomTlv(value, cfg, error);
    if (tag == 0x0D) {
        bool ok = false;
        cfg.dnsUpstreams = decodeStringList(value, &ok);
        if (!ok && error)
            *error = QCoreApplication::translate("DeepLink", "malformed dns_upstreams list");
        return ok;
    }
    if (tag == 0x0E)
        return applyDeepLinkSubscriptionTlv(value, cfg, error);
    if (tag == 0x0F)
        return applyDeepLinkAuthKeyTlv(value, cfg, error);
    return true;
}

QString normalizeDeepLinkBody(QString s)
{
    const int ttIdx = s.indexOf(QLatin1String("tt="));
    if (ttIdx >= 0 && !s.startsWith(QLatin1String("tt://")))
        s = QStringLiteral("tt://?") + s.mid(ttIdx + 3);
    if (s.startsWith(QLatin1String("tt://?")))
        return s.mid(6);
    if (s.startsWith(QLatin1String("tt://")))
        return s.mid(5);
    return {};
}

// One record: its tag and its value, with *pos moved past it.
bool readDeepLinkTlv(const QByteArray &payload, int *pos, quint64 *tag, QByteArray *value,
                     QString *error)
{
    quint64 len = 0;
    if (!readVarint(payload, *pos, *tag) || !readVarint(payload, *pos, len)) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "truncated TLV header");
        return false;
    }
    if (!tlvLengthFits(payload, *pos, len)) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "TLV length exceeds payload");
        return false;
    }
    *value = payload.mid(*pos, static_cast<int>(len));
    *pos += static_cast<int>(len);
    return true;
}

bool readDeepLinkTlvEntry(const QByteArray &payload, int *pos, DeepLinkConfig &cfg,
                          DeepLinkFieldFlags &flags, QString *error)
{
    quint64 tag = 0;
    QByteArray value;
    return readDeepLinkTlv(payload, pos, &tag, &value, error)
            && applyDeepLinkTlv(tag, value, cfg, flags, error);
}

// The version a payload declares, read before any of its fields. A newer format
// may mean something else by a field, so a link in one has to be refused as newer
// rather than as damaged by whatever that field holds: read in one pass, a version
// 3 link whose subscription address was no longer https:// would have been called
// broken. Only damage to the records themselves comes first, since without them
// there is no finding the version.
//
// Every version record counts, as in TrustTunnel's own decoder, which refuses a
// link if any of them is too new: the newest is the link's version. When only the
// last one counted, a link saying 3 and then 2 was read here as version 2, and
// refused by TrustTunnel's clients. A record whose value is not a whole varint
// is damage, as it is there too; read as version 0, it let a link of any version
// through.
bool readDeepLinkVersion(const QByteArray &payload, quint64 *version, QString *error)
{
    quint64 newest = 0;
    int pos = 0;
    while (pos < payload.size()) {
        quint64 tag = 0;
        QByteArray value;
        if (!readDeepLinkTlv(payload, &pos, &tag, &value, error))
            return false;
        if (tag != 0x00)
            continue;
        int p = 0;
        quint64 declared = 0;
        if (!readVarint(value, p, declared)) {
            if (error)
                *error = QCoreApplication::translate("DeepLink", "malformed version value");
            return false;
        }
        newest = std::max(newest, declared);
    }
    *version = newest;
    return true;
}

bool deepLinkHasRequiredFields(const DeepLinkConfig &cfg, const DeepLinkFieldFlags &flags)
{
    return flags.hostname && !cfg.addresses.isEmpty() && flags.user && flags.pass;
}

std::optional<DeepLinkConfig> decodeDeepLinkPayload(const QByteArray &payload, QString *error)
{
    quint64 version = 0;
    if (!readDeepLinkVersion(payload, &version, error))
        return std::nullopt;
    // Compared as the varint it is, up to 62 bits. Cut to an int's low bits, a
    // version of 2^32 + 1 is 1, and a link far newer than this client was taken
    // for one it reads.
    if (version > static_cast<quint64>(kDeepLinkMaxVersion)) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "unsupported deep link version %1").arg(version);
        return std::nullopt;
    }
    DeepLinkConfig cfg;
    cfg.version = static_cast<int>(version);
    DeepLinkFieldFlags flags;
    int pos = 0;
    while (pos < payload.size()) {
        if (!readDeepLinkTlvEntry(payload, &pos, cfg, flags, error))
            return std::nullopt;
    }
    // With a subscription address every other field is optional (DEEP_LINK.md,
    // version 2): the server is meant to come from the subscription. Whether such
    // a link can be imported is the import's question (deepLinkHasStaticServer).
    if (!deepLinkHasRequiredFields(cfg, flags) && cfg.subscriptionUrl.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "deep link missing required fields "
                                                             "(hostname, address, username, password)");
        return std::nullopt;
    }
    return cfg;
}

void writeOptionalDeepLinkFlagTlvs(QByteArray &p, const DeepLinkConfig &cfg)
{
    if (!cfg.hasIpv6)
        writeTlv(p, 0x04, QByteArray(1, '\0'));
    if (cfg.skipVerification)
        writeTlv(p, 0x07, QByteArray(1, '\1'));
    if (cfg.antiDpi)
        writeTlv(p, 0x0A, QByteArray(1, '\1'));
}

// The client random as a link carries it: as the core reads it, and not at all
// when the core could not use it, rather than in a link the import refuses.
//
// A mask the core cannot decode is the exception. The core does without the mask
// and sends the prefix unmasked, and 1.2.2 could leave a pair like that behind
// ("deadbeef" and "fff"); dropping the whole value left whoever imported the link
// connecting differently from this config. The prefix alone is what is used.
QString clientRandomForLink(const QString &value)
{
    const QString v = clientRandomForCore(value);
    if (isValidClientRandom(v))
        return v;
    const QString prefix = v.section(QLatin1Char('/'), 0, 0);
    return isValidClientRandom(prefix) ? prefix : QString();
}

// The certificates alone, whatever the config holds: a link is made to be handed
// on, and an element of another kind, such as a private key, is not for whoever
// it is handed to.
void writeOptionalDeepLinkMetaTlvs(QByteArray &p, const DeepLinkConfig &cfg)
{
    const QString clientRandom = clientRandomForLink(cfg.clientRandomPrefix);
    const QByteArray certificates = deepLinkCertificates(cfg.certificate);
    if (!cfg.customSni.isEmpty())
        writeTlv(p, 0x03, cfg.customSni.toUtf8());
    if (!certificates.isEmpty())
        writeTlv(p, 0x08, certificates);
    if (cfg.upstreamProtocol != UpstreamProtocol::Http2)
        writeTlv(p, 0x09, varintBytes(static_cast<quint64>(cfg.upstreamProtocol)));
    if (!clientRandom.isEmpty())
        writeTlv(p, 0x0B, clientRandom.toUtf8());
    if (!cfg.name.isEmpty())
        writeTlv(p, 0x0C, cfg.name.toUtf8());
    if (!cfg.dnsUpstreams.isEmpty())
        writeTlv(p, 0x0D, encodeStringList(cfg.dnsUpstreams));
}

void writeOptionalDeepLinkTlvs(QByteArray &p, const DeepLinkConfig &cfg)
{
    writeOptionalDeepLinkFlagTlvs(p, cfg);
    writeOptionalDeepLinkMetaTlvs(p, cfg);
}

} // namespace

std::optional<DeepLinkConfig> parseDeepLink(const QString &uri, QString *error) {
    QString s = uri.trimmed();
    s = normalizeDeepLinkBody(s);
    if (s.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "not a tt:// deep link");
        return std::nullopt;
    }

    const QByteArray payload =
            QByteArray::fromBase64(s.toLatin1(), QByteArray::Base64UrlEncoding);
    if (payload.isEmpty()) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "invalid base64url payload");
        return std::nullopt;
    }
    return decodeDeepLinkPayload(payload, error);
}

bool deepLinkHasStaticServer(const DeepLinkConfig &cfg)
{
    // An address record can be empty as well, and an empty one is no more an
    // address to connect to than an empty hostname is a host.
    const bool anAddress = std::any_of(cfg.addresses.cbegin(), cfg.addresses.cend(),
                                       [](const QString &address) { return !address.isEmpty(); });
    return !cfg.hostname.isEmpty() && anAddress && !cfg.username.isEmpty() && !cfg.password.isEmpty();
}

QByteArray deepLinkCertificates(const QByteArray &der, bool *dropped)
{
    QByteArray out;
    bool whole = false;
    bool lost = false;
    for (const QByteArray &element : derElements(der, &whole)) {
        if (isCertificateDer(element))
            out += element;
        else
            lost = true;
    }
    if (dropped)
        *dropped = lost || !whole;
    return out;
}

QString encodeDeepLink(const DeepLinkConfig &cfg) {
    QByteArray p;
    // kDeepLinkWriteVersion, not the newest this client reads (see DeepLink.h),
    // unless the link carries the auth key, which only version 2 has a tag for.
    // Leaving subscriptionUrl out is what keeps the rest true.
    const bool authKey = !cfg.clientRandomAuthKey.isEmpty();
    writeTlv(p, 0x00, varintBytes(authKey ? kDeepLinkAuthKeyWriteVersion : kDeepLinkWriteVersion));
    writeTlv(p, 0x01, cfg.hostname.toUtf8());
    for (const QString &addr : cfg.addresses)
        writeTlv(p, 0x02, addr.toUtf8());
    writeTlv(p, 0x05, cfg.username.toUtf8());
    writeTlv(p, 0x06, cfg.password.toUtf8());
    writeOptionalDeepLinkTlvs(p, cfg);
    if (authKey)
        writeTlv(p, 0x0F, cfg.clientRandomAuthKey.toLatin1());
    return QStringLiteral("tt://?")
            + QString::fromLatin1(p.toBase64(QByteArray::Base64UrlEncoding
                                             | QByteArray::OmitTrailingEquals));
}

static QString quotedTomlList(const QStringList &items)
{
    QStringList out;
    for (const QString &item : items)
        out << QStringLiteral("\"%1\"").arg(tomlEscape(item));
    return out.join(QStringLiteral(", "));
}

static QString endpointTomlSection(const DeepLinkConfig &cfg)
{
    QString t;
    t += QStringLiteral("\n[endpoint]\n");
    t += QStringLiteral("hostname = \"%1\"\n").arg(tomlEscape(cfg.hostname));
    t += QStringLiteral("addresses = [%1]\n").arg(quotedTomlList(cfg.addresses));
    t += QStringLiteral("username = \"%1\"\n").arg(tomlEscape(cfg.username));
    t += QStringLiteral("password = \"%1\"\n").arg(tomlEscape(cfg.password));
    // Whole, as the link gives it ("prefix[/mask]"): the core splits it itself. It
    // used to be split here into client_random and a client_random_mask key the
    // core has no such key for, and the mask was lost.
    t += QStringLiteral("client_random = \"%1\"\n").arg(tomlEscape(clientRandomForCore(cfg.clientRandomPrefix)));
    // Hex, checked when the link was read; the import moves it to the credential
    // store, as it does the password. Only when there is one: the core takes an
    // empty key for none, but a config without the line reads as the plain case.
    if (!cfg.clientRandomAuthKey.isEmpty())
        t += QStringLiteral("client_random_auth_key = \"%1\"\n").arg(tomlEscape(cfg.clientRandomAuthKey));
    t += QStringLiteral("custom_sni = \"%1\"\n").arg(tomlEscape(cfg.customSni));
    t += QStringLiteral("has_ipv6 = %1\n").arg(cfg.hasIpv6 ? "true" : "false");
    t += QStringLiteral("skip_verification = %1\n").arg(cfg.skipVerification ? "true" : "false");
    t += QStringLiteral("upstream_protocol = \"%1\"\n")
                 .arg(cfg.upstreamProtocol == UpstreamProtocol::Http3 ? "http3" : "http2");
    t += QStringLiteral("anti_dpi = %1\n").arg(cfg.antiDpi ? "true" : "false");
    const QString pem = derToPem(cfg.certificate);
    if (!pem.isEmpty())
        t += QStringLiteral("certificate = \"\"\"\n%1\"\"\"\n").arg(pem);
    else
        t += QStringLiteral("certificate = \"\"\n");
    return t;
}

static QString listenerTomlSection()
{
    return QStringLiteral("\n[listener.tun]\n"
                          "bound_if = \"\"\n"
                          "mtu_size = 1500\n"
                          "change_system_dns = true\n"
                          "included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n"
                          "excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", "
                          "\"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n");
}

QString deepLinkConfigToToml(const DeepLinkConfig &cfg) {
    QString t;
    t += QStringLiteral("loglevel = \"info\"\n");
    t += QStringLiteral("vpn_mode = \"general\"\n");
    t += QStringLiteral("killswitch_enabled = false\n");
    t += QStringLiteral("post_quantum_group_enabled = true\n");
    t += QStringLiteral("dns_upstreams = [%1]\n").arg(quotedTomlList(cfg.dnsUpstreams));
    t += endpointTomlSection(cfg);
    t += listenerTomlSection();
    return t;
}

} // namespace freetunnel
