#include <QtTest>

#include "core/ConfigPaths.h"

class TestConfigPaths : public QObject {
    Q_OBJECT

private slots:
    void sanitizeAndUniquePath();
};

void TestConfigPaths::sanitizeAndUniquePath()
{
    const QString stem = freetunnel::sanitizeConfigBaseName(QStringLiteral("My Server"));
    QCOMPARE(stem, QStringLiteral("My_Server"));
    const QString path = freetunnel::uniqueOwnerConfigPath(stem);
    QVERIFY(path.endsWith(QStringLiteral(".toml")));
}

QTEST_MAIN(TestConfigPaths)
#include "test_configpaths.moc"
