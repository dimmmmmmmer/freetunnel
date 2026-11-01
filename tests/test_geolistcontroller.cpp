// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoListController.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace freetunnel;
class TestGeoListController : public QObject {
    Q_OBJECT
private slots:
    void disabledListsAreNotLoadedOrFetched() {
        QTemporaryDir directory;
        GeoListController controller(directory.path());
        GeoListDefinition def; def.source = QStringLiteral("https://example.org/list");
        controller.setDefinitions({def}, true);
        QVERIFY(!controller.state(def).loaded);
        QVERIFY(!controller.state(def).busy);
    }
    void loadsCacheAndSharesAcrossProfiles() {
        QTemporaryDir directory;
        GeoListDefinition def; def.source = QStringLiteral("https://example.org/list"); def.enabled = true;
        CachedGeoList result; QString error;
        QVERIFY(GeoListCache(directory.path()).store(def.source, def.kind, def.direction,
                                                      "domain:example.org", &result, &error));
        GeoListController controller(directory.path());
        auto disabled = def; disabled.enabled = false; disabled.id = QStringLiteral("other-profile");
        controller.setDefinitions({def, disabled}, false);
        QTRY_VERIFY(controller.state(def).loaded);
        QVERIFY(!controller.state(def).busy);
        QCOMPARE(controller.state(def).list.entries, result.entries);
        QCOMPARE(controller.state(disabled).list.entries, result.entries);
        auto otherDirection = def; otherDirection.direction = GeoDirection::Bypass;
        QVERIFY(controller.state(otherDirection).list.entries.empty());
    }
};
QTEST_GUILESS_MAIN(TestGeoListController)
#include "test_geolistcontroller.moc"
