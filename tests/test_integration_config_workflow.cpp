// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
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
    void aCommentedHeaderDoesNotKeepThePasswordOnDisk();
    void aClientRandomReachesTheCoreInAFormItTakes();
    void aQuotedPasswordGoesToTheStore();
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

// The whole path a hand-written config takes, for one whose table headers have
// comments after them: the migration on import and the one before every connect.
// The headers were not recognised. The password ended up among the root's unknown
// keys, so each migration wrote it back to the file it had just taken it from, and
// the core was sent the default routes instead of the file's.
void TestIntegrationConfigWorkflow::aCommentedHeaderDoesNotKeepThePasswordOnDisk()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(
            QStringLiteral("commented-header-%1.toml").arg(QCoreApplication::applicationPid()));
    const QString key = CredentialStore::keyForConfigPath(target);
    const auto cleanup = qScopeGuard([&] {
        QFile::remove(target);
        CredentialStore::deletePassword(key);
    });
    QFile out(target);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write("loglevel = \"info\"\n"
              "[endpoint] # main server\n"
              "hostname = \"vpn.example.com\"\n"
              "addresses = [\"203.0.113.10:443\"]\n"
              "username = \"u\"\n"
              "password = \"commented-pass\"\n"
              "[listener.tun] # the provider's routes\n"
              "mtu_size = 1280\n"
              "excluded_routes = [\"10.9.0.0/16\"]\n");
    out.close();

    const auto onDisk = [&target] {
        QFile in(target);
        return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
    };
    QVERIFY(migrateConfigPassword(target));
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("commented-pass"));
    QVERIFY2(!onDisk().contains(QStringLiteral("commented-pass")), qPrintable(onDisk()));

    // The connect path migrates again first; the password reaches the core from
    // the store, in [endpoint], and the file still does not have it.
    const QString connect = buildConnectConfigToml(target);
    const ConfigToml sent = parseConfigToml(connect);
    QCOMPARE(sent.password, QStringLiteral("commented-pass"));
    QCOMPARE(sent.hostname, QStringLiteral("vpn.example.com"));
    QVERIFY2(connect.contains(QStringLiteral("excluded_routes = [\"10.9.0.0/16\"]")), qPrintable(connect));
    QVERIFY2(!connect.contains(QStringLiteral("192.168.0.0/16")), qPrintable(connect));
    QVERIFY2(!onDisk().contains(QStringLiteral("commented-pass")), qPrintable(onDisk()));

    // A config 1.2.2 already rewrote that way - the endpoint's keys at the root,
    // the password among them - is cleaned up by the next connect.
    QFile damaged(target);
    QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
    damaged.write("loglevel = \"info\"\n"
                  "hostname = \"vpn.example.com\"\n"
                  "password = \"left-at-root\"\n"
                  "\n[endpoint]\n"
                  "hostname = \"vpn.example.com\"\n"
                  "addresses = [\"203.0.113.10:443\"]\n"
                  "username = \"u\"\n"
                  "\n[listener.tun]\n"
                  "mtu_size = 1280\n");
    damaged.close();
    QCOMPARE(parseConfigToml(buildConnectConfigToml(target)).password, QStringLiteral("left-at-root"));
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("left-at-root"));
    QVERIFY2(!onDisk().contains(QStringLiteral("left-at-root")), qPrintable(onDisk()));
}

// The client random on the real path to the core: a config 1.2.2 imported from a
// link, with the mask under a key of its own, and one whose value ends in more
// slashes than one. What the helper is handed must be "prefix[/mask]" with
// something after any slash, or the core refuses the whole config.
void TestIntegrationConfigWorkflow::aClientRandomReachesTheCoreInAFormItTakes()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(
            QStringLiteral("client-random-%1.toml").arg(QCoreApplication::applicationPid()));
    const QString key = CredentialStore::keyForConfigPath(target);
    const auto cleanup = qScopeGuard([&] {
        QFile::remove(target);
        CredentialStore::deletePassword(key);
    });
    const auto connectWith = [&](const QByteArray &clientRandomKeys) {
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString();
        out.write("loglevel = \"info\"\n"
                  "[endpoint]\n"
                  "hostname = \"vpn.example.com\"\n"
                  "addresses = [\"203.0.113.10:443\"]\n"
                  "username = \"u\"\n"
                  "password = \"random-pass\"\n"
                  + clientRandomKeys
                  + "[listener.tun]\n"
                    "mtu_size = 1500\n");
        out.close();
        return buildConnectConfigToml(target);
    };

    const QString joined = connectWith("client_random = \"deadbeef\"\nclient_random_mask = \"ffff0000\"\n");
    QVERIFY2(joined.contains(QStringLiteral("client_random = \"deadbeef/ffff0000\"\n")), qPrintable(joined));
    QVERIFY2(!joined.contains(QStringLiteral("client_random_mask")), qPrintable(joined));

    // Each pass on the way (the migration's read and write, the connect's read and
    // write) dropped one slash, so it takes this many to show.
    const QString slashes = connectWith("client_random = \"aa////////\"\n");
    QVERIFY2(slashes.contains(QStringLiteral("client_random = \"aa\"\n")), qPrintable(slashes));
}

// A key may be quoted, and `"password"` is the server's password as much as
// `password` is. Compared as written it was not found: the import left it in the
// file in plain text, and the connect had no password to send.
void TestIntegrationConfigWorkflow::aQuotedPasswordGoesToTheStore()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(
            QStringLiteral("quoted-key-%1.toml").arg(QCoreApplication::applicationPid()));
    const QString key = CredentialStore::keyForConfigPath(target);
    const auto cleanup = qScopeGuard([&] {
        QFile::remove(target);
        CredentialStore::deletePassword(key);
    });
    QFile out(target);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write("loglevel = \"info\"\n"
              "[endpoint]\n"
              "hostname = \"vpn.example.com\"\n"
              "addresses = [\"203.0.113.10:443\"]\n"
              "username = \"u\"\n"
              "\"password\" = \"quoted-pass\"\n"
              "[listener.tun]\n"
              "mtu_size = 1500\n");
    out.close();
    const auto onDisk = [&target] {
        QFile in(target);
        return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
    };

    QVERIFY(migrateConfigPassword(target));
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("quoted-pass"));
    QVERIFY2(!onDisk().contains(QStringLiteral("quoted-pass")), qPrintable(onDisk()));

    // Sent once, from the store: a second password key would make the core refuse
    // the config.
    const QString connect = buildConnectConfigToml(target);
    QCOMPARE(parseConfigToml(connect).password, QStringLiteral("quoted-pass"));
    QCOMPARE(connect.count(QStringLiteral("password")), 1);
}

QTEST_MAIN(TestIntegrationConfigWorkflow)
#include "test_integration_config_workflow.moc"
