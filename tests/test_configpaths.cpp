// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include "core/ConfigPaths.h"

class TestConfigPaths : public QObject {
    Q_OBJECT

private slots:
    void sanitizeAndUniquePath();
    void ownerConfigPathForSaveReusesExisting();
    void entryMatchingPrefersTheExactNameThenFoldsCase();
    void existingConfigPathFindsAnExactCollision();
    void existingConfigPathReportsNoCollisionForAFreeName();
};

void TestConfigPaths::sanitizeAndUniquePath()
{
    const QString stem = freetunnel::sanitizeConfigBaseName(QStringLiteral("My Server"));
    QCOMPARE(stem, QStringLiteral("My_Server"));
    const QString path = freetunnel::uniqueOwnerConfigPath(stem);
    QVERIFY(path.endsWith(QStringLiteral(".toml")));
}

void TestConfigPaths::ownerConfigPathForSaveReusesExisting()
{
    const QString stem = QStringLiteral("vpn-test");
    const QString first = freetunnel::uniqueOwnerConfigPath(stem);
    QVERIFY(QFileInfo(first).completeBaseName() == stem);
    QCOMPARE(freetunnel::ownerConfigPathForSave(stem, first), first);
    const QString renamed = freetunnel::ownerConfigPathForSave(QStringLiteral("vpn-renamed"), first);
    QVERIFY(renamed.endsWith(QStringLiteral(".toml")));
    QVERIFY(renamed != first);
}

// The case-folding branch decides which credential a deep-link "Replace" deletes
// and which configs.json row it matches, so it has to be covered on every host —
// including the case-sensitive ones where the collision itself cannot happen.
// That is why the folding is a pure function over a directory listing.
void TestConfigPaths::entryMatchingPrefersTheExactNameThenFoldsCase()
{
    const QStringList entries{QStringLiteral("Home.toml"), QStringLiteral("Work.toml")};

    // Exact wins even when a case variant is also present: on a case-sensitive
    // filesystem those are genuinely two different configs.
    const QStringList both{QStringLiteral("Work.toml"), QStringLiteral("work.toml")};
    QCOMPARE(freetunnel::configEntryMatching(both, QStringLiteral("work.toml")),
             QStringLiteral("work.toml"));

    // This is the APFS/NTFS case: the link carried "work.toml", the user's file is
    // "Work.toml", and the name on disk is the one that must be used downstream.
    QCOMPARE(freetunnel::configEntryMatching(entries, QStringLiteral("work.toml")),
             QStringLiteral("Work.toml"));
    QCOMPARE(freetunnel::configEntryMatching(entries, QStringLiteral("WORK.TOML")),
             QStringLiteral("Work.toml"));

    QVERIFY(freetunnel::configEntryMatching(entries, QStringLiteral("other.toml")).isEmpty());
}

void TestConfigPaths::existingConfigPathFindsAnExactCollision()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile f(QDir(dir.path()).filePath(QStringLiteral("Work.toml")));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();

    QCOMPARE(freetunnel::existingConfigPath(dir.path(), QStringLiteral("Work.toml")),
             QDir(dir.path()).filePath(QStringLiteral("Work.toml")));
}

void TestConfigPaths::existingConfigPathReportsNoCollisionForAFreeName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // Nothing there at all, and — on a case-sensitive filesystem — a name that
    // differs only in case from an existing file is a different config, not a
    // collision. The filesystem is what decides, so this assertion holds on both
    // kinds of host: it is the same question the filesystem was just asked.
    QVERIFY(freetunnel::existingConfigPath(dir.path(), QStringLiteral("Work.toml")).isEmpty());
}

QTEST_MAIN(TestConfigPaths)
#include "test_configpaths.moc"
