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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QDesktopServices>
#include <QUrl>

#include "app/Backend.h"
#include "core/UpdateChecker.h"
#include "mock_http_server.h"

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
    void theUpdateLineFollowsALanguageChange();
    void aReleaseWithNothingForThisPlatformOffersItsPage();
    void aRefusedDownloadIsCheckedAgainNotRetried();
    void aFailedDownloadIsRetried();
    void aStalledCheckGivesUp();

private:
    // Serve one release payload and wait for the check to settle.
    static void serveRelease(MockHttpServer &http, const QString &tag, bool withInstaller = false);
    static bool settled(const Backend &backend);

    QTemporaryDir m_home;
};

void TestBackendUpdates::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BackendUpdatesTest"));
}

void TestBackendUpdates::serveRelease(MockHttpServer &http, const QString &tag, bool withInstaller)
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
    void open(const QUrl &url) { urls << url; }
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
    QDesktopServices::setUrlHandler(QStringLiteral("http"), &catcher, "open");
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

QTEST_MAIN(TestBackendUpdates)
#include "test_backend_updates.moc"
