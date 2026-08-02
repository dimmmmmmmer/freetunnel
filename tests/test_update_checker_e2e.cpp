// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryFile>

#include "core/ReleaseVerify.h"
#include "core/UpdateChecker.h"
#include "mock_http_server.h"

class TestUpdateCheckerE2e : public QObject {
    Q_OBJECT

private slots:
    void checkNowFindsNewerRelease();
    void checkNowNoUpdateWhenCurrent();
    void checkNowNetworkError();
    void checkNowInvalidJson();
    void downloadVerifiesChecksum();
    void downloadRejectsBadChecksum();
    void downloadRejectsMissingChecksums();
    void downloadRejectsMissingSignature();
    void downloadRejectsInvalidSignature();
    void rejectsAssetsFromAnotherRepo();
    void rejectsAssetsWhosePathTagDiffersFromTheRelease();
    void acceptsAssetsPublishedUnderThisReleaseTag();
    void replacesHostileHtmlUrlWithTheCanonicalReleasePage();
    void rejectsImplausibleTag();
};


static QString sha256HexOfBytes(const QByteArray &body)
{
    QTemporaryFile tf;
    tf.setAutoRemove(true);
    if (!tf.open())
        return QString();
    tf.write(body);
    tf.close();
    return sha256HexOfFile(tf.fileName());
}


static QString testInstallerAssetName()
{
#if defined(_WIN32)
    return QStringLiteral("freetunnel-test.exe");
#elif defined(__APPLE__)
    return QStringLiteral("freetunnel-test.dmg");
#else
    return QStringLiteral("freetunnel-test.AppImage");
#endif
}

static QJsonObject makeReleaseJson(const QString &tag, const QString &baseUrl,
                                   const QString &installerName)
{
    QJsonObject release;
    release[QStringLiteral("tag_name")] = tag;
    release[QStringLiteral("html_url")] = baseUrl + QStringLiteral("/release");
    release[QStringLiteral("body")] = QStringLiteral("notes");

    QJsonArray assets;
    QJsonObject sumsAsset;
    sumsAsset[QStringLiteral("name")] = QStringLiteral("SHA256SUMS.txt");
    sumsAsset[QStringLiteral("browser_download_url")] = baseUrl + QStringLiteral("/checksums");
    assets.append(sumsAsset);

    QJsonObject installerAsset;
    installerAsset[QStringLiteral("name")] = installerName;
    installerAsset[QStringLiteral("browser_download_url")] = baseUrl + QStringLiteral("/installer");
    assets.append(installerAsset);

    release[QStringLiteral("assets")] = assets;
    return release;
}

// The URL/tag gatekeepers are SKIPPED for hosts matching FT_GITHUB_API_BASE, so
// tests that build every URL from the mock's base never execute them — deleting
// the checks would keep the suite green. These helpers keep the API on the mock
// (so checkNow() still reaches it) while putting REAL github.com URLs in the
// payload, which is what makes the validators run.
static QJsonObject assetJson(const QString &name, const QString &url)
{
    QJsonObject a;
    a[QStringLiteral("name")] = name;
    a[QStringLiteral("browser_download_url")] = url;
    return a;
}

static QJsonObject releaseWithAssetBase(const QString &tag, const QString &assetBase,
                                        const QString &htmlUrl)
{
    QJsonObject release;
    release[QStringLiteral("tag_name")] = tag;
    release[QStringLiteral("html_url")] = htmlUrl;
    QJsonArray assets;
    assets.append(assetJson(QStringLiteral("SHA256SUMS.txt"), assetBase + QStringLiteral("/SHA256SUMS.txt")));
    assets.append(assetJson(testInstallerAssetName(),
                            assetBase + QStringLiteral("/") + testInstallerAssetName()));
    release[QStringLiteral("assets")] = assets;
    return release;
}

// Serve `release` from the mock and run a check against it.
static bool runCheck(MockHttpServer &http, const QJsonObject &release, UpdateChecker &checker,
                     QSignalSpy &available, QSignalSpy &none)
{
    MockHttpServer::Route route;
    route.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), route);
    checker.checkNow();
    return QTest::qWaitFor([&]() { return available.count() > 0 || none.count() > 0; }, 5000);
}

void TestUpdateCheckerE2e::checkNowFindsNewerRelease()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());

    const QJsonObject release =
            makeReleaseJson(QStringLiteral("v2.0.0"), base, QStringLiteral("freetunnel-test.AppImage"));
    MockHttpServer::Route route;
    route.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), route);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);

    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return available.count() > 0 || none.count() > 0; }, 5000));
    QCOMPARE(available.count(), 1);
    QCOMPARE(checker.latestRelease().version, QStringLiteral("2.0.0"));
    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::checkNowNoUpdateWhenCurrent()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());

    const QJsonObject release =
            makeReleaseJson(QStringLiteral("v1.0.0"), base, QStringLiteral("freetunnel-test.AppImage"));
    MockHttpServer::Route route;
    route.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), route);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return none.count() > 0; }, 5000));
    QCOMPARE(none.count(), 1);
    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::downloadVerifiesChecksum()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");

    const QByteArray installerBody = QByteArrayLiteral("installer-payload");
    const QString installerName = testInstallerAssetName();
    const QString hex = sha256HexOfBytes(installerBody);
    const QByteArray sums = (hex + QStringLiteral("  ") + installerName + QChar('\n')).toUtf8();

    const QJsonObject release = makeReleaseJson(QStringLiteral("v2.0.0"), base, installerName);
    MockHttpServer::Route releaseRoute;
    releaseRoute.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), releaseRoute);

    MockHttpServer::Route sumsRoute;
    sumsRoute.body = sums;
    sumsRoute.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sumsRoute);

    MockHttpServer::Route installerRoute;
    installerRoute.body = installerBody;
    installerRoute.contentType = QByteArrayLiteral("application/octet-stream");
    http.setRoute(QStringLiteral("/installer"), installerRoute);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return available.count() > 0; }, 5000));

    QSignalSpy ready(&checker, &UpdateChecker::downloadReady);
    QSignalSpy failed(&checker, &UpdateChecker::downloadFailed);
    checker.downloadLatest();
    QVERIFY(QTest::qWaitFor([&]() { return ready.count() > 0 || failed.count() > 0; }, 10000));
    QCOMPARE(ready.count(), 1);
    QVERIFY(QFile::exists(ready.at(0).at(0).toString()));

    qunsetenv("FT_GITHUB_API_BASE");
    qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
}

void TestUpdateCheckerE2e::checkNowNetworkError()
{
    qputenv("FT_GITHUB_API_BASE", QByteArrayLiteral("http://127.0.0.1:1"));

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return none.count() > 0; }, 5000));
    QCOMPARE(none.count(), 1);
    QVERIFY(none.at(0).at(0).toString().contains(QStringLiteral("Network error")));

    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::checkNowInvalidJson()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());

    MockHttpServer::Route route;
    route.body = QByteArrayLiteral("not-json");
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), route);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return none.count() > 0; }, 5000));
    QCOMPARE(none.at(0).at(0).toString(), QStringLiteral("Invalid response from GitHub API"));

    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::downloadRejectsMissingChecksums()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());

    QJsonObject release;
    release[QStringLiteral("tag_name")] = QStringLiteral("v2.0.0");
    release[QStringLiteral("html_url")] = base + QStringLiteral("/release");
    release[QStringLiteral("body")] = QStringLiteral("notes");
    QJsonArray assets;
    QJsonObject installerAsset;
    installerAsset[QStringLiteral("name")] = testInstallerAssetName();
    installerAsset[QStringLiteral("browser_download_url")] = base + QStringLiteral("/installer");
    assets.append(installerAsset);
    release[QStringLiteral("assets")] = assets;

    MockHttpServer::Route releaseRoute;
    releaseRoute.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), releaseRoute);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return available.count() > 0; }, 5000));

    QSignalSpy failed(&checker, &UpdateChecker::downloadFailed);
    checker.downloadLatest();
    QVERIFY(QTest::qWaitFor([&]() { return failed.count() > 0; }, 5000));
    QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("SHA256SUMS.txt")));

    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::downloadRejectsMissingSignature()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());

    const QString installerName = testInstallerAssetName();
    const QJsonObject release = makeReleaseJson(QStringLiteral("v2.0.0"), base, installerName);
    MockHttpServer::Route releaseRoute;
    releaseRoute.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), releaseRoute);

    const QByteArray sums = QByteArrayLiteral("abc123  ") + installerName.toUtf8() + "\n";
    MockHttpServer::Route sumsRoute;
    sumsRoute.body = sums;
    sumsRoute.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sumsRoute);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return available.count() > 0; }, 5000));

    QSignalSpy failed(&checker, &UpdateChecker::downloadFailed);
    checker.downloadLatest();
    QVERIFY(QTest::qWaitFor([&]() { return failed.count() > 0; }, 5000));
    QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("not signed")));

    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::downloadRejectsInvalidSignature()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());

    const QString installerName = testInstallerAssetName();
    QJsonObject release = makeReleaseJson(QStringLiteral("v2.0.0"), base, installerName);
    QJsonArray assets = release[QStringLiteral("assets")].toArray();
    QJsonObject sigAsset;
    sigAsset[QStringLiteral("name")] = QStringLiteral("SHA256SUMS.txt.sig");
    sigAsset[QStringLiteral("browser_download_url")] = base + QStringLiteral("/signature");
    assets.append(sigAsset);
    release[QStringLiteral("assets")] = assets;

    MockHttpServer::Route releaseRoute;
    releaseRoute.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), releaseRoute);

    const QByteArray sums = QByteArrayLiteral("abc123  ") + installerName.toUtf8() + "\n";
    MockHttpServer::Route sumsRoute;
    sumsRoute.body = sums;
    sumsRoute.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sumsRoute);

    MockHttpServer::Route sigRoute;
    sigRoute.body = QByteArrayLiteral("not-a-valid-signature");
    sigRoute.contentType = QByteArrayLiteral("application/octet-stream");
    http.setRoute(QStringLiteral("/signature"), sigRoute);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return available.count() > 0; }, 5000));

    QSignalSpy failed(&checker, &UpdateChecker::downloadFailed);
    checker.downloadLatest();
    QVERIFY(QTest::qWaitFor([&]() { return failed.count() > 0; }, 10000));
    QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("signature")));

    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::downloadRejectsBadChecksum()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    const QString base = http.baseUrl();
    qputenv("FT_GITHUB_API_BASE", base.toUtf8());
    qputenv("FT_TEST_SKIP_UPDATE_SIG", "1");

    const QByteArray installerBody = QByteArrayLiteral("installer-payload");
    const QString installerName = testInstallerAssetName();
    const QByteArray sums = QByteArray("deadbeef  ") + testInstallerAssetName().toUtf8() + "\n";

    const QJsonObject release = makeReleaseJson(QStringLiteral("v2.0.0"), base, installerName);
    MockHttpServer::Route releaseRoute;
    releaseRoute.body = QJsonDocument(release).toJson(QJsonDocument::Compact);
    http.setRoute(QStringLiteral("/repos/dimmmmmmmer/freetunnel/releases/latest"), releaseRoute);

    MockHttpServer::Route sumsRoute;
    sumsRoute.body = sums;
    sumsRoute.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sumsRoute);

    MockHttpServer::Route installerRoute;
    installerRoute.body = installerBody;
    installerRoute.contentType = QByteArrayLiteral("application/octet-stream");
    http.setRoute(QStringLiteral("/installer"), installerRoute);

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    checker.checkNow();
    QVERIFY(QTest::qWaitFor([&]() { return available.count() > 0; }, 5000));

    QSignalSpy failed(&checker, &UpdateChecker::downloadFailed);
    checker.downloadLatest();
    QVERIFY(QTest::qWaitFor([&]() { return failed.count() > 0; }, 10000));
    QCOMPARE(failed.count(), 1);

    qunsetenv("FT_GITHUB_API_BASE");
    qunsetenv("FT_TEST_SKIP_UPDATE_SIG");
}

// An asset hosted under someone else's repo must not be picked up, even when the
// tag matches — otherwise a hostile API response points the installer at a file
// the attacker controls.
void TestUpdateCheckerE2e::rejectsAssetsFromAnotherRepo()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    QVERIFY(runCheck(http,
                     releaseWithAssetBase(
                             QStringLiteral("v2.0.0"),
                             QStringLiteral("https://github.com/attacker/freetunnel/releases/download/v2.0.0"),
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/tag/v2.0.0")),
                     checker, available, none));

    // The release itself is newer, so it is announced — but nothing downloadable
    // was accepted from it.
    QCOMPARE(available.count(), 1);
    QVERIFY(checker.latestRelease().installerUrl.isEmpty());
    QVERIFY(checker.latestRelease().checksumsUrl.isEmpty());

    QSignalSpy failed(&checker, &UpdateChecker::downloadFailed);
    checker.downloadLatest();
    QVERIFY(QTest::qWaitFor([&]() { return failed.count() > 0; }, 5000));
    qunsetenv("FT_GITHUB_API_BASE");
}

// The tag in the asset path is what binds the manifest and the installer to the
// advertised release; without it, tag v9.9.9 could serve an older signed build.
void TestUpdateCheckerE2e::rejectsAssetsWhosePathTagDiffersFromTheRelease()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    QVERIFY(runCheck(http,
                     releaseWithAssetBase(
                             QStringLiteral("v9.9.9"),
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/download/v1.1.5"),
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/tag/v9.9.9")),
                     checker, available, none));

    QCOMPARE(available.count(), 1);
    QVERIFY(checker.latestRelease().installerUrl.isEmpty());
    QVERIFY(checker.latestRelease().checksumsUrl.isEmpty());
    qunsetenv("FT_GITHUB_API_BASE");
}

// The mirror of the two rejections: a genuine release's assets must be accepted,
// so an over-strict validator (which would strand every user) fails here too.
void TestUpdateCheckerE2e::acceptsAssetsPublishedUnderThisReleaseTag()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());

    const QString assetBase =
            QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/download/v2.0.0");
    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    QVERIFY(runCheck(http,
                     releaseWithAssetBase(
                             QStringLiteral("v2.0.0"), assetBase,
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/tag/v2.0.0")),
                     checker, available, none));

    QCOMPARE(available.count(), 1);
    QCOMPARE(checker.latestRelease().installerUrl,
             assetBase + QStringLiteral("/") + testInstallerAssetName());
    QCOMPARE(checker.latestRelease().checksumsUrl, assetBase + QStringLiteral("/SHA256SUMS.txt"));
    QCOMPARE(checker.latestRelease().htmlUrl,
             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/tag/v2.0.0"));
    qunsetenv("FT_GITHUB_API_BASE");
}

// html_url is handed to the user's browser and arrives in the same untrusted
// JSON as everything else.
void TestUpdateCheckerE2e::replacesHostileHtmlUrlWithTheCanonicalReleasePage()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    QVERIFY(runCheck(http,
                     releaseWithAssetBase(
                             QStringLiteral("v2.0.0"),
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/download/v2.0.0"),
                             QStringLiteral("https://evil.example/phish")),
                     checker, available, none));

    QCOMPARE(available.count(), 1);
    const QString shown = checker.latestRelease().htmlUrl;
    QVERIFY2(!shown.contains(QStringLiteral("evil.example")), qPrintable(shown));
    QVERIFY(shown.startsWith(QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/")));
    qunsetenv("FT_GITHUB_API_BASE");
}

void TestUpdateCheckerE2e::rejectsImplausibleTag()
{
    MockHttpServer http;
    QVERIFY(http.listen());
    qputenv("FT_GITHUB_API_BASE", http.baseUrl().toUtf8());

    UpdateChecker checker(QStringLiteral("dimmmmmmmer/freetunnel"), QStringLiteral("1.0.0"));
    QSignalSpy available(&checker, &UpdateChecker::updateAvailable);
    QSignalSpy none(&checker, &UpdateChecker::noUpdateAvailable);
    QVERIFY(runCheck(http,
                     releaseWithAssetBase(
                             QStringLiteral("../../etc/passwd"),
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/download/v2.0.0"),
                             QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/latest")),
                     checker, available, none));

    QCOMPARE(available.count(), 0);
    QCOMPARE(none.count(), 1);
    QVERIFY(none.first().at(0).toString().contains(QStringLiteral("Invalid response")));
    qunsetenv("FT_GITHUB_API_BASE");
}

QTEST_MAIN(TestUpdateCheckerE2e)
#include "test_update_checker_e2e.moc"
