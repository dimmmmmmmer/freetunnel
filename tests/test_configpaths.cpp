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
    void mixedScriptNamesAreFlaggedAndSingleScriptOnesAreNot();
    void aNameIsKeptAsTypedUnlessAFileCannotHoldIt_data();
    void aNameIsKeptAsTypedUnlessAFileCannotHoldIt();
    void aNameWithNothingLeftFallsBack();
    void twoSpellingsAreOneFileOnlyWhereTheFileSystemSaysSo();
};

void TestConfigPaths::sanitizeAndUniquePath()
{
    const QString stem = freetunnel::sanitizeConfigBaseName(QStringLiteral("My Server"));
    QCOMPARE(stem, QStringLiteral("My Server"));
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

// A deep link chooses the name it shows the user, so a name that reads like a
// config they already trust is the cheap half of a swap. Sanitizing the character
// set away is not available — this app ships a Russian UI and Cyrillic names are
// ordinary — so what is left is telling them.
void TestConfigPaths::mixedScriptNamesAreFlaggedAndSingleScriptOnesAreNot()
{
    // "Work" with a Cyrillic о: identical on screen to the Latin one.
    QVERIFY(freetunnel::nameMixesScripts(QStringLiteral("W\u043Erk")));
    QVERIFY(freetunnel::nameMixesScripts(QStringLiteral("Работа Work")));

    // Ordinary names in one alphabet must not be flagged, or the warning becomes
    // noise and stops being read — including the non-Latin ones this app expects.
    QVERIFY(!freetunnel::nameMixesScripts(QStringLiteral("Work")));
    QVERIFY(!freetunnel::nameMixesScripts(QStringLiteral("Работа")));
    QVERIFY(!freetunnel::nameMixesScripts(QStringLiteral("東京")));

    // Digits, punctuation and spaces belong to no script; a name made only of them
    // has nothing to mix.
    QVERIFY(!freetunnel::nameMixesScripts(QStringLiteral("Work-2 (fast)")));
    QVERIFY(!freetunnel::nameMixesScripts(QStringLiteral("Работа-2")));
    QVERIFY(!freetunnel::nameMixesScripts(QStringLiteral("12.34")));
    QVERIFY(!freetunnel::nameMixesScripts(QString()));
}

void TestConfigPaths::aNameIsKeptAsTypedUnlessAFileCannotHoldIt_data()
{
    QTest::addColumn<QString>("typed");
    QTest::addColumn<QString>("stem");
    // The editor's own example used to come out as "Germany___Frankfurt".
    QTest::newRow("spaces and a dot") << QStringLiteral("Germany · Frankfurt")
                                      << QStringLiteral("Germany · Frankfurt");
    QTest::newRow("Cyrillic") << QStringLiteral("Москва — центр") << QStringLiteral("Москва — центр");
    QTest::newRow("brackets and commas") << QStringLiteral("Work (fast), #2") << QStringLiteral("Work (fast), #2");
    QTest::newRow("an emoji") << QStringLiteral("Home \U0001F3E0") << QStringLiteral("Home \U0001F3E0");
    QTest::newRow("what Windows reserves")
            << QStringLiteral("a/b\\c:d*e?f\"g<h>i|j") << QStringLiteral("a_b_c_d_e_f_g_h_i_j");
    QTest::newRow("control characters") << QStringLiteral("tab\there\nnext") << QStringLiteral("tab_here_next");
    QTest::newRow("text turned around") << QStringLiteral("abc\u202Etxt.exe") << QStringLiteral("abc_txt.exe");
    QTest::newRow("outer spaces") << QStringLiteral("  padded  ") << QStringLiteral("padded");
    QTest::newRow("hidden file") << QStringLiteral(".hidden") << QStringLiteral("_hidden");
    QTest::newRow("a swept leftover") << QStringLiteral(".connect-x") << QStringLiteral("_connect-x");
    QTest::newRow("a device name") << QStringLiteral("CON") << QStringLiteral("CON_");
    QTest::newRow("one with a suffix") << QStringLiteral("con.backup") << QStringLiteral("con_.backup");
    QTest::newRow("a numbered port") << QStringLiteral("COM1") << QStringLiteral("COM1_");
    QTest::newRow("only looks like one") << QStringLiteral("Company") << QStringLiteral("Company");
}

// The file name is the name the list, the Connection page and the tray show.
void TestConfigPaths::aNameIsKeptAsTypedUnlessAFileCannotHoldIt()
{
    QFETCH(QString, typed);
    QFETCH(QString, stem);
    QCOMPARE(freetunnel::sanitizeConfigBaseName(typed), stem);
}

void TestConfigPaths::aNameWithNothingLeftFallsBack()
{
    QVERIFY(freetunnel::sanitizeConfigBaseName(QStringLiteral("   ")).startsWith(QLatin1String("imported-")));
    QVERIFY(freetunnel::sanitizeConfigBaseName(QString(), QStringLiteral("config"))
                    .startsWith(QLatin1String("config-")));
}

// On APFS and NTFS "work.toml" and "Work.toml" are one file; on ext4 they are
// two, and each exists only if it was made. Which one this host has is found out
// here the same way the code does, and the answer checked against it.
void TestConfigPaths::twoSpellingsAreOneFileOnlyWhereTheFileSystemSaysSo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString upper = dir.filePath(QStringLiteral("Work.toml"));
    const QString lower = dir.filePath(QStringLiteral("work.toml"));
    QFile f(upper);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    const bool foldsCase = QFileInfo::exists(lower);
    QCOMPARE(freetunnel::namesTheSameFile(upper, lower), foldsCase);
    QVERIFY(freetunnel::namesTheSameFile(upper, upper));
    QVERIFY(!freetunnel::namesTheSameFile(upper, dir.filePath(QStringLiteral("Home.toml"))));
    if (!foldsCase) {
        // Two real files that differ only in case are two configs.
        QFile g(lower);
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.close();
        QVERIFY(!freetunnel::namesTheSameFile(upper, lower));
    }
}

QTEST_MAIN(TestConfigPaths)
#include "test_configpaths.moc"
