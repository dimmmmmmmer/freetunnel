// cppcheck-suppress-file missingIncludeSystem
// Split-tunnel rules had no tests at all, which is how the two halves of this
// module drifted apart: isValidBypassRule() accepted a leading-dot rule and
// coreBypassRuleFor() dropped it, so ".example.com" was accepted by the UI,
// stored, listed back to the user — and never reached the core. In bypass mode
// its traffic went through the tunnel anyway; in "Through VPN" mode it did not go
// through at all. Either way the user was told the rule existed.
#include <QtTest>

#include "core/BypassRules.h"

class TestBypassRules : public QObject {
    Q_OBJECT

private slots:
    void everythingTheUiAcceptsReachesTheCore();
    void everythingTheUiAcceptsReachesTheCore_data();
    void leadingDotMeansTheSameAsAStar();
    void rejectsWhatIsNotARule();
    void rejectsWhatIsNotARule_data();
    void punycodesInternationalDomains();
    void sanitizeDropsUnusableRulesAndDuplicates();
    void aWildcardOnAnAddressIsRefusedAndSaysWhy();
};

// The invariant that broke. Validation and translation are two functions with
// separate notions of what a rule is, and nothing forced them to agree: a rule
// the UI accepts but the core never sees fails silently, which for a routing rule
// means traffic going somewhere the user asked it not to go.
void TestBypassRules::everythingTheUiAcceptsReachesTheCore_data()
{
    QTest::addColumn<QString>("rule");
    QTest::newRow("plain domain") << QStringLiteral("example.com");
    QTest::newRow("subdomain") << QStringLiteral("api.example.com");
    QTest::newRow("star wildcard") << QStringLiteral("*.example.com");
    QTest::newRow("leading dot") << QStringLiteral(".example.com");
    QTest::newRow("ipv4") << QStringLiteral("10.0.0.1");
    QTest::newRow("ipv4 cidr") << QStringLiteral("10.0.0.0/8");
    QTest::newRow("ipv6") << QStringLiteral("2001:db8::1");
    QTest::newRow("ipv6 cidr") << QStringLiteral("2001:db8::/32");
    QTest::newRow("idn") << QStringLiteral("пример.рф");
    QTest::newRow("idn wildcard") << QStringLiteral("*.пример.рф");
    // The same domain in the spelling an address bar or a log shows it in.
    QTest::newRow("punycode idn") << QStringLiteral("xn--e1afmkfd.xn--p1ai");
    QTest::newRow("punycode idn wildcard") << QStringLiteral("*.xn--e1afmkfd.xn--p1ai");
    QTest::newRow("punycode idn in capitals") << QStringLiteral("XN--E1AFMKFD.XN--P1AI");
}

void TestBypassRules::everythingTheUiAcceptsReachesTheCore()
{
    QFETCH(QString, rule);
    QVERIFY2(isValidBypassRule(rule), "the UI would reject this — fix the row, not the code");
    QVERIFY2(!coreBypassRuleFor(rule).isEmpty(),
             qPrintable(QStringLiteral("accepted by the UI but dropped before the core: %1")
                                .arg(rule)));
}

void TestBypassRules::leadingDotMeansTheSameAsAStar()
{
    // Not merely non-empty: the two spellings have to mean the same thing to the
    // core, or the rule silently covers a different set of hosts than it reads.
    QCOMPARE(coreBypassRuleFor(QStringLiteral(".example.com")),
             coreBypassRuleFor(QStringLiteral("*.example.com")));
    QCOMPARE(coreBypassRuleFor(QStringLiteral(".example.com")),
             QStringLiteral("*.example.com"));
}

void TestBypassRules::rejectsWhatIsNotARule_data()
{
    QTest::addColumn<QString>("rule");
    QTest::newRow("empty") << QString();
    QTest::newRow("bare dot") << QStringLiteral(".");
    QTest::newRow("bare star") << QStringLiteral("*.");
    QTest::newRow("no tld") << QStringLiteral("localhost");
    QTest::newRow("trailing dot") << QStringLiteral("example.");
    QTest::newRow("space") << QStringLiteral("exa mple.com");
    QTest::newRow("cidr out of range") << QStringLiteral("10.0.0.0/33");
    QTest::newRow("punycode tld ending in a hyphen") << QStringLiteral("example.xn--p1ai-");
    QTest::newRow("punycode prefix alone") << QStringLiteral("example.xn--");
    // A wildcard on an address. The core reads the first as subdomains of a
    // domain called 1.2.3.4 and drops the rest as malformed, so none of them
    // would ever match.
    QTest::newRow("star on an address") << QStringLiteral("*.1.2.3.4");
    QTest::newRow("dot on an address") << QStringLiteral(".1.2.3.4");
    QTest::newRow("star on a subnet") << QStringLiteral("*.10.0.0.0/8");
    QTest::newRow("dot on a subnet") << QStringLiteral(".10.0.0.0/8");
    QTest::newRow("star on an ipv6 address") << QStringLiteral("*.2001:db8::1");
    // Every address of its kind. The core takes it, and under "Bypass VPN" it
    // took all of that traffic out of the tunnel.
    QTest::newRow("every ipv4 address") << QStringLiteral("0.0.0.0/0");
    QTest::newRow("every ipv6 address") << QStringLiteral("::/0");
    QTest::newRow("every address, any base, /00") << QStringLiteral("10.0.0.0/00");
}

void TestBypassRules::rejectsWhatIsNotARule()
{
    QFETCH(QString, rule);
    QVERIFY2(!isValidBypassRule(rule), qPrintable(QStringLiteral("accepted: %1").arg(rule)));
    QVERIFY(coreBypassRuleFor(rule).isEmpty());
}

void TestBypassRules::punycodesInternationalDomains()
{
    // The core matches ASCII hostnames, so an IDN rule that reached it unconverted
    // would match nothing at all while looking perfectly correct in the list.
    QCOMPARE(coreBypassRuleFor(QStringLiteral("пример.рф")), QStringLiteral("xn--e1afmkfd.xn--p1ai"));
    QCOMPARE(coreBypassRuleFor(QStringLiteral("*.пример.рф")),
             QStringLiteral("*.xn--e1afmkfd.xn--p1ai"));
    // And one already in that spelling reaches the core as the same rule, rather
    // than being turned away for a top-level domain that is not all letters.
    QCOMPARE(coreBypassRuleFor(QStringLiteral("xn--e1afmkfd.xn--p1ai")),
             coreBypassRuleFor(QStringLiteral("пример.рф")));
    QCOMPARE(coreBypassRuleFor(QStringLiteral("*.XN--E1AFMKFD.XN--P1AI")),
             QStringLiteral("*.xn--e1afmkfd.xn--p1ai"));
}

void TestBypassRules::sanitizeDropsUnusableRulesAndDuplicates()
{
    const QStringList in{QStringLiteral("example.com"), QStringLiteral("  example.com  "),
                         QStringLiteral("localhost"), QStringLiteral(".example.org"),
                         QString()};
    const QStringList out = sanitizedBypassRules(in);
    // The user's own spelling is kept as the label; only unusable rules go.
    QCOMPARE(out, QStringList({QStringLiteral("example.com"), QStringLiteral(".example.org")}));
}

// The same refusal from the two places it has to come from: the field, which
// can say what to write instead, and a list saved by an earlier version, where
// the rule was stored and shown and did nothing. Kept, it would also count as a
// rule in "Through VPN", which then sent nothing at all through the tunnel.
void TestBypassRules::aWildcardOnAnAddressIsRefusedAndSaysWhy()
{
    QVERIFY(isWildcardAddressRule(QStringLiteral("*.10.0.0.0/8")));
    QVERIFY(isWildcardAddressRule(QStringLiteral(" .1.2.3.4 ")));
    QVERIFY(!isWildcardAddressRule(QStringLiteral("*.example.com")));
    QVERIFY(!isWildcardAddressRule(QStringLiteral("10.0.0.0/8")));

    const QStringList saved{QStringLiteral("*.1.2.3.4"), QStringLiteral("example.com"),
                            QStringLiteral("*.10.0.0.0/8")};
    QCOMPARE(sanitizedBypassRules(saved), QStringList{QStringLiteral("example.com")});
    QVERIFY(coreBypassRules({QStringLiteral("*.1.2.3.4"), QStringLiteral(".10.0.0.0/8")}).isEmpty());
}

QTEST_MAIN(TestBypassRules)
#include "test_bypassrules.moc"
