// cppcheck-suppress-file missingIncludeSystem
#include "core/DeepLink.h"
#include "core/ConfigToml.h"

#include <QCoreApplication>

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

// Where the DER element starting at `pos` ends, or -1 when the bytes there are
// not one. A certificate is an ASN.1 SEQUENCE: the tag 0x30, then a length that
// is either one short byte or a count byte followed by that many big-endian
// length bytes.
int derElementEnd(const QByteArray &der, int pos)
{
    if (pos + 2 > der.size() || static_cast<unsigned char>(der.at(pos)) != 0x30)
        return -1;
    const auto first = static_cast<unsigned char>(der.at(pos + 1));
    int header = 2;
    qint64 length = first;
    if ((first & 0x80) != 0) {
        const int count = first & 0x7F;
        // Zero is the indefinite form, which DER forbids; more than four bytes
        // is a certificate larger than this has any business accepting.
        if (count == 0 || count > 4 || pos + 2 + count > der.size())
            return -1;
        length = 0;
        for (int i = 0; i < count; ++i)
            length = (length << 8) | static_cast<unsigned char>(der.at(pos + 2 + i));
        header = 2 + count;
    }
    const qint64 end = static_cast<qint64>(pos) + header + length;
    return end > der.size() ? -1 : static_cast<int>(end);
}

// The inverse of the export side, which concatenates the DER of every PEM block
// it finds (pemCertsToDer in BackendConfig.cpp) — tag 0x08 is a chain, and the
// header and DEEP_LINK.md both say so.
//
// This used to base64 the whole blob into a single BEGIN/END pair, so a config
// pinning a leaf and its intermediate came back through its own share link as
// one block that is not a certificate at all: the export split the chain
// correctly and the import had no way to put it back. A blob that does not parse
// as a sequence of elements is emitted whole, exactly as before, because that is
// no worse than the old behaviour and the alternative is discarding it.
QString derToPem(const QByteArray &der) {
    if (der.isEmpty())
        return QString();
    QString out;
    int pos = 0;
    while (pos < der.size()) {
        const int end = derElementEnd(der, pos);
        if (end <= pos)
            return out.isEmpty() ? onePemBlock(der) : out + onePemBlock(der.mid(pos));
        out += onePemBlock(der.mid(pos, end - pos));
        pos = end;
    }
    return out;
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

bool applyDeepLinkVersionTlv(const QByteArray &value, DeepLinkConfig &cfg)
{
    int p = 0;
    quint64 v = 0;
    readVarint(value, p, v);
    cfg.version = static_cast<int>(v);
    return true;
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
    case 0x00: return applyDeepLinkVersionTlv(value, cfg);
    case 0x04:
    case 0x07:
    case 0x0A: return applyDeepLinkBoolTlv(tag, value, cfg);
    case 0x08: cfg.certificate = value; return true;
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

bool applyDeepLinkTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg,
                      DeepLinkFieldFlags &flags, QString *error)
{
    if (applyDeepLinkStringTlv(tag, value, cfg, flags))
        return true;
    if (applyDeepLinkScalarTlv(tag, value, cfg))
        return true;
    if (tag == 0x0B)
        return applyDeepLinkClientRandomTlv(value, cfg, error);
    if (tag == 0x0D) {
        bool ok = false;
        cfg.dnsUpstreams = decodeStringList(value, &ok);
        if (!ok && error)
            *error = QCoreApplication::translate("DeepLink", "malformed dns_upstreams list");
        return ok;
    }
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

bool readDeepLinkTlvEntry(const QByteArray &payload, int *pos, DeepLinkConfig &cfg,
                          DeepLinkFieldFlags &flags, QString *error)
{
    quint64 tag = 0;
    quint64 len = 0;
    if (!readVarint(payload, *pos, tag) || !readVarint(payload, *pos, len)) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "truncated TLV header");
        return false;
    }
    if (!tlvLengthFits(payload, *pos, len)) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "TLV length exceeds payload");
        return false;
    }
    const QByteArray value = payload.mid(*pos, static_cast<int>(len));
    *pos += static_cast<int>(len);
    return applyDeepLinkTlv(tag, value, cfg, flags, error);
}

bool deepLinkHasRequiredFields(const DeepLinkConfig &cfg, const DeepLinkFieldFlags &flags)
{
    return flags.hostname && !cfg.addresses.isEmpty() && flags.user && flags.pass;
}

std::optional<DeepLinkConfig> decodeDeepLinkPayload(const QByteArray &payload, QString *error)
{
    DeepLinkConfig cfg;
    DeepLinkFieldFlags flags;
    int pos = 0;
    while (pos < payload.size()) {
        if (!readDeepLinkTlvEntry(payload, &pos, cfg, flags, error))
            return std::nullopt;
    }
    if (cfg.version > kDeepLinkMaxVersion) {
        if (error)
            *error = QCoreApplication::translate("DeepLink", "unsupported deep link version %1").arg(cfg.version);
        return std::nullopt;
    }
    if (!deepLinkHasRequiredFields(cfg, flags)) {
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

void writeOptionalDeepLinkMetaTlvs(QByteArray &p, const DeepLinkConfig &cfg)
{
    const QString clientRandom = clientRandomForLink(cfg.clientRandomPrefix);
    if (!cfg.customSni.isEmpty())
        writeTlv(p, 0x03, cfg.customSni.toUtf8());
    if (!cfg.certificate.isEmpty())
        writeTlv(p, 0x08, cfg.certificate);
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

QString encodeDeepLink(const DeepLinkConfig &cfg) {
    QByteArray p;
    writeTlv(p, 0x00, varintBytes(kDeepLinkMaxVersion));
    writeTlv(p, 0x01, cfg.hostname.toUtf8());
    for (const QString &addr : cfg.addresses)
        writeTlv(p, 0x02, addr.toUtf8());
    writeTlv(p, 0x05, cfg.username.toUtf8());
    writeTlv(p, 0x06, cfg.password.toUtf8());
    writeOptionalDeepLinkTlvs(p, cfg);
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
    t += QStringLiteral("custom_sni = \"%1\"\n").arg(tomlEscape(cfg.customSni));
    t += QStringLiteral("has_ipv6 = %1\n").arg(cfg.hasIpv6 ? "true" : "false");
    t += QStringLiteral("skip_verification = %1\n").arg(cfg.skipVerification ? "true" : "false");
    t += QStringLiteral("upstream_protocol = \"%1\"\n")
                 .arg(cfg.upstreamProtocol == UpstreamProtocol::Http3 ? "http3" : "http2");
    t += QStringLiteral("anti_dpi = %1\n").arg(cfg.antiDpi ? "true" : "false");
    if (!cfg.certificate.isEmpty())
        t += QStringLiteral("certificate = \"\"\"\n%1\"\"\"\n").arg(derToPem(cfg.certificate));
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
