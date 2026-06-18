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
    void downloadVerifiesChecksum();
    void downloadRejectsBadChecksum();
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
    http.setRoute(QStringLiteral("/repos/enrvate/freetunnel/releases/latest"), route);

    UpdateChecker checker(QStringLiteral("enrvate/freetunnel"), QStringLiteral("1.0.0"));
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
    http.setRoute(QStringLiteral("/repos/enrvate/freetunnel/releases/latest"), route);

    UpdateChecker checker(QStringLiteral("enrvate/freetunnel"), QStringLiteral("1.0.0"));
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
    http.setRoute(QStringLiteral("/repos/enrvate/freetunnel/releases/latest"), releaseRoute);

    MockHttpServer::Route sumsRoute;
    sumsRoute.body = sums;
    sumsRoute.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sumsRoute);

    MockHttpServer::Route installerRoute;
    installerRoute.body = installerBody;
    installerRoute.contentType = QByteArrayLiteral("application/octet-stream");
    http.setRoute(QStringLiteral("/installer"), installerRoute);

    UpdateChecker checker(QStringLiteral("enrvate/freetunnel"), QStringLiteral("1.0.0"));
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
    http.setRoute(QStringLiteral("/repos/enrvate/freetunnel/releases/latest"), releaseRoute);

    MockHttpServer::Route sumsRoute;
    sumsRoute.body = sums;
    sumsRoute.contentType = QByteArrayLiteral("text/plain");
    http.setRoute(QStringLiteral("/checksums"), sumsRoute);

    MockHttpServer::Route installerRoute;
    installerRoute.body = installerBody;
    installerRoute.contentType = QByteArrayLiteral("application/octet-stream");
    http.setRoute(QStringLiteral("/installer"), installerRoute);

    UpdateChecker checker(QStringLiteral("enrvate/freetunnel"), QStringLiteral("1.0.0"));
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

QTEST_MAIN(TestUpdateCheckerE2e)
#include "test_update_checker_e2e.moc"
