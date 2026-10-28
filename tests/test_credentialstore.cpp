// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QTemporaryFile>

#include "core/CredentialStore.h"
#include "core/ConfigToml.h"

#include <optional>

using namespace freetunnel;

namespace {

StoredSecret secret(SecretStatus status, const QString &value = QString(), bool legacy = false,
                    const QString &detail = QString())
{
    StoredSecret s;
    s.status = status;
    s.value = value;
    s.fromLegacyFile = legacy;
    s.detail = detail;
    return s;
}

StoredSecret foundAs(const QString &value) { return secret(SecretStatus::Found, value); }
StoredSecret notFound() { return secret(SecretStatus::NotFound); }
StoredSecret failed(const QString &why = QStringLiteral("could not ask")) { return secret(SecretStatus::Error, QString(), false, why); }

// Warnings the store logged, to say what they carry (and what they never do).
QStringList g_logged;
QtMessageHandler g_previousHandler = nullptr;
void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (QLatin1String(context.category) == QLatin1String("freetunnel.credentialstore") && type == QtWarningMsg) {
        g_logged << message;
        return;
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

} // namespace

class TestCredentialStore : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void storeLoadDelete();
    void anEntryThatIsNotThereIsNotFound();
    void theLinuxSourcesAddUp();
    void aReadFailureIsAnErrorForTheChosenEntriesOnly();
    void anErrorIsLoggedWithoutItsValue();
    void aConfigsInlineSecretsAreUsedWithoutAskingTheStore();
    void aStoredPasswordWithAKeyThatCannotBeReadIsUnreadable();
    void aPasswordFromTheLegacyFileHasNoKeyInTheStore();
    void aStoredPasswordWithNoKeyIsAConfigWithoutOne();
    void aPasswordThatCannotBeReadStopsTheReading();
    void aStoredKeyIsRead();
    void anInlineKeyIsUsedWithoutAskingForIt();
    void noConnectConfigIsBuiltWhenASecretCannotBeRead();
    void migrationTouchesNothingWhenTheOldPasswordCannotBeRead();
    void migrationOfOneSecretNeedsNoRead();
    void migrateStripsPassword();
    void migrateMovesTheAuthKeyToo();
    void keepsTheEventLoopRunningWhileItWaits();
    void worksOffTheMainThread();
    void reentryDoesNotStackEventLoops();

private:
    // A config file in the app's folder with the given secrets inline; the entries
    // it would have in the store are forgotten when the test is done.
    QString configFile(const QString &stem, const QString &password = QString(),
                       const QString &authKey = QString());
    static ConfigToml parsed(const QString &path);
    static QString readText(const QString &path);
    QStringList m_made;
};

void TestCredentialStore::init()
{
    // Hermetic: standard paths point away from the real user scope, and under
    // FT_ENABLE_TEST_HOOKS credentialServiceName() is unique to this process, so
    // nothing here can name — let alone overwrite — a real entry.
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

QString TestCredentialStore::configFile(const QString &stem, const QString &password, const QString &authKey)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString path = QDir(base).filePath(
            QStringLiteral("%1-%2.toml").arg(stem).arg(QCoreApplication::applicationPid()));
    ConfigToml c;
    c.hostname = QStringLiteral("h.example");
    c.addresses = QStringLiteral("1.2.3.4:443");
    c.username = QStringLiteral("u");
    c.password = password;
    c.clientRandomAuthKey = authKey;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(buildConfigToml(c).toUtf8());
    f.close();
    m_made << path;
    return path;
}

QString TestCredentialStore::readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

ConfigToml TestCredentialStore::parsed(const QString &path)
{
    return parseConfigToml(readText(path));
}

void TestCredentialStore::cleanup()
{
    CredentialStore::setCallHookForTest({});
    CredentialStore::setReadFailureForTest({});
    CredentialStore::setLookupOverrideForTest({});
    for (const QString &path : std::as_const(m_made)) {
        CredentialStore::deletePassword(CredentialStore::keyForConfigPath(path));
        CredentialStore::deletePassword(CredentialStore::keyForConfigAuthKey(path));
        QFile::remove(path);
    }
    m_made.clear();
    if (g_previousHandler) {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
    }
    g_logged.clear();
}

void TestCredentialStore::storeLoadDelete()
{
    const QString key = QStringLiteral("/tmp/test-config.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));
    const StoredSecret stored = CredentialStore::lookup(key);
    QVERIFY2(stored.found(), "a stored entry was not found");
    QCOMPARE(stored.value, QStringLiteral("s3cret"));
    QVERIFY(!stored.fromLegacyFile);
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QVERIFY(CredentialStore::deletePassword(key));
    const StoredSecret gone = CredentialStore::lookup(key);
    QVERIFY2(gone.notFound(), "a deleted entry is not 'not found'");
    QVERIFY(gone.value.isEmpty());
}

// The guard for every config without an auth key, which is nearly all of them: the
// real store, on each platform CI runs, says "not found" for an entry it holds
// nothing under, and not that it could not be asked. A store that said the second
// would stop every keyless config from connecting.
void TestCredentialStore::anEntryThatIsNotThereIsNotFound()
{
    const StoredSecret none = CredentialStore::lookup(QStringLiteral("/tmp/ft-never-stored-%1.toml")
                                                              .arg(QCoreApplication::applicationPid()));
    if (none.failed() && !CredentialStore::secureStorageAvailable())
        QSKIP("there is no credential store on this machine to ask");
    QVERIFY2(none.notFound(), qPrintable(none.detail));
    QVERIFY(none.value.isEmpty());
    QVERIFY(!none.fromLegacyFile);
}

// How the Linux sources become one answer. Pure, so it is checked here on every
// platform; the sources themselves are the rest of the file's, and the probe test's.
void TestCredentialStore::theLinuxSourcesAddUp()
{
    struct Case {
        const char *name;
        std::optional<StoredSecret> libsecret;
        std::optional<StoredSecret> tool;
        StoredSecret file;
        bool fallback;
        SecretStatus want;
        QString value;
        bool legacy;
        QString detail;
    };
    const std::optional<StoredSecret> noSource;
    const QList<Case> cases = {
        {"libsecret has it", foundAs("a"), foundAs("b"), foundAs("c"), false, SecretStatus::Found, "a", false, {}},
        {"libsecret does not, secret-tool does", notFound(), foundAs("b"), notFound(), false, SecretStatus::Found, "b", false, {}},
        {"libsecret could not look, secret-tool has it", failed("l"), foundAs("b"), notFound(), false, SecretStatus::Found, "b", false, {}},
        {"only the legacy file has it", notFound(), notFound(), foundAs("c"), false, SecretStatus::Found, "c", true, {}},
        {"only the legacy file has it, with the fallback", notFound(), notFound(), foundAs("c"), true, SecretStatus::Found, "c", false, {}},
        {"libsecret could not look, the legacy file has it", failed("l"), notFound(), foundAs("c"), false, SecretStatus::Found, "c", true, {}},
        {"libsecret could not look, nothing else has it", failed("l"), notFound(), notFound(), false, SecretStatus::Error, {}, false, "l"},
        {"libsecret could not look, secret-tool is not there", failed("l"), noSource, notFound(), false, SecretStatus::Error, {}, false, "l"},
        {"libsecret says no, secret-tool could not look", notFound(), failed("t"), notFound(), false, SecretStatus::NotFound, {}, false, {}},
        {"libsecret says no, secret-tool is not there", notFound(), noSource, notFound(), false, SecretStatus::NotFound, {}, false, {}},
        {"no libsecret, secret-tool says no", noSource, notFound(), notFound(), false, SecretStatus::NotFound, {}, false, {}},
        {"no libsecret, secret-tool could not look", noSource, failed("t"), notFound(), false, SecretStatus::Error, {}, false, "t"},
        {"no libsecret, secret-tool could not look, the legacy file has it", noSource, failed("t"), foundAs("c"), false, SecretStatus::Found, "c", true, {}},
        {"no libsecret, secret-tool has it", noSource, foundAs("b"), notFound(), false, SecretStatus::Found, "b", false, {}},
        {"nothing to ask, no fallback", noSource, noSource, notFound(), false, SecretStatus::Error, {}, false, {}},
        {"nothing to ask, the legacy file has it", noSource, noSource, foundAs("c"), false, SecretStatus::Found, "c", true, {}},
        {"nothing to ask, with the fallback", noSource, noSource, notFound(), true, SecretStatus::NotFound, {}, false, {}},
        {"nothing to ask, with the fallback, the file has it", noSource, noSource, foundAs("c"), true, SecretStatus::Found, "c", false, {}},
    };
    for (const Case &c : cases) {
        LinuxLookupSources in;
        in.libsecret = c.libsecret;
        in.secretTool = c.tool;
        in.legacyFile = c.file;
        in.insecureFallbackBuild = c.fallback;
        const StoredSecret got = combineLinuxLookup(in);
        QVERIFY2(got.status == c.want, c.name);
        QVERIFY2(got.value == c.value, c.name);
        QVERIFY2(got.fromLegacyFile == c.legacy, c.name);
        if (!c.detail.isEmpty())
            QVERIFY2(got.detail == c.detail, c.name);
    }
}

void TestCredentialStore::aReadFailureIsAnErrorForTheChosenEntriesOnly()
{
    const QString a = QStringLiteral("/tmp/ft-read-failure-a.toml");
    const QString b = QStringLiteral("/tmp/ft-read-failure-b.toml");
    QVERIFY(CredentialStore::storePassword(a, QStringLiteral("pa")));
    QVERIFY(CredentialStore::storePassword(b, QStringLiteral("pb")));

    CredentialStore::setReadFailureForTest([&a](const QString &key) { return key == a; });
    const StoredSecret failedRead = CredentialStore::lookup(a);
    QVERIFY(failedRead.failed());
    QVERIFY(!failedRead.found() && !failedRead.notFound());
    QVERIFY2(failedRead.value.isEmpty(), "an unreadable entry handed out a value");
    QVERIFY(CredentialStore::loadPassword(a).isEmpty());
    QCOMPARE(CredentialStore::lookup(b).value, QStringLiteral("pb"));

    CredentialStore::setReadFailureForTest({});
    QCOMPARE(CredentialStore::lookup(a).value, QStringLiteral("pa")); // it was never touched

    CredentialStore::deletePassword(a);
    CredentialStore::deletePassword(b);
}

// What the log says about an entry that could not be read: which kind and whose,
// and why. Never what it holds.
void TestCredentialStore::anErrorIsLoggedWithoutItsValue()
{
    const QString secretValue = QStringLiteral("hunter2-do-not-log");
    const QString path = QStringLiteral("/home/someone/configs/office.toml");
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    const QString subscriptionEntry = CredentialStore::keyForSubscription(QStringLiteral("source-1"));
    QVERIFY(CredentialStore::storePassword(passwordEntry, secretValue));

    g_previousHandler = qInstallMessageHandler(collect);
    CredentialStore::setReadFailureForTest([](const QString &) { return true; });
    QVERIFY(CredentialStore::lookup(passwordEntry).failed());
    QVERIFY(CredentialStore::lookup(keyEntry).failed());
    QVERIFY(CredentialStore::lookup(subscriptionEntry).failed());
    QVERIFY(CredentialStore::lookup(CredentialStore::keyForInstanceToken()).failed());
    const QStringList logged = g_logged;
    CredentialStore::setReadFailureForTest({});
    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;

    QCOMPARE(logged.size(), 4);
    QVERIFY2(logged.at(0).contains(QStringLiteral("password of office.toml")), qPrintable(logged.at(0)));
    QVERIFY2(logged.at(1).contains(QStringLiteral("auth key of office.toml")), qPrintable(logged.at(1)));
    QVERIFY2(logged.at(2).contains(QStringLiteral("subscription address")), qPrintable(logged.at(2)));
    QVERIFY2(logged.at(3).contains(QStringLiteral("instance token")), qPrintable(logged.at(3)));
    for (const QString &line : logged) {
        QVERIFY2(!line.contains(secretValue), qPrintable(line));
        QVERIFY2(!line.contains(QStringLiteral("/home/someone")), "the folder a config is in is not logged");
    }
    CredentialStore::deletePassword(passwordEntry);
}

// The file's own copy of a secret is the one in use, and no store is asked for it.
// A password in the file and a key that cannot be read (a Linux machine with no
// keyring, whose configs keep their password in the file) is a config that
// connects, as it always has.
void TestCredentialStore::aConfigsInlineSecretsAreUsedWithoutAskingTheStore()
{
    const QString path = configFile(QStringLiteral("inline"), QStringLiteral("file-pw"));
    QStringList asked;
    CredentialStore::setCallHookForTest([&asked](const char *call, const QString &key) {
        if (QLatin1String(call) == QLatin1String("load"))
            asked << key;
    });
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == keyEntry; });

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY2(secrets.unreadable == SecretUnreadable::None, "a key that a file-kept password never had was asked for");
    QCOMPARE(secrets.password, QStringLiteral("file-pw"));
    QVERIFY(secrets.authKey.isEmpty());
    QVERIFY2(!asked.contains(passwordEntry), "the store was asked for a password the file holds");

    // Both inline: neither is asked for.
    asked.clear();
    const QString both = configFile(QStringLiteral("inline-both"), QStringLiteral("p"), QStringLiteral("aabb"));
    const ConfigSecrets bothSecrets = readConfigSecrets(both, parsed(both));
    QCOMPARE(bothSecrets.password, QStringLiteral("p"));
    QCOMPARE(bothSecrets.authKey, QStringLiteral("aabb"));
    QVERIFY(bothSecrets.unreadable == SecretUnreadable::None);
    QVERIFY2(asked.isEmpty(), qPrintable(asked.join(QLatin1Char(' '))));
}

// The store is where a config keeps its secrets when its password is there; a key
// it cannot be asked for is then a key that may exist, and the config is not read
// as one without.
void TestCredentialStore::aStoredPasswordWithAKeyThatCannotBeReadIsUnreadable()
{
    const QString path = configFile(QStringLiteral("stored"));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForConfigPath(path), QStringLiteral("store-pw")));
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == keyEntry; });

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY(secrets.unreadable == SecretUnreadable::AuthKey);
    QCOMPARE(secrets.password, QStringLiteral("store-pw"));
}

// Linux, no keyring: the password never reached the store, so no key did either.
void TestCredentialStore::aPasswordFromTheLegacyFileHasNoKeyInTheStore()
{
    const QString path = configFile(QStringLiteral("legacy"));
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    CredentialStore::setLookupOverrideForTest([&](const QString &key) -> std::optional<StoredSecret> {
        if (key == passwordEntry)
            return secret(SecretStatus::Found, QStringLiteral("legacy-pw"), /*legacy=*/true);
        if (key == keyEntry)
            return failed();
        return std::nullopt;
    });

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY2(secrets.unreadable == SecretUnreadable::None, "a keyless config on a machine with no keyring cannot connect");
    QCOMPARE(secrets.password, QStringLiteral("legacy-pw"));
    QVERIFY(secrets.authKey.isEmpty());

    // The same password found in the store proper is the other case.
    CredentialStore::setLookupOverrideForTest([&](const QString &key) -> std::optional<StoredSecret> {
        if (key == passwordEntry)
            return foundAs(QStringLiteral("store-pw"));
        if (key == keyEntry)
            return failed();
        return std::nullopt;
    });
    QVERIFY(readConfigSecrets(path, parsed(path)).unreadable == SecretUnreadable::AuthKey);
}

void TestCredentialStore::aStoredPasswordWithNoKeyIsAConfigWithoutOne()
{
    const QString path = configFile(QStringLiteral("keyless"));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForConfigPath(path), QStringLiteral("store-pw")));

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY(secrets.unreadable == SecretUnreadable::None);
    QCOMPARE(secrets.password, QStringLiteral("store-pw"));
    QVERIFY(secrets.authKey.isEmpty());
}

void TestCredentialStore::aPasswordThatCannotBeReadStopsTheReading()
{
    const QString path = configFile(QStringLiteral("locked"));
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    QVERIFY(CredentialStore::storePassword(passwordEntry, QStringLiteral("store-pw")));
    QVERIFY(CredentialStore::storePassword(keyEntry, QStringLiteral("aabb")));
    QStringList asked;
    CredentialStore::setCallHookForTest([&asked](const char *call, const QString &key) {
        if (QLatin1String(call) == QLatin1String("load"))
            asked << key;
    });
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == passwordEntry; });

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY(secrets.unreadable == SecretUnreadable::Password);
    QVERIFY2(secrets.password.isEmpty() && secrets.authKey.isEmpty(), "a failed read handed out a secret");
    QVERIFY2(!asked.contains(keyEntry), "the key was read after the password could not be");
}

void TestCredentialStore::aStoredKeyIsRead()
{
    const QString path = configFile(QStringLiteral("withkey"));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForConfigPath(path), QStringLiteral("store-pw")));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForConfigAuthKey(path), QStringLiteral("aabbccdd")));

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY(secrets.unreadable == SecretUnreadable::None);
    QCOMPARE(secrets.password, QStringLiteral("store-pw"));
    QCOMPARE(secrets.authKey, QStringLiteral("aabbccdd"));
}

// A key in the file is used without asking for the one in the store.
void TestCredentialStore::anInlineKeyIsUsedWithoutAskingForIt()
{
    const QString path = configFile(QStringLiteral("inlinekey"), QString(), QStringLiteral("11223344"));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForConfigPath(path), QStringLiteral("store-pw")));
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == keyEntry; });

    const ConfigSecrets secrets = readConfigSecrets(path, parsed(path));
    QVERIFY(secrets.unreadable == SecretUnreadable::None);
    QCOMPARE(secrets.authKey, QStringLiteral("11223344"));
}

// A TOML missing a secret the config has would connect without it, and a server
// that checks the key would turn the client away with nothing to say why. So no
// config is built at all, whichever secret is missing, and the reason is told apart.
void TestCredentialStore::noConnectConfigIsBuiltWhenASecretCannotBeRead()
{
    using Problem = ConnectConfig::Problem;

    // The password cannot be read.
    const QString path = configFile(QStringLiteral("connect-pw"));
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    QVERIFY(CredentialStore::storePassword(passwordEntry, QStringLiteral("store-pw")));
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == passwordEntry; });
    ConnectConfig built = buildConnectConfig(path);
    QVERIFY(built.problem == Problem::PasswordUnreadable);
    QVERIFY(built.toml.isEmpty());

    // The key cannot be read.
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == keyEntry; });
    built = buildConnectConfig(path);
    QVERIFY(built.problem == Problem::AuthKeyUnreadable);
    QVERIFY(built.toml.isEmpty());
    CredentialStore::setReadFailureForTest({});

    // Nothing is wrong: a config without a key.
    built = buildConnectConfig(path);
    QVERIFY(built.problem == Problem::None);
    QVERIFY(built.toml.contains(QStringLiteral("store-pw")));
    QVERIFY(!built.toml.contains(QStringLiteral("client_random_auth_key")));

    // No password at all, and the key cannot be read either: the missing password
    // is what is said, as the key is only of use with one.
    const QString bare = configFile(QStringLiteral("connect-bare"));
    const QString bareKeyEntry = CredentialStore::keyForConfigAuthKey(bare);
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == bareKeyEntry; });
    built = buildConnectConfig(bare);
    QVERIFY(built.problem == Problem::NoPassword);
    QVERIFY(built.toml.isEmpty());
    CredentialStore::setReadFailureForTest({});
    built = buildConnectConfig(bare);
    QVERIFY(built.problem == Problem::NoPassword);
    QVERIFY(built.toml.isEmpty());

    // And a key in the store reaches the TOML.
    QVERIFY(CredentialStore::storePassword(keyEntry, QStringLiteral("aabbccdd")));
    built = buildConnectConfig(path);
    QVERIFY(built.problem == Problem::None);
    QCOMPARE(parseConfigToml(built.toml).clientRandomAuthKey, QStringLiteral("aabbccdd"));
}

// Reading the password's entry is what migrating both secrets needs first, to put
// it back should the key be refused. An entry that cannot be read is not an empty
// one: carrying on would overwrite it, and the refusal that follows would delete it.
void TestCredentialStore::migrationTouchesNothingWhenTheOldPasswordCannotBeRead()
{
    const QString path = configFile(QStringLiteral("migrate-locked"), QStringLiteral("new-pw"), QStringLiteral("aabb"));
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    QVERIFY(CredentialStore::storePassword(passwordEntry, QStringLiteral("old-pw")));
    const QString before = readText(path);

    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == passwordEntry; });
    SecretRefused refused = SecretRefused::None;
    QVERIFY(!migrateConfigPassword(path, &refused));
    QVERIFY(refused == SecretRefused::Password);
    CredentialStore::setReadFailureForTest({});

    QCOMPARE(readText(path), before); // both secrets still in the file
    QCOMPARE(CredentialStore::lookup(passwordEntry).value, QStringLiteral("old-pw"));
    QVERIFY(CredentialStore::lookup(keyEntry).notFound());

    // And once it can be read, the same migration goes through.
    QVERIFY(migrateConfigPassword(path, &refused));
    QCOMPARE(CredentialStore::lookup(passwordEntry).value, QStringLiteral("new-pw"));
    QCOMPARE(CredentialStore::lookup(keyEntry).value, QStringLiteral("aabb"));
}

// With one secret in the file there is nothing to put back, so nothing to read.
void TestCredentialStore::migrationOfOneSecretNeedsNoRead()
{
    const QString path = configFile(QStringLiteral("migrate-one"), QStringLiteral("only-pw"));
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    CredentialStore::setReadFailureForTest([&](const QString &key) { return key == passwordEntry; });
    QVERIFY(migrateConfigPassword(path));
    CredentialStore::setReadFailureForTest({});
    QVERIFY(parsed(path).password.isEmpty());
    QCOMPARE(CredentialStore::lookup(passwordEntry).value, QStringLiteral("only-pw"));
}

void TestCredentialStore::migrateStripsPassword()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    // A plain file, not a QTemporaryFile: migrateConfigPassword() now commits by
    // renaming over the target, and on Windows that cannot replace a path a live
    // QTemporaryFile still owns. Production configs are plain files anyway.
    const QString path = QDir(base).filePath(
            QStringLiteral("migrate-%1.toml").arg(QCoreApplication::applicationPid()));
    QFile::remove(path);
    QFile tf(path);
    QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ConfigToml c;
    c.hostname = "h.example";
    c.addresses = "1.2.3.4:443";
    c.username = "u";
    c.password = "pw";
    tf.write(buildConfigToml(c).toUtf8());
    tf.close();

    QVERIFY(migrateConfigPassword(path));

    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    ConfigToml out = parseConfigToml(QString::fromUtf8(rf.readAll()));
    QVERIFY(out.password.isEmpty());
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigPath(path)),
             QStringLiteral("pw"));

    // Leave nothing behind: the credential entry is real (in the test-only
    // service) and the file is ours.
    CredentialStore::deletePassword(CredentialStore::keyForConfigPath(path));
    rf.close();
    QFile::remove(path);
}

// The second secret of a config, the client random auth key (TrustTunnel 1.3),
// leaves the file as the password does, into an entry of its own, and goes back
// into the text the core is given. The file is rewritten once, with neither.
void TestCredentialStore::migrateMovesTheAuthKeyToo()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString path = QDir(base).filePath(
            QStringLiteral("migrate-key-%1.toml").arg(QCoreApplication::applicationPid()));
    QFile::remove(path);
    ConfigToml c;
    c.hostname = "h.example";
    c.addresses = "1.2.3.4:443";
    c.username = "u";
    c.password = "pw";
    c.clientRandomAuthKey = "aabbccdd";
    QFile tf(path);
    QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    tf.write(buildConfigToml(c).toUtf8());
    tf.close();

    // The entries are the config's own, and not each other's.
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    QVERIFY(passwordEntry != keyEntry);
    QVERIFY(keyEntry.endsWith(passwordEntry));
    QVERIFY(!QFileInfo(keyEntry).isAbsolute()); // never another config's own entry

    QVERIFY(migrateConfigPassword(path));

    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    const QString onDisk = QString::fromUtf8(rf.readAll());
    rf.close();
    QVERIFY2(!onDisk.contains(QStringLiteral("aabbccdd")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("auth_key")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("pw")), qPrintable(onDisk));
    QCOMPARE(CredentialStore::loadPassword(passwordEntry), QStringLiteral("pw"));
    QCOMPARE(CredentialStore::loadPassword(keyEntry), QStringLiteral("aabbccdd"));

    // And back, for the core, in memory.
    const ConfigToml connect = parseConfigToml(buildConnectConfig(path).toml);
    QCOMPARE(connect.password, QStringLiteral("pw"));
    QCOMPARE(connect.clientRandomAuthKey, QStringLiteral("aabbccdd"));

    // A key alone migrates too, when the password is already stored.
    ConfigToml keyOnly = parseConfigToml(onDisk);
    keyOnly.clientRandomAuthKey = QStringLiteral("11223344");
    QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    tf.write(buildConfigToml(keyOnly).toUtf8());
    tf.close();
    QVERIFY(migrateConfigPassword(path));
    QCOMPARE(CredentialStore::loadPassword(passwordEntry), QStringLiteral("pw"));
    QCOMPARE(CredentialStore::loadPassword(keyEntry), QStringLiteral("11223344"));

    CredentialStore::deletePassword(passwordEntry);
    CredentialStore::deletePassword(keyEntry);
    QFile::remove(path);
}

// The OS credential store BLOCKS its caller: on macOS the request goes to
// securityd, which does not answer while the system is asking the user to
// authorize this binary. Called straight from the GUI thread that meant a frozen
// window with a spinning cursor — reported from a real session, on the edit
// form, on save, and on copying a config link.
//
// Rather than time anything (the store answers in microseconds when it is not
// asking a human, so a duration assert would prove nothing), assert the property
// that makes the freeze impossible: while the call is in flight, the calling
// thread's event loop keeps delivering. A queued call posted first is therefore
// dispatched BEFORE loadPassword() returns. A direct, blocking implementation
// cannot do that — it never reaches the event loop.
void TestCredentialStore::keepsTheEventLoopRunningWhileItWaits()
{
    const QString key = QStringLiteral("/tmp/test-responsive.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));

    bool painted = false;
    QMetaObject::invokeMethod(qApp, [&painted]() { painted = true; }, Qt::QueuedConnection);
    QVERIFY2(!painted, "the queued call must still be pending when the store is entered");

    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QVERIFY2(painted, "the UI thread was blocked for the whole credential call");

    QVERIFY(CredentialStore::deletePassword(key));
}

// ...and the same call off the main thread must still work, since that is the
// path the connect sequence takes. There is no UI to keep alive there, so it
// runs directly — the wrapper must not deadlock waiting on an event loop that
// nobody is running.
void TestCredentialStore::worksOffTheMainThread()
{
    const QString key = QStringLiteral("/tmp/test-worker.toml");
    QString loaded;
    bool stored = false;
    bool deleted = false;
    QThread *worker = QThread::create([&]() {
        stored = CredentialStore::storePassword(key, QStringLiteral("from-worker"));
        loaded = CredentialStore::loadPassword(key);
        deleted = CredentialStore::deletePassword(key);
    });
    worker->start();
    QVERIFY2(worker->wait(10000), "a credential call off the main thread hung");
    delete worker;

    QVERIFY(stored);
    QCOMPARE(loaded, QStringLiteral("from-worker"));
    QVERIFY(deleted);
}

// Keeping the UI painted means running an event loop, and an event loop can
// dispatch a queued call that asks the credential store for something else. That
// must not open a second nested loop on top of the first: nesting has to stay
// bounded no matter what the dispatched code does.
//
// Observable as: a call posted from inside the re-entrant call is NOT dispatched
// before it returns. If the inner call had spun its own loop, it would be.
void TestCredentialStore::reentryDoesNotStackEventLoops()
{
    const QString key = QStringLiteral("/tmp/test-reentry.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));

    bool innerDispatched = false;
    bool sawInnerDispatchedTooEarly = false;
    bool reentrantAvailable = false;
    QString reentrantResult;

    QMetaObject::invokeMethod(
            qApp,
            [&]() {
                QMetaObject::invokeMethod(
                        qApp, [&innerDispatched]() { innerDispatched = true; },
                        Qt::QueuedConnection);
                // Deliberately a DIFFERENT operation from the outer one: it
                // returns bool where the outer returns QString, so the two go
                // through separate template instantiations. A guard kept as a
                // static local would be per-instantiation and would not hold
                // here — which is the whole failure mode being pinned.
                reentrantAvailable = CredentialStore::secureStorageAvailable();
                reentrantResult = CredentialStore::loadPassword(key);
                sawInnerDispatchedTooEarly = innerDispatched;
            },
            Qt::QueuedConnection);

    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QCOMPARE(reentrantResult, QStringLiteral("s3cret")); // the re-entrant call still works
    QVERIFY(reentrantAvailable);
    QVERIFY2(!sawInnerDispatchedTooEarly,
             "the re-entrant call spun its own event loop — nesting is unbounded");

    QVERIFY(CredentialStore::deletePassword(key));
}

QTEST_MAIN(TestCredentialStore)
#include "test_credentialstore.moc"
