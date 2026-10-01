// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QClipboard>
#include <QDir>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QStandardPaths>

#include <QCoreApplication>
#include <QSignalSpy>

#include "app/Backend.h"
#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigStore.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"

class TestBackendDeepLinkImport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void skipVerificationRequiresConfirmation();
    void theConfirmationNamesTheServerAndWarns();
    void confirmImportsUnsafeLink();
    void secondLinkWithTheSameNameOffersReplace();
    void replaceOverwritesInsteadOfAddingACopy();
    void aLinkSentAgainFindsTheConfig120Named();
    void addingACopyKeepsBothConfigs();
    void replaceDoesNotInheritTheOldStoredPassword();
    void aLinkWithAVeryLongNameImports();
    void aLinkSentAgainFindsTheConfigItNamedWhole();

    void importFileRejectsAMissingFile();
    void importFileRejectsSomethingThatIsNotAConfig();
    void importFileCopiesTheConfigIntoTheAppDirectory();
    void importFileLeavesTheOriginalAlone();
    void importFileSaysWhenTheConfigSkipsCertificateChecks();
    void clipboardImportNeedsALink();
    void cleanupTestCase();
};

void TestBackendDeepLinkImport::initTestCase()
{
    // A credential service unique to THIS RUN. Items in the OS store carry an
    // ACL tied to the binary that created them, and this test binary is rebuilt
    // constantly — so reading an entry a previous build left behind makes macOS
    // pop an authorization dialog and the test hangs until a human answers it.
    // A fresh service name per run can never collide with an older build's item.
    qputenv("FT_TEST_CREDENTIAL_SERVICE",
            QStringLiteral("com.freetunnel.app.test.deeplink.%1")
                    .arg(QCoreApplication::applicationPid())
                    .toUtf8());
    // Keep the imported configs out of the real per-user location too, so runs
    // don't accumulate and every case starts from a known-empty directory.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DeepLinkImportTest"));

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir(base).removeRecursively();
    QDir().mkpath(base);
}

// ---- import from a file ---------------------------------------------------
// The other half of "add a server": the user picks a .toml someone sent them.
// None of it was covered, though it writes a password-bearing file into the app
// directory and can make the imported server the active one.

static QString writeTempConfig(const QString &name, const QByteArray &body)
{
    const QString dir = QDir::tempPath() + QStringLiteral("/ft-import-test");
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QString();
    f.write(body);
    return path;
}

// The shape the app itself writes: keys under [endpoint], addresses as an array.
static QByteArray validImportBody()
{
    return QByteArrayLiteral("loglevel = \"warn\"\n"
                             "vpn_mode = \"general\"\n"
                             "dns_upstreams = [\"1.1.1.1\"]\n"
                             "\n[endpoint]\n"
                             "hostname = \"file.example.com\"\n"
                             "addresses = [\"203.0.113.9:443\"]\n"
                             "username = \"fileuser\"\n"
                             "password = \"filepass\"\n"
                             "upstream_protocol = \"http2\"\n");
}

void TestBackendDeepLinkImport::importFileRejectsAMissingFile()
{
    Backend backend;
    const QString missing = QDir::tempPath() + QStringLiteral("/ft-does-not-exist.toml");
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFile(missing));
    QCOMPARE(failed.count(), 1);
    // Naming the path is the point: "could not read the file" leaves the user
    // guessing which file, and a picker can hand over a stale or moved path.
    const QString message = failed.at(0).at(0).toString();
    QVERIFY2(message.contains(missing), qPrintable(message));
}

// A .toml with no server address or no credentials is not a config, and half-
// importing it would leave a row that can never connect.
void TestBackendDeepLinkImport::importFileRejectsSomethingThatIsNotAConfig()
{
    const QString path = writeTempConfig(QStringLiteral("junk.toml"),
                                         QByteArrayLiteral("hostname = \"x\"\n"));
    QVERIFY(!path.isEmpty());

    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(backend.configs().size(), before); // nothing was added
    QFile::remove(path);
}

void TestBackendDeepLinkImport::importFileCopiesTheConfigIntoTheAppDirectory()
{
    const QString path = writeTempConfig(QStringLiteral("from-a-friend.toml"), validImportBody());
    QVERIFY(!path.isEmpty());

    Backend backend;
    QSignalSpy imported(&backend, &Backend::configImported);
    QVERIFY(backend.importFile(path));
    QCOMPARE(imported.count(), 1);
    QVERIFY2(backend.configs().contains(QStringLiteral("from-a-friend")),
             qPrintable(backend.configs().join(QLatin1Char(','))));

    // The app must own its copy: the config lives in the app config directory,
    // not wherever the user happened to leave the file.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString copied = QDir(base).filePath(QStringLiteral("from-a-friend.toml"));
    QVERIFY2(QFileInfo::exists(copied), qPrintable(copied));

    // ...and the password moved into the credential store on the way in.
    QFile c(copied);
    QVERIFY(c.open(QIODevice::ReadOnly));
    const QString onDisk = QString::fromUtf8(c.readAll());
    c.close();
    QVERIFY2(!onDisk.contains(QStringLiteral("filepass")),
             "the imported config still holds the password in cleartext");
    QCOMPARE(freetunnel::CredentialStore::loadPassword(
                     freetunnel::CredentialStore::keyForConfigPath(copied)),
             QStringLiteral("filepass"));
    QFile::remove(path);
}

// Importing copies; it does not adopt, move or rewrite the file the user picked.
// That file may live in a shared folder or be someone else's.
void TestBackendDeepLinkImport::importFileLeavesTheOriginalAlone()
{
    const QString path = writeTempConfig(QStringLiteral("untouched.toml"), validImportBody());
    QVERIFY(!path.isEmpty());

    Backend backend;
    QVERIFY(backend.importFile(path));

    QFile f(path);
    QVERIFY2(f.open(QIODevice::ReadOnly), "the source file was moved or removed");
    QCOMPARE(f.readAll(), validImportBody()); // including its password, still there
    f.close();
    QFile::remove(path);
}

// A link that turns off certificate verification says so before it is added.
// A file did not: one that turned it off was imported without a word.
void TestBackendDeepLinkImport::importFileSaysWhenTheConfigSkipsCertificateChecks()
{
    QByteArray body = validImportBody();
    body.append("skip_verification = true\n");
    const QString path = writeTempConfig(QStringLiteral("unchecked.toml"), body);
    QVERIFY(!path.isEmpty());
    const QString plain = writeTempConfig(QStringLiteral("checked.toml"), validImportBody());
    QVERIFY(!plain.isEmpty());

    Backend backend;
    QSignalSpy notices(&backend, &Backend::errorOccurred);
    QVERIFY(backend.importFile(path));
    QCOMPARE(notices.count(), 1);
    const QString notice = notices.at(0).at(0).toString();
    QVERIFY2(notice.contains(QStringLiteral("unchecked"))
                     && notice.contains(QStringLiteral("certificate verification")),
             qPrintable(notice));

    // A config that checks certificates is added with nothing more to say.
    QVERIFY(backend.importFile(plain));
    QCOMPARE(notices.count(), 1);
    QFile::remove(path);
    QFile::remove(plain);
}

// "Paste link" is aimed at a clipboard that usually holds something else
// entirely, so the two failures have to read differently: nothing there at all,
// versus something there that is not one of our links.
void TestBackendDeepLinkImport::clipboardImportNeedsALink()
{
    Backend backend;
    QGuiApplication::clipboard()->setText(QString());
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFromClipboard());
    QCOMPARE(failed.count(), 1);
    QVERIFY2(failed.at(0).at(0).toString().contains(QStringLiteral("empty"), Qt::CaseInsensitive),
             qPrintable(failed.at(0).at(0).toString()));

    QGuiApplication::clipboard()->setText(QStringLiteral("just some text"));
    QVERIFY(!backend.importFromClipboard());
    QCOMPARE(failed.count(), 2);
    // Rejected as "not a link", not passed to the link parser to fail there with
    // whatever internal complaint it happens to produce.
    QVERIFY2(failed.at(1).at(0).toString().contains(QStringLiteral("tt://")),
             qPrintable(failed.at(1).at(0).toString()));
}

void TestBackendDeepLinkImport::cleanupTestCase()
{
    // Delete every credential this run created: the service name is unique, so
    // nothing else can be using them, and leaving entries in the user's keychain
    // is not acceptable for a test.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QFileInfoList files =
            QDir(base).entryInfoList({QStringLiteral("*.toml")}, QDir::Files);
    for (const QFileInfo &fi : files) {
        freetunnel::CredentialStore::deletePassword(
                freetunnel::CredentialStore::keyForConfigPath(fi.absoluteFilePath()));
    }
    QDir(base).removeRecursively();
}

static QString unsafeLink()
{
    freetunnel::DeepLinkConfig c;
    c.hostname = QStringLiteral("unsafe.example.com");
    c.addresses = {QStringLiteral("203.0.113.1:443")};
    c.username = QStringLiteral("user");
    c.password = QStringLiteral("pass");
    c.skipVerification = true;
    return freetunnel::encodeDeepLink(c);
}

// Collides with unsafeLink()'s config (the name is derived from the host) but
// points somewhere else and carries no password of its own.
static QString passwordlessLink()
{
    freetunnel::DeepLinkConfig c;
    c.hostname = QStringLiteral("unsafe.example.com");
    c.addresses = {QStringLiteral("198.51.100.9:443")};
    c.username = QStringLiteral("user");
    c.skipVerification = true;
    return freetunnel::encodeDeepLink(c);
}

// The config file the imports land on, found by content rather than by
// reconstructing the naming rules.
static QString importedConfigPath(const QString &hostname)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QFileInfoList files =
            QDir(base).entryInfoList({QStringLiteral("*.toml")}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains(hostname))
            return fi.absoluteFilePath();
    }
    return QString();
}

void TestBackendDeepLinkImport::skipVerificationRequiresConfirmation()
{
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy importedSpy(&backend, &Backend::configImported);

    QVERIFY(!backend.importDeepLink(unsafeLink()));
    QCOMPARE(confirmSpy.count(), 1);
    QCOMPARE(importedSpy.count(), 0);
}

// The confirmation is all the defence a link meets, and what it says is that
// defence: the server the config will really connect to, a warning when the name
// mixes alphabets as a look-alike does, and one when the link turns certificate
// checks off. Only that the question was asked used to be tested, so any of the
// three could have gone quietly.
void TestBackendDeepLinkImport::theConfirmationNamesTheServerAndWarns()
{
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    const auto ask = [&](const freetunnel::DeepLinkConfig &c) {
        const int before = confirmSpy.count();
        if (backend.importDeepLink(freetunnel::encodeDeepLink(c)) || confirmSpy.count() != before + 1)
            return QString();
        return confirmSpy.last().at(0).toString();
    };

    // "Wоrk" with a Cyrillic о, aimed somewhere else, with the checks off.
    freetunnel::DeepLinkConfig c;
    c.name = QStringLiteral("W\u043Erk");
    c.hostname = QStringLiteral("look-alike.example.net");
    c.addresses = {QStringLiteral("203.0.113.66:443")};
    c.username = QStringLiteral("user");
    c.password = QStringLiteral("pass");
    c.skipVerification = true;
    const QString hostile = ask(c);
    QVERIFY2(hostile.contains(QStringLiteral("Server: look-alike.example.net")), qPrintable(hostile));
    QVERIFY2(hostile.contains(QStringLiteral("This name mixes letters from different alphabets.")),
             qPrintable(hostile));
    QVERIFY2(hostile.contains(QStringLiteral("This link turns off server certificate verification.")),
             qPrintable(hostile));

    // An ordinary link is told only what is true of it. A name in one alphabet
    // is ordinary, Cyrillic included.
    c.skipVerification = false;
    c.hostname = QStringLiteral("plain.example.net");
    for (const QString &name : {QStringLiteral("Work"), QStringLiteral("\u0420\u0430\u0431\u043E\u0442\u0430")}) {
        c.name = name;
        const QString plain = ask(c);
        QVERIFY2(plain.contains(QStringLiteral("Server: plain.example.net")), qPrintable(plain));
        QVERIFY2(!plain.contains(QStringLiteral("alphabets")), qPrintable(plain));
        QVERIFY2(!plain.contains(QStringLiteral("certificate")), qPrintable(plain));
    }
}

void TestBackendDeepLinkImport::confirmImportsUnsafeLink()
{
    Backend backend;
    QSignalSpy importedSpy(&backend, &Backend::configImported);

    QVERIFY(backend.confirmDeepLinkImport(unsafeLink()));
    QCOMPARE(importedSpy.count(), 1);
}

// A link naming a config that already exists must say so, so the dialog can
// offer to replace it. Silently taking the name over is what this whole flow
// exists to prevent; silently refusing to ever replace is merely annoying.
void TestBackendDeepLinkImport::secondLinkWithTheSameNameOffersReplace()
{
    Backend backend;
    QVERIFY(backend.confirmDeepLinkImport(unsafeLink()));
    const int afterFirst = backend.configs().size();

    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importDeepLink(unsafeLink()));
    QCOMPARE(confirmSpy.count(), 1);
    // Third argument is the colliding config's name — empty means "no collision".
    QVERIFY(!confirmSpy.first().at(2).toString().isEmpty());
    QCOMPARE(backend.configs().size(), afterFirst); // nothing imported yet
}

// 1.2.0 turned every space in a name into '_', so a config imported then from a
// link named "My Server" is My_Server.toml. Names now keep their spaces, and the
// same link sent again looked for "My Server.toml", found nothing, and offered to
// add a second copy instead of replacing the first.
void TestBackendDeepLinkImport::aLinkSentAgainFindsTheConfig120Named()
{
    freetunnel::DeepLinkConfig c;
    c.name = QStringLiteral("My Server");
    c.hostname = QStringLiteral("legacy-name.example.com");
    c.addresses = {QStringLiteral("203.0.113.8:443")};
    c.username = QStringLiteral("user");
    c.password = QStringLiteral("pass");
    const QString link = freetunnel::encodeDeepLink(c);
    {
        Backend first;
        QVERIFY(first.confirmDeepLinkImport(link));
    }
    // Put it where 1.2.0 would have.
    const QString made = importedConfigPath(QStringLiteral("legacy-name.example.com"));
    QVERIFY(!made.isEmpty());
    const QString legacy = QFileInfo(made).dir().filePath(QStringLiteral("My_Server.toml"));
    QVERIFY(QFile::rename(made, legacy));
    QStringList stored = loadStoredConfigs();
    stored.replace(stored.indexOf(made), legacy);
    saveStoredConfigs(stored);

    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importDeepLink(link));
    QCOMPARE(confirmSpy.count(), 1);
    QCOMPARE(confirmSpy.first().at(2).toString(), QStringLiteral("My_Server"));
    QFile::remove(legacy);
}

void TestBackendDeepLinkImport::replaceOverwritesInsteadOfAddingACopy()
{
    Backend backend;
    QVERIFY(backend.confirmDeepLinkImport(unsafeLink()));
    const int afterFirst = backend.configs().size();

    QVERIFY(backend.confirmDeepLinkImport(unsafeLink(), /*replaceExisting=*/true));
    QCOMPARE(backend.configs().size(), afterFirst);
}

void TestBackendDeepLinkImport::addingACopyKeepsBothConfigs()
{
    Backend backend;
    QVERIFY(backend.confirmDeepLinkImport(unsafeLink()));
    const int afterFirst = backend.configs().size();

    QVERIFY(backend.confirmDeepLinkImport(unsafeLink(), /*replaceExisting=*/false));
    QCOMPARE(backend.configs().size(), afterFirst + 1);
}

// The credential is keyed by config PATH, so replacing a config in place would
// otherwise leave the previous password sitting under the new server's entry —
// a link that carries no password of its own would inherit the user's real one
// and hand it to whatever server the link names.
void TestBackendDeepLinkImport::replaceDoesNotInheritTheOldStoredPassword()
{
    // Start from an empty directory so the first import lands on the base name —
    // which is exactly the path a replacing link collides with. Earlier cases in
    // this run have already taken that name, and a "-2" copy would leave the
    // replace pointing at a different file than the one under test.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    for (const QFileInfo &fi : QDir(base).entryInfoList({QStringLiteral("*.toml")}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());

    Backend backend;
    QVERIFY(backend.confirmDeepLinkImport(unsafeLink()));
    const QString path = importedConfigPath(QStringLiteral("unsafe.example.com"));
    QVERIFY(!path.isEmpty());
    const QString key = freetunnel::CredentialStore::keyForConfigPath(path);

    // Stand in for "the user's real password already stored for this config".
    QVERIFY(freetunnel::CredentialStore::storePassword(key, QStringLiteral("the-users-secret")));
    QCOMPARE(freetunnel::CredentialStore::loadPassword(key), QStringLiteral("the-users-secret"));

    QVERIFY(backend.confirmDeepLinkImport(passwordlessLink(), /*replaceExisting=*/true));

    const QString after = freetunnel::CredentialStore::loadPassword(key);
    QVERIFY2(after != QStringLiteral("the-users-secret"),
             "replacing a config kept the previous password for the new server");

    freetunnel::CredentialStore::deletePassword(key);
}

// A link names its config, at any length, and the name is the file's name. One
// past the file system's 255 bytes, as 130 Cyrillic letters are, failed to
// import with "Could not write config". It is cut to the limit instead.
void TestBackendDeepLinkImport::aLinkWithAVeryLongNameImports()
{
    freetunnel::DeepLinkConfig c;
    c.name = QString(130, QChar(0x0416));
    c.hostname = QStringLiteral("long-name.example.com");
    c.addresses = {QStringLiteral("203.0.113.11:443")};
    c.username = QStringLiteral("user");
    c.password = QStringLiteral("pass");

    Backend backend;
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(backend.confirmDeepLinkImport(freetunnel::encodeDeepLink(c)));
    QCOMPARE(failed.count(), 0);
    QVERIFY(backend.configs().contains(c.name.left(freetunnel::kMaxConfigNameLength)));
}

// A name past the limit was taken whole before it was cut, and a file system
// takes 60 Latin letters easily, so a config imported then has its whole name.
// The same link sent again looked only for the cut name, found nothing, and
// offered to add a second copy beside it instead of replacing it.
void TestBackendDeepLinkImport::aLinkSentAgainFindsTheConfigItNamedWhole()
{
    freetunnel::DeepLinkConfig c;
    c.name = QStringLiteral("Frankfurt ").repeated(6).trimmed(); // 59 characters
    c.hostname = QStringLiteral("whole-name.example.com");
    c.addresses = {QStringLiteral("203.0.113.12:443")};
    c.username = QStringLiteral("user");
    c.password = QStringLiteral("pass");
    const QString link = freetunnel::encodeDeepLink(c);
    {
        Backend first;
        QVERIFY(first.confirmDeepLinkImport(link));
    }
    // Put it where 1.2.2 would have.
    const QString made = importedConfigPath(QStringLiteral("whole-name.example.com"));
    QVERIFY(!made.isEmpty());
    const QString whole = QFileInfo(made).dir().filePath(c.name + QStringLiteral(".toml"));
    QVERIFY(QFile::rename(made, whole));
    QStringList stored = loadStoredConfigs();
    stored.replace(stored.indexOf(made), whole);
    // And another config, named with the 50 characters the link's name is cut to:
    // the question cannot show the two apart, so the link must not land on it.
    const auto prepared = freetunnel::prepareDeepLinkImport(link, nullptr);
    QVERIFY(prepared && !prepared->unclippedFileName.isEmpty());
    const QString clipped = QFileInfo(made).dir().filePath(prepared->fileName);
    QVERIFY(!QFileInfo::exists(clipped));
    const QByteArray otherBody("[endpoint]\n"
                               "hostname = \"another.example.com\"\n"
                               "addresses = [\"203.0.113.13:443\"]\n"
                               "username = \"other\"\n");
    {
        QFile other(clipped);
        QVERIFY(other.open(QIODevice::WriteOnly));
        other.write(otherBody);
    }
    stored.append(clipped);
    saveStoredConfigs(stored);
    const auto cleanUp = qScopeGuard([&] {
        for (const QString &path : {whole, clipped}) {
            freetunnel::CredentialStore::deletePassword(
                    freetunnel::CredentialStore::keyForConfigPath(path));
            QFile::remove(path);
        }
    });

    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importDeepLink(link));
    QCOMPARE(confirmSpy.count(), 1);
    QCOMPARE(confirmSpy.first().at(2).toString(), c.name);

    // And Replace lands on it, under the name it has, and leaves the other alone.
    const int before = backend.configs().size();
    QVERIFY(backend.confirmDeepLinkImport(link, /*replaceExisting=*/true));
    QCOMPARE(backend.configs().size(), before);
    QVERIFY(backend.configs().contains(c.name));
    QFile other(clipped);
    QVERIFY(other.open(QIODevice::ReadOnly));
    QCOMPARE(other.readAll(), otherBody);
}

QTEST_MAIN(TestBackendDeepLinkImport)
#include "test_backend_deeplink_import.moc"
