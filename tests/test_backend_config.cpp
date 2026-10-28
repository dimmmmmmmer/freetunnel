// cppcheck-suppress-file missingIncludeSystem
// The config surface the main window is built on: create, edit, rename, export,
// share as a link, reorder, remove.
//
// This is where the user's VPN password lives, and until now the only coverage
// was the deep-link import path. Everything asserted here is something a bug on
// this branch actually did, or could do unnoticed: write the password into the
// .toml instead of the credential store, leave it world-readable, drop the
// credential when a config is renamed, or destroy an existing config because a
// save failed halfway.
#include <QtTest>

#include <QScopeGuard>

#include <QThread>

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>

#include "app/Backend.h"
#include "app/BackendConfigShared.h"
#include "certificate_vectors.h"
#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"

#include <functional>

class TestBackendConfig : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void createStoresThePasswordOutsideTheConfigFile();
    void createdConfigIsOwnerOnly();
    void editInPlaceKeepsThePathAndUpdatesTheFields();
    void renamingCarriesThePasswordToTheNewPath();
    void aFailedSaveLeavesTheExistingConfigIntact();
    void exportCarriesThePasswordAndStaysOwnerOnly();
    void deepLinkRoundTripsBackIntoTheSameConfig();
    void moveConfigReordersTheList();
    void removeConfigForgetsThePassword();
    void deletingTheActiveConfigRemembersTheOneThatTookOver();
    void renamingOnlyTheLetterCaseRenames();
    void readAccessorsRejectOutOfRangeIndexes();
    void theEditorTakesAClientRandomWithAMask();
    void theEditorChecksEveryAddress();
    void pingsAreResetForEveryConfig();
    void theConnectConfigIsBuiltOffTheGuiThread();
    void anEditSavesTheConfigItOpenedEvenIfTheListMoved();
    void anEditOfADeletedConfigIsRefused();
    void anEditKeepsWhatTheFormDoesNotShow();
    void anEditKeepsPostQuantumOff();
    void aProxyListenerIsNotKeptBesideTheTunnel();
    void renamingAConfigKeptElsewhereLeavesTheOriginal();
    void aStoredSubscriptionAddressIsTakenOutAtStart();
    void aStoredKeyIsTakenOutAtStart();
    void theEditorSavesOnlyTheCertificates();
    void theEditorRefusesACertificateWithNoCertificate();
    void aShareLinkNeverCarriesAKey();
    void aTomlExportNeverCarriesAKey();
    void aChainInOneBlockIsSharedWhole();
    void aFailedCommitPutsThePreviousPasswordBack();
    void aFailedEditSaveKeepsTheStoredPassword();
    void aRefusedCertificateFileIsExplained();
    void theEditorRefusesANameTooLongForAFile();
    void aConfigWithALongerNameStillSaves();
    void aRefusedPasswordIsExplainedForThisPlatform();
    void aPasswordTheStoreRefusesIsExplainedInItsTerms();
    void aRefusedPasswordSaysWhatToDoAboutTheKeyring();
    void aSecretTheStoreCannotReadIsExplainedInItsTerms();
    void theEditorIsNotOpenedOnASecretTheStoreCannotRead();
    void anEditSavedWhileASecretCannotBeReadChangesNothing();
    void aReadBackThatCannotBeAnsweredIsNotAConfirmation();

    // The client random auth key (TrustTunnel 1.3), a second secret beside the password.
    void theEditorStoresTheAuthKeyBesideThePassword();
    void theCoreIsNotHandedAClientRandomItWouldIgnore();
    void aNewConfigDoesNotTakeAKeyLeftAtItsPath();
    void theEditorRefusesAnAuthKeyThatIsNotHex();
    void clearingTheAuthKeyTakesItOutOfTheStore();
    void renamingCarriesTheAuthKeyToTheNewPath();
    void removeConfigForgetsTheAuthKey();
    void aShareLinkCarriesTheAuthKeyAsVersion2();
    void aTomlExportCarriesTheAuthKey();
    void aFailedCommitPutsThePreviousAuthKeyBack();
    void anAuthKeyTheStoreRefusesLeavesTheConfigAndThePasswordAlone();

private:
    // A complete, valid create form; individual cases override what they exercise.
    static QVariantMap form(const QString &name, const QString &password);
    // The form as the editor sends it back for row `index`: every field it was
    // opened with, plus which config it is saving.
    static QVariantMap editorForm(Backend &backend, int index);
    static QString readAll(const QString &path);
    QString pathFor(const Backend &backend, int index) const;
    static void assertOwnerOnly(const QString &path);

    QTemporaryDir m_home;
};

void TestBackendConfig::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    // Test mode + *Test names + a credential service unique to this process:
    // nothing here can reach the user's real configs.json or their keychain.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BackendConfigTest"));
    // See the same note in test_backend_split: the Backend writes through a
    // default-constructed QSettings, which on Unix is NativeFormat (*.conf), so
    // clearing the IniFormat store (*.ini) in init() never touched it. Redirect the
    // default format into this run's temp dir so the store is genuinely per-run.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_home.path());
}

void TestBackendConfig::init()
{
    QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("FreeTunnelTest"),
              QStringLiteral("BackendConfigTest"))
            .clear();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.toml")}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());
    QFile::remove(QDir(dir).filePath(QStringLiteral("configs.json")));
}

void TestBackendConfig::cleanup()
{
    // Leave no credential entries behind, not even in the per-process service.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.toml")}, QDir::Files)) {
        freetunnel::CredentialStore::deletePassword(
                freetunnel::CredentialStore::keyForConfigPath(fi.absoluteFilePath()));
        freetunnel::CredentialStore::deletePassword(
                freetunnel::CredentialStore::keyForConfigAuthKey(fi.absoluteFilePath()));
    }
}

QVariantMap TestBackendConfig::form(const QString &name, const QString &password)
{
    QVariantMap f;
    f[QStringLiteral("name")] = name;
    f[QStringLiteral("hostname")] = QStringLiteral("vpn.example.org");
    f[QStringLiteral("addresses")] = QStringLiteral("198.51.100.7:443");
    f[QStringLiteral("username")] = QStringLiteral("alice");
    f[QStringLiteral("password")] = password;
    f[QStringLiteral("protocol")] = QStringLiteral("http2");
    f[QStringLiteral("dns")] = QStringLiteral("1.1.1.1");
    return f;
}

QVariantMap TestBackendConfig::editorForm(Backend &backend, int index)
{
    QVariantMap f = backend.configFields(index);
    f[QStringLiteral("editIndex")] = index;
    f[QStringLiteral("editPath")] = f.value(QStringLiteral("path"));
    return f;
}

QString TestBackendConfig::readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// Owner-only is a POSIX-mode claim. On Windows the code relies on the config
// directory's ACL instead of mode bits, and Qt does not consult NTFS ACLs unless
// permission checking is explicitly enabled — it reports every file as readable
// by everyone — so the check would test Qt's default, not our code.
void TestBackendConfig::assertOwnerOnly(const QString &path)
{
#if defined(Q_OS_UNIX)
    const QFile::Permissions perms = QFile::permissions(path);
    QVERIFY2(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)),
             qPrintable(QStringLiteral("%1 is readable by other local users").arg(path)));
#else
    QVERIFY(QFileInfo::exists(path));
#endif
}

QString TestBackendConfig::pathFor(const Backend &backend, int index) const
{
    // The list exposes display names; the file is the one that carries the name.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString name = backend.configs().value(index);
    for (const QFileInfo &fi : QDir(dir).entryInfoList({QStringLiteral("*.toml")}, QDir::Files)) {
        if (fi.completeBaseName() == name)
            return fi.absoluteFilePath();
    }
    return QString();
}

// The whole point of the credential store: the config file that ends up on disk
// must not contain the password, and the password must be retrievable.
void TestBackendConfig::createStoresThePasswordOutsideTheConfigFile()
{
    Backend backend;
    QSignalSpy configs(&backend, &Backend::configsChanged);
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    QVERIFY(configs.count() > 0);
    QCOMPARE(backend.configs(), QStringList{QStringLiteral("Alpha")});

    const QString path = pathFor(backend, 0);
    QVERIFY2(!path.isEmpty(), "the created config is not on disk");
    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString onDisk = QString::fromUtf8(f.readAll());
    QVERIFY2(!onDisk.contains(QStringLiteral("hunter2")),
             "the password was written into the config file in cleartext");

    // ...and it round-trips back into the edit form.
    QCOMPARE(backend.configFields(0).value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
    QCOMPARE(backend.configFields(0).value(QStringLiteral("username")).toString(),
             QStringLiteral("alice"));
}

// A config carries the server, the username and (on the import path, briefly)
// the password. Another local user has no business reading it.
void TestBackendConfig::createdConfigIsOwnerOnly()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    assertOwnerOnly(pathFor(backend, 0));
}

void TestBackendConfig::editInPlaceKeepsThePathAndUpdatesTheFields()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    const QString before = pathFor(backend, 0);

    QVariantMap edit = form(QStringLiteral("Alpha"), QStringLiteral("newpass"));
    edit[QStringLiteral("username")] = QStringLiteral("bob");
    edit[QStringLiteral("editIndex")] = 0;
    QVERIFY(backend.createConfig(edit));

    QCOMPARE(backend.configs().size(), 1); // an edit must not fork a second entry
    QCOMPARE(pathFor(backend, 0), before); // ...and saves over the original file
    const QVariantMap fields = backend.configFields(0);
    QCOMPARE(fields.value(QStringLiteral("username")).toString(), QStringLiteral("bob"));
    QCOMPARE(fields.value(QStringLiteral("password")).toString(), QStringLiteral("newpass"));
}

// Renaming moves the file, and the credential is keyed BY PATH — so the store
// has to be updated too, or the config silently loses its password and fails to
// connect with nothing on screen to explain why.
void TestBackendConfig::renamingCarriesThePasswordToTheNewPath()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    const QString oldPath = pathFor(backend, 0);

    QVariantMap edit = form(QStringLiteral("Renamed"), QStringLiteral("hunter2"));
    edit[QStringLiteral("editIndex")] = 0;
    QVERIFY(backend.createConfig(edit));

    QCOMPARE(backend.configs(), QStringList{QStringLiteral("Renamed")});
    const QString newPath = pathFor(backend, 0);
    QVERIFY(newPath != oldPath);
    QVERIFY2(!QFileInfo::exists(oldPath), "the old config file was left behind");
    QCOMPARE(backend.configFields(0).value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
    // The stale entry must not linger in the credential store either.
    QVERIFY(freetunnel::CredentialStore::loadPassword(
                    freetunnel::CredentialStore::keyForConfigPath(oldPath))
                    .isEmpty());
}

// saveConfigWithPassword stages the body and only replaces the destination once
// the password is in the store. The old order truncated the file first, so a
// locked keyring destroyed a working config outright. Here the failure is a
// write failure (an undeletable directory in place of the target), but the
// invariant under test is the same one: a failed save leaves the config alone.
void TestBackendConfig::aFailedSaveLeavesTheExistingConfigIntact()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString target = QDir(dir).filePath(QStringLiteral("staged.toml"));
    QVERIFY(freetunnel::backend_config::writeConfigFile(target, QByteArrayLiteral("original\n")));

    // A path that cannot be written: QSaveFile stages next to the target, and the
    // commit over a directory cannot succeed.
    const QString blocked = QDir(dir).filePath(QStringLiteral("blocked.toml"));
    QVERIFY(QDir().mkpath(blocked));
    QString err;
    QVERIFY(!freetunnel::backend_config::saveConfigWithPassword(
            blocked, QByteArrayLiteral("body\n"), QStringLiteral("pw"), QString(), QString(), QString(),
            &err));
    QCOMPARE(err, QStringLiteral("write"));

    // The untouched config next to it is still exactly what it was.
    QFile f(target);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArrayLiteral("original\n"));
    freetunnel::CredentialStore::deletePassword(
            freetunnel::CredentialStore::keyForConfigPath(blocked));
    QDir().rmdir(blocked);
    QFile::remove(target);
}

namespace {

// Whether a QSaveFile writing `target` has staged its body. Qt names the staged
// file "<target>.XXXXXX", next to the target, except on Linux, where it is
// opened without a name (O_TMPFILE) and only the process's open files show it,
// as "<directory>/#<inode> (deleted)". A test that times something "between
// staging and commit" asks this, so that a store call added before the staging
// makes it fail instead of letting the save stop at open and pass unexamined.
bool bodyIsStaged(const QString &target)
{
    const QFileInfo fi(target);
    const QString prefix = fi.fileName() + QLatin1Char('.');
    const QStringList siblings = fi.dir().entryList(QDir::Files | QDir::Hidden);
    for (const QString &name : siblings) {
        if (name.startsWith(prefix) && name.size() == prefix.size() + 6)
            return true;
    }
#if defined(Q_OS_LINUX)
    const QString unnamed = QFileInfo(fi.absolutePath()).canonicalFilePath() + QStringLiteral("/#");
    const QFileInfoList fds = QDir(QStringLiteral("/proc/self/fd"))
                                      .entryInfoList(QDir::System | QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fd : fds) {
        if (fd.symLinkTarget().startsWith(unnamed))
            return true;
    }
#endif
    return false;
}

} // namespace

// The other half of the same promise. The password goes into the store before
// the file is committed, so a commit that failed left the store holding the new
// password and the file the old config: a connect then sent the new password
// with the old username, and failed with nothing to say why.
//
// The commit is made to fail from inside the save. On this thread the credential
// store is called through a local event loop (see withoutFreezingTheUi), which
// runs the call queued here after the body is staged and before it is committed;
// it puts a directory where the file was, and no rename can replace that.
void TestBackendConfig::aFailedCommitPutsThePreviousPasswordBack()
{
    using freetunnel::CredentialStore;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString target = QDir(dir).filePath(QStringLiteral("committed.toml"));
    QVERIFY(freetunnel::backend_config::writeConfigFile(target, QByteArrayLiteral("original\n")));
    const QString key = CredentialStore::keyForConfigPath(target);
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("old-pass")));
    auto tidy = qScopeGuard([&] {
        CredentialStore::deletePassword(key);
        QDir().rmdir(target);
        QFile::remove(target);
    });

    bool staged = false;
    bool swapped = false;
    QTimer::singleShot(0, this, [&] {
        staged = bodyIsStaged(target);
        swapped = QFile::remove(target) && QDir().mkdir(target);
    });
    QString err;
    const bool saved = freetunnel::backend_config::saveConfigWithPassword(
            target, QByteArrayLiteral("new\n"), QStringLiteral("new-pass"), QStringLiteral("old-pass"),
            QString(), QString(), &err);
    QVERIFY2(swapped, "the file was not swapped out between staging and commit");
    QVERIFY2(staged, "the file was swapped out before the body was staged");
    QVERIFY(!saved);
    QCOMPARE(err, QStringLiteral("write"));
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("old-pass"));

    // A file that had no password before has none after.
    const QString fresh = QDir(dir).filePath(QStringLiteral("fresh.toml"));
    const QString freshKey = CredentialStore::keyForConfigPath(fresh);
    auto tidyFresh = qScopeGuard([&] {
        CredentialStore::deletePassword(freshKey);
        QDir().rmdir(fresh);
    });
    staged = false;
    swapped = false;
    QTimer::singleShot(0, this, [&] {
        staged = bodyIsStaged(fresh);
        swapped = QDir().mkdir(fresh);
    });
    QVERIFY(!freetunnel::backend_config::saveConfigWithPassword(
            fresh, QByteArrayLiteral("new\n"), QStringLiteral("new-pass"), QString(), QString(), QString(),
            &err));
    QVERIFY(swapped);
    QVERIFY2(staged, "the new file was blocked before the body was staged");
    QVERIFY2(CredentialStore::loadPassword(freshKey).isEmpty(),
             "a password was left behind for a config that was never written");
}

namespace {

// Runs `act` in the credential store's local event loop that comes after the
// first `calls` of them. Every store call on this thread turns a local event loop
// until its worker is done (see withoutFreezingTheUi), and a plain queued call
// runs in the first one. In createConfig() those are the edit snapshot's lookups
// of the old password and the old auth key, before the body is staged: a
// directory put in place there fails the save at open, before the store is
// touched. A worker's end reaches its
// loop as a queued call to QEventLoop::quit, so those are counted and `act` is
// queued behind the last; no event loop turns between two store calls, so it
// runs in the next one.
class AfterCredentialCalls : public QObject {
public:
    AfterCredentialCalls(int calls, std::function<void()> act) : m_left(calls), m_act(std::move(act))
    {
        QCoreApplication::instance()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_left > 0 && event->type() == QEvent::MetaCall && qobject_cast<QEventLoop *>(watched)
                && --m_left == 0)
            QMetaObject::invokeMethod(this, m_act, Qt::QueuedConnection);
        return false;
    }

private:
    int m_left;
    std::function<void()> m_act;
};

} // namespace

// The restore above puts back what it is told the store held, and the editor's
// save is what tells it. A wrong answer is worse than the bug it fixes: an
// in-place edit told "nothing" deleted the stored password when its commit
// failed, leaving the old config without one; a rename told the old password
// left it under a file that was never written.
void TestBackendConfig::aFailedEditSaveKeepsTheStoredPassword()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("old-pass"))));
    const QString path = pathFor(backend, 0);
    QVERIFY(!path.isEmpty());
    const QString key = CredentialStore::keyForConfigPath(path);
    const QString renamed = QFileInfo(path).dir().filePath(QStringLiteral("Beta.toml"));
    const QString renamedKey = CredentialStore::keyForConfigPath(renamed);
    auto tidy = qScopeGuard([&] {
        CredentialStore::deletePassword(key);
        CredentialStore::deletePassword(renamedKey);
        QDir().rmdir(path);
        QDir().rmdir(renamed);
    });
    bool staged = false;
    bool swapped = false;

    // Renamed, so the save makes a new file, and that commit fails.
    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("name")] = QStringLiteral("Beta");
    edit[QStringLiteral("password")] = QStringLiteral("new-pass");
    {
        AfterCredentialCalls block(2, [&] {
            staged = bodyIsStaged(renamed);
            swapped = QDir().mkdir(renamed);
        });
        QVERIFY(!backend.createConfig(edit));
    }
    QVERIFY2(swapped, "the new file was not blocked between staging and commit");
    QVERIFY2(staged, "the new file was blocked before the body was staged");
    QVERIFY2(CredentialStore::loadPassword(renamedKey).isEmpty(),
             "a password was left behind for a config that was never written");
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("old-pass"));
    QVERIFY(QDir().rmdir(renamed));

    // Saved over the same file, and that commit fails.
    edit[QStringLiteral("name")] = QStringLiteral("Alpha");
    staged = false;
    swapped = false;
    {
        AfterCredentialCalls swap(2, [&] {
            staged = bodyIsStaged(path);
            swapped = QFile::remove(path) && QDir().mkdir(path);
        });
        QVERIFY(!backend.createConfig(edit));
    }
    QVERIFY2(swapped, "the file was not swapped out between staging and commit");
    QVERIFY2(staged, "the file was swapped out before the body was staged");
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("old-pass"));
}

// Export is the one path that deliberately writes the password in cleartext, into
// a directory the user picked. It has to land owner-only from the first byte.
void TestBackendConfig::exportCarriesThePasswordAndStaysOwnerOnly()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));

    const QString dest = QDir(m_home.path()).filePath(QStringLiteral("exported.toml"));
    QFile::remove(dest);
    QVERIFY(backend.exportConfigToml(0, dest));

    QFile f(dest);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(f.readAll()));
    f.close();
    QCOMPARE(c.username, QStringLiteral("alice"));
    QCOMPARE(c.password, QStringLiteral("hunter2")); // pulled back out of the store
    assertOwnerOnly(dest);
    QFile::remove(dest);
}

// "Copy link" then "paste link" is a supported way to move a server between
// machines, so the link the app produces has to survive its own parser.
void TestBackendConfig::deepLinkRoundTripsBackIntoTheSameConfig()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));

    const QString link = backend.configDeepLink(0);
    QVERIFY(!link.isEmpty());

    QString err;
    const std::optional<freetunnel::DeepLinkConfig> parsed =
            freetunnel::parseDeepLink(link, &err);
    QVERIFY2(parsed.has_value(), qPrintable(err));
    const freetunnel::DeepLinkConfig &dl = *parsed;
    QCOMPARE(dl.hostname, QStringLiteral("vpn.example.org"));
    QCOMPARE(dl.username, QStringLiteral("alice"));
    QCOMPARE(dl.password, QStringLiteral("hunter2"));
    QCOMPARE(dl.addresses, QStringList{QStringLiteral("198.51.100.7:443")});
    QCOMPARE(dl.name, QStringLiteral("Alpha"));
}

void TestBackendConfig::moveConfigReordersTheList()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("a"))));
    QVERIFY(backend.createConfig(form(QStringLiteral("Beta"), QStringLiteral("b"))));
    const QStringList before = backend.configs();
    QCOMPARE(before.size(), 2);

    backend.moveConfig(0, 1);
    QStringList expected = before;
    expected.move(0, 1);
    QCOMPARE(backend.configs(), expected);

    // Out-of-range and no-op moves must leave the order alone rather than throw
    // the list away.
    backend.moveConfig(0, 0);
    backend.moveConfig(-1, 1);
    backend.moveConfig(0, 99);
    QCOMPARE(backend.configs(), expected);
}

// A removed config's password has no owner left. Leaving it in the OS store
// means a credential the user cannot see and cannot delete from the app.
void TestBackendConfig::removeConfigForgetsThePassword()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    const QString path = pathFor(backend, 0);
    const QString key = freetunnel::CredentialStore::keyForConfigPath(path);
    QCOMPARE(freetunnel::CredentialStore::loadPassword(key), QStringLiteral("hunter2"));

    backend.removeConfig(0);
    QVERIFY(backend.configs().isEmpty());
    QVERIFY2(!QFileInfo::exists(path), "the config file survived removal");
    QVERIFY2(freetunnel::CredentialStore::loadPassword(key).isEmpty(),
             "the password outlived the config it belonged to");
}

// Deleting the active config hands the slot to the first one left, and that is
// the config the window shows as active. It was never saved as such: the setting
// kept naming the deleted file, so the next launch fell back to whatever row was
// first by then — after an import or a drag, a config nobody had picked, and
// "Connect on startup" connected to it.
void TestBackendConfig::deletingTheActiveConfigRemembersTheOneThatTookOver()
{
    QString takeover;
    {
        Backend backend;
        QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("a"))));
        QVERIFY(backend.createConfig(form(QStringLiteral("Beta"), QStringLiteral("b"))));
        QVERIFY(backend.createConfig(form(QStringLiteral("Gamma"), QStringLiteral("c"))));
        backend.selectConfig(1);
        const QString deleted = pathFor(backend, 1);
        backend.removeConfig(1);
        QVERIFY(backend.activeIndex() >= 0);
        takeover = pathFor(backend, backend.activeIndex());
        QVERIFY(takeover != deleted);

        // Another config to the top of the list, where an import or a drag puts it.
        backend.moveConfig(1, 0);
        QVERIFY(pathFor(backend, 0) != takeover);
    }
    Backend relaunched;
    QCOMPARE(pathFor(relaunched, relaunched.activeIndex()), takeover);
}

// "work" to "Work": on APFS and NTFS the new name is the same file, which the save
// took for another config and answered with "Work-2". Removing the old spelling
// afterwards would remove the file itself there, and deleting the old password
// would take the new one from the Windows credential store, which folds case too.
void TestBackendConfig::renamingOnlyTheLetterCaseRenames()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("work"), QStringLiteral("hunter2"))));
    QVariantMap edit = form(QStringLiteral("Work"), QStringLiteral("hunter2"));
    edit[QStringLiteral("editIndex")] = 0;
    edit[QStringLiteral("editPath")] = backend.configFields(0).value(QStringLiteral("path"));
    QVERIFY(backend.createConfig(edit));

    QCOMPARE(backend.configs(), QStringList{QStringLiteral("Work")});
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QCOMPARE(QDir(dir).entryList({QStringLiteral("*.toml")}, QDir::Files),
             QStringList{QStringLiteral("Work.toml")});
    QCOMPARE(backend.configFields(0).value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
}

// QML asks for fields by row index, and rows disappear (removal, reload) between
// the click and the call. Every read accessor has to tolerate that.
void TestBackendConfig::readAccessorsRejectOutOfRangeIndexes()
{
    Backend backend;
    QVERIFY(backend.configFields(-1).isEmpty());
    QVERIFY(backend.configFields(0).isEmpty()); // no configs at all yet
    QVERIFY(backend.configDeepLink(-1).isEmpty());
    QVERIFY(backend.configDeepLink(5).isEmpty());
    QVERIFY(!backend.exportConfigToml(3, QDir(m_home.path()).filePath(QStringLiteral("no.toml"))));

    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("a"))));
    QVERIFY(backend.configFields(1).isEmpty()); // one past the end
    QVERIFY(backend.configDeepLink(1).isEmpty());
    // An empty destination is not a path — exporting there must fail, not write
    // the password to some default location.
    QVERIFY(!backend.exportConfigToml(0, QString()));
}

// pingConfigs() restarts the whole column: one placeholder per config, in the
// list's own order, and any probe still in flight from a previous run must not
// write into it afterwards.
void TestBackendConfig::pingsAreResetForEveryConfig()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("a"))));
    QVERIFY(backend.createConfig(form(QStringLiteral("Beta"), QStringLiteral("b"))));

    QSignalSpy pings(&backend, &Backend::pingsChanged);
    backend.pingConfigs();
    QVERIFY(pings.count() > 0);
    QCOMPARE(backend.pings().size(), backend.configs().size());

    // Restart immediately: the second run supersedes the first, and the column is
    // still exactly as long as the list — the stale probes cannot append to it.
    backend.pingConfigs();
    QCOMPARE(backend.pings().size(), backend.configs().size());
    QTest::qWait(600);
    QCOMPARE(backend.pings().size(), backend.configs().size());
}

// buildConnectTomlAsync() exists for one reason: building the connect config means
// reading the password out of the OS keychain, which blocks — on Linux for as long
// as the user takes to answer a keyring prompt. Doing that on the GUI thread
// freezes the window mid-connect.
//
// It was doing exactly that. A QThread OBJECT lives in the thread that created it,
// so connecting to its own started() signal with the default (auto) type queues the
// body back to the GUI thread: the worker starts, emits, and the work happens on
// the UI thread anyway. Nothing observable changes when this regresses — the app
// still connects, it just stops responding while it does — so the thread it ran on
// is recorded and asserted here.
void TestBackendConfig::theConnectConfigIsBuiltOffTheGuiThread()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));

    // Point the VPN client at a port nothing answers on. Without this connectVpn()
    // would try to spawn the privileged helper for real — an elevation prompt in
    // the middle of a unit test. The config build under test happens first either
    // way; what follows is expected to fail, and that is fine.
    qputenv("FT_TEST_HELPER_PORT", QByteArrayLiteral("1"));
    qputenv("FT_TEST_HELPER_TOKEN", QByteArrayLiteral("unused"));
    auto restore = qScopeGuard([] {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
    });

    backend.connectVpn();

    // Checked the instant connectVpn() returns, while the worker is certainly
    // still alive, and deterministic because it asks about ownership rather than
    // timing: the build worker must not be a child of Backend.
    //
    // It used to be `new QThread(this)`. ~QObject then deletes it as part of
    // Backend's own destruction, and if the thread is still inside the keychain
    // read QThread's destructor reaches qFatal("Destroyed while thread is still
    // running") and the process aborts. That is how this test failed on macOS:
    // its assertions passed, then SIGABRT during teardown. Linux hid it because
    // the credential read there is microseconds and the worker always won the
    // race. The same window is reachable in the app — quitting while macOS has
    // its authorization dialog up, which is the very situation this worker exists
    // for.
    QVERIFY2(backend.findChildren<QThread *>().isEmpty(),
             "the config-build worker must not be parented to Backend: ~QObject would "
             "delete it mid-read and QThread's destructor calls qFatal");

    // lastTomlBuildThread() is now recorded when the build COMPLETES, not when it
    // starts, so waiting for it here also means this test can no longer outrun the
    // worker it started.
    QTRY_VERIFY_WITH_TIMEOUT(backend.lastTomlBuildThread() != nullptr, 10000);
    QVERIFY2(backend.lastTomlBuildThread() != QThread::currentThread(),
             "the connect config was built on the GUI thread — the keychain read blocks it");

    backend.disconnectVpn();
}

// The editor took only plain hex, so a client random with a mask ("prefix/mask",
// as links carry it) could not be entered, nor a config holding one saved again.
void TestBackendConfig::theEditorTakesAClientRandomWithAMask()
{
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    QVariantMap f = form(QStringLiteral("Masked"), QStringLiteral("pw"));
    f[QStringLiteral("clientRandom")] = QStringLiteral("deadbeef/ffff0000");
    QVERIFY(backend.createConfig(f));
    QFile saved(pathFor(backend, 0));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QString toml = QString::fromUtf8(saved.readAll());
    QVERIFY2(toml.contains(QStringLiteral("client_random = \"deadbeef/ffff0000\"\n")), qPrintable(toml));

    // And a link made from it carries the mask on.
    QString err;
    const auto link = freetunnel::parseDeepLink(backend.configDeepLink(0), &err);
    QVERIFY2(link.has_value(), qPrintable(err));
    QCOMPARE(link->clientRandomPrefix, QStringLiteral("deadbeef/ffff0000"));

    // Still hex only, and a slash needs a mask after it. In whole bytes, too, and
    // no more than 32 of them, or the core goes without it.
    for (const QString &bad : {QStringLiteral("deadbeef/"), QStringLiteral("xyz"), QStringLiteral("dead/beef/00"),
                               QStringLiteral("abc"), QStringLiteral("aa/fff"), QString(66, QLatin1Char('a'))}) {
        f[QStringLiteral("name")] = QStringLiteral("Bad");
        f[QStringLiteral("clientRandom")] = bad;
        QVERIFY2(!backend.createConfig(f), qPrintable(bad));
    }
    QCOMPARE(errors.count(), 6);
    QCOMPARE(errors.last().at(0).toString(),
             QStringLiteral("Client random must be hexadecimal in whole bytes (an even number of "
                            "digits, at most 64), optionally followed by /mask"));
}

// Every entry of the list, by the rule a subscription's address is checked by
// (isValidEndpointAddress), and not the first one alone.
void TestBackendConfig::theEditorChecksEveryAddress()
{
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    QVariantMap f = form(QStringLiteral("Two"), QStringLiteral("pw"));
    f[QStringLiteral("addresses")] = QStringLiteral("198.51.100.7:443, [2001:db8::1]:8443");
    QVERIFY(backend.createConfig(f));
    for (const QString &bad : {QStringLiteral("198.51.100.7"), QStringLiteral("198.51.100.7:443, 198.51.100.8"),
                               QStringLiteral(":443"), QStringLiteral("198.51.100.7:443, :443"),
                               QStringLiteral("198.51.100.7:0, 198.51.100.8:443")}) {
        f[QStringLiteral("name")] = QStringLiteral("Bad");
        f[QStringLiteral("addresses")] = bad;
        QVERIFY2(!backend.createConfig(f), qPrintable(bad));
    }
    QCOMPARE(errors.count(), 5);
    QCOMPARE(errors.last().at(0).toString(), QStringLiteral("Address must be host:port, e.g. 1.2.3.4:443"));
    QCOMPARE(backend.configs(), QStringList{QStringLiteral("Two")});
}

// ---- the client random auth key ---------------------------------------------

// Core 1.3 derives the TLS client random from this key, and the server turns away
// a client without it. It is a secret in the way the password is (whoever holds it
// passes the server's gate, which defeats its point), so it is kept where the
// password is and not in the config file, which is copied and attached to support
// threads. It comes back into the editor, and goes to the core with the password.
void TestBackendConfig::theEditorStoresTheAuthKeyBesideThePassword()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral(" aabbccdd ");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    QVERIFY(!path.isEmpty());

    const QString onDisk = readAll(path);
    QVERIFY2(!onDisk.contains(QStringLiteral("aabbccdd")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("client_random_auth_key")), qPrintable(onDisk));
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(path)), QStringLiteral("aabbccdd"));
    // The password's entry is not the key's, and vice versa.
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigPath(path)), QStringLiteral("hunter2"));
    QCOMPARE(backend.configFields(0).value(QStringLiteral("authKey")).toString(), QStringLiteral("aabbccdd"));

    const QString toml = freetunnel::buildConnectConfig(path).toml;
    QVERIFY2(toml.contains(QStringLiteral("client_random_auth_key = \"aabbccdd\"\n")), qPrintable(toml));

    // An edit that leaves it alone keeps it, with the form's other changes.
    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("username")] = QStringLiteral("bob");
    QVERIFY(backend.createConfig(edit));
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(path)), QStringLiteral("aabbccdd"));
    QVERIFY2(!readAll(path).contains(QStringLiteral("aabbccdd")), qPrintable(readAll(path)));

    // A config made without one has no entry for it.
    QVERIFY(backend.createConfig(form(QStringLiteral("Plain"), QStringLiteral("p"))));
    const QString plain = QDir(QFileInfo(path).absolutePath()).filePath(QStringLiteral("Plain.toml"));
    QVERIFY(QFileInfo::exists(plain));
    QVERIFY(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(plain)).isEmpty());
}

// With a key the core derives the client random from it and ignores client_random,
// and logs a warning about the ignored one on every TLS handshake and ping. So the
// core is handed the key alone; the config keeps the user's client random for when
// the key goes, and the connect TOML gets it back then.
void TestBackendConfig::theCoreIsNotHandedAClientRandomItWouldIgnore()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("clientRandom")] = QStringLiteral("deadbeef/ffff0000");
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);

    const QString connect = freetunnel::buildConnectConfig(path).toml;
    QVERIFY2(connect.contains(QStringLiteral("client_random_auth_key = \"aabbccdd\"\n")), qPrintable(connect));
    QVERIFY2(connect.contains(QStringLiteral("client_random = \"\"\n")), qPrintable(connect));
    QVERIFY2(!connect.contains(QStringLiteral("deadbeef")), qPrintable(connect));
    // The config itself, and what an export or a share link is made from, keep it.
    QVERIFY2(readAll(path).contains(QStringLiteral("client_random = \"deadbeef/ffff0000\"\n")),
             qPrintable(readAll(path)));
    QCOMPARE(backend.configFields(0).value(QStringLiteral("clientRandom")).toString(),
             QStringLiteral("deadbeef/ffff0000"));

    // Without a key it is the config's again.
    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("authKey")] = QString();
    QVERIFY(backend.createConfig(edit));
    QVERIFY(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(path)).isEmpty());
    const QString without = freetunnel::buildConnectConfig(path).toml;
    QVERIFY2(without.contains(QStringLiteral("client_random = \"deadbeef/ffff0000\"\n")), qPrintable(without));
    QVERIFY(!without.contains(QStringLiteral("client_random_auth_key")));
}

// Credentials are kept by path and a name is used again. A key entry left at the
// path by a config of that name (a removal the keyring refused, or one made under
// 1.2.x, which knows nothing of these entries) is not the key of a new config made
// without one: it would be sent to whatever server that config names.
void TestBackendConfig::aNewConfigDoesNotTakeAKeyLeftAtItsPath()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Stale"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    const QString entry = CredentialStore::keyForConfigAuthKey(path);
    QCOMPARE(CredentialStore::loadPassword(entry), QStringLiteral("aabbccdd"));

    backend.removeConfig(0);
    QVERIFY(!QFileInfo::exists(path));
    // As a keyring that refused the delete leaves it.
    QVERIFY(CredentialStore::storePassword(entry, QStringLiteral("aabbccdd")));

    QVERIFY(backend.createConfig(form(QStringLiteral("Stale"), QStringLiteral("other-pass"))));
    QCOMPARE(pathFor(backend, 0), path);
    QVERIFY2(CredentialStore::loadPassword(entry).isEmpty(), "the new config took the old one's key");
    const QString connect = freetunnel::buildConnectConfig(path).toml;
    QVERIFY(connect.contains(QStringLiteral("other-pass")));
    QVERIFY2(!connect.contains(QStringLiteral("client_random_auth_key")), qPrintable(connect));
    QVERIFY(backend.configFields(0).value(QStringLiteral("authKey")).toString().isEmpty());

    // One that brings a key keeps it: the entry is not dropped from under it.
    backend.removeConfig(0);
    QVariantMap keyed = form(QStringLiteral("Stale"), QStringLiteral("third-pass"));
    keyed[QStringLiteral("authKey")] = QStringLiteral("11223344");
    QVERIFY(backend.createConfig(keyed));
    QCOMPARE(CredentialStore::loadPassword(entry), QStringLiteral("11223344"));
}

// The core quietly connects without a key it cannot decode, and a server that
// checks one then turns the client away with nothing to say why: refused here.
void TestBackendConfig::theEditorRefusesAnAuthKeyThatIsNotHex()
{
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    for (const QString &bad : {QStringLiteral("abc"), QStringLiteral("zz"), QStringLiteral("aa bb")}) {
        QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
        f[QStringLiteral("authKey")] = bad;
        QVERIFY2(!backend.createConfig(f), qPrintable(bad));
        QVERIFY2(errors.last().at(0).toString().contains(QStringLiteral("auth key")),
                 qPrintable(errors.last().at(0).toString()));
    }
    QVERIFY(backend.configs().isEmpty());
}

// The key is not the config's for ever: a server that stops checking one, or a
// config pointed at another, must not go on sending the old.
void TestBackendConfig::clearingTheAuthKeyTakesItOutOfTheStore()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    const QString entry = CredentialStore::keyForConfigAuthKey(path);
    QCOMPARE(CredentialStore::loadPassword(entry), QStringLiteral("aabbccdd"));

    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("authKey")] = QString();
    QVERIFY(backend.createConfig(edit));
    QVERIFY2(CredentialStore::loadPassword(entry).isEmpty(), "the removed key stayed in the store");
    QVERIFY(!freetunnel::buildConnectConfig(path).toml.contains(QStringLiteral("client_random_auth_key")));
    QVERIFY(backend.configFields(0).value(QStringLiteral("authKey")).toString().isEmpty());
}

void TestBackendConfig::renamingCarriesTheAuthKeyToTheNewPath()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString oldPath = pathFor(backend, 0);

    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("name")] = QStringLiteral("Renamed");
    QVERIFY(backend.createConfig(edit));
    const QString newPath = pathFor(backend, 0);
    QVERIFY(newPath != oldPath);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(newPath)),
             QStringLiteral("aabbccdd"));
    QVERIFY2(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(oldPath)).isEmpty(),
             "the old path's key entry was left behind");
    QCOMPARE(backend.configFields(0).value(QStringLiteral("authKey")).toString(), QStringLiteral("aabbccdd"));
}

void TestBackendConfig::removeConfigForgetsTheAuthKey()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    const QString entry = CredentialStore::keyForConfigAuthKey(path);
    QCOMPARE(CredentialStore::loadPassword(entry), QStringLiteral("aabbccdd"));

    backend.removeConfig(0);
    QVERIFY2(CredentialStore::loadPassword(entry).isEmpty(), "the key outlived the config it belonged to");
}

// A link without the key cannot connect to a server that checks one, so the link
// of such a config carries it, as version 2, the version that has a tag for it. A
// config without a key stays version 1, which older clients still read.
void TestBackendConfig::aShareLinkCarriesTheAuthKeyAsVersion2()
{
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    QString err;
    const auto keyed = freetunnel::parseDeepLink(backend.configDeepLink(0), &err);
    QVERIFY2(keyed.has_value(), qPrintable(err));
    QCOMPARE(keyed->version, 2);
    QCOMPARE(keyed->clientRandomAuthKey, QStringLiteral("aabbccdd"));
    QCOMPARE(keyed->password, QStringLiteral("hunter2"));

    // And it imports as the same config, key included.
    const auto prepared = freetunnel::prepareDeepLinkImport(backend.configDeepLink(0), &err);
    QVERIFY2(prepared.has_value(), qPrintable(err));
    QCOMPARE(freetunnel::parseConfigToml(prepared->tomlContent).clientRandomAuthKey, QStringLiteral("aabbccdd"));

    QVERIFY(backend.createConfig(form(QStringLiteral("Plain"), QStringLiteral("p"))));
    const int plainIndex = backend.configs().indexOf(QStringLiteral("Plain"));
    QVERIFY(plainIndex >= 0);
    const auto plain = freetunnel::parseDeepLink(backend.configDeepLink(plainIndex), &err);
    QVERIFY2(plain.has_value(), qPrintable(err));
    QCOMPARE(plain->version, 1);
    QVERIFY(plain->clientRandomAuthKey.isEmpty());
}

// An export is made to be handed on whole, as the password is: with the key.
void TestBackendConfig::aTomlExportCarriesTheAuthKey()
{
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString dest = QDir(m_home.path()).filePath(QStringLiteral("exported-key.toml"));
    QFile::remove(dest);
    QVERIFY(backend.exportConfigToml(0, dest));
    const QString exported = readAll(dest);
    assertOwnerOnly(dest);
    QFile::remove(dest);
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(exported);
    QCOMPARE(c.clientRandomAuthKey, QStringLiteral("aabbccdd"));
    QCOMPARE(c.password, QStringLiteral("hunter2"));
}

// As for the password: a commit that fails puts the store back to what the config
// on disk goes with, key included.
void TestBackendConfig::aFailedCommitPutsThePreviousAuthKeyBack()
{
    using freetunnel::CredentialStore;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString target = QDir(dir).filePath(QStringLiteral("keyed-commit.toml"));
    QVERIFY(freetunnel::backend_config::writeConfigFile(target, QByteArrayLiteral("original\n")));
    const QString passwordEntry = CredentialStore::keyForConfigPath(target);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(target);
    QVERIFY(CredentialStore::storePassword(passwordEntry, QStringLiteral("old-pass")));
    QVERIFY(CredentialStore::storePassword(keyEntry, QStringLiteral("0102")));
    auto tidy = qScopeGuard([&] {
        CredentialStore::deletePassword(passwordEntry);
        CredentialStore::deletePassword(keyEntry);
        QDir().rmdir(target);
        QFile::remove(target);
    });

    bool staged = false;
    bool swapped = false;
    QTimer::singleShot(0, this, [&] {
        staged = bodyIsStaged(target);
        swapped = QFile::remove(target) && QDir().mkdir(target);
    });
    QString err;
    const bool saved = freetunnel::backend_config::saveConfigWithPassword(
            target, QByteArrayLiteral("new\n"), QStringLiteral("new-pass"), QStringLiteral("old-pass"),
            QStringLiteral("aabb"), QStringLiteral("0102"), &err);
    QVERIFY2(swapped, "the file was not swapped out between staging and commit");
    QVERIFY2(staged, "the file was swapped out before the body was staged");
    QVERIFY(!saved);
    QCOMPARE(err, QStringLiteral("write"));
    QCOMPARE(CredentialStore::loadPassword(passwordEntry), QStringLiteral("old-pass"));
    QCOMPARE(CredentialStore::loadPassword(keyEntry), QStringLiteral("0102"));

    // A key removed by the save comes back too.
    QVERIFY(QDir().rmdir(target));
    QVERIFY(freetunnel::backend_config::writeConfigFile(target, QByteArrayLiteral("original\n")));
    staged = false;
    swapped = false;
    QTimer::singleShot(0, this, [&] {
        staged = bodyIsStaged(target);
        swapped = QFile::remove(target) && QDir().mkdir(target);
    });
    QVERIFY(!freetunnel::backend_config::saveConfigWithPassword(
            target, QByteArrayLiteral("new\n"), QStringLiteral("old-pass"), QStringLiteral("old-pass"), QString(),
            QStringLiteral("0102"), &err));
    QVERIFY(swapped);
    QCOMPARE(CredentialStore::loadPassword(keyEntry), QStringLiteral("0102"));
}

// The store takes the password and refuses the key: neither is kept, and the
// config on disk is the one it was. (A store that refuses is a locked keyring or a
// denied Keychain prompt; a test environment's is scripted to.)
void TestBackendConfig::anAuthKeyTheStoreRefusesLeavesTheConfigAndThePasswordAlone()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("old-pass"));
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    const QString before = readAll(path);

    // The key's own item is the one the store refuses, as a Keychain prompt for a
    // new item that is denied does; the password's item is there and works, so
    // the save can put it back.
    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("password")] = QStringLiteral("new-pass");
    edit[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    edit[QStringLiteral("username")] = QStringLiteral("bob");
    int stores = 0;
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    CredentialStore::setCallHookForTest([&](const char *call, const QString &key) {
        if (QLatin1String(call) != QLatin1String("store"))
            return;
        if (key == keyEntry) {
            ++stores;
            qputenv("FT_TEST_KEYRING", "locked");
        } else if (stores > 0) {
            qunsetenv("FT_TEST_KEYRING");
        }
    });
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    const bool saved = backend.createConfig(edit);
    CredentialStore::setCallHookForTest(nullptr);
    qunsetenv("FT_TEST_KEYRING");
    QVERIFY2(stores >= 1, "the key was never offered to the store");
    QVERIFY(!saved);
    QCOMPARE(readAll(path), before);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigPath(path)), QStringLiteral("old-pass"));
    QVERIFY(CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(path)).isEmpty());
    QVERIFY2(!errors.isEmpty(), "the user was told nothing");
}

QTEST_MAIN(TestBackendConfig)
// The editor opens on a row and saves minutes later, and the list does not hold
// still under it: an imported config is prepended, which shifts every position
// by one. Saving against the number the editor opened with then wrote the form
// over a DIFFERENT config — silently, and over one the user never opened.
//
// Reproduced here by moving the row the same way the import does, which is what
// moveConfig() already exists to do.
void TestBackendConfig::anEditSavesTheConfigItOpenedEvenIfTheListMoved()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("alpha-pass"))));
    QVERIFY(backend.createConfig(form(QStringLiteral("Beta"), QStringLiteral("beta-pass"))));

    // The editor opens on Beta, and takes its identity with the fields.
    const int openedAt = backend.configs().indexOf(QStringLiteral("Beta"));
    QVERIFY(openedAt >= 0);
    const QVariantMap opened = backend.configFields(openedAt);
    const QString editPath = opened.value(QStringLiteral("path")).toString();
    QVERIFY2(!editPath.isEmpty(), "the fields have to say which file they came from");

    // Now the list moves under it.
    backend.moveConfig(0, 1);
    QVERIFY(backend.configs().indexOf(QStringLiteral("Beta")) != openedAt);

    QVariantMap edit = form(QStringLiteral("Beta"), QStringLiteral("beta-pass"));
    edit[QStringLiteral("username")] = QStringLiteral("edited");
    edit[QStringLiteral("editIndex")] = openedAt; // the stale one, as the UI would send
    edit[QStringLiteral("editPath")] = editPath;
    QVERIFY(backend.createConfig(edit));

    QCOMPARE(backend.configs().size(), 2); // no third entry forked off
    const int beta = backend.configs().indexOf(QStringLiteral("Beta"));
    const int alpha = backend.configs().indexOf(QStringLiteral("Alpha"));
    QVERIFY(beta >= 0 && alpha >= 0);
    QCOMPARE(backend.configFields(beta).value(QStringLiteral("username")).toString(),
             QStringLiteral("edited"));
    QVERIFY2(backend.configFields(alpha).value(QStringLiteral("username")).toString()
                     != QStringLiteral("edited"),
             "the config the user did not open must be untouched");
    QCOMPARE(backend.configFields(alpha).value(QStringLiteral("password")).toString(),
             QStringLiteral("alpha-pass"));
}

// And a config deleted while its editor was open is refused rather than written
// back as a new one.
void TestBackendConfig::anEditOfADeletedConfigIsRefused()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("alpha-pass"))));
    const QVariantMap opened = backend.configFields(0);

    backend.removeConfig(0);
    QCOMPARE(backend.configs().size(), 0);

    QSignalSpy errors(&backend, &Backend::errorOccurred);
    QVariantMap edit = form(QStringLiteral("Alpha"), QStringLiteral("alpha-pass"));
    edit[QStringLiteral("editIndex")] = 0;
    edit[QStringLiteral("editPath")] = opened.value(QStringLiteral("path")).toString();
    QVERIFY(!backend.createConfig(edit));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(backend.configs().size(), 0);
}

// A provider's config says how it wants to be routed, and can carry keys and
// tables the editor has no field for. The editor built the file from the form
// alone, so even a Save that changed nothing wrote the default routes over the
// provider's and dropped the rest — and a rename did the same.
void TestBackendConfig::anEditKeepsWhatTheFormDoesNotShow()
{
    const QString source = QDir(m_home.path()).filePath(QStringLiteral("provider.toml"));
    QFile out(source);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write("loglevel = \"info\"\n"
              "exclusions = [\"intranet.example.org\"]\n"
              "dns_upstreams = [\"1.1.1.1\"]\n"
              "\n[endpoint]\n"
              "hostname = \"vpn.example.org\"\n"
              "addresses = [\"198.51.100.7:443\"]\n"
              "username = \"alice\"\n"
              "password = \"hunter2\"\n"
              "provider_hint = \"kept\"\n"
              "\n[listener.tun]\n"
              "included_routes = [\"0.0.0.0/0\"]\n"
              "excluded_routes = [\"10.9.0.0/16\"]\n"
              "mtu_size = 1280\n"
              "\n[provider]\n"
              "plan = \"basic\"\n");
    out.close();

    Backend backend;
    QVERIFY(backend.importFile(source));
    QFile::remove(source);

    // Saved as it opened, then renamed.
    for (const QString &name : {QStringLiteral("provider"), QStringLiteral("Renamed")}) {
        QVariantMap edit = editorForm(backend, 0);
        edit[QStringLiteral("name")] = name;
        QVERIFY(backend.createConfig(edit));
        QCOMPARE(backend.configs(), QStringList{name});
        const QString saved = readAll(backend.configFields(0).value(QStringLiteral("path")).toString());
        const auto keeps = [&](const char *text) {
            return saved.contains(QLatin1String(text));
        };
        QVERIFY2(keeps("excluded_routes = [\"10.9.0.0/16\"]") && keeps("mtu_size = 1280"),
                 qPrintable(QStringLiteral("the file's own routing was replaced:\n") + saved));
        QVERIFY2(!keeps("192.168.0.0/16"), qPrintable(saved)); // not the defaults on top
        QVERIFY2(keeps("exclusions = [\"intranet.example.org\"]"), qPrintable(saved));
        QVERIFY2(keeps("provider_hint = \"kept\""), qPrintable(saved));
        QVERIFY2(keeps("[provider]") && keeps("plan = \"basic\""), qPrintable(saved));
        QVERIFY2(!keeps("hunter2"), qPrintable(saved)); // and the password stays out
    }
}

// A config may turn the post-quantum key exchange off. The editor has no switch
// for it, and its Save built the file from the form, which wrote the default
// back: saved once, even unchanged, the config had it on again.
void TestBackendConfig::anEditKeepsPostQuantumOff()
{
    const QString source = QDir(m_home.path()).filePath(QStringLiteral("classic.toml"));
    QFile out(source);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write("loglevel = \"info\"\n"
              "post_quantum_group_enabled = false\n"
              "\n[endpoint]\n"
              "hostname = \"vpn.example.org\"\n"
              "addresses = [\"198.51.100.7:443\"]\n"
              "username = \"alice\"\n"
              "password = \"hunter2\"\n");
    out.close();

    Backend backend;
    QVERIFY(backend.importFile(source));
    QFile::remove(source);

    // Saved as it opened, then renamed: both write the file anew.
    for (const QString &name : {QStringLiteral("classic"), QStringLiteral("Classic, renamed")}) {
        QVariantMap edit = editorForm(backend, 0);
        edit[QStringLiteral("name")] = name;
        QVERIFY(backend.createConfig(edit));
        QCOMPARE(backend.configs(), QStringList{name});
        const QString path = backend.configFields(0).value(QStringLiteral("path")).toString();
        const QString saved = readAll(path);
        QVERIFY2(saved.contains(QStringLiteral("post_quantum_group_enabled = false")),
                 qPrintable(QStringLiteral("the editor turned post-quantum back on:\n") + saved));
        QVERIFY2(!freetunnel::parseConfigToml(saved).postQuantum, qPrintable(saved));
        QVERIFY2(freetunnel::buildConnectConfig(path).toml.contains(
                         QStringLiteral("post_quantum_group_enabled = false")),
                 "the connection turned post-quantum back on");
    }

    // A config made in the editor has no file to keep it from, and gets the default.
    QVERIFY(backend.createConfig(form(QStringLiteral("fresh"), QStringLiteral("pw"))));
    const QString fresh = readAll(pathFor(backend, backend.configs().indexOf(QStringLiteral("fresh"))));
    QVERIFY2(fresh.contains(QStringLiteral("post_quantum_group_enabled = true")), qPrintable(fresh));
}

// TrustTunnel's own client can run a config as a local SOCKS proxy instead of a
// tunnel; its setup writes [listener] and [listener.socks] for that. FreeTunnel
// runs the tunnel and always writes [listener.tun], and carrying the proxy's
// tables over beside it named two listeners, which the core refuses: imported,
// such a config could not connect. The editor's save had been the one way to
// repair it, by dropping every table the form does not show, and keeping those
// (above) would have taken that away as well.
void TestBackendConfig::aProxyListenerIsNotKeptBesideTheTunnel()
{
    using freetunnel::CredentialStore;
    const auto oneListener = [](const QString &toml) {
        return toml.count(QStringLiteral("[listener.tun]")) == 1
                && !toml.contains(QStringLiteral("[listener]"))
                && !toml.contains(QStringLiteral("[listener.socks]"))
                && !toml.contains(QStringLiteral("127.0.0.1:1080"));
    };

    // As 1.1.8 to 1.2.2 left one after importing it: the password in the store,
    // and the proxy's tables after the tunnel's.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString path = QDir(dir).filePath(QStringLiteral("proxy.toml"));
    QVERIFY(freetunnel::backend_config::writeConfigFile(
            path, QByteArrayLiteral("loglevel = \"info\"\n"
                                    "vpn_mode = \"general\"\n"
                                    "\n[endpoint]\n"
                                    "hostname = \"vpn.example.org\"\n"
                                    "addresses = [\"198.51.100.7:443\"]\n"
                                    "username = \"alice\"\n"
                                    "\n[listener.tun]\n"
                                    "included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n"
                                    "\n[listener]\n"
                                    "\n[listener.socks]\n"
                                    "address = \"127.0.0.1:1080\"\n")));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForConfigPath(path),
                                           QStringLiteral("hunter2")));
    saveStoredConfigs({path});

    // It connects as it is, without being opened...
    const QString connect = freetunnel::buildConnectConfig(path).toml;
    QVERIFY2(connect.contains(QStringLiteral("hunter2")), "no password to connect with");
    QVERIFY2(oneListener(connect), qPrintable(connect));
    // ...and a save from the editor writes it as the core takes it.
    Backend backend;
    QVERIFY(backend.createConfig(editorForm(backend, 0)));
    QCOMPARE(backend.configs(), QStringList{QStringLiteral("proxy")});
    const QString saved = readAll(path);
    QVERIFY2(oneListener(saved), qPrintable(saved));

    // And one imported from a file as TrustTunnel's setup writes it is a tunnel.
    const QString source = QDir(m_home.path()).filePath(QStringLiteral("socks.toml"));
    QFile out(source);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write("loglevel = \"info\"\n"
              "vpn_mode = \"general\"\n"
              "killswitch_enabled = false\n"
              "\n[endpoint]\n"
              "hostname = \"vpn.example.org\"\n"
              "addresses = [\"198.51.100.8:443\"]\n"
              "username = \"bob\"\n"
              "password = \"swordfish\"\n"
              "\n# Defines the way to listen to network traffic by the kind of the nested table.\n"
              "[listener]\n"
              "\n[listener.socks]\n"
              "address = \"127.0.0.1:1080\"\n"
              "username = \"\"\n"
              "password = \"\"\n");
    out.close();
    QVERIFY(backend.importFile(source));
    QFile::remove(source);
    QCOMPARE(backend.configs().value(0), QStringLiteral("socks"));
    const QString imported = readAll(backend.configFields(0).value(QStringLiteral("path")).toString());
    QVERIFY2(oneListener(imported), qPrintable(imported));
    QCOMPARE(backend.configFields(0).value(QStringLiteral("password")).toString(),
             QStringLiteral("swordfish"));
}

// configs.json can name a config outside the app's directory (a very early build
// listed a picked file where it was instead of copying it). A rename moves the
// config into the app's directory, and it deleted the original: the user's own
// file, which removeConfig() has always left alone.
void TestBackendConfig::renamingAConfigKeptElsewhereLeavesTheOriginal()
{
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString original = QDir(elsewhere.path()).filePath(QStringLiteral("work.toml"));
    const QByteArray body("[endpoint]\n"
                          "hostname = \"vpn.example.org\"\n"
                          "addresses = [\"198.51.100.7:443\"]\n"
                          "username = \"alice\"\n"
                          "password = \"hunter2\"\n");
    QFile out(original);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(body);
    out.close();
    saveStoredConfigs({original});

    Backend backend;
    QCOMPARE(backend.configs(), QStringList{QStringLiteral("work")});
    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("name")] = QStringLiteral("Work, renamed");
    QVERIFY(backend.createConfig(edit));

    QCOMPARE(backend.configs(), QStringList{QStringLiteral("Work, renamed")});
    const QString moved = backend.configFields(0).value(QStringLiteral("path")).toString();
    QCOMPARE(QFileInfo(moved).absolutePath(),
             QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                     .absoluteFilePath());
    QCOMPARE(backend.configFields(0).value(QStringLiteral("password")).toString(),
             QStringLiteral("hunter2"));
    QVERIFY2(QFileInfo::exists(original), "renaming the config deleted the user's own file");
    QCOMPARE(readAll(original).toUtf8(), body);
}

// FreeTunnel 1.2.3 and earlier kept every table they did not know as the file
// had it, so a TrustTunnel 1.3 config imported then still holds its subscription
// address, VPN password and all, once its password has moved to the credential
// store. Nothing rewrites such a config until it is edited; the next start takes
// the address out, and nothing else. A config kept elsewhere is the user's own,
// which TrustTunnel's own client may read the address from, and is left alone.
void TestBackendConfig::aStoredSubscriptionAddressIsTakenOutAtStart()
{
    const QByteArray body("# my work server\n"
                          "subscription_url = \"https://alice:s3cret@vpn.example.org/subscription\"\n"
                          "[endpoint]\n"
                          "hostname = \"vpn.example.org\"\n"
                          "addresses = [\"198.51.100.7:443\"]\n"
                          "username = \"alice\"\n"
                          "password = \"\"\n"
                          "\n"
                          "[endpoint.subscription]\n"
                          "url = \"https://alice:s3cret@vpn.example.org/subscription\"\n"
                          "\n"
                          "[listener.tun]\n"
                          "mtu_size = 1500\n");
    const auto write = [&body](const QString &path) {
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        QCOMPARE(out.write(body), body.size());
    };
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString stored = QDir(dir).filePath(QStringLiteral("work.toml"));
    write(stored);
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString theirs = QDir(elsewhere.path()).filePath(QStringLiteral("theirs.toml"));
    write(theirs);
    QStringList configs{stored, theirs};
#if !defined(Q_OS_WIN)
    // Nor through a link to it in the app's own folder.
    const QString linked = QDir(dir).filePath(QStringLiteral("linked.toml"));
    QVERIFY(QFile::link(theirs, linked));
    const auto unlink = qScopeGuard([&linked] { QFile::remove(linked); });
    configs << linked;
#endif
    saveStoredConfigs(configs);

    Backend backend;
    QCOMPARE(backend.configs().size(), configs.size());
    const QString after = readAll(stored);
    QVERIFY2(!after.contains(QStringLiteral("s3cret")), qPrintable(after));
    QVERIFY2(!after.contains(QStringLiteral("subscription")), qPrintable(after));
    QCOMPARE(after, QStringLiteral("# my work server\n"
                                   "[endpoint]\n"
                                   "hostname = \"vpn.example.org\"\n"
                                   "addresses = [\"198.51.100.7:443\"]\n"
                                   "username = \"alice\"\n"
                                   "password = \"\"\n"
                                   "\n"
                                   "[listener.tun]\n"
                                   "mtu_size = 1500\n"));
    assertOwnerOnly(stored);
    QCOMPARE(readAll(theirs).toUtf8(), body);
#if !defined(Q_OS_WIN)
    QVERIFY(QFileInfo(linked).isSymLink());
#endif
}

// Before the import kept only the certificates, a certificate pasted or imported
// with its private key beside it was stored as it came, and went out with every
// link and export made from the config. The next start takes the key out, by the
// sweep's rules: from the copies in the app's own folder alone, never through a
// link, and nothing else in the file changes.
void TestBackendConfig::aStoredKeyIsTakenOutAtStart()
{
    const auto config = [](const QString &certificate) {
        return (QStringLiteral("# my work server\n"
                               "[endpoint]\n"
                               "hostname = \"vpn.example.org\"\n"
                               "addresses = [\"198.51.100.7:443\"]\n"
                               "username = \"alice\"\n"
                               "password = \"\"\n")
                + certificate
                + QStringLiteral("custom_sni = \"\"\n"
                                 "\n"
                                 "[listener.tun]\n"
                                 "mtu_size = 1500\n"))
                .toUtf8();
    };
    const auto block = [](const QString &pem) {
        return QStringLiteral("certificate = \"\"\"\n") + pem + QStringLiteral("\"\"\"\n");
    };
    const QByteArray body = config(block(certs::kLeafPem + certs::keyPem()));
    const auto write = [](const QString &path, const QByteArray &bytes) {
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        QCOMPARE(out.write(bytes), bytes.size());
    };
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString stored = QDir(dir).filePath(QStringLiteral("work.toml"));
    write(stored, body);
    // A key alone, with no certificate to keep: no certificate, as the core made
    // of it.
    const QString keyOnly = QDir(dir).filePath(QStringLiteral("key-only.toml"));
    write(keyOnly, config(block(certs::keyPem())));
    // A subscription address as well, which goes with the key.
    const QString both = QDir(dir).filePath(QStringLiteral("both.toml"));
    write(both, config(QStringLiteral("subscription_url = \"https://alice:s3cret@vpn.example.org/subscription\"\n")
                       + block(certs::kLeafPem + certs::keyPem())));
    // A chain in one block, as 1.1.9 and earlier wrote a link's: nothing but
    // certificates, so nothing to take out.
    const QString oneBlock = QDir(dir).filePath(QStringLiteral("one-block.toml"));
    const QByteArray chainInOneBlock = config(block(certs::pemBlock(
            QStringLiteral("CERTIFICATE"), certs::derOf(certs::kLeafPem) + certs::derOf(certs::kCaPem))));
    write(oneBlock, chainInOneBlock);
    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString theirs = QDir(elsewhere.path()).filePath(QStringLiteral("theirs.toml"));
    write(theirs, body);
    QStringList configs{stored, keyOnly, both, oneBlock, theirs};
#if !defined(Q_OS_WIN)
    const QString linked = QDir(dir).filePath(QStringLiteral("linked.toml"));
    QVERIFY(QFile::link(theirs, linked));
    const auto unlink = qScopeGuard([&linked] { QFile::remove(linked); });
    configs << linked;
#endif
    saveStoredConfigs(configs);

    Backend backend;
    QCOMPARE(backend.configs().size(), configs.size());
    QCOMPARE(readAll(stored).toUtf8(), config(block(certs::kLeafPem)));
    QCOMPARE(readAll(keyOnly).toUtf8(), config(QStringLiteral("certificate = \"\"\n")));
    QCOMPARE(readAll(both).toUtf8(), config(block(certs::kLeafPem)));
    QCOMPARE(readAll(oneBlock).toUtf8(), chainInOneBlock);
    assertOwnerOnly(stored);
    QCOMPARE(readAll(theirs).toUtf8(), body);
#if !defined(Q_OS_WIN)
    QVERIFY(QFileInfo(linked).isSymLink());
#endif
}

// A file that combines the certificate with its private key is pasted into the
// editor as readily as the certificate alone. The key is not saved, and the
// editor says so once the config is.
void TestBackendConfig::theEditorSavesOnlyTheCertificates()
{
    Backend backend;
    QSignalSpy notices(&backend, &Backend::errorOccurred);
    QVariantMap f = form(QStringLiteral("Pinned"), QStringLiteral("hunter2"));
    f[QStringLiteral("certificate")] = certs::kBagAttributes + certs::kLeafPem + certs::keyPem();
    QVERIFY(backend.createConfig(f));
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).toString(), freetunnel::certificateExtraIgnoredNote());
    const QString onDisk = readAll(pathFor(backend, 0));
    QVERIFY2(!onDisk.contains(certs::keyLabel()), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("Bag Attributes")), qPrintable(onDisk));
    QCOMPARE(backend.configFields(0).value(QStringLiteral("certificate")).toString(), certs::kLeafPem.trimmed());

    // Certificates alone are saved as they are, with nothing to say.
    QVariantMap chain = editorForm(backend, 0);
    chain[QStringLiteral("certificate")] = certs::kChainPem;
    QVERIFY(backend.createConfig(chain));
    QCOMPARE(notices.count(), 1);
    QCOMPARE(backend.configFields(0).value(QStringLiteral("certificate")).toString(), certs::kChainPem.trimmed());
    // And so is a chain in one block, as 1.1.9 and earlier wrote a link's.
    const QString merged = certs::pemBlock(QStringLiteral("CERTIFICATE"),
                                           certs::derOf(certs::kLeafPem) + certs::derOf(certs::kCaPem));
    QVariantMap oneBlock = editorForm(backend, 0);
    oneBlock[QStringLiteral("certificate")] = merged;
    QVERIFY(backend.createConfig(oneBlock));
    QCOMPARE(notices.count(), 1);
    QCOMPARE(backend.configFields(0).value(QStringLiteral("certificate")).toString(), merged.trimmed());
}

// A certificate field with no certificate in it is refused, and says what a
// certificate looks like: saved as no certificate, the pin the user meant to set
// would be quietly gone. An empty field is no certificate, as before.
void TestBackendConfig::theEditorRefusesACertificateWithNoCertificate()
{
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    const QStringList refused{certs::keyPem(), QStringLiteral("TEST"),
                              certs::pemBlock(QStringLiteral("CERTIFICATE"), certs::kPkcs8Der)};
    for (const QString &certificate : refused) {
        QVariantMap f = form(QStringLiteral("Refused"), QStringLiteral("hunter2"));
        f[QStringLiteral("certificate")] = certificate;
        QVERIFY(!backend.createConfig(f));
        QCOMPARE(errors.last().at(0).toString(),
                 QStringLiteral("The certificate must be in PEM format, as one or more BEGIN CERTIFICATE blocks"));
    }
    QCOMPARE(errors.count(), refused.size());
    QVERIFY(backend.configs().isEmpty());

    QVariantMap blank = form(QStringLiteral("Unpinned"), QStringLiteral("hunter2"));
    blank[QStringLiteral("certificate")] = QStringLiteral(" \n");
    QVERIFY(backend.createConfig(blank));
    QCOMPARE(errors.count(), refused.size());
}

// Written past the editor, the way a config kept outside the app's folder, which
// is never cleaned at start, can still be.
static void putInTheCertificate(const QString &path, const QString &certificate)
{
    QFile in(path);
    QVERIFY(in.open(QIODevice::ReadOnly));
    QString text = QString::fromUtf8(in.readAll());
    in.close();
    QVERIFY(text.contains(QStringLiteral("certificate = \"\"\n")));
    text.replace(QStringLiteral("certificate = \"\"\n"),
                 QStringLiteral("certificate = \"\"\"\n") + certificate + QStringLiteral("\"\"\"\n"));
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write(text.toUtf8());
}

// A key under the certificate label, and a key block of its own, beside the
// certificate.
static void putAKeyInTheCertificate(const QString &path)
{
    putInTheCertificate(path, certs::pemBlock(QStringLiteral("CERTIFICATE"), certs::kPkcs8Der) + certs::kLeafPem
                                      + certs::keyPem());
}

// A link is made to be handed on: it carries the certificate alone.
void TestBackendConfig::aShareLinkNeverCarriesAKey()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    putAKeyInTheCertificate(pathFor(backend, 0));

    QString err;
    const auto dl = freetunnel::parseDeepLink(backend.configDeepLink(0), &err);
    QVERIFY2(dl.has_value(), qPrintable(err));
    QCOMPARE(dl->certificate, certs::derOf(certs::kLeafPem));
    QCOMPARE(dl->password, QStringLiteral("hunter2"));
}

// So is an export.
void TestBackendConfig::aTomlExportNeverCarriesAKey()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    putAKeyInTheCertificate(pathFor(backend, 0));

    const QString dest = QDir(m_home.path()).filePath(QStringLiteral("exported.toml"));
    QFile::remove(dest);
    QVERIFY(backend.exportConfigToml(0, dest));
    const QString exported = readAll(dest);
    QFile::remove(dest);
    QVERIFY2(!exported.contains(certs::keyLabel()), qPrintable(exported));
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(exported);
    QCOMPARE(c.certificate, certs::kLeafPem.trimmed());
    QCOMPARE(c.password, QStringLiteral("hunter2"));
}

// A chain in one block, as 1.1.9 and earlier wrote a link's, holds nothing but
// certificates: its link carries the whole chain, as it always did, and so does
// its export.
void TestBackendConfig::aChainInOneBlockIsSharedWhole()
{
    Backend backend;
    QVERIFY(backend.createConfig(form(QStringLiteral("Alpha"), QStringLiteral("hunter2"))));
    const QByteArray chain = certs::derOf(certs::kLeafPem) + certs::derOf(certs::kCaPem);
    const QString oneBlock = certs::pemBlock(QStringLiteral("CERTIFICATE"), chain);
    putInTheCertificate(pathFor(backend, 0), oneBlock);

    QString err;
    const auto dl = freetunnel::parseDeepLink(backend.configDeepLink(0), &err);
    QVERIFY2(dl.has_value(), qPrintable(err));
    QCOMPARE(dl->certificate, chain);

    const QString dest = QDir(m_home.path()).filePath(QStringLiteral("exported.toml"));
    QFile::remove(dest);
    QVERIFY(backend.exportConfigToml(0, dest));
    const QString exported = readAll(dest);
    QFile::remove(dest);
    QCOMPARE(freetunnel::parseConfigToml(exported).certificate, oneBlock.trimmed());
}

// The editor keeps its certificate field when a pick reads nothing, so the
// reason has to come from here: before, the pick wiped the field without a word.
void TestBackendConfig::aRefusedCertificateFileIsExplained()
{
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    const auto lastError = [&] { return errors.isEmpty() ? QString() : errors.last().at(0).toString(); };

    QTemporaryFile big(QDir::tempPath() + QStringLiteral("/ft-cert-big-XXXXXX.pem"));
    QVERIFY(big.open());
    big.write(QByteArray(2 * 1024 * 1024, 'A'));
    big.close();
    QVERIFY(backend.readTextFile(big.fileName()).isEmpty());
    QCOMPARE(errors.count(), 1);
    QVERIFY2(lastError().contains(QStringLiteral("1 MB")), qPrintable(lastError()));

    QTemporaryFile empty(QDir::tempPath() + QStringLiteral("/ft-cert-empty-XXXXXX.pem"));
    QVERIFY(empty.open());
    empty.close();
    QVERIFY(backend.readTextFile(empty.fileName()).isEmpty());
    QCOMPARE(errors.count(), 2);
    QCOMPARE(lastError(), QStringLiteral("That file is empty"));

#if defined(Q_OS_UNIX)
    QVERIFY(backend.readTextFile(QStringLiteral("/etc/hosts")).isEmpty());
    QCOMPARE(errors.count(), 3);
    QVERIFY2(lastError().contains(QStringLiteral("Downloads")), qPrintable(lastError()));
    // The temporary files folder is allowed as well (the files read below are
    // there), and the message says so.
    QVERIFY2(lastError().contains(QStringLiteral("temporary files")), qPrintable(lastError()));
#endif

    // A file that is read is returned, and nothing is said.
    QTemporaryFile pem(QDir::tempPath() + QStringLiteral("/ft-cert-XXXXXX.pem"));
    QVERIFY(pem.open());
    pem.write("-----BEGIN CERTIFICATE-----\nTEST\n-----END CERTIFICATE-----\n");
    pem.close();
    const int before = errors.count();
    QVERIFY(backend.readTextFile(pem.fileName()).contains(QStringLiteral("TEST")));
    QCOMPARE(errors.count(), before);
}

// A name is a file name, and one too long for the file system failed as "Could
// not write config", which said nothing about the name. The editor now says what
// is wrong, and before anything is written.
void TestBackendConfig::theEditorRefusesANameTooLongForAFile()
{
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    const int limit = freetunnel::kMaxConfigNameLength;
    // Two bytes a letter in a file name: 260 of the 255 there are, which failed.
    QVERIFY(!backend.createConfig(form(QString(130, QChar(0x0416)), QStringLiteral("pw"))));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(errors.last().at(0).toString(),
             QStringLiteral("The name is too long: %1 characters at most").arg(limit));
    QVERIFY(!backend.createConfig(form(QString(limit + 1, QLatin1Char('a')), QStringLiteral("pw"))));
    QCOMPARE(errors.count(), 2);
    QVERIFY(backend.configs().isEmpty());

    const QString atLimit(limit, QChar(0x0416));
    QVERIFY(backend.createConfig(form(atLimit, QStringLiteral("pw"))));
    QCOMPARE(backend.configs(), QStringList{atLimit});

    // Left empty, the name is the hostname, and a long one is cut rather than
    // refused: nobody typed it.
    QVariantMap unnamed = form(QString(), QStringLiteral("pw"));
    unnamed[QStringLiteral("hostname")] = QString(70, QLatin1Char('h')) + QStringLiteral(".example.org");
    QVERIFY(backend.createConfig(unnamed));
    QVERIFY(backend.configs().contains(QString(limit, QLatin1Char('h'))));
}

// A config that already has a longer name, from a link or a file before the
// limit, still saves under it. Only a new name has to fit.
void TestBackendConfig::aConfigWithALongerNameStillSaves()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString longName(freetunnel::kMaxConfigNameLength + 10, QLatin1Char('n'));
    const QString path = QDir(dir).filePath(longName + QStringLiteral(".toml"));
    QVERIFY(freetunnel::backend_config::writeConfigFile(
            path, QByteArrayLiteral("[endpoint]\n"
                                    "hostname = \"vpn.example.org\"\n"
                                    "addresses = [\"198.51.100.7:443\"]\n"
                                    "username = \"alice\"\n"
                                    "password = \"hunter2\"\n")));
    saveStoredConfigs({path});

    Backend backend;
    QVariantMap edit = editorForm(backend, 0);
    edit[QStringLiteral("username")] = QStringLiteral("bob");
    QVERIFY(backend.createConfig(edit));
    QCOMPARE(backend.configs(), QStringList{longName});
    QCOMPARE(backend.configFields(0).value(QStringLiteral("username")).toString(), QStringLiteral("bob"));

    QSignalSpy errors(&backend, &Backend::errorOccurred);
    edit = editorForm(backend, 0);
    edit[QStringLiteral("name")] = longName + QStringLiteral("x");
    QVERIFY(!backend.createConfig(edit));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(backend.configs(), QStringList{longName});
}

// A save the credential store refused told the user to install gnome-keyring or
// KWallet on every platform — Linux advice, shown to the Windows and macOS users
// who are most of them, and who have neither to install.
void TestBackendConfig::aRefusedPasswordIsExplainedForThisPlatform()
{
    using freetunnel::backend_config::passwordNotStoredMessage;
    for (const bool storeIsThere : {true, false}) {
        const QString message = passwordNotStoredMessage(storeIsThere);
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
        // A keyring that is there and refused was locked: installing one is not
        // the answer, unlocking it is.
        if (storeIsThere) {
            QVERIFY2(message.contains(QLatin1String("unlock"))
                             && !message.contains(QLatin1String("gnome-keyring")),
                     qPrintable(message));
        } else {
            QVERIFY2(message.contains(QLatin1String("gnome-keyring")), qPrintable(message));
        }
#else
        QVERIFY2(!message.contains(QLatin1String("gnome-keyring"))
                         && !message.contains(QLatin1String("KWallet")),
                 qPrintable(message));
#if defined(Q_OS_MACOS)
        QVERIFY2(message.contains(QLatin1String("Keychain")), qPrintable(message));
#else
        // The refusal known to happen is a password too long for it, and another
        // try changes nothing: the message says what the limit is instead.
        QVERIFY2(message.contains(QLatin1String("Credential Manager"))
                         && message.contains(QLatin1String("2560"))
                         && !message.contains(QLatin1String("Try again")),
                 qPrintable(message));
#endif
#endif
    }
}

// And that is what a save says when it happens. Credential Manager is the one
// store a test can make refuse a password: it holds at most 2560 bytes
// (CRED_MAX_CREDENTIAL_BLOB_SIZE), and CredWrite turns down anything longer.
void TestBackendConfig::aPasswordTheStoreRefusesIsExplainedInItsTerms()
{
#if !defined(Q_OS_WIN)
    QSKIP("only Windows Credential Manager can be made to refuse a password here");
#else
    Backend backend;
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.createConfig(
            form(QStringLiteral("Long"), QString(8192, QLatin1Char('x')))));
    QCOMPARE(errors.count(), 1);
    const QString message = errors.takeFirst().at(0).toString();
    QCOMPARE(message, freetunnel::backend_config::passwordNotStoredMessage(true));
    QVERIFY2(!message.contains(QLatin1String("gnome-keyring")), qPrintable(message));
    QCOMPARE(backend.configs().size(), 0);
#endif
}

// What a refused password is told depends on whether a store is there at all,
// and is asked anew when it happens, by an import as much as by a save from the
// editor: the import never asked, so the Settings banner stayed as it was at
// startup. FT_TEST_KEYRING stands in for the two stores no test can have on
// demand: one that is not there, and one that is there and refuses, as a locked
// keyring does once its unlock prompt is dismissed.
// What the user is told when the store cannot be asked is said in the terms of the
// store the platform has, as what it refused to take is, and is not that message:
// a read that failed is not a password that was not saved.
void TestBackendConfig::aSecretTheStoreCannotReadIsExplainedInItsTerms()
{
    using freetunnel::backend_config::authKeyNotReadMessage;
    using freetunnel::backend_config::passwordNotReadMessage;
    for (const bool storeIsThere : {true, false}) {
        const QString message = passwordNotReadMessage(storeIsThere);
        QVERIFY2(message.startsWith(QLatin1String("Could not read the VPN password")), qPrintable(message));
        QVERIFY2(message != freetunnel::backend_config::passwordNotStoredMessage(storeIsThere), qPrintable(message));
#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
        if (storeIsThere) {
            QVERIFY2(message.contains(QLatin1String("unlock")) && !message.contains(QLatin1String("gnome-keyring")),
                     qPrintable(message));
        } else {
            QVERIFY2(message.contains(QLatin1String("unavailable")) && message.contains(QLatin1String("gnome-keyring")),
                     qPrintable(message));
        }
#elif defined(Q_OS_MACOS)
        QVERIFY2(message.contains(QLatin1String("Keychain")), qPrintable(message));
#else
        QVERIFY2(message.contains(QLatin1String("Credential Manager")), qPrintable(message));
#endif
    }
    const QString key = authKeyNotReadMessage();
    QVERIFY2(key.startsWith(QLatin1String("Could not read the auth key")), qPrintable(key));
    QVERIFY(key != freetunnel::backend_config::authKeyNotStoredMessage());
}

// The editor opens a config on what the file and the store hold. A form made when
// the store could not be asked for one of the two would look complete, and saving
// it would write the config over without the secret. So there is no form: the
// path, and why. (The window shows the toast and closes.)
void TestBackendConfig::theEditorIsNotOpenedOnASecretTheStoreCannotRead()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    const auto reset = qScopeGuard([] { CredentialStore::setReadFailureForTest({}); });

    QSignalSpy toasts(&backend, &Backend::errorOccurred);
    CredentialStore::setReadFailureForTest(
            [key = CredentialStore::keyForConfigAuthKey(path)](const QString &k) { return k == key; });
    QVariantMap opened = backend.configFields(0);
    // The editor's own toast says it; a second one from here would overwrite it.
    QCOMPARE(toasts.count(), 0);
    QCOMPARE(opened.keys(), (QStringList{QStringLiteral("path"), QStringLiteral("secretError")}));
    QCOMPARE(opened.value(QStringLiteral("path")).toString(), path);
    QCOMPARE(opened.value(QStringLiteral("secretError")).toString(),
             freetunnel::backend_config::authKeyNotReadMessage());
    QVERIFY2(backend.logText().contains(freetunnel::backend_config::authKeyNotReadMessage()),
             "the log does not say why the editor did not open");

    CredentialStore::setReadFailureForTest(
            [key = CredentialStore::keyForConfigPath(path)](const QString &k) { return k == key; });
    opened = backend.configFields(0);
    QCOMPARE(opened.keys(), (QStringList{QStringLiteral("path"), QStringLiteral("secretError")}));
    const QString said = opened.value(QStringLiteral("secretError")).toString();
    QVERIFY2(said == freetunnel::backend_config::passwordNotReadMessage(true)
                     || said == freetunnel::backend_config::passwordNotReadMessage(false),
             qPrintable(said));

    // Readable again, the same config opens whole.
    CredentialStore::setReadFailureForTest({});
    opened = backend.configFields(0);
    QVERIFY(!opened.contains(QStringLiteral("secretError")));
    QCOMPARE(opened.value(QStringLiteral("password")).toString(), QStringLiteral("hunter2"));
    QCOMPARE(opened.value(QStringLiteral("authKey")).toString(), QStringLiteral("aabbccdd"));
}

// A form made before the store went away is saved after: nothing is written,
// deleted or moved, because the secrets a failed save puts back and a rename
// carries over are the ones the save could not read.
void TestBackendConfig::anEditSavedWhileASecretCannotBeReadChangesNothing()
{
    using freetunnel::CredentialStore;
    Backend backend;
    QVariantMap f = form(QStringLiteral("Alpha"), QStringLiteral("hunter2"));
    f[QStringLiteral("authKey")] = QStringLiteral("aabbccdd");
    QVERIFY(backend.createConfig(f));
    const QString path = pathFor(backend, 0);
    const QString passwordEntry = CredentialStore::keyForConfigPath(path);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(path);
    const QString before = readAll(path);
    const auto reset = qScopeGuard([] { CredentialStore::setReadFailureForTest({}); });

    for (const QString &unreadable : {keyEntry, passwordEntry}) {
        for (const bool rename : {false, true}) {
            QVariantMap edit = editorForm(backend, 0);
            edit[QStringLiteral("username")] = QStringLiteral("bob");
            edit[QStringLiteral("password")] = QStringLiteral("new-pass");
            edit[QStringLiteral("authKey")] = QStringLiteral("11223344");
            if (rename)
                edit[QStringLiteral("name")] = QStringLiteral("Renamed");
            CredentialStore::setReadFailureForTest([unreadable](const QString &k) { return k == unreadable; });
            QSignalSpy errors(&backend, &Backend::errorOccurred);
            QVERIFY(!backend.createConfig(edit));
            CredentialStore::setReadFailureForTest({});

            QVERIFY2(!errors.isEmpty(), "the user was told nothing");
            QCOMPARE(readAll(path), before);
            QCOMPARE(backend.configs(), QStringList{QStringLiteral("Alpha")});
            QCOMPARE(CredentialStore::lookup(passwordEntry).value, QStringLiteral("hunter2"));
            QCOMPARE(CredentialStore::lookup(keyEntry).value, QStringLiteral("aabbccdd"));
            // Nothing under the new name either: no file, and no entries left there.
            const QString renamed = QFileInfo(path).dir().filePath(QStringLiteral("Renamed.toml"));
            QVERIFY(!QFileInfo::exists(renamed));
            QVERIFY(CredentialStore::lookup(CredentialStore::keyForConfigPath(renamed)).notFound());
            QVERIFY(CredentialStore::lookup(CredentialStore::keyForConfigAuthKey(renamed)).notFound());
        }
    }
}

// The read-backs that decide whether a save took: a value that could not be read
// back is not a value that was stored, and a key that could not be asked for is
// not one that is gone.
void TestBackendConfig::aReadBackThatCannotBeAnsweredIsNotAConfirmation()
{
    using freetunnel::CredentialStore;
    using freetunnel::backend_config::storeConfigAuthKey;
    using freetunnel::backend_config::storeConfigPassword;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString target = QDir(dir).filePath(QStringLiteral("readback.toml"));
    const QString passwordEntry = CredentialStore::keyForConfigPath(target);
    const QString keyEntry = CredentialStore::keyForConfigAuthKey(target);
    const auto cleanUp = qScopeGuard([&] {
        CredentialStore::setReadFailureForTest({});
        CredentialStore::deletePassword(passwordEntry);
        CredentialStore::deletePassword(keyEntry);
    });

    QVERIFY(storeConfigPassword(target, QStringLiteral("pw")));
    QVERIFY(storeConfigAuthKey(target, QStringLiteral("aabb"), QString()));

    CredentialStore::setReadFailureForTest([&](const QString &k) { return k == passwordEntry || k == keyEntry; });
    QVERIFY2(!storeConfigPassword(target, QStringLiteral("pw2")), "an unreadable entry confirmed the password");
    QVERIFY2(!storeConfigAuthKey(target, QStringLiteral("ccdd"), QStringLiteral("aabb")),
             "an unreadable entry confirmed the key");
    // Deleted, but the store will not say so: the key may be there still.
    QVERIFY2(!storeConfigAuthKey(target, QString(), QStringLiteral("ccdd")),
             "a key that could not be asked for was taken for deleted");
    CredentialStore::setReadFailureForTest({});

    // And once it can be asked, the same delete is confirmed.
    QVERIFY(storeConfigAuthKey(target, QStringLiteral("eeff"), QString()));
    QVERIFY(storeConfigAuthKey(target, QString(), QStringLiteral("eeff")));
    QVERIFY(CredentialStore::lookup(keyEntry).notFound());
}

void TestBackendConfig::aRefusedPasswordSaysWhatToDoAboutTheKeyring()
{
    const auto unset = qScopeGuard([] { qunsetenv("FT_TEST_KEYRING"); });
    const QString source = QDir(m_home.path()).filePath(QStringLiteral("refused.toml"));
    {
        QFile out(source);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write("[endpoint]\n"
                  "hostname = \"vpn.example.org\"\n"
                  "addresses = [\"198.51.100.7:443\"]\n"
                  "username = \"alice\"\n"
                  "password = \"hunter2\"\n");
    }
    for (const bool storeIsThere : {true, false}) {
        qputenv("FT_TEST_KEYRING", storeIsThere ? "locked" : "absent");
        const QString expected = freetunnel::backend_config::passwordNotStoredMessage(storeIsThere);
        for (const bool imported : {false, true}) {
            Backend backend;
            QSignalSpy errors(&backend, &Backend::errorOccurred);
            QSignalSpy banner(&backend, &Backend::credentialStorageChanged);
            QVERIFY(imported ? !backend.importFile(source)
                             : !backend.createConfig(form(QStringLiteral("Refused"), QStringLiteral("pw"))));
            const QString what = imported ? QStringLiteral("import") : QStringLiteral("save");
            QCOMPARE(errors.count(), 1);
            QVERIFY2(errors.at(0).at(0).toString() == expected,
                     qPrintable(what + QStringLiteral(": ") + errors.at(0).at(0).toString()));
            QCOMPARE(backend.configs().size(), 0);
#if defined(Q_OS_LINUX)
            // Asked again: the banner shows once there is no keyring, and only then.
            QVERIFY2(banner.count() == (storeIsThere ? 0 : 1), qPrintable(what));
            QCOMPARE(backend.credentialStorageWarning().isEmpty(), storeIsThere);
#endif
        }
    }
}

#include "test_backend_config.moc"
