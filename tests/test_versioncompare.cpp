#include <QtTest>

#include "core/VersionCompare.h"

class TestVersionCompare : public QObject {
    Q_OBJECT

private slots:
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

QTEST_MAIN(TestVersionCompare)
#include "test_versioncompare.moc"
