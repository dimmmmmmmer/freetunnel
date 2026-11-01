// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoRules.h"

#include <QHostAddress>
#include <QList>
#include <QPair>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace freetunnel {

namespace {

// ---------- a minimal reader of the protobuf wire format ----------
//
// The .dat files are protobuf messages (V2Ray's GeoSiteList and GeoIPList). Only
// what those two need is read: varints and length-delimited fields; fixed-width
// fields are skipped. Every length is checked against what is left, so a
// truncated or hostile file ends the read instead of reading past the buffer.
class WireReader {
public:
    explicit WireReader(std::string_view data) : m_data(data) {}

    bool atEnd() const { return m_pos >= m_data.size(); }
    bool failed() const { return m_failed; }

    // The next field's number and wire type; false at the end or on bad data.
    bool next(uint32_t *field, uint32_t *wireType)
    {
        if (atEnd() || m_failed)
            return false;
        uint64_t key = 0;
        if (!varint(&key) || key >> 3 == 0 || key >> 3 > 0x1fffffff)
            return fail();
        *field = static_cast<uint32_t>(key >> 3);
        *wireType = static_cast<uint32_t>(key & 7);
        return true;
    }

    bool varint(uint64_t *out)
    {
        uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (m_pos >= m_data.size())
                return fail();
            const auto byte = static_cast<uint8_t>(m_data[m_pos++]);
            // The tenth byte may contain only bit 63; reject overflowing varints.
            if (shift == 63 && (byte & 0xfe) != 0)
                return fail();
            value |= uint64_t(byte & 0x7f) << shift;
            if ((byte & 0x80) == 0) {
                *out = value;
                return true;
            }
        }
        return fail();
    }

    bool bytes(std::string_view *out)
    {
        uint64_t len = 0;
        if (!varint(&len) || len > m_data.size() - m_pos)
            return fail();
        *out = m_data.substr(m_pos, static_cast<size_t>(len));
        m_pos += static_cast<size_t>(len);
        return true;
    }

    // Steps over a field of a type this reader has no use for.
    bool skip(uint32_t wireType)
    {
        switch (wireType) {
        case 0: {
            uint64_t ignored = 0;
            return varint(&ignored);
        }
        case 1: return advance(8);
        case 2: {
            std::string_view ignored;
            return bytes(&ignored);
        }
        case 5: return advance(4);
        default: return fail(); // groups (3, 4) are not in these files
        }
    }

private:
    bool advance(size_t n)
    {
        if (n > m_data.size() - m_pos)
            return fail();
        m_pos += n;
        return true;
    }
    bool fail()
    {
        m_failed = true;
        return false;
    }

    std::string_view m_data;
    size_t m_pos = 0;
    bool m_failed = false;
};

std::string_view viewOf(const QByteArray &data)
{
    return {data.constData(), static_cast<size_t>(data.size())};
}

// ---------- names and subnets ----------

// A domain name as the core matches it: lower case, ASCII (an internationalised
// name in its xn-- form), labels of letters, digits, '-' and '_', no empty one.
// A single label is allowed: the lists name whole zones (ru, su, xn--p1ai).
std::optional<std::string> normalisedDomain(QString name)
{
    name = name.trimmed().toLower();
    if (name.endsWith(QLatin1Char('.')))
        name.chop(1);
    if (name.isEmpty() || name.size() > 253)
        return std::nullopt;
    bool ascii = true;
    for (const QChar c : name) {
        if (c.unicode() > 0x7f) {
            ascii = false;
            break;
        }
    }
    if (!ascii) {
        const QByteArray ace = QUrl::toAce(name);
        if (ace.isEmpty())
            return std::nullopt;
        name = QString::fromLatin1(ace).toLower();
    }
    const QByteArray latin = name.toLatin1();
    char last = '.';
    for (const char ch : latin) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'
                || ch == '.';
        if (!ok || (ch == '.' && last == '.'))
            return std::nullopt;
        last = ch;
    }
    if (latin.startsWith('.') || latin.startsWith('-'))
        return std::nullopt;
    return std::string(latin.constData(), static_cast<size_t>(latin.size()));
}

// "a.b.c.d/n" or "x::/n", with the host bits cleared; an address alone gets its
// full length. Empty when it is neither.
QString normalisedSubnet(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return {};
    QPair<QHostAddress, int> subnet;
    if (t.contains(QLatin1Char('/'))) {
        subnet = QHostAddress::parseSubnet(t);
    } else {
        const QHostAddress a(t);
        if (a.isNull())
            return {};
        subnet = {a, a.protocol() == QAbstractSocket::IPv6Protocol ? 128 : 32};
    }
    if (subnet.first.isNull() || subnet.second < 0)
        return {};
    // Host bits cleared here: QHostAddress::parseSubnet leaves them in place
    // within a partly covered byte of an IPv6 address (2a14:e900::/29 stays as
    // it is), and two spellings of one subnet would not be the same entry.
    const int prefix = subnet.second;
    QHostAddress network;
    if (subnet.first.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 mask = prefix == 0 ? 0 : ~quint32(0) << (32 - prefix);
        network = QHostAddress(subnet.first.toIPv4Address() & mask);
    } else {
        Q_IPV6ADDR a = subnet.first.toIPv6Address();
        for (int i = 0; i < 16; ++i) {
            const int keep = std::clamp(prefix - i * 8, 0, 8);
            a.c[i] = static_cast<quint8>(a.c[i] & (0xff00 >> keep));
        }
        network = QHostAddress(a);
    }
    return network.toString() + QLatin1Char('/') + QString::number(prefix);
}

QString subnetFromBytes(std::string_view ip, uint64_t prefix)
{
    if (ip.size() == 4 && prefix <= 32) {
        const auto *b = reinterpret_cast<const uint8_t *>(ip.data());
        const quint32 v = (quint32(b[0]) << 24) | (quint32(b[1]) << 16) | (quint32(b[2]) << 8) | b[3];
        return normalisedSubnet(QHostAddress(v).toString() + QLatin1Char('/') + QString::number(prefix));
    }
    if (ip.size() == 16 && prefix <= 128) {
        Q_IPV6ADDR a{};
        std::copy(ip.begin(), ip.end(), reinterpret_cast<char *>(a.c));
        return normalisedSubnet(QHostAddress(a).toString() + QLatin1Char('/') + QString::number(prefix));
    }
    return {};
}

// What one list adds, kept to itself until the whole list has been read: a list
// that fails half way adds nothing.
struct Collector {
    GeoDirection direction;
    std::vector<std::string> entries;
    GeoListStats stats;

    void domain(const QString &name, bool withSubdomains)
    {
        const std::optional<std::string> d = normalisedDomain(name);
        if (!d) {
            ++stats.skipped;
            return;
        }
        entries.push_back(*d);
        if (withSubdomains)
            entries.push_back("*." + *d);
        ++stats.entries;
    }

    void subnet(const QString &cidr)
    {
        if (cidr.isEmpty() || (direction == GeoDirection::Bypass && isTooWideToBypass(cidr))) {
            ++stats.skipped;
            return;
        }
        // Every address of its kind is refused either way: a list cannot mean it.
        if (cidr.endsWith(QLatin1String("/0"))) {
            ++stats.skipped;
            return;
        }
        entries.push_back(cidr.toStdString());
        ++stats.entries;
    }
};

// ---------- .dat ----------

// V2Ray's Domain.Type.
enum DomainType : uint64_t { Plain = 0, Regex = 1, RootDomain = 2, Full = 3 };

bool readSiteDomain(std::string_view msg, Collector *c)
{
    WireReader r(msg);
    uint64_t type = Plain;
    std::string_view value;
    uint32_t field = 0;
    uint32_t wire = 0;
    while (r.next(&field, &wire)) {
        if (field == 1 && wire == 0) {
            if (!r.varint(&type))
                return false;
        } else if (field == 2 && wire == 2) {
            if (!r.bytes(&value))
                return false;
        } else if (!r.skip(wire)) {
            return false;
        }
    }
    if (r.failed())
        return false;
    const QString name = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    if (type == RootDomain)
        c->domain(name, true);
    else if (type == Full)
        c->domain(name, false);
    else
        ++c->stats.skipped; // a keyword or a regular expression: the core matches neither
    return true;
}

bool readIpCidr(std::string_view msg, Collector *c)
{
    WireReader r(msg);
    std::string_view ip;
    uint64_t prefix = 0;
    uint32_t field = 0;
    uint32_t wire = 0;
    while (r.next(&field, &wire)) {
        if (field == 1 && wire == 2) {
            if (!r.bytes(&ip))
                return false;
        } else if (field == 2 && wire == 0) {
            if (!r.varint(&prefix))
                return false;
        } else if (!r.skip(wire)) {
            return false;
        }
    }
    if (r.failed())
        return false;
    c->subnet(subnetFromBytes(ip, prefix));
    return true;
}

// One GeoSite or GeoIP message: its code (field 1), and if it is the category
// asked for, its entries (field 2). *matched says whether it was.
bool readDatEntry(std::string_view msg, const QString &category, GeoListKind kind, Collector *c,
                  bool *matched, QString *error)
{
    // The code comes first in every file seen, but nothing in the format says
    // so: collect the entries' spans, and read them once the code is known.
    WireReader r(msg);
    QString code;
    std::vector<std::string_view> items;
    bool reverse = false;
    uint32_t field = 0;
    uint32_t wire = 0;
    while (r.next(&field, &wire)) {
        if (field == 1 && wire == 2) {
            std::string_view v;
            if (!r.bytes(&v))
                return false;
            code = QString::fromUtf8(v.data(), static_cast<qsizetype>(v.size()));
        } else if (field == 2 && wire == 2) {
            std::string_view v;
            if (!r.bytes(&v))
                return false;
            items.push_back(v);
        } else if (kind == GeoListKind::Ips && field == 3 && wire == 0) {
            // GeoIP.reverse_match (inverse_match in V2Ray).
            uint64_t flag = 0;
            if (!r.varint(&flag))
                return false;
            reverse = flag != 0;
        } else if (!r.skip(wire)) {
            return false;
        }
    }
    if (r.failed())
        return false;
    *matched = code.compare(category, Qt::CaseInsensitive) == 0;
    if (!*matched)
        return true;
    if (reverse) {
        // "Everything but these": nothing the core can be given as a list.
        *error = QStringLiteral("category %1 is an inverted match, which cannot be used").arg(code);
        return false;
    }
    for (const std::string_view item : items) {
        const bool ok = kind == GeoListKind::Sites ? readSiteDomain(item, c) : readIpCidr(item, c);
        if (!ok)
            return false;
    }
    return true;
}

bool readDat(const QByteArray &data, const GeoSource &source, GeoListKind kind, Collector *c,
             QString *error)
{
    WireReader r(viewOf(data));
    bool found = false;
    uint32_t field = 0;
    uint32_t wire = 0;
    while (r.next(&field, &wire)) {
        if (field != 1 || wire != 2) {
            if (!r.skip(wire))
                break;
            continue;
        }
        std::string_view entry;
        if (!r.bytes(&entry))
            break;
        bool matched = false;
        QString why;
        if (!readDatEntry(entry, source.category, kind, c, &matched, &why)) {
            *error = why.isEmpty() ? QStringLiteral("the file is damaged") : why;
            return false;
        }
        found = found || matched;
    }
    if (r.failed()) {
        *error = QStringLiteral("the file is damaged or not a %1 file")
                         .arg(kind == GeoListKind::Sites ? QStringLiteral("geosite") : QStringLiteral("geoip"));
        return false;
    }
    if (!found) {
        *error = QStringLiteral("the file has no category %1").arg(source.category);
        return false;
    }
    return true;
}

// ---------- text ----------

// One entry per line, in the forms domain lists are published in: V2Ray's
// "domain:", "full:", "keyword:" and "regexp:" prefixes, a bare domain (with its
// subdomains), an address or a subnet. A '#' starts a comment, and anything
// after the first blank (V2Ray's "@attribute" marks) is ignored.
void readText(const QByteArray &data, Collector *c)
{
    qsizetype start = 0;
    while (start < data.size()) {
        qsizetype end = data.indexOf('\n', start);
        if (end < 0)
            end = data.size();
        QString line = QString::fromUtf8(data.constData() + start, end - start);
        start = end + 1;
        const qsizetype hash = line.indexOf(QLatin1Char('#'));
        if (hash >= 0)
            line.truncate(hash);
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("//")))
            continue;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line.at(i).isSpace()) {
                line.truncate(i);
                break;
            }
        }

        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        const QString prefix = colon > 0 ? line.left(colon).toLower() : QString();
        if (prefix == QLatin1String("domain")) {
            c->domain(line.mid(colon + 1), true);
        } else if (prefix == QLatin1String("full")) {
            c->domain(line.mid(colon + 1), false);
        } else if (prefix == QLatin1String("keyword") || prefix == QLatin1String("regexp")
                   || prefix == QLatin1String("include")) {
            ++c->stats.skipped;
        } else if (const QString cidr = normalisedSubnet(line); !cidr.isEmpty()) {
            c->subnet(cidr);
        } else {
            c->domain(line, true);
        }
    }
}

} // namespace

GeoSource parseGeoSource(const QString &spec, QString *why)
{
    GeoSource out;
    const QUrl url(spec.trimmed(), QUrl::StrictMode);
    bool https = url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0;
#ifdef FT_ENABLE_TEST_HOOKS
    // The tests' HTTP server, on the loopback address alone. Compiled out of
    // release builds.
    https = https || (url.scheme() == QLatin1String("http") && url.host() == QLatin1String("127.0.0.1"));
#endif
    if (!url.isValid() || !https || url.host().isEmpty()) {
        if (why)
            *why = QStringLiteral("not an https address");
        return out;
    }
    if (!url.userInfo().isEmpty()) {
        if (why)
            *why = QStringLiteral("an address with a user name or password");
        return out;
    }
    GeoSource candidate;
    candidate.category = url.fragment(QUrl::FullyDecoded).trimmed();
    candidate.file = url.adjusted(QUrl::RemoveFragment);
    candidate.dat = candidate.file.path().endsWith(QLatin1String(".dat"), Qt::CaseInsensitive);
    if (candidate.dat && candidate.category.isEmpty()) {
        if (why)
            *why = QStringLiteral("a .dat file needs a category after '#'");
        return out;
    }
    if (candidate.dat && candidate.category.contains(QLatin1Char('@'))) {
        if (why)
            *why = QStringLiteral("attribute filters (@) are not supported");
        return out;
    }
    return candidate;
}

bool appendGeoEntries(const QByteArray &data, const GeoSource &source, GeoListKind kind,
                      GeoDirection direction, std::vector<std::string> *out,
                      GeoListStats *stats, QString *error)
{
    Collector c{direction, {}, {}};
    QString why;
    if (source.dat) {
        if (!readDat(data, source, kind, &c, &why)) {
            if (error)
                *error = why;
            return false;
        }
    } else {
        readText(data, &c);
    }
    if (c.stats.entries == 0) {
        if (error)
            *error = QStringLiteral("the list has no entries that can be used");
        return false;
    }
    out->insert(out->end(), std::make_move_iterator(c.entries.begin()),
                std::make_move_iterator(c.entries.end()));
    if (stats) {
        stats->entries += c.stats.entries;
        stats->skipped += c.stats.skipped;
    }
    return true;
}

std::string joinGeoEntries(std::vector<std::string> entries)
{
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
    size_t size = 0;
    for (const std::string &e : entries)
        size += e.size() + 1;
    std::string out;
    out.reserve(size);
    for (const std::string &e : entries) {
        out.append(e);
        out.push_back('\n');
    }
    return out;
}

bool isTooWideToBypass(const QString &cidr)
{
    const QPair<QHostAddress, int> subnet = QHostAddress::parseSubnet(cidr);
    if (subnet.first.isNull())
        return false;
    const bool v6 = subnet.first.protocol() == QAbstractSocket::IPv6Protocol;
    return subnet.second < (v6 ? 16 : 8);
}

} // namespace freetunnel
