#include <QtTest>

#include <QTemporaryFile>

#include "vpn/vpn_helper_launch.h"

class TestVpnHelperLaunch : public QObject {
    Q_OBJECT

private:
    QString writeTempTokenFile(const QByteArray &body)
    {
        QTemporaryFile tf;
        tf.setAutoRemove(false);
        if (!tf.open())
            return QString();
        tf.write(body);
        tf.close();
        return tf.fileName();
    }

private slots:
    void parseArgsReadsTokenFile();
    void parseArgsRejectsMissingToken();
    void readTokenFileDeletesFile();
};

void TestVpnHelperLaunch::parseArgsReadsTokenFile()
{
    const QString path = writeTempTokenFile("secret-token");
    QVERIFY(!path.isEmpty());

    const QStringList args = {
        QStringLiteral("FreeTunnel"),
        QStringLiteral("--helper"),
        QStringLiteral("--port"),
        QStringLiteral("12345"),
        QStringLiteral("--token-file"),
        path,
    };
    const freetunnel::HelperLaunchConfig cfg = freetunnel::parseHelperLaunchArgs(args);
    QCOMPARE(cfg.port, static_cast<quint16>(12345));
    QCOMPARE(cfg.token, QStringLiteral("secret-token"));
    QVERIFY(cfg.ok());
    QVERIFY(!QFile::exists(path));
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
    const QString path = writeTempTokenFile("tok");
    QVERIFY(!path.isEmpty());
    QCOMPARE(freetunnel::readHelperTokenFile(path), QStringLiteral("tok"));
    QVERIFY(!QFile::exists(path));
}

QTEST_MAIN(TestVpnHelperLaunch)
#include "test_vpn_helper_launch.moc"
