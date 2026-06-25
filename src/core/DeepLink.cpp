// cppcheck-suppress-file missingIncludeSystem
#include "core/DeepLink.h"

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
            char b = static_cast<char>((v >> shift) & 0xFF);
            if (shift == 56) {
                b = static_cast<char>(0xC0 | (static_cast<quint8>(b) & 0x3F));
            }
            buf.append(b);
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

QString derToPem(const QByteArray &der) {
    const QByteArray b64 = der.toBase64();
    QString body;
    for (int i = 0; i < b64.size(); i += 64) {
        body += QString::fromLatin1(b64.mid(i, 64)) + '\n';
    }
    return QStringLiteral("-----BEGIN CERTIFICATE-----\n") + body
            + QStringLiteral("-----END CERTIFICATE-----\n");
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
    case 0x0B: cfg.clientRandomPrefix = QString::fromUtf8(value); return true;
    case 0x0C: cfg.name = QString::fromUtf8(value); return true;
    default: return false;
    }
}

bool applyDeepLinkScalarTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg)
{
    const auto asBool = [&](bool def) {
        return value.isEmpty() ? def : (value.at(0) != 0);
    };
    const auto asVarint = [&](quint64 def) {
        int p = 0;
        quint64 v = def;
        readVarint(value, p, v);
        return v;
    };

    switch (tag) {
    case 0x00: cfg.version = static_cast<int>(asVarint(0)); return true;
    case 0x04: cfg.hasIpv6 = asBool(true); return true;
    case 0x07: cfg.skipVerification = asBool(false); return true;
    case 0x08: cfg.certificate = value; return true;
    case 0x09:
        cfg.upstreamProtocol = asVarint(1) == 2 ? UpstreamProtocol::Http3 : UpstreamProtocol::Http2;
        return true;
    case 0x0A: cfg.antiDpi = asBool(false); return true;
    default: return false;
    }
}

bool applyDeepLinkTlv(quint64 tag, const QByteArray &value, DeepLinkConfig &cfg,
                      DeepLinkFieldFlags &flags, QString *error)
{
    if (applyDeepLinkStringTlv(tag, value, cfg, flags))
        return true;
    if (applyDeepLinkScalarTlv(tag, value, cfg))
        return true;
    if (tag == 0x0D) {
        bool ok = false;
        cfg.dnsUpstreams = decodeStringList(value, &ok);
        if (!ok && error)
            *error = QStringLiteral("malformed dns_upstreams list");
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
    quint64 tag = 0, len = 0;
    if (!readVarint(payload, *pos, tag) || !readVarint(payload, *pos, len)) {
        if (error)
            *error = QStringLiteral("truncated TLV header");
        return false;
    }
    if (!tlvLengthFits(payload, *pos, len)) {
        if (error)
            *error = QStringLiteral("TLV length exceeds payload");
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
            *error = QStringLiteral("unsupported deep link version %1").arg(cfg.version);
        return std::nullopt;
    }
    if (!deepLinkHasRequiredFields(cfg, flags)) {
        if (error)
            *error = QStringLiteral("deep link missing required fields "
                                     "(hostname, address, username, password)");
        return std::nullopt;
    }
    return cfg;
}

void writeOptionalDeepLinkTlvs(QByteArray &p, const DeepLinkConfig &cfg)
{
    if (!cfg.customSni.isEmpty())
        writeTlv(p, 0x03, cfg.customSni.toUtf8());
    if (!cfg.hasIpv6)
        writeTlv(p, 0x04, QByteArray(1, '\0'));
    if (cfg.skipVerification)
        writeTlv(p, 0x07, QByteArray(1, '\1'));
    if (!cfg.certificate.isEmpty())
        writeTlv(p, 0x08, cfg.certificate);
    if (cfg.upstreamProtocol != UpstreamProtocol::Http2)
        writeTlv(p, 0x09, varintBytes(static_cast<quint64>(cfg.upstreamProtocol)));
    if (cfg.antiDpi)
        writeTlv(p, 0x0A, QByteArray(1, '\1'));
    if (!cfg.clientRandomPrefix.isEmpty())
        writeTlv(p, 0x0B, cfg.clientRandomPrefix.toUtf8());
    if (!cfg.name.isEmpty())
        writeTlv(p, 0x0C, cfg.name.toUtf8());
    if (!cfg.dnsUpstreams.isEmpty())
        writeTlv(p, 0x0D, encodeStringList(cfg.dnsUpstreams));
}

} // namespace

std::optional<DeepLinkConfig> parseDeepLink(const QString &uri, QString *error) {
    QString s = uri.trimmed();
    s = normalizeDeepLinkBody(s);
    if (s.isEmpty()) {
        if (error)
            *error = QStringLiteral("not a tt:// deep link");
        return std::nullopt;
    }

    const QByteArray payload =
            QByteArray::fromBase64(s.toLatin1(), QByteArray::Base64UrlEncoding);
    if (payload.isEmpty()) {
        if (error)
            *error = QStringLiteral("invalid base64url payload");
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

static void splitClientRandom(const DeepLinkConfig &cfg, QString *random, QString *mask)
{
    *random = cfg.clientRandomPrefix;
    mask->clear();
    const int slash = random->indexOf('/');
    if (slash >= 0) {
        *mask = random->mid(slash + 1);
        *random = random->left(slash);
    }
}

static QString endpointTomlSection(const DeepLinkConfig &cfg, const QString &clientRandom,
                                    const QString &clientRandomMask)
{
    QString t;
    t += QStringLiteral("\n[endpoint]\n");
    t += QStringLiteral("hostname = \"%1\"\n").arg(tomlEscape(cfg.hostname));
    t += QStringLiteral("addresses = [%1]\n").arg(quotedTomlList(cfg.addresses));
    t += QStringLiteral("username = \"%1\"\n").arg(tomlEscape(cfg.username));
    t += QStringLiteral("password = \"%1\"\n").arg(tomlEscape(cfg.password));
    t += QStringLiteral("client_random = \"%1\"\n").arg(tomlEscape(clientRandom));
    if (!clientRandomMask.isEmpty())
        t += QStringLiteral("client_random_mask = \"%1\"\n").arg(tomlEscape(clientRandomMask));
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
    QString clientRandom;
    QString clientRandomMask;
    splitClientRandom(cfg, &clientRandom, &clientRandomMask);

    QString t;
    t += QStringLiteral("loglevel = \"info\"\n");
    t += QStringLiteral("vpn_mode = \"general\"\n");
    t += QStringLiteral("killswitch_enabled = false\n");
    t += QStringLiteral("post_quantum_group_enabled = true\n");
    t += QStringLiteral("dns_upstreams = [%1]\n").arg(quotedTomlList(cfg.dnsUpstreams));
    t += endpointTomlSection(cfg, clientRandom, clientRandomMask);
    t += listenerTomlSection();
    return t;
}

} // namespace freetunnel
