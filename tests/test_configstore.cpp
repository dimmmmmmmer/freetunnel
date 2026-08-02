// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

#include "core/ConfigStore.h"

// Persistence + de-duplication of the stored config-path list. Uses
// QStandardPaths test mode so it writes to an isolated location.
class TestConfigStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyWhenNone();
    void roundTrip();
    void deduplicatesKeepingOrder();
    void aDamagedIndexIsRebuiltFromTheConfigsOnDisk();
    void anEmptyIndexIsNotTreatedAsDamage();
    void nonStringEntriesAreIgnored();
    void theIndexIsOwnerOnly();

private:
    static QString appConfigDir();
    static void clearConfigDir();
    static void writeConfig(const QString &name);
};

void TestConfigStore::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("FreeTunnelTest"));
    QStandardPaths::setTestModeEnabled(true);
}

QString TestConfigStore::appConfigDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

void TestConfigStore::clearConfigDir() {
    QDir d(appConfigDir());
    QDir().mkpath(appConfigDir());
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("*.toml")}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());
    QFile::remove(storagePath());
}

void TestConfigStore::writeConfig(const QString &name) {
    QFile f(QDir(appConfigDir()).filePath(name));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QByteArrayLiteral("[endpoint]\nhostname = \"x\"\n"));
}

void TestConfigStore::emptyWhenNone() {
    QFile::remove(storagePath());
    QVERIFY(loadStoredConfigs().isEmpty());
}

void TestConfigStore::roundTrip() {
    const QStringList in{QStringLiteral("/a/one.toml"), QStringLiteral("/b/two.toml")};
    saveStoredConfigs(in);
    QCOMPARE(loadStoredConfigs(), in);
}

void TestConfigStore::deduplicatesKeepingOrder() {
    saveStoredConfigs({QStringLiteral("/a/x.toml"), QStringLiteral("/a/x.toml"),
                       QStringLiteral("/b/y.toml")});
    QCOMPARE(loadStoredConfigs(),
             (QStringList{QStringLiteral("/a/x.toml"), QStringLiteral("/b/y.toml")}));
}

// A crash or a full disk mid-save used to leave a truncated configs.json, and
// with it every server vanished from the UI — while the .toml files were still
// sitting right there. The index is only an index, so rebuild it from them.
void TestConfigStore::aDamagedIndexIsRebuiltFromTheConfigsOnDisk() {
    clearConfigDir();
    writeConfig(QStringLiteral("alpha.toml"));
    writeConfig(QStringLiteral("beta.toml"));

    QFile broken(storagePath());
    QVERIFY(broken.open(QIODevice::WriteOnly | QIODevice::Truncate));
    broken.write(QByteArrayLiteral("[\"alpha.tom")); // truncated mid-write
    broken.close();

    const QStringList recovered = loadStoredConfigs();
    QCOMPARE(recovered.size(), 2);
    QVERIFY2(recovered.at(0).endsWith(QStringLiteral("alpha.toml")), qPrintable(recovered.at(0)));
    QVERIFY2(recovered.at(1).endsWith(QStringLiteral("beta.toml")), qPrintable(recovered.at(1)));

    // ...and the damaged index is replaced, so the next start does not have to
    // rediscover all over again.
    QFile fixed(storagePath());
    QVERIFY(fixed.open(QIODevice::ReadOnly));
    QVERIFY2(QJsonDocument::fromJson(fixed.readAll()).isArray(),
             "the damaged index was left in place");
}

// Removing every config is a legitimate thing to do. Resurrecting the .toml
// files from disk in that case would undo the user's deletion at every start.
void TestConfigStore::anEmptyIndexIsNotTreatedAsDamage() {
    clearConfigDir();
    writeConfig(QStringLiteral("leftover.toml"));
    saveStoredConfigs({});
    QVERIFY2(loadStoredConfigs().isEmpty(), "a deleted config came back on its own");
}

// The index is JSON on disk; anything can end up in it. Non-strings must be
// dropped rather than turned into empty paths that show as blank rows.
void TestConfigStore::nonStringEntriesAreIgnored() {
    clearConfigDir();
    QFile f(storagePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(QByteArrayLiteral("[\"/a/one.toml\", 42, null, {\"x\": 1}, [\"nested\"]]"));
    f.close();
    QCOMPARE(loadStoredConfigs(), QStringList{QStringLiteral("/a/one.toml")});
}

// The index lists paths to files holding hostnames, usernames and certificates.
void TestConfigStore::theIndexIsOwnerOnly() {
#if defined(Q_OS_UNIX)
    saveStoredConfigs({QStringLiteral("/a/one.toml")});
    const QFile::Permissions perms = QFile::permissions(storagePath());
    QVERIFY2(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)),
             "configs.json is readable by other local users");
#else
    QSKIP("no POSIX mode bits: the file inherits the app directory ACL");
#endif
}

QTEST_MAIN(TestConfigStore)
#include "test_configstore.moc"
