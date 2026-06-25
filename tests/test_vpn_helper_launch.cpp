#include <QtTest>

#include <QDir>
#include <QTemporaryFile>

#include "vpn/vpn_helper_launch.h"

class TestVpnHelperLaunch : public QObject {
    Q_OBJECT

private slots:
    void parseArgsReadsTokenFile();
    void parseArgsRejectsMissingToken();
    void readTokenFileDeletesFile();
};

void TestVpnHelperLaunch::parseArgsReadsTokenFile()
{
    QTemporaryFile tf(QDir::tempPath() + QStringLiteral("/ft-helper-token-XXXXXX"));
    tf.setAutoRemove(false);
    QVERIFY(tf.open());
    tf.write("secret-token");
    tf.close();

    const QStringList args = {
        QStringLiteral("FreeTunnel"),
        QStringLiteral("--helper"),
        QStringLiteral("--port"),
        QStringLiteral("12345"),
        QStringLiteral("--token-file"),
        tf.fileName(),
    };
    const freetunnel::HelperLaunchConfig cfg = freetunnel::parseHelperLaunchArgs(args);
    QCOMPARE(cfg.port, static_cast<quint16>(12345));
    QCOMPARE(cfg.token, QStringLiteral("secret-token"));
    QVERIFY(cfg.ok());
    QVERIFY(!QFile::exists(tf.fileName()));
}

void TestVpnHelperLaunch::parseArgsRejectsMissingToken()
{
    const QStringList args = {
        QStringLiteral("FreeTunnel"),
        QStringLiteral("--helper"),
        QStringLiteral("--port"),
        QStringLiteral("12345"),
    };
    const freetunnel::HelperLaunchConfig cfg = freetunnel::parseHelperLaunchArgs(args);
    QVERIFY(!cfg.ok());
}

void TestVpnHelperLaunch::readTokenFileDeletesFile()
{
    QTemporaryFile tf(QDir::tempPath() + QStringLiteral("/ft-helper-read-XXXXXX"));
    tf.setAutoRemove(false);
    QVERIFY(tf.open());
    tf.write("tok");
    tf.close();
    QCOMPARE(freetunnel::readHelperTokenFile(tf.fileName()), QStringLiteral("tok"));
    QVERIFY(!QFile::exists(tf.fileName()));
}

QTEST_MAIN(TestVpnHelperLaunch)
#include "test_vpn_helper_launch.moc"
