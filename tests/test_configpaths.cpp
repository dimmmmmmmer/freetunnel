#include <QtTest>

#include <QFileInfo>

#include "core/ConfigPaths.h"

class TestConfigPaths : public QObject {
    Q_OBJECT

private slots:
    void sanitizeAndUniquePath();
    void ownerConfigPathForSaveReusesExisting();
};

void TestConfigPaths::sanitizeAndUniquePath()
{
    const QString stem = freetunnel::sanitizeConfigBaseName(QStringLiteral("My Server"));
    QCOMPARE(stem, QStringLiteral("My_Server"));
    const QString path = freetunnel::uniqueOwnerConfigPath(stem);
    QVERIFY(path.endsWith(QStringLiteral(".toml")));
}

void TestConfigPaths::ownerConfigPathForSaveReusesExisting()
{
    const QString stem = QStringLiteral("vpn-test");
    const QString first = freetunnel::uniqueOwnerConfigPath(stem);
    QVERIFY(QFileInfo(first).completeBaseName() == stem);
    QCOMPARE(freetunnel::ownerConfigPathForSave(stem, first), first);
    const QString renamed = freetunnel::ownerConfigPathForSave(QStringLiteral("vpn-renamed"), first);
    QVERIFY(renamed.endsWith(QStringLiteral(".toml")));
    QVERIFY(renamed != first);
}

QTEST_MAIN(TestConfigPaths)
#include "test_configpaths.moc"
