// cppcheck-suppress-file missingIncludeSystem
// TrustTunnel subscriptions in Backend, behind experimental/subscriptions: the
// consent before any fetch, linking a config, what a refresh does to it, and the
// link kept through what happens to the config. Fetches go to a stand-in
// (fake_subscription_fetcher.h) but for one, which goes to a TLS server of the
// test's own. The clock and the jitter are the test's, and the scheduler checks
// only when the test says.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <atomic>
#include <memory>

#include "app/Backend.h"
#include "app/BackendConfigShared.h"
#include "app/SubscriptionController.h"
#include "core/AppSettings.h"
#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "core/SubscriptionStore.h"
#include "certificate_vectors.h"
#include "deeplink_v2_vectors.h"
#include "endpoint_export_vectors.h"
#include "fake_subscription_fetcher.h"
#if defined(FT_TEST_OPENSSL_CLI)
#include "tls_test_certs.h"
#include "tls_test_server.h"
#endif

using freetunnel::CredentialStore;
using freetunnel::SubscriptionFetchError;
using freetunnel::SubscriptionRecord;

Q_DECLARE_METATYPE(freetunnel::SubscriptionFetchError)

namespace {

constexpr qint64 kStart = 1'900'000'000; // the test's clock starts here
constexpr qint64 kDay = 24 * 60 * 60;
constexpr qint64 kCheck = 10 * 60;

QString appConfigDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString readText(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString passwordOf(const QString &path)
{
    return CredentialStore::loadPassword(CredentialStore::keyForConfigPath(path));
}

// Every entry the tests leave in this run's credential service: the password of
// each config, and the address of each linked subscription. The service is the
// run's own, but on a machine with a keychain it is in the user's keychain.
void removeStoredCredentials()
{
    const QFileInfoList files = QDir(appConfigDir()).entryInfoList({QStringLiteral("*.toml")}, QDir::Files);
    for (const QFileInfo &file : files)
        CredentialStore::deletePassword(CredentialStore::keyForConfigPath(file.absoluteFilePath()));
    for (const SubscriptionRecord &record : freetunnel::loadSubscriptions())
        CredentialStore::deletePassword(CredentialStore::keyForSubscription(record.sourceId));
}

// A link with only a version 2 record and a subscription address.
QString subscriptionOnlyLink(const QString &url)
{
    return v2::linkFromRecords({{0x00, QByteArray(1, 2)}, {0x0e, url.toUtf8()}});
}

// A version 1 link with a server and no subscription.
QString staticLink(const QString &name, const QString &hostname)
{
    return v2::linkFromRecords({{0x00, QByteArray(1, 1)},
                                {0x01, hostname.toUtf8()},
                                {0x02, QByteArrayLiteral("198.51.100.4:443")},
                                {0x05, QByteArrayLiteral("bob")},
                                {0x06, QByteArrayLiteral("hunter2")},
                                {0x0c, name.toUtf8()}});
}

// A version 2 link with a server of its own, an auth key and a subscription
// address: what the endpoint exports from 1.3.0 on for a server that checks a key.
// The key is in the link alone; the subscription's document has no field for it.
QString keyedSubscriptionLink(const QString &key)
{
    return v2::linkFromRecords({{0x00, QByteArray(1, 2)},
                                {0x01, QByteArrayLiteral("vpn.example.com")},
                                {0x02, QByteArrayLiteral("203.0.113.7:443")},
                                {0x05, QByteArrayLiteral("alice")},
                                {0x06, QByteArrayLiteral("s3cret")},
                                {0x0c, QByteArrayLiteral("Keyed")},
                                {0x0e, v2::kSubscription.toUtf8()},
                                {0x0f, key.toUtf8()}});
}

QString authKeyOf(const QString &path)
{
    return CredentialStore::loadPassword(CredentialStore::keyForConfigAuthKey(path));
}

// The app's state for one Backend: the settings it starts with, the stand-in
// network, the clock and the jitter. Declared before the Backend, so they
// outlive it.
struct Rig {
    explicit Rig(bool enabled = true, bool fixedJitter = true)
    {
        AppSettings settings = loadAppSettings();
        settings.subscriptions_enabled = enabled;
        saveAppSettings(settings);
        backend = std::make_unique<Backend>();
        network.install(subs());
        subs()->setClockForTest([this]() { return clock; });
        if (fixedJitter)
            subs()->setRandomForTest([this](qint64) { return jitter; });
        subs()->stopSchedulerForTest();
    }
    SubscriptionController *subs() const { return backend->subscriptions(); }
    std::optional<SubscriptionRecord> record(const QString &path) const { return subs()->record(path); }
    QString pathOf(const QString &name) const { return backend->configPath(backend->configs().indexOf(name)); }

    FakeSubscriptionNetwork network;
    qint64 clock = kStart;
    qint64 jitter = 0;
    std::unique_ptr<Backend> backend;
};

// Through the whole flow, as the user would: the link, yes to the fetch, the
// fetched server, yes to adding it. The path of the config, or empty.
QString linkThroughImport(Rig &rig, const QString &link, const QByteArray &document)
{
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy imported(rig.backend.get(), &Backend::configImported);
    rig.backend->importDeepLink(link);
    if (consent.count() != 1 || !rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString())
            || !rig.network.answer(FakeSubscriptionNetwork::document(document)) || asked.count() != 1
            || !rig.backend->confirmDeepLinkImport(asked.last().at(1).toString(), false) || imported.count() != 1)
        return QString();
    return rig.pathOf(imported.last().at(0).toString());
}

// The next automatic refresh of `path`, made due and asked for.
bool refreshByItself(Rig &rig, const QString &path)
{
    const std::optional<SubscriptionRecord> r = rig.record(path);
    if (!r)
        return false;
    rig.clock = std::max(r->nextAttemptAt, r->lastAttemptAt + kCheck);
    const int before = rig.network.fetches();
    rig.subs()->runSchedulerTickForTest();
    return rig.network.fetches() == before + 1;
}

} // namespace

class TestBackendSubscription : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    // The setting, and adding a config from a subscription.
    void theSettingIsOffByDefault();
    void withTheSettingOffALinkIsTakenAsBefore();
    void aSubscriptionLinkAsksBeforeAnyFetch();
    void theConsentFetchesOnceAndAsksAboutTheFetchedServer();
    void aLinkedConfigKeepsItsAddressInTheCredentialStoreAlone();
    void cancellingOrWaitingTooLongAddsNothing();
    void aFailedFetchOfASubscriptionOnlyLinkAddsNothing();
    void aFailedFetchOfALinkWithItsServerOffersThatServer();
    void anInvalidDocumentIsAFailedFetch();
    void withoutSecureStorageASubscriptionIsRefused();
    void theSameSubscriptionWithAnotherPasswordOffersTheConfigItMade();
    void aSubscriptionAddressOnItsOwnAsksFirstToo();
    void anEndpointExportWithASubscriptionAsksBeforeAnyFetch();
    void aLinkRefusedForSomethingElseIsRefusedAsBefore();
    void aPinnedLinkFetchesFromItsOwnServer();
    void aSubscriptionThatNamesAnotherServerThanItsLinkSaysBoth();
    void anAddressTheStoreRefusesAddsNothing();
    void aLinkThatCannotBeSavedLeavesAPlainConfigAndNoAddress();
    void aSubscriptionThatCannotBeAddedLeavesTheServerGivenWithIt();
    void anotherSubscriptionsServerOfTheSameNameSaysWhatItReplaces();

    // The link, through what happens to its config.
    void aRenameKeepsTheLink();
    void aRenameTheListCannotRecordKeepsTheLinkWhereTheListHasIt();
    void removingTheConfigRemovesItsAddress();
    void detachingKeepsTheConfigAndDropsTheAddress();
    void replacingWithAnotherServerDetachesAndSaysSo();
    void sharingALinkedConfigLeavesTheSubscriptionOut();
    void anEditorSaveKeepsWhatTheSubscriptionOwns();
    void aKeyedLinkKeepsItsKeyThroughTheFetch();
    void aRefreshThatSaysNothingOfTheKeyLeavesItInTheStore();
    void aRefreshThatDoesSayItFollowsIt();
    void theEditorCanSetTheKeyOfALinkedConfig();
    void withTheSettingOffAnEditorSaveWritesTheForm();
    void linksWhoseConfigIsGoneAreRemovedAtStart();

    // Refreshing.
    void aConfigNotInUseIsUpdatedAndSaysSo();
    void anUnchangedReplyMovesOnlyTheFetchTime();
    void anotherServerIsHeldUntilAccepted();
    void anotherServerAcceptedAfterARestartIsFetchedAgain();
    void theOldServerBackAgainDropsTheQuestion();
    void anotherServerRefusedStopsAutomaticRefresh();
    void anUpdateWaitsWhileTheEditorHasTheConfigOpen();
    void aStoreThatRefusesLeavesTheConfigAsItWas();
    void anUpdateHeldForAConnectThatEndsBeforeTheHelperIsApplied();
    void aConfigUnlinkedWhileItsPasswordIsReadIsNotWritten();
    void aMissingAddressIsTriedAgainSoon();
    void aServerThatOffersNoUpdatesIsAskedOnlyByTheUser_data();
    void aServerThatOffersNoUpdatesIsAskedOnlyByTheUser();
    void failuresThatMayPassBackOffThenWaitTheInterval();
    void theJitterStaysWithinTenMinutes();
    void onlyWhatIsDueIsFetchedAndNoMoreThanTwoAtOnce();
    void noConfigIsFetchedByItselfTwiceInTenMinutes();
    void aCustomSniConfigIsNeverRefreshedByItself();
    void theSchedulerRunsOnlyWithTheSettingOn();
    void theChecksStartAfterTheJitterThenComeEveryTenMinutes();
    void aRemovalWhileItFetchesDropsTheResult();

    // With the real fetcher, against a TLS server of the test's own.
    void endToEndNothingIsFetchedBeforeTheConsent();
};

void TestBackendSubscription::initTestCase()
{
    // A credential service of this run's own: see test_backend_deeplink_import.
    qputenv("FT_TEST_CREDENTIAL_SERVICE",
            QStringLiteral("com.freetunnel.app.test.subscription.%1").arg(QCoreApplication::applicationPid()).toUtf8());
    // A store that is there and keeps what it is given: a subscription is never
    // linked without one, and this machine may have no keyring.
    qputenv("FT_TEST_KEYRING", "present");
}

void TestBackendSubscription::init()
{
    qputenv("FT_TEST_KEYRING", "present");
    removeStoredCredentials();
    QDir(appConfigDir()).removeRecursively();
    QDir().mkpath(appConfigDir());
    QSettings().clear();
    AppSettings settings;
    settings.hotkeys_enabled = false;
    settings.log_path = QDir(appConfigDir()).filePath(QStringLiteral("subscription-test.log"));
    saveAppSettings(settings);
}

void TestBackendSubscription::cleanupTestCase()
{
    qputenv("FT_TEST_KEYRING", "present");
    removeStoredCredentials();
    QDir(appConfigDir()).removeRecursively();
}

// ---- the setting, and adding a config ----------------------------------------

void TestBackendSubscription::theSettingIsOffByDefault()
{
    QSettings().clear();
    QVERIFY(!AppSettings().subscriptions_enabled);
    QVERIFY(!loadAppSettings().subscriptions_enabled);
    // And it is the hidden key, which nothing in the window writes.
    QSettings().setValue(QStringLiteral("experimental/subscriptions"), true);
    QVERIFY(loadAppSettings().subscriptions_enabled);
}

// Off, a subscription link is what it was in 1.2.4: refused when it has no
// server, the server added without the address when it has one.
void TestBackendSubscription::withTheSettingOffALinkIsTakenAsBefore()
{
    Rig rig(false);
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), freetunnel::subscriptionOnlyRefusal());
    rig.backend->importDeepLink(v2::kFull);
    QCOMPARE(asked.count(), 1);
    QVERIFY(asked.last().at(0).toString().contains(freetunnel::subscriptionNotKeptNote()));
    QCOMPARE(asked.last().at(1).toString(), v2::kFull);
    QVERIFY(rig.backend->confirmDeepLinkImport(v2::kFull));
    QCOMPARE(consent.count(), 0);
    QCOMPARE(rig.network.fetches(), 0);
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    QVERIFY(!rig.backend->beginSubscriptionImport(v2::kSubscription));
    QCOMPARE(consent.count(), 0);
}

// Whoever wrote the link chose the address: fetching it unasked would tell them
// the link was opened, when, and from where.
void TestBackendSubscription::aSubscriptionLinkAsksBeforeAnyFetch()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    QVERIFY(!rig.backend->importDeepLink(v2::kSubscriptionOnly));
    QCOMPARE(consent.count(), 1);
    QCOMPARE(rig.network.fetches(), 0);
    QCOMPARE(asked.count(), 0);
    QCOMPARE(failed.count(), 0);
    const QString token = consent.last().at(0).toString();
    QVERIFY(SubscriptionController::isImportToken(token));
    const QVariantMap details = consent.last().at(1).toMap();
    // The origin and path, which say where it fetches from; never the userinfo.
    QCOMPARE(details.value(QStringLiteral("origin")).toString(), QStringLiteral("https://vpn.example.com/subscription"));
    QCOMPARE(details.value(QStringLiteral("server")).toString(), QString());
    QVERIFY(!details.value(QStringLiteral("fromFile")).toBool());
    // The log says it waits, by the origin alone.
    QVERIFY2(rig.backend->logText().contains(
                     QStringLiteral("Subscription https://vpn.example.com: nothing is fetched from it until you agree")),
             qPrintable(rig.backend->logText()));
    QVERIFY(!rig.backend->logText().contains(QStringLiteral("s3cret")));

    // A link with its server says which server it gives.
    QVERIFY(!rig.backend->importDeepLink(v2::kFull));
    QCOMPARE(consent.count(), 2);
    QCOMPARE(consent.last().at(1).toMap().value(QStringLiteral("server")).toString(), QStringLiteral("vpn.example.com"));
    QCOMPARE(rig.network.fetches(), 0);
    // One import at a time: the first one's token is gone.
    QVERIFY(!rig.backend->fetchSubscriptionForImport(token));
    QCOMPARE(rig.network.fetches(), 0);
}

void TestBackendSubscription::theConsentFetchesOnceAndAsksAboutTheFetchedServer()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    const QString token = consent.last().at(0).toString();
    QVERIFY(rig.backend->fetchSubscriptionForImport(token));
    QVERIFY(!rig.backend->fetchSubscriptionForImport(token)); // one fetch for one consent
    QCOMPARE(rig.network.fetches(), 1);
    QCOMPARE(rig.network.requests().constFirst().url, v2::kSubscription);
    QVERIFY(!rig.network.requests().constFirst().pinned); // the link pins nothing

    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("fetched.example.com", "203.0.113.7:443"))));
    QCOMPARE(asked.count(), 1);
    const QStringList lines = asked.last().at(0).toString().split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(QStringLiteral("Server: fetched.example.com")), qPrintable(lines.join('|')));
    QVERIFY2(lines.contains(QStringLiteral("Updates from https://vpn.example.com")), qPrintable(lines.join('|')));
    // The answer comes back with the token, which is no link and holds nothing.
    QCOMPARE(asked.last().at(1).toString(), token);
    QVERIFY(!asked.last().at(0).toString().contains(QStringLiteral("s3cret")));
    QCOMPARE(asked.last().at(2).toString(), QString());

    QSignalSpy imported(rig.backend.get(), &Backend::configImported);
    QVERIFY(rig.backend->confirmDeepLinkImport(token, false));
    QCOMPARE(imported.count(), 1);
    const QString path = rig.pathOf(imported.last().at(0).toString());
    QVERIFY(rig.subs()->isLinked(path));
    const QString toml = readText(path);
    QVERIFY2(toml.contains(QStringLiteral("hostname = \"fetched.example.com\"")), qPrintable(toml));
    QVERIFY2(toml.contains(QStringLiteral("203.0.113.7:443")), qPrintable(toml));
    QCOMPARE(passwordOf(path), QStringLiteral("s3cret"));
    // The new config is the active one, as any import makes it when none was.
    QCOMPARE(rig.backend->activeIndex(), rig.backend->configIndex(path));
    // Answered once: the token is spent.
    QVERIFY(!rig.backend->confirmDeepLinkImport(token, false));
    QCOMPARE(imported.count(), 1);
}

void TestBackendSubscription::aLinkedConfigKeepsItsAddressInTheCredentialStoreAlone()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const std::optional<SubscriptionRecord> record = rig.record(path);
    QVERIFY(record);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(record->sourceId)), v2::kSubscription);
    QCOMPARE(record->origin, QStringLiteral("https://vpn.example.com"));
    QCOMPARE(record->lastStatus, QStringLiteral("ok"));
    QCOMPARE(record->lastFetchedAt, kStart);
    QCOMPARE(record->nextAttemptAt, kStart + kDay); // the jitter is 0 here
    QVERIFY(record->autoRefresh);
    // Nothing that holds the password is on disk: not the address, not the password.
    const QString toml = readText(path);
    QVERIFY2(!toml.contains(QStringLiteral("subscription")), qPrintable(toml));
    QVERIFY2(!toml.contains(QStringLiteral("s3cret")), qPrintable(toml));
    const QString stored = readText(freetunnel::subscriptionsPath());
    QVERIFY2(!stored.contains(QStringLiteral("s3cret")) && !stored.contains(QStringLiteral("alice")), qPrintable(stored));
    QVERIFY(!rig.backend->logText().contains(QStringLiteral("s3cret")));
    QVERIFY(!rig.backend->logText().contains(QStringLiteral("alice@")));
}

void TestBackendSubscription::cancellingOrWaitingTooLongAddsNothing()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    const QByteArray document = subscriptionDocument("vpn.example.com", "203.0.113.7:443");

    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QString token = consent.last().at(0).toString();
    QVERIFY(rig.backend->fetchSubscriptionForImport(token));
    rig.backend->cancelSubscriptionImport(token);
    QCOMPARE(rig.network.aborts(), 1);
    QVERIFY(!rig.network.answer(FakeSubscriptionNetwork::document(document)));
    QCOMPARE(asked.count(), 0);

    // Ten minutes from the question: the fetched server can no longer be added.
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    token = consent.last().at(0).toString();
    QVERIFY(rig.backend->fetchSubscriptionForImport(token));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(document)));
    QCOMPARE(asked.count(), 1);
    rig.clock += 10 * 60 + 1;
    QVERIFY(!rig.backend->confirmDeepLinkImport(token, false));
    QCOMPARE(failed.last().at(0).toString(), QStringLiteral("That import has expired. Start it again."));
    QCOMPARE(rig.backend->configs().size(), 0);
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());

    // Another link while one fetches: the new one takes its place, fetch and all.
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QCOMPARE(rig.network.waiting(), 1);
    rig.backend->importDeepLink(v2::kFull);
    QCOMPARE(rig.network.waiting(), 0);
    QCOMPARE(rig.network.aborts(), 2);
}

void TestBackendSubscription::aFailedFetchOfASubscriptionOnlyLinkAddsNothing()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    const QString token = consent.last().at(0).toString();
    QVERIFY(rig.backend->fetchSubscriptionForImport(token));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(SubscriptionFetchError::Http401, 401)));
    QCOMPARE(asked.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(),
             freetunnel::subscriptionFailureText(SubscriptionFetchError::Http401, freetunnel::SubscriptionDocError::Invalid));
    QVERIFY(!rig.backend->confirmSubscriptionImport(token, false, true));
    QCOMPARE(rig.backend->configs().size(), 0);
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
}

// The link's own server can be added instead, and is linked: it updates once
// the subscription answers, sooner than a day.
void TestBackendSubscription::aFailedFetchOfALinkWithItsServerOffersThatServer()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    rig.backend->importDeepLink(v2::kFull);
    const QString token = consent.last().at(0).toString();
    // Not before the fetch has failed.
    QVERIFY(!rig.backend->confirmSubscriptionImport(token, false, true));
    QVERIFY(rig.backend->fetchSubscriptionForImport(token));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(SubscriptionFetchError::Network)));
    QCOMPARE(asked.count(), 1);
    const QString message = asked.last().at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("Server: vpn.example.com")), qPrintable(message));
    const QStringList lines = message.split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(freetunnel::subscriptionFailureText(SubscriptionFetchError::Network,
                                                                freetunnel::SubscriptionDocError::Invalid)),
             qPrintable(message));
    QVERIFY2(lines.contains(QStringLiteral(
                     "The server given with the subscription can be added now, and updates from it later.")),
             qPrintable(message));
    // It is linked, so it is not said that it would not be.
    QVERIFY2(!message.contains(freetunnel::subscriptionNotKeptNote()), qPrintable(message));
    QCOMPARE(asked.last().at(1).toString(), token);

    QSignalSpy imported(rig.backend.get(), &Backend::configImported);
    QVERIFY(rig.backend->confirmDeepLinkImport(token, false));
    const QString path = rig.pathOf(imported.last().at(0).toString());
    QVERIFY(readText(path).contains(QStringLiteral("hostname = \"vpn.example.com\"")));
    const std::optional<SubscriptionRecord> record = rig.record(path);
    QVERIFY(record);
    QCOMPARE(record->failures, 1);
    QCOMPARE(record->lastStatus, QStringLiteral("network"));
    QCOMPARE(record->lastFetchedAt, 0);
    QCOMPARE(record->nextAttemptAt, kStart + 10 * 60);
    QVERIFY(record->autoRefresh);

    // Added after a fetch that a server offering no updates refused, it is not
    // asked again by itself either.
    rig.backend->importDeepLink(v2::kFull);
    const QString second = consent.last().at(0).toString();
    QVERIFY(rig.backend->fetchSubscriptionForImport(second));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(SubscriptionFetchError::Http403, 403)));
    QVERIFY(rig.backend->confirmDeepLinkImport(second, false));
    const QString copy = rig.pathOf(imported.last().at(0).toString());
    QVERIFY(copy != path);
    QVERIFY(!rig.record(copy)->autoRefresh);
    QCOMPARE(rig.record(copy)->lastStatus, QStringLiteral("http_403"));
}

void TestBackendSubscription::anInvalidDocumentIsAFailedFetch()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    // A document the parser takes, with a server the editor would not save: a
    // DNS server that is neither an address nor a URL.
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(subscriptionDocument(
            "vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"), QStringLiteral(R"(,"dns_upstreams":["dns.example"])")))));
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(),
             freetunnel::subscriptionFailureText(SubscriptionFetchError::Document, freetunnel::SubscriptionDocError::Invalid));
    QCOMPARE(rig.backend->configs().size(), 0);
}

void TestBackendSubscription::withoutSecureStorageASubscriptionIsRefused()
{
    Rig rig;
    qputenv("FT_TEST_KEYRING", "absent");
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QCOMPARE(consent.count(), 0);
    QCOMPARE(failed.count(), 1);
    QVERIFY2(failed.last().at(0).toString().startsWith(QStringLiteral("Secure credential storage is unavailable")),
             qPrintable(failed.last().at(0).toString()));
    QVERIFY(!rig.backend->beginSubscriptionImport(v2::kSubscription));
    QCOMPARE(consent.count(), 0);
    QCOMPARE(rig.network.fetches(), 0);
}

// The same subscription is the same origin, path and user, whatever the
// password: sent again with a new one, it offers to update the config it made,
// whatever that one is called now.
void TestBackendSubscription::theSameSubscriptionWithAnotherPasswordOffersTheConfigItMade()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString oldSource = rig.record(path)->sourceId;
    const QString newUrl = QStringLiteral("https://alice:n3w@vpn.example.com/subscription");

    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    rig.backend->importDeepLink(subscriptionOnlyLink(newUrl));
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(subscriptionDocument(
            "vpn.example.com", "203.0.113.7:443", QStringLiteral("n3w"), QStringLiteral(R"(,"name":"Renamed")")))));
    QCOMPARE(asked.last().at(2).toString(), rig.backend->configs().value(rig.backend->configIndex(path)));
    QVERIFY2(!asked.last().at(0).toString().contains(QStringLiteral("Replacing it stops that")),
             qPrintable(asked.last().at(0).toString()));
    QVERIFY(rig.backend->confirmDeepLinkImport(asked.last().at(1).toString(), true));
    QCOMPARE(rig.backend->configs().size(), 1);
    QCOMPARE(passwordOf(path), QStringLiteral("n3w"));
    const std::optional<SubscriptionRecord> record = rig.record(path);
    QVERIFY(record && record->sourceId != oldSource);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(record->sourceId)), newUrl);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(oldSource)), QString());
}

void TestBackendSubscription::aSubscriptionAddressOnItsOwnAsksFirstToo()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    QVERIFY(!rig.backend->beginSubscriptionImport(QStringLiteral("http://alice:s3cret@vpn.example.com/s")));
    QCOMPARE(failed.count(), 1);
    QVERIFY(rig.backend->beginSubscriptionImport(QStringLiteral(" ") + v2::kSubscription));
    QCOMPARE(consent.count(), 1);
    QCOMPARE(rig.network.fetches(), 0);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QCOMPARE(rig.network.requests().constFirst().url, v2::kSubscription);
}

void TestBackendSubscription::anEndpointExportWithASubscriptionAsksBeforeAnyFetch()
{
    const QString file = QDir(appConfigDir()).filePath(QStringLiteral("../export-with-subscription.toml"));
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(endpoint_export::text().toUtf8());
    }
    const auto remove = qScopeGuard([&file]() { QFile::remove(file); });
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(!rig.backend->importFile(file));
    QCOMPARE(consent.count(), 1);
    QCOMPARE(asked.count(), 0);
    QCOMPARE(rig.network.fetches(), 0);
    const QVariantMap details = consent.last().at(1).toMap();
    QVERIFY(details.value(QStringLiteral("fromFile")).toBool());
    QCOMPARE(details.value(QStringLiteral("server")).toString(), QStringLiteral("vpn.example.com"));
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    const freetunnel::SubscriptionFetchRequest request = rig.network.requests().constFirst();
    QCOMPARE(request.url, endpoint_export::Fields().subscriptionUrl);
    // The export pins its certificate, and the fetch trusts that alone.
    QVERIFY(request.pinned);
    QCOMPARE(request.connectHost, QStringLiteral("203.0.113.5"));

    // One whose auth key the core could not use is refused as damage, as it is
    // without a subscription: nothing is asked and nothing is fetched.
    endpoint_export::Fields withKey;
    withKey.clientRandomAuthKey = QStringLiteral("abc");
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(endpoint_export::text(withKey).toUtf8());
    }
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    QVERIFY(!rig.backend->importFile(file));
    QCOMPARE(consent.count(), 1);
    QCOMPARE(failed.last().at(0).toString(),
             QStringLiteral("Not a valid TrustTunnel config: malformed client_random_auth_key value"));
}

void TestBackendSubscription::aLinkRefusedForSomethingElseIsRefusedAsBefore()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    // An auth key that is not hex, beside the address: damage, said before any
    // question about the subscription.
    rig.backend->importDeepLink(v2::linkFromRecords({{0x00, QByteArray(1, 2)},
                                                     {0x0e, v2::kSubscription.toUtf8()},
                                                     {0x0f, QByteArrayLiteral("abc")}}));
    QCOMPARE(failed.last().at(0).toString(),
             QStringLiteral("Link error: malformed client_random_auth_key value"));
    rig.backend->importDeepLink(v2::kHttpSubscription);
    QVERIFY(failed.last().at(0).toString().startsWith(QStringLiteral("Link error: ")));
    QCOMPARE(consent.count(), 0);
}

// What the link pins is what its fetch trusts, connecting where the link's
// server is.
void TestBackendSubscription::aPinnedLinkFetchesFromItsOwnServer()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    rig.backend->importDeepLink(v2::kSelfSigned);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    const freetunnel::SubscriptionFetchRequest request = rig.network.requests().constFirst();
    QVERIFY(request.pinned);
    QCOMPARE(request.peerVerifyName, QStringLiteral("vpn.example.com"));
    QCOMPARE(request.connectHost, QStringLiteral("1.2.3.4"));
    QCOMPARE(request.connectPort, 443);
}

void TestBackendSubscription::aSubscriptionThatNamesAnotherServerThanItsLinkSaysBoth()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    rig.backend->importDeepLink(v2::kFull);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("other.example.com", "203.0.113.7:443"))));
    const QStringList lines = asked.last().at(0).toString().split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(QStringLiteral("Server: other.example.com")), qPrintable(lines.join('|')));
    QVERIFY2(lines.contains(QStringLiteral("The server given with it is vpn.example.com.")), qPrintable(lines.join('|')));

    // The same server in other letter case is not another one.
    rig.backend->importDeepLink(v2::kFull);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("VPN.example.com", "203.0.113.7:443"))));
    QVERIFY2(!asked.last().at(0).toString().contains(QStringLiteral("The server given with it")),
             qPrintable(asked.last().at(0).toString()));
}

// Before anything is written: a store that will not keep the address leaves no
// config behind that would look linked and never update.
void TestBackendSubscription::anAddressTheStoreRefusesAddsNothing()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.7:443"))));
    qputenv("FT_TEST_KEYRING", "locked");
    QVERIFY(!rig.backend->confirmDeepLinkImport(asked.last().at(1).toString(), false));
    qputenv("FT_TEST_KEYRING", "present");
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(), freetunnel::backend_config::passwordNotStoredMessage(true));
    QCOMPARE(rig.backend->configs().size(), 0);
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    QCOMPARE(rig.subs()->storedAddressesForTest().size(), 1);
    QCOMPARE(CredentialStore::loadPassword(
                     CredentialStore::keyForSubscription(rig.subs()->storedAddressesForTest().constFirst())),
             QString());
}

// The config is added, and says it will not update: what links it could not be
// written, and its address is not left in the store with nothing to find it by.
void TestBackendSubscription::aLinkThatCannotBeSavedLeavesAPlainConfigAndNoAddress()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    QSignalSpy imported(rig.backend.get(), &Backend::configImported);
    rig.backend->importDeepLink(v2::kSubscriptionOnly);
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.7:443"))));
    QVERIFY(QDir().mkpath(freetunnel::subscriptionsPath()));
    const auto clear = qScopeGuard([]() { QDir(freetunnel::subscriptionsPath()).removeRecursively(); });
    QVERIFY(rig.backend->confirmDeepLinkImport(asked.last().at(1).toString(), false));
    QCOMPARE(imported.count(), 1);
    const QString path = rig.pathOf(imported.last().at(0).toString());
    QVERIFY(readText(path).contains(QStringLiteral("vpn.example.com")));
    QCOMPARE(passwordOf(path), QStringLiteral("s3cret"));
    QVERIFY(!rig.subs()->isLinked(path));
    QCOMPARE(failed.count(), 1);
    QVERIFY2(failed.last().at(0).toString().contains(QStringLiteral("won't update from its subscription")),
             qPrintable(failed.last().at(0).toString()));
    QVERIFY(!rig.backend->refreshSubscription(path));
    QCOMPARE(rig.subs()->storedAddressesForTest().size(), 1);
    QCOMPARE(CredentialStore::loadPassword(
                     CredentialStore::keyForSubscription(rig.subs()->storedAddressesForTest().constFirst())),
             QString());
}

// A subscription address this version cannot fetch, such as one whose user has a
// colon in it, which Basic credentials cannot carry: said, and the server the
// link gives is added as it would be with the setting off.
void TestBackendSubscription::aSubscriptionThatCannotBeAddedLeavesTheServerGivenWithIt()
{
    Rig rig;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(rig.backend.get(), &Backend::errorOccurred);
    const QString link = v2::linkFromRecords(
            {{0x00, QByteArray(1, 2)},
             {0x01, QByteArrayLiteral("vpn.example.com")},
             {0x02, QByteArrayLiteral("198.51.100.4:443")},
             {0x05, QByteArrayLiteral("bob")},
             {0x06, QByteArrayLiteral("hunter2")},
             {0x0e, QByteArrayLiteral("https://al%3Aice:s3cret@vpn.example.com/subscription")}});
    QVERIFY(freetunnel::parseDeepLink(link));
    rig.backend->importDeepLink(link);
    QCOMPARE(consent.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(failed.last().at(0).toString(),
             freetunnel::subscriptionFailureText(SubscriptionFetchError::BadUrl, freetunnel::SubscriptionDocError::Invalid));
    QCOMPARE(asked.count(), 1);
    QVERIFY(asked.last().at(0).toString().contains(freetunnel::subscriptionNotKeptNote()));
    QCOMPARE(asked.last().at(1).toString(), link);
    QVERIFY(rig.backend->confirmDeepLinkImport(link, false));
    QCOMPARE(rig.backend->configs().size(), 1);
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    QCOMPARE(rig.network.fetches(), 0);

    // The same for an endpoint's export.
    const QString file = QDir(appConfigDir()).filePath(QStringLiteral("../export-bad-subscription.toml"));
    const auto remove = qScopeGuard([&file]() { QFile::remove(file); });
    endpoint_export::Fields fields;
    fields.subscriptionUrl = QStringLiteral("https://al%3Aice:s3cret@vpn.example.com/s");
    {
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(endpoint_export::text(fields).toUtf8());
    }
    QVERIFY(!rig.backend->importFile(file));
    QCOMPARE(consent.count(), 0);
    QCOMPARE(failed.count(), 2);
    QCOMPARE(asked.count(), 2);
    QVERIFY(asked.last().at(0).toString().contains(freetunnel::subscriptionNotKeptNote()));
}

// Two subscriptions that name their configs alike are two subscriptions: the
// question says the config it would replace updates from one already, and
// would stop to update from the other one.
void TestBackendSubscription::anotherSubscriptionsServerOfTheSameNameSaysWhatItReplaces()
{
    Rig rig;
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly,
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"),
                                                                QStringLiteral(R"(,"name":"Work")")));
    QVERIFY(!path.isEmpty());
    const QString source = rig.record(path)->sourceId;
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    const QString otherUrl = QStringLiteral("https://carol:pw@other.example.com/subscription");
    rig.backend->importDeepLink(subscriptionOnlyLink(otherUrl));
    QVERIFY(rig.backend->fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(
            subscriptionDocument("other.example.com", "198.51.100.9:443", QStringLiteral("pw"),
                                 QStringLiteral(R"(,"name":"Work")")))));
    QCOMPARE(asked.last().at(2).toString(), QStringLiteral("Work"));
    const QStringList lines = asked.last().at(0).toString().split(QLatin1Char('\n'));
    QVERIFY2(lines.contains(QStringLiteral("“Work” updates from a subscription. Replacing it stops that.")),
             qPrintable(lines.join('|')));
    QVERIFY2(lines.contains(QStringLiteral("Updates from https://other.example.com")), qPrintable(lines.join('|')));
    QVERIFY(rig.backend->confirmDeepLinkImport(asked.last().at(1).toString(), true));
    QCOMPARE(rig.backend->configs().size(), 1);
    QVERIFY(rig.record(path) && rig.record(path)->sourceId != source);
    QCOMPARE(rig.subs()->address(path), otherUrl);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(source)), QString());
}

// ---- the link, through what happens to its config ------------------------------

void TestBackendSubscription::aRenameKeepsTheLink()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString source = rig.record(path)->sourceId;
    QVariantMap form = rig.backend->configFields(rig.backend->configIndex(path));
    form.insert(QStringLiteral("name"), QStringLiteral("Renamed"));
    form.insert(QStringLiteral("editIndex"), rig.backend->configIndex(path));
    form.insert(QStringLiteral("editPath"), path);
    QVERIFY(rig.backend->createConfig(form));
    const QString renamed = rig.pathOf(QStringLiteral("Renamed"));
    QVERIFY(renamed != path);
    QVERIFY(!rig.subs()->isLinked(path));
    QVERIFY(rig.subs()->isLinked(renamed));
    QCOMPARE(rig.record(renamed)->sourceId, source);
    QCOMPARE(rig.subs()->address(renamed), v2::kSubscription);
    QVERIFY(freetunnel::loadSubscriptions().contains(renamed));
    QVERIFY(rig.backend->refreshSubscription(renamed));
    QCOMPARE(rig.network.requests().last().url, v2::kSubscription);
}

// The link follows a rename once the config list names the new file. A list that
// could not be written still names the old one, and a link moved anyway would
// be removed at the next start, address and all, as one whose config is gone.
void TestBackendSubscription::aRenameTheListCannotRecordKeepsTheLinkWhereTheListHasIt()
{
    QString path;
    QString source;
    {
        Rig rig;
        path = linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
        QVERIFY(!path.isEmpty());
        source = rig.record(path)->sourceId;
        QVERIFY(QFile::setPermissions(storagePath(), QFileDevice::ReadOwner));
        const auto writable = qScopeGuard(
                []() { QFile::setPermissions(storagePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner); });
        if (QFileInfo(storagePath()).isWritable())
            QSKIP("configs.json stays writable here (run as root?)");
        QVariantMap form = rig.backend->configFields(rig.backend->configIndex(path));
        form.insert(QStringLiteral("name"), QStringLiteral("Renamed"));
        form.insert(QStringLiteral("editIndex"), rig.backend->configIndex(path));
        form.insert(QStringLiteral("editPath"), path);
        rig.backend->createConfig(form);
        QCOMPARE(loadStoredConfigs(), QStringList{path});
        QVERIFY(rig.subs()->isLinked(path));
    }
    Rig rig;
    QVERIFY(rig.subs()->isLinked(path));
    QCOMPARE(rig.subs()->address(path), v2::kSubscription);
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(source)), v2::kSubscription);
}

void TestBackendSubscription::removingTheConfigRemovesItsAddress()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString key = CredentialStore::keyForSubscription(rig.record(path)->sourceId);
    QVERIFY(rig.backend->refreshSubscription(path));
    rig.backend->removeConfig(rig.backend->configIndex(path));
    QVERIFY(!rig.subs()->isLinked(path));
    QCOMPARE(CredentialStore::loadPassword(key), QString());
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    // The fetch it had in flight went with it.
    QCOMPARE(rig.network.waiting(), 0);
}

void TestBackendSubscription::detachingKeepsTheConfigAndDropsTheAddress()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString key = CredentialStore::keyForSubscription(rig.record(path)->sourceId);
    const QString toml = readText(path);
    QVERIFY(rig.backend->detachSubscription(path));
    QVERIFY(!rig.backend->detachSubscription(path));
    QCOMPARE(readText(path), toml);
    QCOMPARE(passwordOf(path), QStringLiteral("s3cret"));
    QCOMPARE(CredentialStore::loadPassword(key), QString());
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    QVERIFY(!rig.backend->refreshSubscription(path));
}

void TestBackendSubscription::replacingWithAnotherServerDetachesAndSaysSo()
{
    Rig rig;
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly,
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"),
                                                                QStringLiteral(R"(,"name":"Work")")));
    QVERIFY(!path.isEmpty());
    const QString key = CredentialStore::keyForSubscription(rig.record(path)->sourceId);
    const QString link = staticLink(QStringLiteral("Work"), QStringLiteral("other.example.com"));
    QSignalSpy asked(rig.backend.get(), &Backend::deepLinkImportConfirmationRequired);
    rig.backend->importDeepLink(link);
    QCOMPARE(asked.last().at(2).toString(), QStringLiteral("Work"));
    QVERIFY2(asked.last().at(0).toString().contains(
                     QStringLiteral("“Work” updates from a subscription. Replacing it stops that.")),
             qPrintable(asked.last().at(0).toString()));
    QVERIFY(rig.backend->confirmDeepLinkImport(link, true));
    QVERIFY(readText(path).contains(QStringLiteral("other.example.com")));
    QVERIFY(!rig.subs()->isLinked(path));
    QCOMPARE(CredentialStore::loadPassword(key), QString());
    // Sent again, it collides with a config no subscription updates now.
    rig.backend->importDeepLink(link);
    QCOMPARE(asked.last().at(2).toString(), QStringLiteral("Work"));
    QVERIFY2(!asked.last().at(0).toString().contains(QStringLiteral("updates from a subscription")),
             qPrintable(asked.last().at(0).toString()));
}

// Share and export write the server as it is now, never the address.
void TestBackendSubscription::sharingALinkedConfigLeavesTheSubscriptionOut()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const int index = rig.backend->configIndex(path);
    const std::optional<freetunnel::DeepLinkConfig> shared = freetunnel::parseDeepLink(rig.backend->configDeepLink(index));
    QVERIFY(shared);
    QCOMPARE(shared->version, 1);
    QCOMPARE(shared->subscriptionUrl, QString());
    QCOMPARE(shared->hostname, QStringLiteral("vpn.example.com"));
    const QString exported = QDir(appConfigDir()).filePath(QStringLiteral("../exported.toml"));
    const auto remove = qScopeGuard([&exported]() { QFile::remove(exported); });
    QVERIFY(rig.backend->exportConfigToml(index, exported));
    const QString text = readText(exported);
    QVERIFY2(!text.contains(QStringLiteral("subscription")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("203.0.113.7:443")), qPrintable(text));
}

// A form opened before a refresh holds the old server; its save keeps the new:
// every field the subscription owns, the accepted hostname among them, which
// written back would be asked about again at the next refresh.
void TestBackendSubscription::anEditorSaveKeepsWhatTheSubscriptionOwns()
{
    Rig rig;
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly,
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"),
                                                                QStringLiteral(R"(,"name":"Work")")));
    QVERIFY(!path.isEmpty());
    QVariantMap form = rig.backend->configFields(rig.backend->configIndex(path));
    const QJsonObject refreshed{
            {QStringLiteral("version"), 1},
            {QStringLiteral("hostname"), QStringLiteral("vpn2.example.com")},
            {QStringLiteral("address"), QStringLiteral("203.0.113.8:8443")},
            {QStringLiteral("username"), QStringLiteral("bob")},
            {QStringLiteral("password"), QStringLiteral("n3w")},
            {QStringLiteral("certificate"), certs::kLeafPem},
            {QStringLiteral("custom_sni"), QStringLiteral("cdn.example.net")},
            {QStringLiteral("client_random_prefix"), QStringLiteral("abcd")},
    };
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(QJsonDocument(refreshed).toJson())));
    QVERIFY(rig.backend->answerHostnameChange(path, true));
    QTRY_VERIFY(readText(path).contains(QStringLiteral("203.0.113.8:8443")));
    const freetunnel::ConfigToml before = freetunnel::parseConfigToml(readText(path));
    QCOMPARE(before.hostname, QStringLiteral("vpn2.example.com"));
    QVERIFY(!before.certificate.isEmpty());
    form.insert(QStringLiteral("editIndex"), rig.backend->configIndex(path));
    form.insert(QStringLiteral("dns"), QStringLiteral("9.9.9.9"));
    QVERIFY(rig.backend->createConfig(form));
    const QString toml = readText(path);
    const freetunnel::ConfigToml after = freetunnel::parseConfigToml(toml);
    QCOMPARE(after.hostname, QStringLiteral("vpn2.example.com"));
    QCOMPARE(after.addresses, QStringLiteral("203.0.113.8:8443"));
    QCOMPARE(after.username, QStringLiteral("bob"));
    QCOMPARE(after.certificate, before.certificate);
    QCOMPARE(after.customSni, QStringLiteral("cdn.example.net"));
    QCOMPARE(after.clientRandom, QStringLiteral("abcd"));
    QCOMPARE(after.dns, QStringLiteral("9.9.9.9")); // the user's own edit stays
    QCOMPARE(passwordOf(path), QStringLiteral("n3w"));
    QVERIFY(rig.subs()->isLinked(path));
    QCOMPARE(rig.record(path)->heldHostname, QString());
}

// Off, a linked config is an ordinary one: the editor writes what the form says.
// The endpoint's document has no field for the auth key (1.3.0-beta.2): only the
// link carries it. The fetch succeeding must not read that as "the server has no
// key" and drop the one the link gave; the server's rules admit the client by it.
void TestBackendSubscription::aKeyedLinkKeepsItsKeyThroughTheFetch()
{
    Rig rig;
    const QString path = linkThroughImport(rig, keyedSubscriptionLink(QStringLiteral("aabbccdd")),
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QVERIFY(rig.subs()->isLinked(path));
    QCOMPARE(authKeyOf(path), QStringLiteral("aabbccdd"));
    const QString connect = freetunnel::buildConnectConfigToml(path);
    QVERIFY2(connect.contains(QStringLiteral("client_random_auth_key = \"aabbccdd\"\n")), qPrintable(connect));
    QVERIFY(!readText(path).contains(QStringLiteral("aabbccdd")));

    // And when the fetch fails, the server given with the link is the one added,
    // with its key.
    QSignalSpy consent(rig.subs(), &SubscriptionController::consentRequired);
    rig.backend->importDeepLink(keyedSubscriptionLink(QStringLiteral("11223344")));
    QCOMPARE(consent.count(), 1);
    const QString token = consent.last().at(0).toString();
    QVERIFY(rig.backend->fetchSubscriptionForImport(token));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(SubscriptionFetchError::Network)));
    QSignalSpy imported(rig.backend.get(), &Backend::configImported);
    QVERIFY(rig.backend->confirmDeepLinkImport(token, false));
    QCOMPARE(authKeyOf(rig.pathOf(imported.last().at(0).toString())), QStringLiteral("11223344"));
}

// A refresh, which the document used to turn into "no key": the first one that
// succeeded deleted the key from the store, and the config stopped connecting as
// soon as the server's rules required it.
void TestBackendSubscription::aRefreshThatSaysNothingOfTheKeyLeavesItInTheStore()
{
    Rig rig;
    const QString path = linkThroughImport(rig, keyedSubscriptionLink(QStringLiteral("aabbccdd")),
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QCOMPARE(authKeyOf(path), QStringLiteral("aabbccdd"));

    // One that changes something else: written, and the key is still there.
    QSignalSpy updated(rig.subs(), &SubscriptionController::updated);
    rig.clock += 3600;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QVERIFY(readText(path).contains(QStringLiteral("203.0.113.9:443")));
    QCOMPARE(updated.count(), 1);
    QVERIFY2(!updated.last().at(1).toString().contains(QStringLiteral("auth_key")),
             qPrintable(updated.last().at(1).toString()));
    QCOMPARE(authKeyOf(path), QStringLiteral("aabbccdd"));
    QVERIFY(freetunnel::buildConnectConfigToml(path).contains(QStringLiteral("client_random_auth_key = \"aabbccdd\"")));

    // And one that changes nothing.
    rig.clock += 3600;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QCOMPARE(updated.count(), 1);
    QCOMPARE(authKeyOf(path), QStringLiteral("aabbccdd"));
}

// A document that does mention the key (a server or a later format that says it)
// is followed, by name in the summary and never by value.
void TestBackendSubscription::aRefreshThatDoesSayItFollowsIt()
{
    Rig rig;
    const QString path = linkThroughImport(rig, keyedSubscriptionLink(QStringLiteral("aabbccdd")),
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QSignalSpy updated(rig.subs(), &SubscriptionController::updated);

    rig.clock += 3600;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(
            subscriptionDocument("vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"),
                                 QStringLiteral(R"(,"client_random_auth_key":"11223344")")))));
    QCOMPARE(authKeyOf(path), QStringLiteral("11223344"));
    QCOMPARE(updated.last().at(1).toString(), QStringLiteral("client_random_auth_key"));
    QVERIFY(!rig.backend->logText().contains(QStringLiteral("11223344")));

    // Said to be none: cleared.
    rig.clock += 3600;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(
            subscriptionDocument("vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"),
                                 QStringLiteral(R"(,"client_random_auth_key":"")")))));
    QVERIFY(authKeyOf(path).isEmpty());
}

// The key is the user's on a linked config, since the subscription cannot send
// it: the editor shows it, takes a new one, and a refresh afterwards keeps that.
void TestBackendSubscription::theEditorCanSetTheKeyOfALinkedConfig()
{
    Rig rig;
    // A link that brought no key, to a server that came to need one.
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly,
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QVERIFY(authKeyOf(path).isEmpty());

    QVariantMap form = rig.backend->configFields(rig.backend->configIndex(path));
    form.insert(QStringLiteral("editIndex"), rig.backend->configIndex(path));
    form.insert(QStringLiteral("authKey"), QStringLiteral("aabbccdd"));
    QVERIFY(rig.backend->createConfig(form));
    QVERIFY(rig.subs()->isLinked(path));
    QCOMPARE(authKeyOf(path), QStringLiteral("aabbccdd"));
    QCOMPARE(rig.backend->configFields(rig.backend->configIndex(path)).value(QStringLiteral("authKey")).toString(),
             QStringLiteral("aabbccdd"));

    rig.clock += 3600;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QVERIFY(readText(path).contains(QStringLiteral("203.0.113.9:443")));
    QCOMPARE(authKeyOf(path), QStringLiteral("aabbccdd"));
}

void TestBackendSubscription::withTheSettingOffAnEditorSaveWritesTheForm()
{
    QString path;
    {
        Rig rig;
        path = linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
        QVERIFY(!path.isEmpty());
    }
    Rig rig(false);
    QVERIFY(rig.subs()->isLinked(path));
    QVariantMap form = rig.backend->configFields(rig.backend->configIndex(path));
    form.insert(QStringLiteral("editIndex"), rig.backend->configIndex(path));
    form.insert(QStringLiteral("editPath"), path);
    form.insert(QStringLiteral("addresses"), QStringLiteral("203.0.113.99:443"));
    form.insert(QStringLiteral("username"), QStringLiteral("dave"));
    QVERIFY(rig.backend->createConfig(form));
    const freetunnel::ConfigToml saved = freetunnel::parseConfigToml(readText(path));
    QCOMPARE(saved.addresses, QStringLiteral("203.0.113.99:443"));
    QCOMPARE(saved.username, QStringLiteral("dave"));
}

void TestBackendSubscription::linksWhoseConfigIsGoneAreRemovedAtStart()
{
    const QString orphanSource = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString keptSource = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForSubscription(orphanSource), v2::kSubscription));
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForSubscription(keptSource), v2::kSubscription));
    const QString kept = QDir(appConfigDir()).filePath(QStringLiteral("kept.toml"));
    {
        freetunnel::ConfigToml c;
        c.hostname = QStringLiteral("vpn.example.com");
        c.addresses = QStringLiteral("203.0.113.7:443");
        c.username = QStringLiteral("alice");
        QFile out(kept);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(freetunnel::buildConfigToml(c).toUtf8());
    }
    const auto forget = qScopeGuard([&orphanSource, &keptSource]() {
        CredentialStore::deletePassword(CredentialStore::keyForSubscription(orphanSource));
        CredentialStore::deletePassword(CredentialStore::keyForSubscription(keptSource));
    });
    saveStoredConfigs({kept});
    SubscriptionRecord orphan;
    orphan.sourceId = orphanSource;
    SubscriptionRecord linked;
    linked.sourceId = keptSource;
    QVERIFY(freetunnel::saveSubscriptions(
            {{QDir(appConfigDir()).filePath(QStringLiteral("gone.toml")), orphan}, {kept, linked}}));

    Rig rig(false); // the clean-up runs with the setting off too
    QCOMPARE(freetunnel::loadSubscriptions().keys(), QStringList{kept});
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(orphanSource)), QString());
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForSubscription(keptSource)), v2::kSubscription);
    // Off, the link is kept and nothing fetches for it, asked or not.
    QVERIFY(!rig.backend->refreshSubscription(kept));
    rig.clock += 100 * kDay;
    rig.subs()->runSchedulerTickForTest();
    QCOMPARE(rig.network.fetches(), 0);
}

// ---- refreshing -------------------------------------------------------------

void TestBackendSubscription::aConfigNotInUseIsUpdatedAndSaysSo()
{
    Rig rig;
    const QString active = linkThroughImport(rig, subscriptionOnlyLink(QStringLiteral("https://u:p@a.example.com/s")),
                                             subscriptionDocument("a.example.com", "192.0.2.1:443"));
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!active.isEmpty() && !path.isEmpty());
    QCOMPARE(rig.backend->activeIndex(), rig.backend->configIndex(active));
    QSignalSpy updated(rig.subs(), &SubscriptionController::updated);
    QSignalSpy activeChanged(rig.backend.get(), &Backend::configChanged);
    QSignalSpy toasts(rig.backend.get(), &Backend::errorOccurred);
    rig.clock += 3600;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QVERIFY(readText(path).contains(QStringLiteral("203.0.113.9:443")));
    QCOMPARE(updated.count(), 1);
    QCOMPARE(updated.last().at(0).toString(), path);
    QCOMPARE(updated.last().at(1).toString(), QStringLiteral("addresses 203.0.113.7:443 → 203.0.113.9:443"));
    QVERIFY(rig.backend->logText().contains(QStringLiteral("addresses 203.0.113.7:443 → 203.0.113.9:443")));
    QVERIFY(rig.backend->logText().contains(QStringLiteral("https://vpn.example.com")));
    QVERIFY(!rig.backend->logText().contains(QStringLiteral("s3cret")));
    // Nothing else moved: not the active config, nor the one to start with.
    QCOMPARE(rig.backend->activeIndex(), rig.backend->configIndex(active));
    QCOMPARE(loadAppSettings().last_config_path, active);
    QCOMPARE(activeChanged.count(), 0);
    QCOMPARE(toasts.count(), 0);
    QCOMPARE(rig.record(path)->lastFetchedAt, kStart + 3600);
}

void TestBackendSubscription::anUnchangedReplyMovesOnlyTheFetchTime()
{
    Rig rig;
    const QByteArray document = subscriptionDocument("vpn.example.com", "203.0.113.7:443");
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly, document);
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    const QDateTime modified = QFileInfo(path).lastModified();
    QSignalSpy updated(rig.subs(), &SubscriptionController::updated);
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(document)));
    QCOMPARE(readText(path), toml);
    QCOMPARE(QFileInfo(path).lastModified(), modified);
    QCOMPARE(updated.count(), 0);
    QCOMPARE(rig.record(path)->lastFetchedAt, rig.clock);
    QCOMPARE(rig.record(path)->nextAttemptAt, rig.clock + kDay);
}

void TestBackendSubscription::anotherServerIsHeldUntilAccepted()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    QSignalSpy held(rig.subs(), &SubscriptionController::hostnameChangeHeld);
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn2.example.com", "198.51.100.2:443"))));
    QCOMPARE(held.count(), 1);
    QCOMPARE(held.last().at(1).toString(), QStringLiteral("vpn.example.com"));
    QCOMPARE(held.last().at(2).toString(), QStringLiteral("vpn2.example.com"));
    QCOMPARE(readText(path), toml);
    QCOMPARE(rig.record(path)->heldHostname, QStringLiteral("vpn2.example.com"));
    // What applies a held update otherwise does not apply this one.
    rig.backend->setConfigOpenInEditor(QString());
    QTest::qWait(20);
    QCOMPARE(readText(path), toml);
    QVERIFY(rig.backend->answerHostnameChange(path, true));
    QTRY_VERIFY(readText(path).contains(QStringLiteral("hostname = \"vpn2.example.com\"")));
    QVERIFY(readText(path).contains(QStringLiteral("198.51.100.2:443")));
    QCOMPARE(rig.record(path)->heldHostname, QString());
    QVERIFY(rig.record(path)->autoRefresh);
    QVERIFY(!rig.backend->answerHostnameChange(path, true));

    // Only the case: not another server.
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("VPN2.Example.com", "198.51.100.2:443"))));
    QCOMPARE(held.count(), 1);
    QVERIFY(readText(path).contains(QStringLiteral("hostname = \"VPN2.Example.com\"")));
}

void TestBackendSubscription::anotherServerAcceptedAfterARestartIsFetchedAgain()
{
    QString path;
    QString toml;
    {
        Rig rig;
        path = linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
        QVERIFY(!path.isEmpty());
        toml = readText(path);
        QVERIFY(refreshByItself(rig, path));
        QVERIFY(rig.network.answer(
                FakeSubscriptionNetwork::document(subscriptionDocument("vpn2.example.com", "198.51.100.2:443"))));
        QCOMPARE(readText(path), toml);
    }
    Rig rig;
    QCOMPARE(rig.record(path)->heldHostname, QStringLiteral("vpn2.example.com"));
    QVERIFY(!rig.subs()->held(path));
    QVERIFY(rig.backend->answerHostnameChange(path, true));
    QCOMPARE(rig.network.fetches(), 1);
    QCOMPARE(rig.network.requests().constFirst().url, v2::kSubscription);
    // The server accepted, in whatever letter case it is named this time.
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("VPN2.example.com", "198.51.100.2:443"))));
    QVERIFY(readText(path).contains(QStringLiteral("hostname = \"VPN2.example.com\"")));
    QCOMPARE(rig.record(path)->heldHostname, QString());
}

// The subscription names the config's own server again before the user
// answered: there is nothing left to ask about.
void TestBackendSubscription::theOldServerBackAgainDropsTheQuestion()
{
    Rig rig;
    const QByteArray document = subscriptionDocument("vpn.example.com", "203.0.113.7:443");
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly, document);
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn2.example.com", "198.51.100.2:443"))));
    QCOMPARE(rig.record(path)->heldHostname, QStringLiteral("vpn2.example.com"));
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(document)));
    QCOMPARE(rig.record(path)->heldHostname, QString());
    QVERIFY(!rig.subs()->held(path));
    QVERIFY(!rig.backend->answerHostnameChange(path, true));
    QCOMPARE(readText(path), toml);
}

void TestBackendSubscription::anotherServerRefusedStopsAutomaticRefresh()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn2.example.com", "198.51.100.2:443"))));
    QVERIFY(rig.backend->answerHostnameChange(path, false));
    // Nor later, by what applies a held update otherwise.
    rig.backend->setConfigOpenInEditor(QString());
    QTest::qWait(20);
    QCOMPARE(readText(path), toml);
    QVERIFY(!rig.subs()->held(path));
    QVERIFY(!rig.record(path)->autoRefresh);
    QVERIFY(!refreshByItself(rig, path));
    // The user's own refresh still fetches, and asks again.
    QSignalSpy held(rig.subs(), &SubscriptionController::hostnameChangeHeld);
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn2.example.com", "198.51.100.2:443"))));
    QCOMPARE(held.count(), 1);
    QVERIFY(!rig.record(path)->autoRefresh);
}

void TestBackendSubscription::anUpdateWaitsWhileTheEditorHasTheConfigOpen()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    QSignalSpy updated(rig.subs(), &SubscriptionController::updated);
    rig.backend->setConfigOpenInEditor(path);
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QCOMPARE(readText(path), toml);
    QVERIFY(rig.subs()->held(path));
    QCOMPARE(rig.record(path)->lastStatus, QStringLiteral("held"));
    QTest::qWait(50);
    QCOMPARE(readText(path), toml);
    rig.backend->setConfigOpenInEditor(QString());
    QTRY_COMPARE(updated.count(), 1);
    QVERIFY(readText(path).contains(QStringLiteral("203.0.113.9:443")));
    QVERIFY(!rig.subs()->held(path));
}

// With the store refusing the new password, neither the file nor the password
// changes: the last good config stays.
void TestBackendSubscription::aStoreThatRefusesLeavesTheConfigAsItWas()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    qputenv("FT_TEST_KEYRING", "locked");
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(
            subscriptionDocument("vpn.example.com", "203.0.113.9:443", QStringLiteral("n3w")))));
    qputenv("FT_TEST_KEYRING", "present");
    QCOMPARE(readText(path), toml);
    QCOMPARE(passwordOf(path), QStringLiteral("s3cret"));
    QCOMPARE(rig.record(path)->lastStatus, QStringLiteral("not_saved"));
    QCOMPARE(rig.record(path)->failures, 1);
    QCOMPARE(rig.record(path)->nextAttemptAt, rig.clock + 10 * 60);
}

// A connect that ends while it reads the password, the keyring giving none,
// reaches no helper, and no state of the helper's says it ended: an update
// held for it is applied all the same.
void TestBackendSubscription::anUpdateHeldForAConnectThatEndsBeforeTheHelperIsApplied()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QCOMPARE(rig.backend->activeIndex(), rig.backend->configIndex(path));
    const QString key = CredentialStore::keyForConfigPath(path);
    QVERIFY(CredentialStore::deletePassword(key));
    // The connect's read waits, on its worker, for the update to be held.
    const auto gate = std::make_shared<QSemaphore>();
    const auto gated = std::make_shared<std::atomic<bool>>(false);
    CredentialStore::setCallHookForTest([gate, gated, key](const char *call, const QString &k) {
        if (QThread::currentThread() != QCoreApplication::instance()->thread() && qstrcmp(call, "load") == 0 && k == key
                && !gated->exchange(true))
            gate->tryAcquire(1, 10000);
    });
    const auto unhook = qScopeGuard([gate]() {
        gate->release();
        CredentialStore::setCallHookForTest({});
    });
    QVERIFY(rig.backend->refreshSubscription(path));
    rig.backend->connectVpn();
    QVERIFY(rig.backend->connecting());
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QCOMPARE(rig.record(path)->lastStatus, QStringLiteral("held"));
    QVERIFY(!readText(path).contains(QStringLiteral("203.0.113.9")));
    gate->release();
    QTRY_VERIFY(!rig.backend->connecting());
    QTRY_VERIFY(readText(path).contains(QStringLiteral("203.0.113.9:443")));
    QVERIFY(!rig.subs()->held(path));
    QCOMPARE(passwordOf(path), QStringLiteral("s3cret"));
}

// Unlinked while the update read the config's password, which runs the
// credential store's event loop: nothing of the update is written.
void TestBackendSubscription::aConfigUnlinkedWhileItsPasswordIsReadIsNotWritten()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const QString toml = readText(path);
    const QString key = CredentialStore::keyForConfigPath(path);
    Backend *backend = rig.backend.get();
    bool detached = false;
    CredentialStore::setCallHookForTest([&](const char *call, const QString &k) {
        if (!detached && qstrcmp(call, "load") == 0 && k == key && QThread::currentThread() == QCoreApplication::instance()->thread())
            detached = backend->detachSubscription(path);
    });
    const auto unhook = qScopeGuard([]() { CredentialStore::setCallHookForTest({}); });
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QVERIFY(detached);
    QCOMPARE(readText(path), toml);
    QVERIFY(!rig.subs()->isLinked(path));
}

// A keyring that gives no address, locked or emptied by hand, may give it next
// time: tried again soon, as what may pass is.
void TestBackendSubscription::aMissingAddressIsTriedAgainSoon()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    const int fetches = rig.network.fetches();
    QVERIFY(CredentialStore::deletePassword(CredentialStore::keyForSubscription(rig.record(path)->sourceId)));
    QVERIFY(!rig.backend->refreshSubscription(path));
    QCOMPARE(rig.network.fetches(), fetches);
    QCOMPARE(rig.record(path)->lastStatus, QStringLiteral("no_address"));
    QCOMPARE(rig.record(path)->failures, 1);
    QCOMPARE(rig.record(path)->nextAttemptAt, rig.clock + 10 * 60);
    QVERIFY(rig.record(path)->autoRefresh);
}

// 403, 404, 405 and 407 are a server offering no updates, 401 credentials it
// refuses, and an untrusted certificate wants a new link: nothing asks again by
// itself, and the user's own refresh, once it gets through, turns it back on.
void TestBackendSubscription::aServerThatOffersNoUpdatesIsAskedOnlyByTheUser_data()
{
    QTest::addColumn<SubscriptionFetchError>("error");
    QTest::addColumn<int>("status");
    QTest::newRow("403") << SubscriptionFetchError::Http403 << 403;
    QTest::newRow("404") << SubscriptionFetchError::HttpNoSubscription << 404;
    QTest::newRow("401") << SubscriptionFetchError::Http401 << 401;
    QTest::newRow("untrusted") << SubscriptionFetchError::CertificateUntrusted << 0;
}

void TestBackendSubscription::aServerThatOffersNoUpdatesIsAskedOnlyByTheUser()
{
    QFETCH(SubscriptionFetchError, error);
    QFETCH(int, status);
    Rig rig;
    const QByteArray document = subscriptionDocument("vpn.example.com", "203.0.113.7:443");
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly, document);
    QVERIFY(!path.isEmpty());
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(error, status)));
    QVERIFY(!rig.record(path)->autoRefresh);
    rig.clock += 30 * kDay;
    const int fetches = rig.network.fetches();
    rig.subs()->runSchedulerTickForTest();
    QCOMPARE(rig.network.fetches(), fetches);
    // A manual one that fails again changes nothing; one that gets through does.
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(error, status)));
    QVERIFY(!rig.record(path)->autoRefresh);
    rig.clock += kCheck;
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(document)));
    QVERIFY(rig.record(path)->autoRefresh);
    QCOMPARE(rig.record(path)->failures, 0);
    QVERIFY(refreshByItself(rig, path));
}

void TestBackendSubscription::failuresThatMayPassBackOffThenWaitTheInterval()
{
    Rig rig;
    rig.jitter = 123;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QCOMPARE(rig.record(path)->nextAttemptAt, kStart + kDay + 123);
    for (const qint64 wait : {qint64(10 * 60), qint64(30 * 60), qint64(2 * 60 * 60), kDay + 123}) {
        QVERIFY(refreshByItself(rig, path));
        QVERIFY(rig.network.answer(FakeSubscriptionNetwork::failure(SubscriptionFetchError::HttpServer, 503)));
        QCOMPARE(rig.record(path)->nextAttemptAt - rig.clock, wait);
        QVERIFY(rig.record(path)->autoRefresh);
        // Not before.
        const qint64 due = rig.record(path)->nextAttemptAt;
        rig.clock = due - 1;
        const int fetches = rig.network.fetches();
        rig.subs()->runSchedulerTickForTest();
        QCOMPARE(rig.network.fetches(), fetches);
    }
    QCOMPARE(rig.record(path)->failures, 4);
    QCOMPARE(rig.record(path)->lastStatus, QStringLiteral("http_server"));
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.7:443"))));
    QCOMPARE(rig.record(path)->failures, 0);
}

void TestBackendSubscription::theJitterStaysWithinTenMinutes()
{
    Rig rig(true, false);
    const QByteArray document = subscriptionDocument("vpn.example.com", "203.0.113.7:443");
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly, document);
    QVERIFY(!path.isEmpty());
    QSet<qint64> seen;
    for (int i = 0; i < 40; ++i) {
        QVERIFY(refreshByItself(rig, path));
        QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(document)));
        const qint64 jitter = rig.record(path)->nextAttemptAt - rig.clock - kDay;
        QVERIFY2(jitter >= 0 && jitter <= 10 * 60, qPrintable(QString::number(jitter)));
        seen.insert(jitter);
    }
    QVERIFY(seen.size() > 1);
    // A stand-in that gives more is held to the bounds all the same.
    rig.subs()->setRandomForTest([](qint64 bound) { return bound * 3; });
    QVERIFY(refreshByItself(rig, path));
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(document)));
    QCOMPARE(rig.record(path)->nextAttemptAt, rig.clock + kDay + 10 * 60);
}

void TestBackendSubscription::onlyWhatIsDueIsFetchedAndNoMoreThanTwoAtOnce()
{
    Rig rig;
    QStringList paths;
    for (const char *host : {"a.example.com", "b.example.com", "c.example.com"}) {
        const QString h = QString::fromLatin1(host);
        paths << linkThroughImport(rig, subscriptionOnlyLink(QStringLiteral("https://u:p@%1/s").arg(h)),
                                   subscriptionDocument(h, "192.0.2.1:443"));
        QVERIFY(!paths.last().isEmpty());
    }
    const QString notDue = linkThroughImport(rig, subscriptionOnlyLink(QStringLiteral("https://u:p@d.example.com/s")),
                                             subscriptionDocument("d.example.com", "192.0.2.1:443"));
    // d fetched an hour after the others, so it is due an hour later.
    QVERIFY(rig.backend->refreshSubscription(notDue));
    rig.clock += 3600;
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(subscriptionDocument("d.example.com", "192.0.2.1:443"))));
    const int before = rig.network.fetches();
    rig.clock = kStart + kDay;
    rig.subs()->runSchedulerTickForTest();
    QCOMPARE(rig.network.fetches(), before + 2);
    QCOMPARE(rig.network.waiting(), 2);
    QVERIFY(rig.network.answer(FakeSubscriptionNetwork::document(subscriptionDocument("a.example.com", "192.0.2.1:443"))));
    QCOMPARE(rig.network.fetches(), before + 3);
    QCOMPARE(rig.network.mostAtOnce(), 2);
    QCOMPARE(rig.network.requests().last().url, QStringLiteral("https://u:p@c.example.com/s"));
    // The same one twice is one fetch.
    QVERIFY(!rig.backend->refreshSubscription(paths.at(2)));
}

// Whatever the schedule says, as a record a crash or an older version left can:
// a config fetched less than ten minutes ago waits.
void TestBackendSubscription::noConfigIsFetchedByItselfTwiceInTenMinutes()
{
    const QString path = QDir(appConfigDir()).filePath(QStringLiteral("recent.toml"));
    {
        freetunnel::ConfigToml c;
        c.hostname = QStringLiteral("vpn.example.com");
        c.addresses = QStringLiteral("203.0.113.7:443");
        c.username = QStringLiteral("alice");
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(freetunnel::buildConfigToml(c).toUtf8());
    }
    saveStoredConfigs({path});
    SubscriptionRecord record;
    record.sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto forget = qScopeGuard(
            [&record]() { CredentialStore::deletePassword(CredentialStore::keyForSubscription(record.sourceId)); });
    QVERIFY(CredentialStore::storePassword(CredentialStore::keyForSubscription(record.sourceId), v2::kSubscription));
    record.lastAttemptAt = kStart - kCheck + 1;
    record.nextAttemptAt = 0;
    QVERIFY(freetunnel::saveSubscriptions({{path, record}}));
    Rig rig;
    rig.subs()->runSchedulerTickForTest();
    QCOMPARE(rig.network.fetches(), 0);
    rig.clock += 1;
    rig.subs()->runSchedulerTickForTest();
    QCOMPARE(rig.network.fetches(), 1);
}

// A custom SNI hides the server's name in the tunnel's handshake, and a fetch
// sends the real one: only the user's own refresh does that.
void TestBackendSubscription::aCustomSniConfigIsNeverRefreshedByItself()
{
    Rig rig;
    const QString path = linkThroughImport(rig, v2::kSubscriptionOnly,
                                           subscriptionDocument("vpn.example.com", "203.0.113.7:443", QStringLiteral("s3cret"),
                                                                QStringLiteral(R"(,"custom_sni":"cdn.example.net")")));
    QVERIFY(!path.isEmpty());
    QVERIFY(readText(path).contains(QStringLiteral("cdn.example.net")));
    QVERIFY(!refreshByItself(rig, path));
    rig.clock += 30 * kDay;
    rig.subs()->runSchedulerTickForTest();
    QCOMPARE(rig.network.waiting(), 0);
    // Not a failure, and nothing to count.
    QCOMPARE(rig.record(path)->failures, 0);
    QVERIFY(rig.backend->refreshSubscription(path));
    QCOMPARE(rig.network.waiting(), 1);
}

void TestBackendSubscription::theSchedulerRunsOnlyWithTheSettingOn()
{
    {
        AppSettings settings = loadAppSettings();
        settings.subscriptions_enabled = false;
        saveAppSettings(settings);
        Backend backend;
        QVERIFY(!backend.subscriptions()->schedulingForTest());
    }
    {
        AppSettings settings = loadAppSettings();
        settings.subscriptions_enabled = true;
        saveAppSettings(settings);
        Backend backend;
        QVERIFY(backend.subscriptions()->schedulingForTest());
    }
}

// The first check comes 0 to 10 minutes after start-up, so the configs of many
// users do not all fetch in the same minute; then one every ten minutes.
void TestBackendSubscription::theChecksStartAfterTheJitterThenComeEveryTenMinutes()
{
    Rig rig;
    rig.jitter = 7 * 60;
    rig.subs()->start(QStringList(), true);
    QCOMPARE(rig.subs()->firstCheckDelayMsForTest(), 7 * 60 * 1000);
    QCOMPARE(rig.subs()->checkIntervalMsForTest(), 10 * 60 * 1000);
    rig.subs()->stopSchedulerForTest();
}

void TestBackendSubscription::aRemovalWhileItFetchesDropsTheResult()
{
    Rig rig;
    const QString path =
            linkThroughImport(rig, v2::kSubscriptionOnly, subscriptionDocument("vpn.example.com", "203.0.113.7:443"));
    QVERIFY(!path.isEmpty());
    QVERIFY(rig.backend->refreshSubscription(path));
    QVERIFY(rig.backend->detachSubscription(path));
    QCOMPARE(rig.network.aborts(), 1);
    QVERIFY(!rig.network.answer(
            FakeSubscriptionNetwork::document(subscriptionDocument("vpn.example.com", "203.0.113.9:443"))));
    QVERIFY(!readText(path).contains(QStringLiteral("203.0.113.9")));
}

// ---- with the real fetcher -----------------------------------------------------

// The link pins the test server's certificate and names 127.0.0.1, as an
// endpoint's export does for a self-signed server: no connection at all until
// the user agrees, then one, and the question names what it fetched.
void TestBackendSubscription::endToEndNothingIsFetchedBeforeTheConsent()
{
#if defined(FT_TEST_OPENSSL_CLI)
    TlsTestCerts certs;
    QString error;
    QVERIFY2(makeTlsTestCerts(QStringLiteral(FT_TEST_OPENSSL_CLI), &certs, &error), qPrintable(error));
    TlsTestServer server;
    QVERIFY(server.listen({certs.selfSigned.certificate}, certs.selfSigned.key));
    const QString address = QStringLiteral("127.0.0.1:%1").arg(server.port());
    TlsTestServer::Response reply;
    reply.body = subscriptionDocument("vpn.test", address);
    server.setRoute("/subscription", reply);
    const QString url = QStringLiteral("https://alice:s3cret@vpn.test:%1/subscription").arg(server.port());
    const QString link = v2::linkFromRecords({{0x00, QByteArray(1, 2)},
                                              {0x01, QByteArrayLiteral("vpn.test")},
                                              {0x02, address.toUtf8()},
                                              {0x05, QByteArrayLiteral("alice")},
                                              {0x06, QByteArrayLiteral("s3cret")},
                                              {0x08, certs.selfSigned.certificate.toDer()},
                                              {0x0e, url.toUtf8()}});

    AppSettings settings = loadAppSettings();
    settings.subscriptions_enabled = true;
    saveAppSettings(settings);
    Backend backend;
    backend.subscriptions()->stopSchedulerForTest();
    QSignalSpy consent(backend.subscriptions(), &SubscriptionController::consentRequired);
    QSignalSpy asked(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy failed(&backend, &Backend::errorOccurred);
    backend.importDeepLink(link);
    QCOMPARE(consent.count(), 1);
    QTest::qWait(200);
    QCOMPARE(server.connections(), 0);
    QVERIFY(backend.fetchSubscriptionForImport(consent.last().at(0).toString()));
    QVERIFY2(QTest::qWaitFor([&]() { return asked.count() + failed.count() > 0; }, 25000),
             "the fetch never ended");
    QVERIFY2(asked.count() == 1,
             qPrintable(failed.isEmpty() ? QStringLiteral("no question") : failed.last().at(0).toString()));
    QCOMPARE(server.connections(), 1);
    QCOMPARE(server.serverNames(), QStringList{QStringLiteral("vpn.test")});
    QVERIFY(asked.last().at(0).toString().contains(QStringLiteral("Server: vpn.test")));
    QSignalSpy imported(&backend, &Backend::configImported);
    QVERIFY(backend.confirmDeepLinkImport(asked.last().at(1).toString(), false));
    QCOMPARE(imported.count(), 1);
#else
    QSKIP("needs the openssl command to make the test server's certificate");
#endif
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir iniDir;
    if (!iniDir.isValid())
        return 1;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, iniDir.path());

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("BackendSubscriptionTest"));
    app.setOrganizationName(QStringLiteral("FreeTunnelTest"));
    TestBackendSubscription tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_backend_subscription.moc"
