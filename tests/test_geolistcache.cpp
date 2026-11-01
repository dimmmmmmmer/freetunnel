// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoListCache.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace freetunnel;
class TestGeoListCache : public QObject {
    Q_OBJECT
private slots:
    void failedUpdatePreservesLastGoodList() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GeoListCache cache(dir.path());
        const QString url = QStringLiteral("https://example.com/sites.txt");
        CachedGeoList result;
        QString error;
        QVERIFY(cache.store(url, GeoListKind::Sites, GeoDirection::Bypass,
                            "domain:example.com\n", &result, &error));
        QVERIFY(result.updated.isValid());
        const auto previous = result.entries;
        QVERIFY(!cache.store(url, GeoListKind::Sites, GeoDirection::Bypass,
                             "# an empty failed update\n", &result, &error));
        QCOMPARE(result.entries, previous);
        CachedGeoList restored;
        QVERIFY(cache.load(url, GeoListKind::Sites, GeoDirection::Bypass, &restored, &error));
        QCOMPARE(restored.entries, previous);
        QVERIFY(!cache.load(url, GeoListKind::Sites, GeoDirection::ThroughVpn, &restored, &error));
        QVERIFY(!cache.load(QStringLiteral("https://example.org/sites.txt"), GeoListKind::Sites,
                            GeoDirection::Bypass, &restored, &error));
    }
    void corruptedCacheIsRejected() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        GeoListCache cache(dir.path());
        const QString url = QStringLiteral("https://example.com/sites.txt");
        CachedGeoList result;
        QString error;
        QVERIFY(cache.store(url, GeoListKind::Sites, GeoDirection::ThroughVpn,
                            "domain:example.com\n", &result, &error));
        const auto files = QDir(dir.path()).entryList({QStringLiteral("*.list")}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(QDir(dir.path()).filePath(files.first()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<html>Service unavailable</html>");
        file.close();
        QVERIFY(!cache.load(url, GeoListKind::Sites, GeoDirection::ThroughVpn, &result, &error));
    }
};
QTEST_GUILESS_MAIN(TestGeoListCache)
#include "test_geolistcache.moc"
