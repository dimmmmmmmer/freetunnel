#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryFile>

#include "core/CredentialStore.h"
#include "core/ConfigToml.h"

using namespace freetunnel;

class TestCredentialStore : public QObject {
    Q_OBJECT

private slots:
    void init();
    void storeLoadDelete();
    void migrateStripsPassword();
};

void TestCredentialStore::init()
{
    // Hermetic file-fallback path: redirect standard paths away from the real
    // user scope (the OS keychain service name is hardcoded, so each test still
    // deletes the entries it creates).
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CredentialStoreTest"));
    QStandardPaths::setTestModeEnabled(true);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir credDir(dir + QStringLiteral("/credentials"));
    if (credDir.exists()) {
        for (const QFileInfo &fi : credDir.entryInfoList(QDir::Files))
            QFile::remove(fi.absoluteFilePath());
    }
}

void TestCredentialStore::storeLoadDelete()
{
    const QString key = QStringLiteral("/tmp/test-config.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QVERIFY(CredentialStore::deletePassword(key));
    QVERIFY(CredentialStore::loadPassword(key).isEmpty());
}

void TestCredentialStore::migrateStripsPassword()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QTemporaryFile tf(QDir(base).filePath(QStringLiteral("migrate-XXXXXX.toml")));
    tf.setAutoRemove(true);
    QVERIFY(tf.open());
    ConfigToml c;
    c.hostname = "h.example";
    c.addresses = "1.2.3.4:443";
    c.username = "u";
    c.password = "pw";
    tf.write(buildConfigToml(c).toUtf8());
    tf.close();

    const QString path = tf.fileName();
    QVERIFY(migrateConfigPassword(path));

    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    ConfigToml out = parseConfigToml(QString::fromUtf8(rf.readAll()));
    QVERIFY(out.password.isEmpty());
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigPath(path)),
             QStringLiteral("pw"));
}

QTEST_MAIN(TestCredentialStore)
#include "test_credentialstore.moc"
