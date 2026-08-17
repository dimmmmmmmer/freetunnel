// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/VersionCompare.h"

class TestVersionCompare : public QObject {
    Q_OBJECT

private slots:
    void preReleasesOrderByNumberNotByText();
    void newerNumeric();
    void sameVersion();
    void suffixOrdering();
    void releaseBeatsPrerelease();
    void nonSemverFallback();
};

void TestVersionCompare::newerNumeric()
{
    QVERIFY(isVersionNewer(QStringLiteral("1.0.0"), QStringLiteral("1.0.1")));
    QVERIFY(isVersionNewer(QStringLiteral("0.5"), QStringLiteral("0.6")));
    QVERIFY(!isVersionNewer(QStringLiteral("2.0.0"), QStringLiteral("1.9.9")));
}

void TestVersionCompare::sameVersion()
{
    QVERIFY(!isVersionNewer(QStringLiteral("1.0.0"), QStringLiteral("1.0.0")));
    QVERIFY(!isVersionNewer(QStringLiteral("0.6b"), QStringLiteral("0.6b")));
}

void TestVersionCompare::suffixOrdering()
{
    QVERIFY(isVersionNewer(QStringLiteral("0.6a"), QStringLiteral("0.6b")));
    QVERIFY(!isVersionNewer(QStringLiteral("0.6b"), QStringLiteral("0.6a")));
}

void TestVersionCompare::releaseBeatsPrerelease()
{
    QVERIFY(isVersionNewer(QStringLiteral("0.6-beta"), QStringLiteral("0.6")));
    QVERIFY(!isVersionNewer(QStringLiteral("0.6"), QStringLiteral("0.6-beta")));
}

void TestVersionCompare::nonSemverFallback()
{
    QVERIFY(isVersionNewer(QStringLiteral("0.5b"), QStringLiteral("0.6b")));
    QVERIFY(!isVersionNewer(QStringLiteral("0.6b"), QStringLiteral("0.5b")));
}

// Pre-release suffixes were compared as plain strings, which puts "-rc10" below
// "-rc9" because '1' sorts before '9'. Anyone running rc9 was therefore never
// offered rc10, and the longer a pre-release cycle ran the more people it left
// behind — silently, since to them it simply looked like no update existed.
void TestVersionCompare::preReleasesOrderByNumberNotByText()
{
    QVERIFY2(isVersionNewer(QStringLiteral("1.2.0-rc9"), QStringLiteral("1.2.0-rc10")),
             "rc10 was not offered to rc9");
    QVERIFY(!isVersionNewer(QStringLiteral("1.2.0-rc10"), QStringLiteral("1.2.0-rc9")));

    // Two digits are not a special case; any run of them counts.
    QVERIFY(isVersionNewer(QStringLiteral("1.2.0-beta2"), QStringLiteral("1.2.0-beta11")));
    QVERIFY(!isVersionNewer(QStringLiteral("1.2.0-beta11"), QStringLiteral("1.2.0-beta2")));

    // Leading zeros are a spelling, not a value.
    QVERIFY(!isVersionNewer(QStringLiteral("1.2.0-rc9"), QStringLiteral("1.2.0-rc09")));

    // The rules that already held must keep holding: a release beats its own
    // pre-releases, and a pre-release never beats the release.
    QVERIFY(isVersionNewer(QStringLiteral("1.2.0-rc10"), QStringLiteral("1.2.0")));
    QVERIFY(!isVersionNewer(QStringLiteral("1.2.0"), QStringLiteral("1.2.0-rc10")));

    // A shorter suffix sorts first, so "-rc" precedes "-rc1".
    QVERIFY(isVersionNewer(QStringLiteral("1.2.0-rc"), QStringLiteral("1.2.0-rc1")));

    // And the numeric part still dominates the suffix entirely.
    QVERIFY(isVersionNewer(QStringLiteral("1.2.0-rc10"), QStringLiteral("1.3.0-rc1")));
}

QTEST_MAIN(TestVersionCompare)
#include "test_versioncompare.moc"
