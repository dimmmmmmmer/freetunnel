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

QTEST_MAIN(TestBypassRules)
#include "test_bypassrules.moc"
