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
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "certificate_vectors.h"
#include "deeplink_v2_vectors.h"
#include "endpoint_export_vectors.h"

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

    // TrustTunnel 1.3: version 2 links and the configs its endpoint exports.
    void aVersion2LinkFromTheEndpointImports();
    void aSubscriptionLinkSaysItsServerWontUpdateItself();
    void aSubscriptionOnlyLinkIsRefusedWithItsReason();
    void aServerThatNeedsAnAuthKeyIsRefusedWithItsReason();
    void aLinkTooNewIsStillALinkError();
    void importFileRefusesAServerThatNeedsAnAuthKey();
    void importFileTakesAnEmptyAuthKey();
    void importFileLeavesTheSubscriptionOut();

    // A certificate keeps only its certificates.
    void aLinkWithAKeyInItsCertificateSaysSoInTheQuestion();
    void aLinkWithNoCertificateInItsCertificateIsALinkError();
    void importFileDropsAKeyFromTheCertificate();
    void importFileRefusesACertificateThatHoldsNoCertificate();
    void importFileTakesABlankCertificate();
    void importFileKeepsAChainInOneBlock();

    // The TOML file an endpoint exports.
    void importFileAsksBeforeAddingAnEndpointExport();
    void anEndpointExportWithTheNameOfAConfigOffersReplace();
    void anEndpointExportWithoutANameIsNamedAfterItsFile();
    void anEndpointExportThatSkipsCertificateChecksSaysSo();
    void importFileRefusesAnEndpointExportThatNeedsAnAuthKey();
    void importFileRefusesAnEndpointExportThatIsOnlyASubscription();
    void importFileRefusesAnEndpointExportWithAMalformedClientRandom();
    void importFileRefusesAnEndpointExportWithoutAServer();
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

// ---- TrustTunnel 1.3 ----------------------------------------------------------

static QString readConfigText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

// The config an import just added, by the name configImported gave it.
static QString pathOfImported(const QSignalSpy &imported)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(base).filePath(imported.last().at(0).toString() + QStringLiteral(".toml"));
}

// Every link the endpoint exports from 1.3.0 on is version 2, and every one was
// refused with "unsupported deep link version 2".
void TestBackendDeepLinkImport::aVersion2LinkFromTheEndpointImports()
{
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importDeepLink(v2::kPlain));
    QCOMPARE(failed.count(), 0);
    QCOMPARE(confirmSpy.count(), 1);
    const QString question = confirmSpy.last().at(0).toString();
    QVERIFY2(question.contains(QStringLiteral("Server: vpn.example.com")), qPrintable(question));
    // Nothing about subscriptions for a link without one.
    QVERIFY2(!question.contains(freetunnel::subscriptionNotKeptNote()), qPrintable(question));

    QSignalSpy imported(&backend, &Backend::configImported);
    QVERIFY(backend.confirmDeepLinkImport(v2::kPlain));
    QCOMPARE(imported.count(), 1);
    QCOMPARE(failed.count(), 0);
}

// A link with its server and a subscription address imports the server, and the
// question says, in one line, that it will not follow the subscription. The
// address, VPN password and all, is not written anywhere.
void TestBackendDeepLinkImport::aSubscriptionLinkSaysItsServerWontUpdateItself()
{
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importDeepLink(v2::kFull));
    QCOMPARE(confirmSpy.count(), 1);
    const QStringList lines = confirmSpy.last().at(0).toString().split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(freetunnel::subscriptionNotKeptNote()), qPrintable(lines.join('|')));
    QVERIFY2(lines.contains(QStringLiteral("Server: vpn.example.com")), qPrintable(lines.join('|')));
    QVERIFY2(!lines.join('\n').contains(QStringLiteral("https://")), qPrintable(lines.join('|')));

    QSignalSpy imported(&backend, &Backend::configImported);
    QVERIFY(backend.confirmDeepLinkImport(v2::kFull));
    QCOMPARE(imported.count(), 1);
    const QString path = pathOfImported(imported);
    const QString onDisk = readConfigText(path);
    QVERIFY2(onDisk.contains(QStringLiteral("hostname = \"vpn.example.com\"")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("subscription")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("s3cret")), qPrintable(onDisk)); // in the address and the password
    // Nor in the credential store, which holds the password alone.
    QCOMPARE(freetunnel::CredentialStore::loadPassword(freetunnel::CredentialStore::keyForConfigPath(path)),
             QStringLiteral("s3cret"));
}

// A subscription-only link names no server until the subscription is fetched,
// which this version cannot do. It is refused, by name, before anything is asked;
// the reason is a sentence of its own, not a "Link error:" about a broken link.
void TestBackendDeepLinkImport::aSubscriptionOnlyLinkIsRefusedWithItsReason()
{
    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importDeepLink(v2::kSubscriptionOnly));
    QCOMPARE(confirmSpy.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), freetunnel::subscriptionOnlyRefusal());

    // The confirm step re-reads the link and refuses it the same way.
    QVERIFY(!backend.confirmDeepLinkImport(v2::kSubscriptionOnly));
    QCOMPARE(failed.count(), 2);
    QCOMPARE(failed.last().at(0).toString(), freetunnel::subscriptionOnlyRefusal());
    QCOMPARE(backend.configs().size(), before);
}

void TestBackendDeepLinkImport::aServerThatNeedsAnAuthKeyIsRefusedWithItsReason()
{
    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importDeepLink(v2::kAuthKey));
    QCOMPARE(confirmSpy.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), freetunnel::newerCoreRefusal());
    QVERIFY(!backend.confirmDeepLinkImport(v2::kAuthKey));
    QCOMPARE(failed.last().at(0).toString(), freetunnel::newerCoreRefusal());
    QCOMPARE(backend.configs().size(), before);
}

// A version this client does not read is still said to be a link problem.
void TestBackendDeepLinkImport::aLinkTooNewIsStillALinkError()
{
    Backend backend;
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importDeepLink(v2::kVersion3));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), QStringLiteral("Link error: unsupported deep link version 3"));
}

// A config file for a server that checks the auth key would import and then fail
// every connect, turned away at the handshake with nothing to say why.
void TestBackendDeepLinkImport::importFileRefusesAServerThatNeedsAnAuthKey()
{
    QByteArray body = validImportBody();
    body.append("client_random_auth_key = \"aabbccdd\"\n");
    const QString path = writeTempConfig(QStringLiteral("auth-key.toml"), body);
    QVERIFY(!path.isEmpty());

    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), freetunnel::newerCoreRefusal());
    QCOMPARE(backend.configs().size(), before);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QVERIFY(!QFileInfo::exists(QDir(base).filePath(QStringLiteral("auth-key.toml"))));
    QFile::remove(path);
}

// The endpoint's export always writes the key, empty for a server without one.
void TestBackendDeepLinkImport::importFileTakesAnEmptyAuthKey()
{
    QByteArray body = validImportBody();
    body.append("client_random_auth_key = \"\"\n");
    const QString path = writeTempConfig(QStringLiteral("empty-auth-key.toml"), body);
    QVERIFY(!path.isEmpty());

    Backend backend;
    QSignalSpy imported(&backend, &Backend::configImported);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(backend.importFile(path));
    QCOMPARE(imported.count(), 1);
    QCOMPARE(failed.count(), 0);
    // Kept as written, like any key this app does not know.
    const QString onDisk = readConfigText(pathOfImported(imported));
    QVERIFY2(onDisk.contains(QStringLiteral("client_random_auth_key = \"\"")), qPrintable(onDisk));
    QFile::remove(path);
}

// A TrustTunnel 1.3 config can carry its subscription address in two places, and
// the address holds the VPN password in plain text. The import moved the password
// itself to the credential store and kept the address on disk as written. It is
// left out of the copy now, and the notice that replaces "Config added" says so.
void TestBackendDeepLinkImport::importFileLeavesTheSubscriptionOut()
{
    const auto body = [](bool password, bool skipVerification) {
        QByteArray b("subscription_url = \"https://alice:s3cret@sub.example.com/subscription\"\n"
                     "\n[endpoint]\n"
                     "hostname = \"sub.example.com\"\n"
                     "addresses = [\"203.0.113.20:443\"]\n"
                     "username = \"alice\"\n");
        if (password)
            b.append("password = \"s3cret\"\n");
        if (skipVerification)
            b.append("skip_verification = true\n");
        b.append("provider_tag = \"gold\"\n"
                 "\n[endpoint.subscription]\n"
                 "url = \"https://alice:s3cret@sub.example.com/subscription\"\n"
                 "\n[provider]\n"
                 "port = 1080\n");
        return b;
    };
    // With a password, which the import then moves to the credential store and
    // rewrites the file for; and without, where the file is written as it came.
    for (const bool password : {true, false}) {
        const QString name = password ? QStringLiteral("sub-with-password") : QStringLiteral("sub-without-password");
        const QString path = writeTempConfig(name + QStringLiteral(".toml"), body(password, false));
        QVERIFY(!path.isEmpty());

        Backend backend;
        QSignalSpy imported(&backend, &Backend::configImported);
        QSignalSpy notices(&backend, &Backend::errorOccurred);
        QVERIFY(backend.importFile(path));
        QCOMPARE(imported.count(), 1);
        const QString added = imported.last().at(0).toString();
        QCOMPARE(notices.count(), 1);
        QCOMPARE(notices.last().at(0).toString(),
                 QStringLiteral("Config added: %1. ").arg(added) + freetunnel::subscriptionNotKeptNote());

        const QString onDisk = readConfigText(pathOfImported(imported));
        QVERIFY2(!onDisk.contains(QStringLiteral("subscription")), qPrintable(onDisk));
        QVERIFY2(!onDisk.contains(QStringLiteral("s3cret")), qPrintable(onDisk));
        // What else the file had is still there.
        QVERIFY2(onDisk.contains(QStringLiteral("hostname = \"sub.example.com\"")), qPrintable(onDisk));
        QVERIFY2(onDisk.contains(QStringLiteral("provider_tag = \"gold\"")), qPrintable(onDisk));
        QVERIFY2(onDisk.contains(QStringLiteral("[provider]")), qPrintable(onDisk));
        // And the file the user picked is theirs, untouched.
        QCOMPARE(readConfigText(path).toUtf8(), body(password, false));
        QFile::remove(path);
    }

    // With certificate checks off as well: one notice, saying both.
    const QString path = writeTempConfig(QStringLiteral("sub-unchecked.toml"), body(true, true));
    QVERIFY(!path.isEmpty());
    Backend backend;
    QSignalSpy imported(&backend, &Backend::configImported);
    QSignalSpy notices(&backend, &Backend::errorOccurred);
    QVERIFY(backend.importFile(path));
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).toString(),
             QStringLiteral("Config added: %1. It turns off server certificate verification. ")
                             .arg(imported.last().at(0).toString())
                     + freetunnel::subscriptionNotKeptNote());
    QFile::remove(path);
}

// A link with a whole server and the given certificate field (tag 0x08).
static QString linkWithCertificate(const QByteArray &certificate)
{
    return v2::linkFromRecords({{0x00, QByteArray(1, 1)}, {0x01, "keyed.fixture.invalid"},
                                {0x02, "203.0.113.30:443"}, {0x05, "u"}, {0x06, "p"},
                                {0x0C, "Keyed"}, {0x08, certificate}});
}

// A link whose certificate has a key beside it says, before anything is added,
// that something was left out; what is added has the certificate alone.
void TestBackendDeepLinkImport::aLinkWithAKeyInItsCertificateSaysSoInTheQuestion()
{
    const QByteArray leaf = certs::derOf(certs::kLeafPem);
    const QString link = linkWithCertificate(leaf + certs::kPkcs8Der);
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importDeepLink(link));
    QCOMPARE(confirmSpy.count(), 1);
    const QStringList lines = confirmSpy.last().at(0).toString().split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(freetunnel::certificateExtraIgnoredNote()), qPrintable(lines.join('|')));
    QVERIFY2(lines.contains(QStringLiteral("Server: keyed.fixture.invalid")), qPrintable(lines.join('|')));

    QSignalSpy imported(&backend, &Backend::configImported);
    QVERIFY(backend.confirmDeepLinkImport(link));
    QCOMPARE(imported.count(), 1);
    const QString onDisk = readConfigText(pathOfImported(imported));
    QCOMPARE(onDisk.count(QStringLiteral("-----BEGIN CERTIFICATE-----")), 1);
    QVERIFY2(!onDisk.contains(certs::keyLabel()), qPrintable(onDisk));
    QCOMPARE(freetunnel::certificatesDer(freetunnel::parseConfigToml(onDisk).certificate), leaf);

    // A link with the certificate alone is told nothing about it.
    QVERIFY(!backend.importDeepLink(linkWithCertificate(leaf)));
    QVERIFY2(!confirmSpy.last().at(0).toString().contains(freetunnel::certificateExtraIgnoredNote()),
             qPrintable(confirmSpy.last().at(0).toString()));
}

// Nothing to pin with: refused before anything is asked, as a damaged link is.
void TestBackendDeepLinkImport::aLinkWithNoCertificateInItsCertificateIsALinkError()
{
    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importDeepLink(linkWithCertificate(certs::kPkcs8Der)));
    QCOMPARE(confirmSpy.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), QStringLiteral("Link error: malformed certificate"));
    QVERIFY(!backend.confirmDeepLinkImport(linkWithCertificate(certs::kPkcs8Der)));
    QCOMPARE(failed.last().at(0).toString(), QStringLiteral("Link error: malformed certificate"));
    QCOMPARE(backend.configs().size(), before);
}

// The certificate of a file that pins its server with a combined file: the key
// goes, and nothing else in the file changes. With a password in the file, which
// the import then moves to the credential store and rewrites the file for; and
// without, where the file is written as it came, but for the certificate. And
// with a subscription address beside it, as an endpoint's export pasted under
// [endpoint] has, which goes too, and both are said.
void TestBackendDeepLinkImport::importFileDropsAKeyFromTheCertificate()
{
    const auto body = [](bool password, bool subscription) {
        QByteArray b("# from the server's admin\n"
                     "[endpoint]\n"
                     "hostname = \"keyed-file.fixture.invalid\"\n"
                     "addresses = [\"203.0.113.31:443\"]\n"
                     "username = \"u\"\n");
        if (password)
            b.append("password = \"p\"\n");
        if (subscription)
            b.append("subscription_url = \"https://u:s3cret@keyed-file.fixture.invalid/subscription\"\n");
        b.append("certificate = \"\"\"\n");
        b.append((certs::kLeafPem + certs::keyPem()).toLatin1());
        b.append("\"\"\"\n"
                 "provider_tag = \"gold\"\n"
                 "\n[provider]\n"
                 "port = 1080\n");
        return b;
    };
    for (const bool subscription : {false, true}) {
        for (const bool password : {true, false}) {
            const QString name = QStringLiteral("keyed-%1-%2")
                                         .arg(password ? QStringLiteral("with-password") : QStringLiteral("without"),
                                              subscription ? QStringLiteral("sub") : QStringLiteral("nosub"));
            const QString path = writeTempConfig(name + QStringLiteral(".toml"), body(password, subscription));
            QVERIFY(!path.isEmpty());

            Backend backend;
            QSignalSpy imported(&backend, &Backend::configImported);
            QSignalSpy notices(&backend, &Backend::errorOccurred);
            QVERIFY(backend.importFile(path));
            QCOMPARE(imported.count(), 1);
            QCOMPARE(notices.count(), 1);
            // Both said, in the order a link's question says them.
            QString notes = freetunnel::certificateExtraIgnoredNote();
            if (subscription)
                notes.prepend(freetunnel::subscriptionNotKeptNote() + QLatin1Char(' '));
            QCOMPARE(notices.last().at(0).toString(),
                     QStringLiteral("Config added: %1. ").arg(imported.last().at(0).toString()) + notes);

            const QString onDisk = readConfigText(pathOfImported(imported));
            QVERIFY2(!onDisk.contains(certs::keyLabel()), qPrintable(onDisk));
            QVERIFY2(!onDisk.contains(QStringLiteral("s3cret@")), qPrintable(onDisk));
            QCOMPARE(freetunnel::parseConfigToml(onDisk).certificate, certs::kLeafPem.trimmed());
            // What else the file had is still there.
            QVERIFY2(onDisk.contains(QStringLiteral("hostname = \"keyed-file.fixture.invalid\"")),
                     qPrintable(onDisk));
            QVERIFY2(onDisk.contains(QStringLiteral("provider_tag = \"gold\"")), qPrintable(onDisk));
            QVERIFY2(onDisk.contains(QStringLiteral("[provider]")), qPrintable(onDisk));
            // Written as it came, comment and all, when there was no password to move.
            if (!password)
                QVERIFY2(onDisk.startsWith(QStringLiteral("# from the server's admin\n")), qPrintable(onDisk));
            // And the file the user picked is theirs, untouched.
            QCOMPARE(readConfigText(path).toUtf8(), body(password, subscription));
            QFile::remove(path);
        }
    }
}

// A file whose certificate is a key alone, or anything else that holds no
// certificate, meant to pin its server and cannot. It is refused, as a link is.
void TestBackendDeepLinkImport::importFileRefusesACertificateThatHoldsNoCertificate()
{
    QByteArray body = validImportBody();
    body.append("certificate = \"\"\"\n");
    body.append(certs::keyPem().toLatin1());
    body.append("\"\"\"\n");
    const QString path = writeTempConfig(QStringLiteral("key-only.toml"), body);
    QVERIFY(!path.isEmpty());

    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), QStringLiteral("Not a valid TrustTunnel config: malformed certificate"));
    QCOMPARE(backend.configs().size(), before);
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QVERIFY(!QFileInfo::exists(QDir(base).filePath(QStringLiteral("key-only.toml"))));
    QFile::remove(path);
}

// A certificate that is blank pins nothing, as an empty one does, and is no
// reason to refuse the file, nor anything to say.
void TestBackendDeepLinkImport::importFileTakesABlankCertificate()
{
    const QList<QByteArray> blanks{"certificate = \" \"\n", "certificate = \"\"\"\n\n\n\"\"\"\n",
                                   "certificate = '\t'\n"};
    for (qsizetype i = 0; i < blanks.size(); ++i) {
        const QString path =
                writeTempConfig(QStringLiteral("blank-certificate-%1.toml").arg(i), validImportBody() + blanks.at(i));
        QVERIFY(!path.isEmpty());
        Backend backend;
        QSignalSpy imported(&backend, &Backend::configImported);
        QSignalSpy notices(&backend, &Backend::errorOccurred);
        QVERIFY2(backend.importFile(path), blanks.at(i).constData());
        QCOMPARE(imported.count(), 1);
        QCOMPARE(notices.count(), 0);
        QFile::remove(path);
    }
}

// FreeTunnel 1.1.9 and earlier wrote a link's chain into one block, and so did
// its export of such a config. That block holds nothing but certificates: the
// file imports, and the copy keeps it as it is.
void TestBackendDeepLinkImport::importFileKeepsAChainInOneBlock()
{
    const QByteArray chain = certs::derOf(certs::kLeafPem) + certs::derOf(certs::kCaPem);
    const QString oneBlock = certs::pemBlock(QStringLiteral("CERTIFICATE"), chain);
    QByteArray body = validImportBody();
    body.append("certificate = \"\"\"\n");
    body.append(oneBlock.toLatin1());
    body.append("\"\"\"\n");
    const QString path = writeTempConfig(QStringLiteral("one-block.toml"), body);
    QVERIFY(!path.isEmpty());

    Backend backend;
    QSignalSpy imported(&backend, &Backend::configImported);
    QSignalSpy notices(&backend, &Backend::errorOccurred);
    QVERIFY(backend.importFile(path));
    QCOMPARE(imported.count(), 1);
    QCOMPARE(notices.count(), 0);
    const QString onDisk = readConfigText(pathOfImported(imported));
    QCOMPARE(freetunnel::parseConfigToml(onDisk).certificate, oneBlock.trimmed());
    QCOMPARE(freetunnel::certificatesDer(freetunnel::parseConfigToml(onDisk).certificate), chain);
    QFile::remove(path);
}

// ---- the endpoint's export ----------------------------------------------------

static QString writeExport(const QString &fileName, const endpoint_export::Fields &f)
{
    return writeTempConfig(fileName, endpoint_export::text(f).toUtf8());
}

// The TOML file a TrustTunnel endpoint exports has no [endpoint], and was refused
// as "Not a valid TrustTunnel config". It is asked about as a link is, with the
// file's words, since whoever sent it chose the server; nothing is added until
// the user says yes, and then the config is the one the link with its server
// gives. The subscription address and the key beside the certificate are left
// out, and the question says both.
void TestBackendDeepLinkImport::importFileAsksBeforeAddingAnEndpointExport()
{
    endpoint_export::Fields f;
    f.certificate = endpoint_export::multiline(certs::kLeafPem + certs::keyPem());
    const QString path = writeExport(QStringLiteral("endpoint-export.toml"), f);
    QVERIFY(!path.isEmpty());

    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy imported(&backend, &Backend::configImported);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(failed.count(), 0);
    QCOMPARE(imported.count(), 0);
    QCOMPARE(backend.configs().size(), before);
    QCOMPARE(confirmSpy.count(), 1);
    const QString question = confirmSpy.last().at(0).toString();
    const QStringList lines = question.split(QLatin1Char('\n'));
    QCOMPARE(lines.first(), QStringLiteral("Add a VPN server from this file?"));
    QVERIFY2(lines.contains(QStringLiteral("Server: vpn.example.com")), qPrintable(question));
    QVERIFY2(lines.contains(freetunnel::subscriptionNotKeptNote()), qPrintable(question));
    QVERIFY2(lines.contains(freetunnel::certificateExtraIgnoredNote()), qPrintable(question));
    QVERIFY2(!question.contains(QStringLiteral("link")), qPrintable(question));
    QVERIFY(confirmSpy.last().at(2).toString().isEmpty()); // no config of that name yet

    // Answered with a link, as a link's question is.
    const QString link = confirmSpy.last().at(1).toString();
    QVERIFY2(link.startsWith(QStringLiteral("tt://?")), qPrintable(link));
    QVERIFY(backend.confirmDeepLinkImport(link));
    QCOMPARE(imported.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(imported.last().at(0).toString(), QStringLiteral("Work VPN"));
    const QString stored = pathOfImported(imported);
    const QString onDisk = readConfigText(stored);
    QVERIFY2(onDisk.contains(QStringLiteral("hostname = \"vpn.example.com\"")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(certs::keyLabel()), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("subscription")), qPrintable(onDisk));
    QVERIFY2(!onDisk.contains(QStringLiteral("s3cret")), qPrintable(onDisk));
    QCOMPARE(freetunnel::certificatesDer(freetunnel::parseConfigToml(onDisk).certificate),
             certs::derOf(certs::kLeafPem));
    QCOMPARE(freetunnel::CredentialStore::loadPassword(freetunnel::CredentialStore::keyForConfigPath(stored)),
             QStringLiteral("s3cret"));
    // And the file the user picked is theirs, untouched.
    QCOMPARE(readConfigText(path), endpoint_export::text(f));
    QFile::remove(path);
}

// The export names its config, and the name can be one the user has already:
// the question offers to replace it, as a link's does.
void TestBackendDeepLinkImport::anEndpointExportWithTheNameOfAConfigOffersReplace()
{
    endpoint_export::Fields f;
    f.name = QStringLiteral("Export Replace");
    f.hostname = QStringLiteral("replace.example.com");
    const QString path = writeExport(QStringLiteral("replace-me.toml"), f);
    QVERIFY(!path.isEmpty());

    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(confirmSpy.count(), 1);
    QVERIFY(backend.confirmDeepLinkImport(confirmSpy.last().at(1).toString()));
    const int afterFirst = backend.configs().size();

    QVERIFY(!backend.importFile(path));
    QCOMPARE(confirmSpy.count(), 2);
    QCOMPARE(confirmSpy.last().at(2).toString(), QStringLiteral("Export Replace"));
    const QString question = confirmSpy.last().at(0).toString();
    QVERIFY2(question.startsWith(QStringLiteral("“Export Replace” already exists.")), qPrintable(question));
    QVERIFY2(!question.contains(QStringLiteral("Add a VPN server")), qPrintable(question));
    QVERIFY(backend.confirmDeepLinkImport(confirmSpy.last().at(1).toString(), /*replaceExisting=*/true));
    QCOMPARE(backend.configs().size(), afterFirst);
    QFile::remove(path);
}

// The endpoint writes `name = ""` for a server it was given no name for, and the
// config is then named after the file, as any other config file is.
void TestBackendDeepLinkImport::anEndpointExportWithoutANameIsNamedAfterItsFile()
{
    endpoint_export::Fields f;
    f.name.clear();
    f.hostname = QStringLiteral("unnamed.example.com");
    const QString path = writeExport(QStringLiteral("branch-office.toml"), f);
    QVERIFY(!path.isEmpty());

    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy imported(&backend, &Backend::configImported);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(confirmSpy.count(), 1);
    QVERIFY(backend.confirmDeepLinkImport(confirmSpy.last().at(1).toString()));
    QCOMPARE(imported.count(), 1);
    QCOMPARE(imported.last().at(0).toString(), QStringLiteral("branch-office"));
    QFile::remove(path);
}

void TestBackendDeepLinkImport::anEndpointExportThatSkipsCertificateChecksSaysSo()
{
    endpoint_export::Fields f;
    f.name = QStringLiteral("Unchecked export");
    f.skipVerification = true;
    f.subscriptionUrl.clear();
    const QString path = writeExport(QStringLiteral("unchecked-export.toml"), f);
    QVERIFY(!path.isEmpty());

    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(confirmSpy.count(), 1);
    const QStringList lines = confirmSpy.last().at(0).toString().split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(QStringLiteral("This file turns off server certificate verification.")),
             qPrintable(lines.join('|')));
    QVERIFY2(!lines.contains(freetunnel::subscriptionNotKeptNote()), qPrintable(lines.join('|')));
    QVERIFY2(!lines.contains(freetunnel::certificateExtraIgnoredNote()), qPrintable(lines.join('|')));
    QFile::remove(path);
}

// What importFile() says about an export it refuses, having asked nothing and
// added nothing; empty when it did either.
static QString refusalOf(const QString &fileName, const endpoint_export::Fields &f)
{
    const QString path = writeExport(fileName, f);
    if (path.isEmpty())
        return QString();
    Backend backend;
    const int before = backend.configs().size();
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    const bool added = backend.importFile(path);
    QFile::remove(path);
    if (added || confirmSpy.count() != 0 || failed.count() != 1 || backend.configs().size() != before)
        return QString();
    return failed.last().at(0).toString();
}

// The refusals a link meets, each with its own sentence.
void TestBackendDeepLinkImport::importFileRefusesAnEndpointExportThatNeedsAnAuthKey()
{
    endpoint_export::Fields f;
    f.clientRandomAuthKey = QStringLiteral("aabbccdd");
    QCOMPARE(refusalOf(QStringLiteral("export-auth-key.toml"), f), freetunnel::newerCoreRefusal());
}

void TestBackendDeepLinkImport::importFileRefusesAnEndpointExportThatIsOnlyASubscription()
{
    endpoint_export::Fields f;
    f.password.clear();
    QCOMPARE(refusalOf(QStringLiteral("export-subscription-only.toml"), f), freetunnel::subscriptionOnlyRefusal());
}

// Damage is said to be about the config, as a link's is said to be about the
// link.
void TestBackendDeepLinkImport::importFileRefusesAnEndpointExportWithAMalformedClientRandom()
{
    endpoint_export::Fields f;
    f.clientRandomPrefix = QStringLiteral("abc");
    QCOMPARE(refusalOf(QStringLiteral("export-client-random.toml"), f),
             QStringLiteral("Not a valid TrustTunnel config: malformed client_random value"));
}

void TestBackendDeepLinkImport::importFileRefusesAnEndpointExportWithoutAServer()
{
    endpoint_export::Fields f;
    f.addresses.clear();
    f.subscriptionUrl.clear();
    QCOMPARE(refusalOf(QStringLiteral("export-no-address.toml"), f),
             QStringLiteral("Not a valid TrustTunnel config: missing address or credentials"));
    // Refused before the question, not after it: an empty address is no address,
    // and the link the answer would have been given with has none.
    f.addresses = QStringLiteral("\"\", \" \"");
    QCOMPARE(refusalOf(QStringLiteral("export-empty-addresses.toml"), f),
             QStringLiteral("Not a valid TrustTunnel config: missing address or credentials"));

    // The server's keys at the top with a table besides is no export, nor a
    // config the core can read, and is refused as before.
    const QString path = writeTempConfig(QStringLiteral("export-and-a-table.toml"),
                                         (endpoint_export::text() + QStringLiteral("\n[listener.tun]\n")).toUtf8());
    QVERIFY(!path.isEmpty());
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    QVERIFY(!backend.importFile(path));
    QCOMPARE(confirmSpy.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(),
             QStringLiteral("Not a valid TrustTunnel config (missing address or credentials)"));
    QFile::remove(path);
}

QTEST_MAIN(TestBackendDeepLinkImport)
#include "test_backend_deeplink_import.moc"
