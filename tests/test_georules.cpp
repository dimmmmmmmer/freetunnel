// cppcheck-suppress-file missingIncludeSystem
// Geo lists: the .dat reader, the text reader, and what may be taken out of the
// tunnel. The .dat files in these tests are built here, field by field, in the
// protobuf wire format the published geosite.dat and geoip.dat use, so that
// every branch of the reader (unknown fields, a damaged length, a missing
// category, an inverted one) can be reached without shipping megabytes of data.
#include <QtTest>

#include "core/GeoRules.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace freetunnel;

namespace {

// ---------- protobuf wire format, the writing half ----------

QByteArray varint(quint64 v)
{
    QByteArray out;
    do {
        char byte = char(v & 0x7f);
        v >>= 7;
        if (v)
            byte = char(byte | 0x80);
        out.append(byte);
    } while (v);
    return out;
}

QByteArray key(quint32 field, quint32 wire) { return varint((quint64(field) << 3) | wire); }
QByteArray lenField(quint32 field, const QByteArray &body) { return key(field, 2) + varint(quint64(body.size())) + body; }
QByteArray intField(quint32 field, quint64 v) { return key(field, 0) + varint(v); }

// Domain { Type type = 1; string value = 2; repeated Attribute attribute = 3; }
QByteArray domain(quint64 type, const QByteArray &value)
{
    return intField(1, type) + lenField(2, value) + lenField(3, lenField(1, "ads"));
}
// GeoSite { string country_code = 1; repeated Domain domain = 2; }
QByteArray geosite(const QByteArray &code, const QList<QByteArray> &domains)
{
    QByteArray body = lenField(1, code);
    for (const QByteArray &d : domains)
        body += lenField(2, d);
    return lenField(1, body); // GeoSiteList { repeated GeoSite entry = 1; }
}
// CIDR { bytes ip = 1; uint32 prefix = 2; }
QByteArray cidr(const QByteArray &ip, quint64 prefix) { return lenField(1, ip) + intField(2, prefix); }
// GeoIP { string country_code = 1; repeated CIDR cidr = 2; bool reverse_match = 3; }
QByteArray geoip(const QByteArray &code, const QList<QByteArray> &cidrs, bool reverse = false)
{
    QByteArray body = lenField(1, code);
    for (const QByteArray &c : cidrs)
        body += lenField(2, c);
    if (reverse)
        body += intField(3, 1);
    return lenField(1, body);
}

QByteArray v4(int a, int b, int c, int d)
{
    QByteArray out;
    for (const int byte : {a, b, c, d})
        out.append(char(byte));
    return out;
}

GeoSource source(const QString &spec)
{
    QString why;
    const GeoSource s = parseGeoSource(spec, &why);
    if (!s.valid())
        qWarning("source %s refused: %s", qPrintable(spec), qPrintable(why));
    return s;
}

struct Read {
    bool ok = false;
    std::vector<std::string> entries;
    GeoListStats stats;
    QString error;
};

Read parseFixture(const QByteArray &data, const QString &spec, GeoListKind kind,
          GeoDirection direction = GeoDirection::ThroughVpn)
{
    Read r;
    r.ok = appendGeoEntries(data, source(spec), kind, direction, &r.entries, &r.stats, &r.error);
    return r;
}

bool has(const Read &r, const std::string &entry)
{
    return std::find(r.entries.begin(), r.entries.end(), entry) != r.entries.end();
}

} // namespace

class TestGeoRules : public QObject {
    Q_OBJECT

private slots:
    void sourcesAreHttpsAndADatFileNamesItsCategory_data();
    void sourcesAreHttpsAndADatFileNamesItsCategory();
    void geositeTakesOneCategoryInEveryCase();
    void geositeDomainTypesMapToWhatTheCoreMatches();
    void geoipGivesNormalisedSubnets();
    void aMissingCategoryIsAnError();
    void anInvertedGeoipCategoryIsRefused();
    void aDamagedDatFileAddsNothing_data();
    void aDamagedDatFileAddsNothing();
    void unknownFieldsAreSkipped();
    void overflowingVarintsAreRejected();
    void invalidFieldNumbersAreRejected();
    void textListsReadEveryPublishedForm();
    void internationalNamesBecomeTheirAsciiForm();
    void aListWithNothingUsableIsAnError();
    void bypassListsCannotTakeTheWholeInternetOut();
    void joinedEntriesAreSortedAndUnique();
};

void TestGeoRules::overflowingVarintsAreRejected()
{
    // Unknown varint field with ten bytes: only bit 0 is legal in byte ten.
    QByteArray data = geosite("x", {domain(2, "example.com")});
    data += key(5, 0) + QByteArray(9, char(0x80)) + QByteArray(1, char(0x02));
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geosite.dat#x"), GeoListKind::Sites);
    QVERIFY(!r.ok);
    QVERIFY(r.entries.empty());
}

void TestGeoRules::sourcesAreHttpsAndADatFileNamesItsCategory_data()
{
    QTest::addColumn<QString>("spec");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<bool>("dat");
    QTest::addColumn<QString>("category");
    QTest::newRow("text list") << "https://example.com/lists/ru-blocked.txt" << true << false << "";
    QTest::newRow("dat with category") << "https://example.com/geoip.dat#ru-blocked" << true << true
                                       << "ru-blocked";
    QTest::newRow("dat upper case") << "https://example.com/GEOSITE.DAT#Category-RU" << true << true
                                    << "Category-RU";
    QTest::newRow("dat without category") << "https://example.com/geoip.dat" << false << false << "";
    QTest::newRow("attribute filter") << "https://example.com/dlc.dat#category-ru@!cn" << false << false
                                      << "";
    QTest::newRow("plain http") << "http://example.com/list.txt" << false << false << "";
    QTest::newRow("a file") << "file:///C:/lists/list.txt" << false << false << "";
    QTest::newRow("credentials") << "https://user:pw@example.com/list.txt" << false << false << "";
    QTest::newRow("not an address") << "ru-blocked" << false << false << "";
}

void TestGeoRules::sourcesAreHttpsAndADatFileNamesItsCategory()
{
    QFETCH(QString, spec);
    QFETCH(bool, valid);
    QFETCH(bool, dat);
    QFETCH(QString, category);
    QString why;
    const GeoSource s = parseGeoSource(spec, &why);
    QCOMPARE(s.valid(), valid);
    if (!valid) {
        QVERIFY(!why.isEmpty());
        return;
    }
    QCOMPARE(s.dat, dat);
    QCOMPARE(s.category, category);
    QVERIFY(!s.file.hasFragment()); // the category never reaches the server
}

void TestGeoRules::geositeTakesOneCategoryInEveryCase()
{
    const QByteArray data = geosite("GOOGLE", {domain(2, "google.com")})
            + geosite("RU-BLOCKED", {domain(2, "blocked.example"), domain(2, "other.example")})
            + geosite("CATEGORY-RU", {domain(2, "ya.ru")});
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geosite.dat#ru-blocked"), GeoListKind::Sites);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.stats.entries, 2);
    QVERIFY(has(r, "blocked.example"));
    QVERIFY(has(r, "other.example"));
    QVERIFY(!has(r, "google.com"));
    QVERIFY(!has(r, "ya.ru"));
}

void TestGeoRules::geositeDomainTypesMapToWhatTheCoreMatches()
{
    const QByteArray data = geosite("X", {domain(0, "keyword"), domain(1, "^re.*gex$"),
                                          domain(2, "root.example"), domain(3, "full.example")});
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geosite.dat#x"), GeoListKind::Sites);
    QVERIFY2(r.ok, qPrintable(r.error));
    // A root domain is itself and every name under it; a full one only itself.
    QVERIFY(has(r, "root.example"));
    QVERIFY(has(r, "*.root.example"));
    QVERIFY(has(r, "full.example"));
    QVERIFY(!has(r, "*.full.example"));
    // The core matches neither keywords nor regular expressions.
    QCOMPARE(r.stats.entries, 2);
    QCOMPARE(r.stats.skipped, 2);
    QCOMPARE(r.entries.size(), size_t(3));
}

void TestGeoRules::geoipGivesNormalisedSubnets()
{
    QByteArray v6(16, '\0');
    v6[0] = char(0x2a);
    v6[1] = char(0x14);
    v6[2] = char(0xe9);
    v6[15] = char(0x01); // host bits, cleared by the /20, as is half of 0xe9
    const QByteArray data = geoip("RU", {cidr(v4(5, 3, 7, 9), 24), cidr(v4(1, 1, 1, 1), 32), cidr(v6, 20)});
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geoip.dat#ru"), GeoListKind::Ips);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(has(r, "5.3.7.0/24"));
    QVERIFY(has(r, "1.1.1.1/32"));
    QVERIFY(has(r, "2a14:e000::/20"));
    QCOMPARE(r.stats.entries, 3);
}

void TestGeoRules::aMissingCategoryIsAnError()
{
    const QByteArray data = geosite("GOOGLE", {domain(2, "google.com")});
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geosite.dat#ru-blocked"), GeoListKind::Sites);
    QVERIFY(!r.ok);
    QVERIFY(r.error.contains(QStringLiteral("ru-blocked")));
    QVERIFY(r.entries.empty());
}

// "Every address but these" cannot be handed to the core as a list; taking it
// as "these" would route the opposite of what the list means.
void TestGeoRules::anInvertedGeoipCategoryIsRefused()
{
    const QByteArray data = geoip("NOT-RU", {cidr(v4(5, 3, 7, 0), 24)}, /*reverse=*/true);
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geoip.dat#not-ru"), GeoListKind::Ips);
    QVERIFY(!r.ok);
    QVERIFY(r.entries.empty());
}

void TestGeoRules::aDamagedDatFileAddsNothing_data()
{
    const QByteArray good = geosite("X", {domain(2, "a.example"), domain(2, "b.example")});
    QTest::addColumn<QByteArray>("data");
    QTest::newRow("truncated") << good.left(good.size() - 3);
    QTest::newRow("length past the end") << (key(1, 2) + varint(1000) + QByteArray("abc"));
    QTest::newRow("endless varint") << QByteArray(16, char(0xff));
    QTest::newRow("group wire type") << (key(7, 3) + good);
    QTest::newRow("field zero") << (QByteArray(1, '\0') + good);
    QTest::newRow("not protobuf") << QByteArray("<html><body>Not Found</body></html>");
}

void TestGeoRules::aDamagedDatFileAddsNothing()
{
    QFETCH(QByteArray, data);
    std::vector<std::string> out{"kept"};
    GeoListStats stats;
    QString error;
    QVERIFY(!appendGeoEntries(data, source(QStringLiteral("https://x.test/geosite.dat#x")),
                              GeoListKind::Sites, GeoDirection::ThroughVpn, &out, &stats, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(out, std::vector<std::string>{"kept"});
    QCOMPARE(stats.entries, 0);
}

void TestGeoRules::unknownFieldsAreSkipped()
{
    // A fixed64, a fixed32 and an unknown length field around the real ones, as
    // a newer file format may add.
    QByteArray body = key(9, 1) + QByteArray(8, 'x') + lenField(1, "X") + key(10, 5) + QByteArray(4, 'y')
            + lenField(2, domain(2, "a.example")) + lenField(11, "resource-hash");
    const QByteArray data = lenField(1, body) + key(2, 0) + varint(5);
    const Read r = parseFixture(data, QStringLiteral("https://x.test/geosite.dat#x"), GeoListKind::Sites);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(has(r, "a.example"));
}

void TestGeoRules::textListsReadEveryPublishedForm()
{
    const QByteArray text = "# a comment\n"
                            "domain:Blocked.Example\n"
                            "full:exact.example @ads\n"
                            "keyword:tracker\n"
                            "regexp:^ads[0-9]+\\.example$\n"
                            "include:other-list\n"
                            "\r\n"
                            "plain.example   # trailing comment\r\n"
                            "// another comment style\n"
                            "203.0.113.7\n"
                            "198.51.100.77/24\n"
                            "2001:db8::1/48\n"
                            "bad..name\n";
    const Read r = parseFixture(text, QStringLiteral("https://x.test/list.txt"), GeoListKind::Sites);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(has(r, "blocked.example"));
    QVERIFY(has(r, "*.blocked.example"));
    QVERIFY(has(r, "exact.example"));
    QVERIFY(!has(r, "*.exact.example"));
    QVERIFY(has(r, "plain.example"));
    QVERIFY(has(r, "*.plain.example"));
    QVERIFY(has(r, "203.0.113.7/32"));
    QVERIFY(has(r, "198.51.100.0/24"));
    QVERIFY(has(r, "2001:db8::/48"));
    QCOMPARE(r.stats.entries, 6);
    QCOMPARE(r.stats.skipped, 4); // keyword, regexp, include, bad..name
}

void TestGeoRules::internationalNamesBecomeTheirAsciiForm()
{
    const Read r = parseFixture(QByteArray("domain:пример.рф\nрф\n"), QStringLiteral("https://x.test/l.txt"),
                        GeoListKind::Sites);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(has(r, "xn--e1afmkfd.xn--p1ai"));
    QVERIFY(has(r, "*.xn--e1afmkfd.xn--p1ai"));
    // A whole zone is a list's to name: category-ru lists ru, su and рф.
    QVERIFY(has(r, "xn--p1ai"));
    QVERIFY(has(r, "*.xn--p1ai"));
}

// An empty or unusable answer is far likelier a broken download (an error page,
// a cut-off file) than a list meant to be empty, and taking it would quietly
// stop routing what the list named.
void TestGeoRules::aListWithNothingUsableIsAnError()
{
    std::vector<std::string> out;
    QString error;
    QVERIFY(!appendGeoEntries(QByteArray("# nothing\n\nkeyword:x\n"),
                              source(QStringLiteral("https://x.test/l.txt")), GeoListKind::Sites,
                              GeoDirection::ThroughVpn, &out, nullptr, &error));
    QVERIFY(out.empty());
    QVERIFY(!error.isEmpty());
}

// A list that takes traffic OUT of the tunnel is trusted with less: a few wide
// subnets would take most of the internet out with them while the window says
// Connected.
void TestGeoRules::bypassListsCannotTakeTheWholeInternetOut()
{
    const QByteArray text = "0.0.0.0/1\n128.0.0.0/1\n10.0.0.0/8\n::/1\n2000::/3\n2a14:e800::/29\n0.0.0.0/0\n";
    const Read bypass = parseFixture(text, QStringLiteral("https://x.test/l.txt"), GeoListKind::Ips,
                             GeoDirection::Bypass);
    QVERIFY2(bypass.ok, qPrintable(bypass.error));
    QVERIFY(has(bypass, "10.0.0.0/8"));
    QVERIFY(has(bypass, "2a14:e800::/29"));
    QVERIFY(!has(bypass, "0.0.0.0/1"));
    QVERIFY(!has(bypass, "128.0.0.0/1"));
    QVERIFY(!has(bypass, "::/1"));
    QVERIFY(!has(bypass, "2000::/3"));
    QCOMPARE(bypass.stats.skipped, 5);

    // Through the tunnel, a wide subnet only sends more through it; /0 still
    // means nothing a list could intend.
    const Read through = parseFixture(text, QStringLiteral("https://x.test/l.txt"), GeoListKind::Ips,
                              GeoDirection::ThroughVpn);
    QVERIFY(has(through, "0.0.0.0/1"));
    QVERIFY(!has(through, "0.0.0.0/0"));

    QVERIFY(isTooWideToBypass(QStringLiteral("0.0.0.0/7")));
    QVERIFY(!isTooWideToBypass(QStringLiteral("10.0.0.0/8")));
    QVERIFY(isTooWideToBypass(QStringLiteral("2000::/15")));
    QVERIFY(!isTooWideToBypass(QStringLiteral("2a00::/16")));
}

void TestGeoRules::joinedEntriesAreSortedAndUnique()
{
    const std::string joined = joinGeoEntries({"b.example", "*.a.example", "a.example", "b.example"});
    QCOMPARE(QString::fromStdString(joined), QStringLiteral("*.a.example\na.example\nb.example\n"));
    QCOMPARE(QString::fromStdString(joinGeoEntries({})), QString());
}

void TestGeoRules::invalidFieldNumbersAreRejected()
{
    const auto data = geosite("x", {domain(2, "example.com")}) + intField(0x20000000, 0);
    const Read result = parseFixture(data, QStringLiteral("https://x.test/geosite.dat#x"), GeoListKind::Sites);
    QVERIFY(!result.ok);
}

QTEST_MAIN(TestGeoRules)
#include "test_georules.moc"
