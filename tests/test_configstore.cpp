#include <QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QStandardPaths>

#include "core/ConfigStore.h"

// Persistence + de-duplication of the stored config-path list. Uses
// QStandardPaths test mode so it writes to an isolated location.
class TestConfigStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void emptyWhenNone();
    void roundTrip();
    void deduplicatesKeepingOrder();
};

void TestConfigStore::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("FreeTunnelTest"));
    QStandardPaths::setTestModeEnabled(true);
}

void TestConfigStore::emptyWhenNone() {
    QFile::remove(storagePath());
    QVERIFY(loadStoredConfigs().isEmpty());
}

void TestConfigStore::roundTrip() {
    const QStringList in{QStringLiteral("/a/one.toml"), QStringLiteral("/b/two.toml")};
    saveStoredConfigs(in);
    QCOMPARE(loadStoredConfigs(), in);
}

void TestConfigStore::deduplicatesKeepingOrder() {
    saveStoredConfigs({QStringLiteral("/a/x.toml"), QStringLiteral("/a/x.toml"),
                       QStringLiteral("/b/y.toml")});
    QCOMPARE(loadStoredConfigs(),
             (QStringList{QStringLiteral("/a/x.toml"), QStringLiteral("/b/y.toml")}));
}

QTEST_MAIN(TestConfigStore)
#include "test_configstore.moc"
