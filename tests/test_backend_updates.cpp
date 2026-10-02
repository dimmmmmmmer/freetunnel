// cppcheck-suppress-file missingIncludeSystem
// Backend's update surface: the state machine QML binds to, and the decision
// behind the one clickable icon in the Settings row.
//
// This file existed at 2.3% line coverage while owning the code that ends in
// launching a downloaded installer. Both bugs found here were found by review,
// not by tests: a failed CHECK was reported to the user as "You have the latest
// version", and the retry icon started a DOWNLOAD instead of re-checking,
// because "did the download or the check fail?" was inferred from
// m_latestVersion — which is set by the first successful check and never
// cleared.
#include <QtTest>
#include <QTranslator>
#include <QScopeGuard>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDesktopServices>
#include <QUrl>

#include <memory>

#include "app/Backend.h"
#include "core/UpdateChecker.h"
#include "mock_http_server.h"

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
#include <unistd.h>
#endif

class TestBackendUpdates : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void checkFindsNewerVersion();
    void checkFindsNothingNewer();
    void failedCheckReportsTheFailure();
    void retryAfterFailedCheckChecksAgainInsteadOfDownloading();
    void backgroundCheckDoesNotPaintCheckingState();
    void asecondCheckWhileOneIsRunningIsIgnored();
    void aCheckWaitsForARunningDownload();
    void aCheckJoinsTheBackgroundCheckStillOut();
    void theUpdateLineFollowsALanguageChange();
    void aReleaseWithNothingForThisPlatformOffersItsPage();
    void aRefusedDownloadIsCheckedAgainNotRetried();
    void aFailedDownloadIsRetried();
    void aStalledCheckGivesUp();
    void downloadsLeftByAnEarlierRunAreDiscarded();
#if defined(Q_OS_UNIX)
    void aStagingDirThatIsALinkIsLeftAlone();
#endif
#if defined(Q_OS_WIN)
    void anInstallerThatWillNotStartKeepsFreeTunnelOpen();
#elif !defined(Q_OS_MACOS)
    void aDebIsShownNotInstalled();
    void anAppImageThatCannotBeReplacedIsLeftAlone();
    void anAppImageThatWillNotStartIsPutBack();
    void aNameThatCannotBeTakenBackIsReported();
    // Last: they end in quitApplication(), as a real update does.
    void anAppImageReplacesItselfAndRestarts();
    void anUnpackedAppImageIsStartedUnpackedAgain();
#endif

    void cleanupTestCase();

private:
    // Serve one release payload and wait for the check to settle.
    static void serveRelease(MockHttpServer &http, const QString &tag, bool withInstaller = false,
                             int delayMs = 0);
    static void serveRealRelease(MockHttpServer &http, const QString &tag,
                                 const QHash<QString, QByteArray> &installers);
    static bool settled(const Backend &backend);
    static QString stagingDir();

    QTemporaryDir m_home;
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    // Stands in for xdg-open, which would otherwise open a real file manager.
    QTemporaryDir m_bin;
    QString revealed() const;
#endif
};

// The installers a release publishes, under the names the release job gives
// them, in the order SHA256SUMS.txt lists them.
static const QStringList kReleaseInstallers = {
    QStringLiteral("freetunnel-linux-x86_64.deb"),
    QStringLiteral("freetunnel-macos-universal.dmg"),
    QStringLiteral("freetunnel-windows-x86_64-Setup.exe"),
    QStringLiteral("freetunnel-x86_64.AppImage"),
};

void TestBackendUpdates::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BackendUpdatesTest"));
    // Installing an AppImage gives up the single-instance socket's name. Never
    // the real one, which a FreeTunnel running on this machine is listening on.
    qputenv("FT_TEST_INSTANCE_NAME", QStringLiteral("freetunnel-backend-updates-%1")
                                             .arg(QCoreApplication::applicationPid())
                                             .toUtf8());
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    QVERIFY(m_bin.isValid());
    QFile stub(m_bin.filePath(QStringLiteral("xdg-open")));
    QVERIFY(stub.open(QIODevice::WriteOnly));
    stub.write("#!/bin/sh\nprintf '%s\\n' \"$@\" >> \"$(dirname \"$0\")/revealed\"\n");
    stub.close();
    QVERIFY(stub.setPermissions(stub.permissions() | QFileDevice::ExeOwner));
    qputenv("PATH", (m_bin.path() + QLatin1Char(':') + qEnvironmentVariable("PATH")).toUtf8());
#endif
}

void TestBackendUpdates::cleanupTestCase()
{
    QDir(stagingDir()).removeRecursively();
}

QString TestBackendUpdates::stagingDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/updates");
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// What the xdg-open stand-in was asked to open, one argument a line.
QString TestBackendUpdates::revealed() const
{
    QFile log(m_bin.filePath(QStringLiteral("revealed")));
    return log.open(QIODevice::ReadOnly) ? QString::fromUtf8(log.readAll()) : QString();
}
#endif

// A release as the release job publishes it: the four installers under their
// real names, SHA256SUMS.txt opening with its #version line and the digest of
// each, and the signature beside it. The signature is a stand-in: this binary
// has the real public key compiled in, so the tests that download set
// FT_TEST_SKIP_UPDATE_SIG. Installers not given are served as filler.
void TestBackendUpdates::serveRealRelease(MockHttpServer &http, const QString &tag,
                                          const QHash<QString, QByteArray> &installers)
{
    const QString base = http.baseUrl();
    QByteArray sums = "#version=" + tag.mid(1).toUtf8() + '\n';
    QJsonArray assets;
    const auto publish = [&](const QString &name, const QByteArray &body) {
        QJsonObject asset;
        asset[QStringLiteral("name")] = name;
        asset[QStringLiteral("browser_download_url")] = base + QLatin1Char('/') + name;
        assets.append(asset);
        MockHttpServer::Route route;
        route.body = body;
        route.contentType = QByteArrayLiteral("application/octet-stream");
        http.setRoute(QLatin1Char('/') + name, route);
    };
    for (const QString &name : kReleaseInstallers) {
        const QByteArray body = installers.value(name, QByteArrayLiteral("another platform's installer"));
        sums += QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex() + "  "
                + name.toUtf8() + '\n';
        publish(name, body);
    }
    publish(QStringLiteral("SHA256SUMS.txt"), sums);
    publish(QStringLiteral("SHA256SUMS.txt.sig"), QByteArrayLiteral("stand-in signature"));

    QJsonObject release;
    release[QStringLiteral("tag_name")] = tag;
    release[QStringLiteral("html_url")] = base + QStringLiteral("/release");
    release[QStringLiteral("assets")] = assets;
    MockHttpServer::Route route;
    route.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), route);
}

void TestBackendUpdates::serveRelease(MockHttpServer &http, const QString &tag, bool withInstaller,
                                      int delayMs)
{
    const QString base = http.baseUrl();
    QJsonObject release;
    release[QStringLiteral("tag_name")] = tag;
    release[QStringLiteral("html_url")] = base + QStringLiteral("/release");

    // Asset URLs on the mock host: UpdateChecker trusts its own test base, so the
    // release resolves without reaching github.com. The URL/tag validation itself
    // is covered in test_update_checker_e2e.
    QJsonArray assets;
    QJsonObject sums;
    sums[QStringLiteral("name")] = QStringLiteral("SHA256SUMS.txt");
    sums[QStringLiteral("browser_download_url")] = base + QStringLiteral("/checksums");
    assets.append(sums);
    if (withInstaller) {
        // Named the way this platform's installer is, so the download has something
        // to fetch; the fetch itself is left to fail on the mock.
#if defined(Q_OS_WIN)
        const QString installer = QStringLiteral("freetunnel-test.exe");
#elif defined(Q_OS_MACOS)
        const QString installer = QStringLiteral("freetunnel-test.dmg");
#else
        const QString installer = QStringLiteral("freetunnel-test.AppImage");
#endif
        QJsonObject asset;
        asset[QStringLiteral("name")] = installer;
        asset[QStringLiteral("browser_download_url")] = base + QStringLiteral("/installer");
        assets.append(asset);
    }
    release[QStringLiteral("assets")] = assets;

    MockHttpServer::Route route;
    route.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    route.delayMs = delayMs;
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), route);
}

bool TestBackendUpdates::settled(const Backend &backend)
{
    const QString s = backend.updateState();
    return s == QLatin1String("current") || s == QLatin1String("available")
            || s == QLatin1String("error");
}

void TestBackendUpdates::checkFindsNewerVersion()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    serveRelease(http, QStringLiteral("v99.0.0"));

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);

    QCOMPARE(backend.updateState(), QStringLiteral("available"));
    QCOMPARE(backend.latestVersion(), QStringLiteral("99.0.0"));
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("99.0.0")),
             qPrintable(backend.updateMessage()));
    // Installing it closes FreeTunnel on Windows, and the tunnel with it: said
    // before the click. Not here, where this test is not an AppImage.
#if defined(Q_OS_WIN)
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("closes FreeTunnel")), qPrintable(backend.updateMessage()));
#else
    QVERIFY2(!backend.updateMessage().contains(QStringLiteral("closes FreeTunnel")), qPrintable(backend.updateMessage()));
#endif
    qunsetenv("FT_GITHUB_API_BASE");
}

// Clicking "Check for updates" twice, or a background timer firing while the
// user has just asked, must not start two checks. Two guards stand between that
// and the network — one keeps a single UpdateChecker, the other refuses to start
// while a check is already in flight — and the sweep found neither was tested.
//
// The observation has to be the request count. Once both checks settle, the
// state, the version and the message all read exactly the same whether one check
// ran or two, so nothing the Backend exposes can tell them apart.
// A check started mid-download answered "available" again and offered the same
// download a second time, into the staging file the first one was still using.
void TestBackendUpdates::aCheckWaitsForARunningDownload()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    serveRelease(http, QStringLiteral("v99.0.0"), /*withInstaller=*/true);

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    QCOMPARE(backend.updateState(), QStringLiteral("available"));

    backend.downloadUpdate();
    QCOMPARE(backend.updateState(), QStringLiteral("downloading"));
    backend.checkForUpdates(true);
    QCOMPARE(backend.updateState(), QStringLiteral("downloading"));
}

// The check Backend starts by itself at startup does not show "checking", so a
// click on "Check for updates" while it was still out started a second one.
// Both answers announce the release: the first let the download start, and the
// second, arriving in the middle of it, offered the same download again — into
// the staging file the first was still writing. The click now waits for the
// check that is already out.
void TestBackendUpdates::aCheckJoinsTheBackgroundCheckStillOut()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    // Slow enough that the click lands while the background check is out.
    serveRelease(http, QStringLiteral("v99.0.0"), /*withInstaller=*/true, /*delayMs=*/300);
    MockHttpServer::Route stalled;
    stalled.silent = true;
    http.setRoute(QStringLiteral("/checksums"), stalled); // the download stays under way
    const QString latest = QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest");

    Backend backend;
    backend.checkForUpdates(false);
    QTRY_COMPARE_WITH_TIMEOUT(http.requestCount(latest), 1, 5000);
    backend.checkForUpdates(true);
    QCOMPARE(backend.updateState(), QStringLiteral("checking"));
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);

    backend.downloadUpdate();
    QCOMPARE(backend.updateState(), QStringLiteral("downloading"));
    // Time for a second answer to arrive, had a second check gone out.
    QTest::qWait(1000);
    QCOMPARE(backend.updateState(), QStringLiteral("downloading"));
    QCOMPARE(http.requestCount(latest), 1);
}

// The update line is worded in C++ and kept. A language switch re-rendered every
// string in the QML and left this one in the language it was made in.
void TestBackendUpdates::theUpdateLineFollowsALanguageChange()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    serveRelease(http, QStringLiteral("v99.0.0"));

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    const QString english = backend.updateMessage();

    QTranslator russian;
    QVERIFY(russian.load(QStringLiteral(":/i18n/freetunnel_ru.qm")));
    QCoreApplication::installTranslator(&russian);
    const auto remove = qScopeGuard([&russian] { QCoreApplication::removeTranslator(&russian); });
    backend.retranslate();
    // On Windows the line also says that installing closes FreeTunnel.
#if defined(Q_OS_WIN)
    const char *source = "Version %1 is available — installing it closes FreeTunnel";
#else
    const char *source = "Version %1 is available";
#endif
    QCOMPARE(backend.updateMessage(),
             QCoreApplication::translate("Backend", source).arg(QStringLiteral("99.0.0")));
    QVERIFY2(backend.updateMessage() != english, "the catalogue has no Russian for it");
}

void TestBackendUpdates::asecondCheckWhileOneIsRunningIsIgnored()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    serveRelease(http, QStringLiteral("v99.0.0"));

    Backend backend;
    backend.checkForUpdates(true);
    backend.checkForUpdates(true);   // the impatient second click
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);

    QCOMPARE(backend.updateState(), QStringLiteral("available"));
    QCOMPARE(http.requestCount(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest")), 1);

    // And only one updater exists to have made it. ensureUpdater() parents each
    // one to the Backend, so a missing guard would leave a pile of them wired to
    // the same signals, every one answering the next check.
    QCOMPARE(backend.findChildren<UpdateChecker *>().size(), 1);

    // A later check is still allowed — the guard is about concurrency, not a
    // one-shot latch.
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    QCOMPARE(http.requestCount(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest")), 2);
    QCOMPARE(backend.findChildren<UpdateChecker *>().size(), 1);

    qunsetenv("FT_GITHUB_API_BASE");
}

void TestBackendUpdates::checkFindsNothingNewer()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    // 0.0.1 can never be newer than whatever this build reports.
    serveRelease(http, QStringLiteral("v0.0.1"));

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);

    QCOMPARE(backend.updateState(), QStringLiteral("current"));
    qunsetenv("FT_GITHUB_API_BASE");
}

// The regression that mattered most: an offline user was told they were up to
// date, because the checker reports its own failures through the same signal it
// uses for "nothing newer".
void TestBackendUpdates::failedCheckReportsTheFailure()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    // No route registered for the releases path -> the request fails.

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);

    QCOMPARE(backend.updateState(), QStringLiteral("error"));
    QVERIFY2(!backend.updateMessage().contains(QStringLiteral("latest version"),
                                               Qt::CaseInsensitive),
             qPrintable(backend.updateMessage()));
    QVERIFY(!backend.updateMessage().isEmpty());
    qunsetenv("FT_GITHUB_API_BASE");
}

// After a check that FAILED there is nothing resolved to download, so the icon
// must retry the check. This only regressed once a release had already been
// seen, so the test establishes that state first — which is exactly what the
// old m_latestVersion proxy got wrong.
void TestBackendUpdates::retryAfterFailedCheckChecksAgainInsteadOfDownloading()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    serveRelease(http, QStringLiteral("v99.0.0"));

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    QCOMPARE(backend.updateState(), QStringLiteral("available")); // a release IS known now

    // Now make the next check fail, the way going offline would.
    MockHttpServer::Route broken;
    broken.status = 500;
    broken.body = QByteArrayLiteral("nope");
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), broken);

    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    QCOMPARE(backend.updateState(), QStringLiteral("error"));

    // The user clicks the icon. It must re-check — and a re-check is observable
    // as the "checking" state, which a download never produces.
    QSignalSpy changed(&backend, &Backend::updateChanged);
    serveRelease(http, QStringLiteral("v99.0.0"));
    backend.openLatestRelease();

    bool sawChecking = backend.updateState() == QLatin1String("checking");
    QTRY_VERIFY_WITH_TIMEOUT(
            [&]() {
                if (backend.updateState() == QLatin1String("checking"))
                    sawChecking = true;
                return sawChecking && settled(backend);
            }(),
            10000);
    QVERIFY2(sawChecking, "the retry did not re-check — it went straight to a download");
    QVERIFY2(backend.updateState() != QLatin1String("downloading")
                     && backend.updateState() != QLatin1String("ready"),
             qPrintable(backend.updateState()));
    qunsetenv("FT_GITHUB_API_BASE");
}

// The automatic check at startup must not paint the Settings row: the user did
// not ask, and a spinner appearing on its own reads as something being wrong.
void TestBackendUpdates::backgroundCheckDoesNotPaintCheckingState()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    serveRelease(http, QStringLiteral("v0.0.1")); // nothing newer

    Backend backend;
    backend.checkForUpdates(false);
    QCOMPARE(backend.updateState(), QString()); // not "checking"

    // And a background check that finds nothing must stay silent rather than
    // announcing "you are up to date" nobody asked about.
    QTest::qWait(1500);
    QVERIFY2(backend.updateState().isEmpty() || backend.updateState() == QLatin1String("available"),
             qPrintable(backend.updateState()));
    qunsetenv("FT_GITHUB_API_BASE");
}

namespace {

// Records what would have been handed to the desktop to open.
class UrlCatcher : public QObject {
    Q_OBJECT
public:
    QList<QUrl> urls;
public slots:
    void handle(const QUrl &url) { urls << url; }
};

} // namespace

// A release with nothing to install on this platform failed the same way on every
// retry, and ↻ was all the row offered; the first click visibly did nothing. The
// release page is where the user can see what there is.
void TestBackendUpdates::aReleaseWithNothingForThisPlatformOffersItsPage()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    serveRelease(http, QStringLiteral("v99.0.0")); // no installer

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    QCOMPARE(backend.updateState(), QStringLiteral("available"));
    backend.downloadUpdate();
    QCOMPARE(backend.updateState(), QStringLiteral("error"));
    QVERIFY(backend.updateErrorOpensPage());

    UrlCatcher catcher;
    QDesktopServices::setUrlHandler(QStringLiteral("http"), &catcher, "handle");
    const auto unhandle = qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("http")); });
    backend.openLatestRelease();
    QCOMPARE(backend.updateState(), QStringLiteral("error"));
    QCOMPARE(catcher.urls, QList<QUrl>{QUrl(http.baseUrl() + QStringLiteral("/release"))});

    // A check starts over: whatever the next release has, it is offered afresh.
    backend.checkForUpdates(true);
    QVERIFY(!backend.updateErrorOpensPage());
}

// An unsigned release, or one whose signature does not hold, is refused. Retrying
// the download fetched the same refused release; checking again may find it fixed.
void TestBackendUpdates::aRefusedDownloadIsCheckedAgainNotRetried()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    serveRelease(http, QStringLiteral("v99.0.0"), /*withInstaller=*/true);
    MockHttpServer::Route sums;
    sums.body = QByteArrayLiteral("0000  freetunnel-test\n");
    sums.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sums); // and no signature: refused

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    backend.downloadUpdate();
    QTRY_VERIFY_WITH_TIMEOUT(backend.updateState() == QLatin1String("error"), 10000);
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("not signed")), qPrintable(backend.updateMessage()));
    QVERIFY(!backend.updateErrorOpensPage());

    const int fetched = http.requestCount(QStringLiteral("/checksums"));
    backend.openLatestRelease();
    QCOMPARE(backend.updateState(), QStringLiteral("checking"));
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    QCOMPARE(http.requestCount(QStringLiteral("/checksums")), fetched);
}

// A download that failed on the way, a network error say, is retried: the same
// release may well come down the next time.
void TestBackendUpdates::aFailedDownloadIsRetried()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    serveRelease(http, QStringLiteral("v99.0.0"), /*withInstaller=*/true);
    MockHttpServer::Route broken;
    broken.status = 503;
    http.setRoute(QStringLiteral("/checksums"), broken);

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
    backend.downloadUpdate();
    QTRY_VERIFY_WITH_TIMEOUT(backend.updateState() == QLatin1String("error"), 10000);
    QVERIFY(!backend.updateErrorOpensPage());
    backend.openLatestRelease();
    QCOMPARE(backend.updateState(), QStringLiteral("downloading"));
}

// With no timeout, a connection that stopped answering held "Checking…" until the
// system gave up on it, many minutes on, with every way out of the row blocked.
void TestBackendUpdates::aStalledCheckGivesUp()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_UPDATE_TRANSFER_TIMEOUT_MS", "300");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_UPDATE_TRANSFER_TIMEOUT_MS");
    });
    MockHttpServer::Route silent;
    silent.silent = true;
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), silent);

    Backend backend;
    backend.checkForUpdates(true);
    QCOMPARE(backend.updateState(), QStringLiteral("checking"));
    QTRY_VERIFY_WITH_TIMEOUT(backend.updateState() == QLatin1String("error"), 10000);
    // In words that do not say the user cancelled something.
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("stopped responding")),
             qPrintable(backend.updateMessage()));
}

// Nothing removed what a download staged: each update left 100 MB or more in the
// cache for good, and an AppImage downloaded under the name AppImages used to
// have (freetunnel-linux-x86_64.AppImage) was not even replaced by the next
// download, which uses the new one. The first check of a run clears whatever an
// earlier run staged.
void TestBackendUpdates::downloadsLeftByAnEarlierRunAreDiscarded()
{
    QVERIFY(QDir().mkpath(stagingDir()));
    const QStringList leftovers = {QStringLiteral("freetunnel-linux-x86_64.AppImage"),
                                   QStringLiteral("freetunnel-x86_64.AppImage"),
                                   QStringLiteral("freetunnel-windows-x86_64-Setup.exe")};
    for (const QString &name : leftovers) {
        QFile f(stagingDir() + QLatin1Char('/') + name);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("an earlier run's download");
    }

    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("FT_GITHUB_API_BASE"); });
    serveRelease(http, QStringLiteral("v0.0.1"));

    Backend backend;
    backend.checkForUpdates(true);
    for (const QString &name : leftovers)
        QVERIFY2(!QFile::exists(stagingDir() + QLatin1Char('/') + name), qPrintable(name));
    QTRY_VERIFY_WITH_TIMEOUT(settled(backend), 10000);
}

#if defined(Q_OS_UNIX)
// The staging directory's path follows XDG_CACHE_HOME, which can lie somewhere
// shared. A symlink another user put there in its place is not ours to empty:
// clearing out downloads through it deleted every file of ours in the directory
// it pointed to.
void TestBackendUpdates::aStagingDirThatIsALinkIsLeftAlone()
{
    // A cache path of this run's own, so nothing else ever finds the link there.
    const QString appName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(
            QStringLiteral("%1-link-%2").arg(appName).arg(QCoreApplication::applicationPid()));
    const auto restoreName = qScopeGuard([&] { QCoreApplication::setApplicationName(appName); });
    const QString parent = QFileInfo(stagingDir()).absolutePath();
    QVERIFY(QDir().mkpath(parent));
    const auto noParent = qScopeGuard([&] { QDir(parent).removeRecursively(); });

    QTemporaryDir elsewhere;
    QVERIFY(elsewhere.isValid());
    const QString theirs = elsewhere.filePath(QStringLiteral("not a download"));
    QFile f(theirs);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("kept");
    f.close();
    QVERIFY(QFile::link(elsewhere.path(), stagingDir()));
    const auto noLink = qScopeGuard([] { QFile::remove(stagingDir()); });

    UpdateChecker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"))
            .discardStagedDownloads();
    QVERIFY(QFile::exists(theirs));
}
#endif

#if defined(Q_OS_WIN)
// Quitting when the installer never started left the user with no FreeTunnel and
// no VPN. The installer's start is refused through a test hook rather than by
// handing Windows a file that is not a program: on a fresh CI runner that launch
// hung past the test's five-minute limit, and passed in seconds when run again.
void TestBackendUpdates::anInstallerThatWillNotStartKeepsFreeTunnelOpen()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    qputenv("FT_TEST_INSTALLER_WONT_START", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
        qunsetenv("FT_TEST_INSTALLER_WONT_START");
    });
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-windows-x86_64-Setup.exe"),
                       QByteArrayLiteral("not a program")}});

    Backend backend;
    QSignalSpy shutdown(&backend, &Backend::aboutToShutdown);
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    QCOMPARE(backend.findChild<UpdateChecker *>()->latestRelease().assetName,
             QStringLiteral("freetunnel-windows-x86_64-Setup.exe"));
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("error"), 10000);
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("could not be started")),
             qPrintable(backend.updateMessage()));
    QCOMPARE(shutdown.count(), 0);
}
#elif !defined(Q_OS_MACOS)
// A .deb is the package manager's to install. The download is shown in the file
// manager, and kept: deleting it after "use" would delete it before the user has
// installed it. The AppImage beside it in the release is not downloaded.
void TestBackendUpdates::aDebIsShownNotInstalled()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
    });
    const QByteArray deb = QByteArrayLiteral("the new .deb");
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-linux-x86_64.deb"), deb}});

    Backend backend;
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    QVERIFY2(!backend.updateMessage().contains(QStringLiteral("closes FreeTunnel")),
             qPrintable(backend.updateMessage()));
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("ready"), 10000);

    QVERIFY2(backend.updateMessage().contains(QStringLiteral("file manager")),
             qPrintable(backend.updateMessage()));
    QTRY_VERIFY_WITH_TIMEOUT(revealed().contains(stagingDir()), 5000);
    QFile staged(stagingDir() + QStringLiteral("/freetunnel-linux-x86_64.deb"));
    QVERIFY(staged.open(QIODevice::ReadOnly));
    QCOMPARE(staged.readAll(), deb);
    QCOMPARE(http.requestCount(QStringLiteral("/freetunnel-x86_64.AppImage")), 0);
}

namespace {

// An AppImage install in a directory of its own: the file FreeTunnel would be
// running from, named the way users keep it, and what the updater is told it is.
struct InstalledAppImage {
    QTemporaryDir dir;
    QString path;
    QString backup;

    bool create()
    {
        if (!dir.isValid())
            return false;
        path = dir.filePath(QStringLiteral("FreeTunnel.AppImage"));
        backup = path + QStringLiteral(".old");
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write("the running build") < 0)
            return false;
        f.close();
        qputenv("FT_TEST_UPDATER_APPIMAGE", path.toUtf8());
        return f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    }
    ~InstalledAppImage() { qunsetenv("FT_TEST_UPDATER_APPIMAGE"); }

    QByteArray contents() const
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
};

} // namespace

// The directory the AppImage sits in cannot be written to: nothing is changed,
// and the row says so and offers to try again.
void TestBackendUpdates::anAppImageThatCannotBeReplacedIsLeftAlone()
{
    if (::geteuid() == 0)
        QSKIP("root writes into a read-only directory all the same");
    InstalledAppImage installed;
    QVERIFY(installed.create());
    QVERIFY(QFile::setPermissions(installed.dir.path(),
                                  QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto writable = qScopeGuard([&] {
        QFile::setPermissions(installed.dir.path(), QFileDevice::ReadOwner
                                      | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    });
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
    });
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-x86_64.AppImage"), QByteArrayLiteral("new")}});

    Backend backend;
    QSignalSpy shutdown(&backend, &Backend::aboutToShutdown);
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("error"), 10000);
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("Could not replace")),
             qPrintable(backend.updateMessage()));
    QCOMPARE(installed.contents(), QByteArrayLiteral("the running build"));
    QCOMPARE(shutdown.count(), 0);
}

// Quitting whether or not the new AppImage started left no FreeTunnel running at
// all when it did not. A file that is no program at all fails to start the same
// way at exec. The build that was running is put back, this instance stays, and
// it is again the one a second launch reaches.
void TestBackendUpdates::anAppImageThatWillNotStartIsPutBack()
{
    InstalledAppImage installed;
    QVERIFY(installed.create());
    QLocalServer listener;
    listener.setSocketOptions(QLocalServer::UserAccessOption); // as the application's is
    const QString instanceName = qEnvironmentVariable("FT_TEST_INSTANCE_NAME");
    QLocalServer::removeServer(instanceName);
    QVERIFY(listener.listen(instanceName));

    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
    });
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-x86_64.AppImage"),
                       QByteArrayLiteral("not a program, so exec refuses it\n")}});

    Backend backend;
    backend.setInstanceServer(&listener);
    QSignalSpy shutdown(&backend, &Backend::aboutToShutdown);
    QSignalSpy handedOver(&backend, &Backend::instanceNameHandedOver);
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("error"), 10000);

    QVERIFY2(backend.updateMessage().contains(QStringLiteral("could not be started")),
             qPrintable(backend.updateMessage()));
    QCOMPARE(shutdown.count(), 0);
    QCOMPARE(handedOver.count(), 0); // and its token is its own still, to delete on quit
    QCOMPARE(installed.contents(), QByteArrayLiteral("the running build"));
    QVERIFY(!QFile::exists(installed.backup));
    QLocalSocket probe;
    probe.connectToServer(instanceName);
    QVERIFY2(probe.waitForConnected(2000), qPrintable(probe.errorString()));
}

// After a failed start this instance listens again, so that a second launch
// reaches it rather than starting a second FreeTunnel. When that listen fails it
// used to say nothing at all; it says so in the log now. Here the folder the
// name lives in turns read-only once this instance listens, so the name can be
// neither given up nor taken again.
void TestBackendUpdates::aNameThatCannotBeTakenBackIsReported()
{
    if (::geteuid() == 0)
        QSKIP("root writes into a read-only directory all the same");
    InstalledAppImage installed;
    QVERIFY(installed.create());
    QTemporaryDir sockets;
    QVERIFY(sockets.isValid());
    const QString instanceName = sockets.filePath(QStringLiteral("instance"));
    const QByteArray previousName = qgetenv("FT_TEST_INSTANCE_NAME");
    qputenv("FT_TEST_INSTANCE_NAME", instanceName.toUtf8());
    QLocalServer listener;
    listener.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY2(listener.listen(instanceName), qPrintable(listener.errorString()));
    QVERIFY(QFile::setPermissions(sockets.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const auto restore = qScopeGuard([&] {
        QFile::setPermissions(sockets.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                      | QFileDevice::ExeOwner);
        qputenv("FT_TEST_INSTANCE_NAME", previousName);
    });

    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
    });
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-x86_64.AppImage"),
                       QByteArrayLiteral("not a program, so exec refuses it\n")}});

    Backend backend;
    backend.setInstanceServer(&listener);
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    // Counted, not looked for: the log is loaded from the file an earlier run of
    // this test wrote to.
    const QString said = QStringLiteral("may start a second FreeTunnel");
    const qsizetype saidBefore = backend.logText().count(said);
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("failed to listen again")));
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("error"), 10000);

    QCOMPARE(installed.contents(), QByteArrayLiteral("the running build"));
    QCOMPARE(backend.logText().count(said), saidBefore + 1);
}

// The whole of a successful AppImage update: the release's AppImage, not its
// .deb, replaces the file FreeTunnel runs from, executable, and is started; the
// old build and the download are removed only after that; FreeTunnel quits. The
// "AppImage" is a shell script that leaves a mark when it runs.
//
// And the new build is the one a second launch reaches, even when it is
// listening before the old one has finished quitting. Only unlinking the name
// left the old listener listening, so when the application destroyed it on the
// way out it unlinked the name again — the new build's socket by then. The new
// FreeTunnel ran on where nothing could reach it, and the next launch or tt://
// link started a second one beside it.
void TestBackendUpdates::anAppImageReplacesItselfAndRestarts()
{
    InstalledAppImage installed;
    QVERIFY(installed.create());
    const QString mark = installed.dir.filePath(QStringLiteral("started"));
    const QByteArray newBuild = "#!/bin/sh\n: > '" + mark.toUtf8() + "'\n";
    // This instance's listener, which the application destroys as it quits.
    auto listener = std::make_unique<QLocalServer>();
    listener->setSocketOptions(QLocalServer::UserAccessOption);
    const QString instanceName = qEnvironmentVariable("FT_TEST_INSTANCE_NAME");
    QLocalServer::removeServer(instanceName);
    QVERIFY(listener->listen(instanceName));

    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
    });
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-x86_64.AppImage"), newBuild}});

    Backend backend;
    backend.setInstanceServer(listener.get());
    QSignalSpy shutdown(&backend, &Backend::aboutToShutdown);
    QSignalSpy handedOver(&backend, &Backend::instanceNameHandedOver);
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    // Said before the click: installing an AppImage closes FreeTunnel.
    QVERIFY2(backend.updateMessage().contains(QStringLiteral("closes FreeTunnel")),
             qPrintable(backend.updateMessage()));
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(shutdown.count(), 1, 10000);
    // Quitting is to leave the token to the new build (wireBackendLifecycle()).
    QCOMPARE(handedOver.count(), 1);

    QCOMPARE(backend.updateState(), QStringLiteral("ready"));
    QCOMPARE(installed.contents(), newBuild);
    const QFileDevice::Permissions runnable = QFileDevice::ExeOwner | QFileDevice::ExeGroup
            | QFileDevice::ExeOther | QFileDevice::ReadOther;
    QCOMPARE(QFile::permissions(installed.path) & runnable, runnable);
    QVERIFY(!QFile::exists(installed.backup));
    QVERIFY(!QFile::exists(stagingDir() + QStringLiteral("/freetunnel-x86_64.AppImage")));
    QCOMPARE(http.requestCount(QStringLiteral("/freetunnel-linux-x86_64.deb")), 0);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(mark), 5000);

    // The new build listens, and only then is the old one's listener destroyed.
    QLocalServer replacement;
    replacement.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY2(replacement.listen(instanceName), qPrintable(replacement.errorString()));
    listener.reset();
    QLocalSocket probe;
    probe.connectToServer(instanceName);
    QVERIFY2(probe.waitForConnected(2000), qPrintable(probe.errorString()));
}

// Started without FUSE, an AppImage has to be started the same way again, or the
// new build may not start at all and nothing is left running. The "AppImage" is
// a shell script that writes down what it was started with.
void TestBackendUpdates::anUnpackedAppImageIsStartedUnpackedAgain()
{
    InstalledAppImage installed;
    QVERIFY(installed.create());
    qputenv("FT_TEST_UPDATER_APPIMAGE_EXTRACTED", "1");
    const auto unsetHook = qScopeGuard([] { qunsetenv("FT_TEST_UPDATER_APPIMAGE_EXTRACTED"); });
    const QString mark = installed.dir.filePath(QStringLiteral("started-with"));
    const QByteArray newBuild = "#!/bin/sh\nprintf '%s\\n' \"$@\" > '" + mark.toUtf8() + "'\n";

    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");
    const auto unset = qScopeGuard([] {
        qunsetenv("FT_GITHUB_API_BASE");
        qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
    });
    serveRealRelease(http, QStringLiteral("v99.0.0"),
                     {{QStringLiteral("freetunnel-x86_64.AppImage"), newBuild}});

    Backend backend;
    QSignalSpy shutdown(&backend, &Backend::aboutToShutdown);
    backend.checkForUpdates(true);
    QTRY_COMPARE_WITH_TIMEOUT(backend.updateState(), QStringLiteral("available"), 10000);
    backend.downloadUpdate();
    QTRY_COMPARE_WITH_TIMEOUT(shutdown.count(), 1, 10000);

    QCOMPARE(installed.contents(), newBuild);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(mark), 5000);
    QFile startedWith(mark);
    QTRY_VERIFY_WITH_TIMEOUT(startedWith.size() > 0, 5000);
    QVERIFY(startedWith.open(QIODevice::ReadOnly));
    QCOMPARE(startedWith.readAll(), QByteArrayLiteral("--appimage-extract-and-run\n"));
}
#endif

QTEST_MAIN(TestBackendUpdates)
#include "test_backend_updates.moc"
