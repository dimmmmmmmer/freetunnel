#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"

using namespace freetunnel;

class TestIntegrationConfigWorkflow : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void deepLinkToStoredConfigRoundTrip();
};

void TestIntegrationConfigWorkflow::initTestCase()
{
    // Hermetic: keep configs.json / standard paths off the real user scope.
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ConfigWorkflowTest"));
    QStandardPaths::setTestModeEnabled(true);
}

void TestIntegrationConfigWorkflow::deepLinkToStoredConfigRoundTrip()
{
    DeepLinkConfig in;
    in.hostname = QStringLiteral("workflow.example.com");
    in.addresses = {QStringLiteral("203.0.113.10:443")};
    in.username = QStringLiteral("workflow-user");
    in.password = QStringLiteral("workflow-pass");
    in.name = QStringLiteral("Workflow Test");

    QString err;
    const auto prepared = prepareDeepLinkImport(encodeDeepLink(in), &err);
    QVERIFY2(prepared.has_value(), qPrintable(err));

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(prepared->fileName);

    QFile out(target);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(out.write(prepared->tomlContent.toUtf8()) > 0);
    out.close();
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    QVERIFY(migrateConfigPassword(target));
    QVERIFY(CredentialStore::loadPassword(CredentialStore::keyForConfigPath(target))
            == QStringLiteral("workflow-pass"));

    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target))
        stored << target;
    saveStoredConfigs(stored);

    const QStringList reloaded = loadStoredConfigs();
    QVERIFY(reloaded.contains(target));

    QFile inFile(target);
    QVERIFY(inFile.open(QIODevice::ReadOnly));
    const ConfigToml parsed = parseConfigToml(QString::fromUtf8(inFile.readAll()));
    QCOMPARE(parsed.hostname, in.hostname);
    QCOMPARE(parsed.username, in.username);
    QVERIFY(parsed.password.isEmpty()); // migrated out of TOML

    stored.removeAll(target);
    saveStoredConfigs(stored);
    QFile::remove(target);
    CredentialStore::deletePassword(CredentialStore::keyForConfigPath(target));
}

QTEST_MAIN(TestIntegrationConfigWorkflow)
#include "test_integration_config_workflow.moc"
