#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "core/InstanceControl.h"

class TestInstanceControl : public QObject {
    Q_OBJECT

private slots:
    void roundTripMessage();
    void rejectsBadMessage();
    void tokenFileRoundTrip();
    void rejectsMismatchedToken();
};

void TestInstanceControl::roundTripMessage()
{
    const QByteArray raw =
            freetunnel::formatInstanceMessage(QStringLiteral("abc123"), QStringLiteral("freetunnel://toggle"));
    QString token;
    QString payload;
    QVERIFY(freetunnel::parseInstanceMessage(raw, &token, &payload));
    QCOMPARE(token, QStringLiteral("abc123"));
    QCOMPARE(payload, QStringLiteral("freetunnel://toggle"));
}

void TestInstanceControl::rejectsBadMessage()
{
    QString token;
    QString payload;
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("no-newline"), &token, &payload));
    QVERIFY(!freetunnel::parseInstanceMessage(QByteArray("\nempty"), &token, &payload));
}

void TestInstanceControl::tokenFileRoundTrip()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    QString written;
    QVERIFY(freetunnel::writeInstanceAuthToken(&written));
    QString read;
    QVERIFY(freetunnel::readInstanceAuthToken(&read));
    QCOMPARE(read, written);

    freetunnel::removeInstanceAuthToken();
    QVERIFY(!freetunnel::readInstanceAuthToken(&read));
}

void TestInstanceControl::rejectsMismatchedToken()
{
    QVERIFY(freetunnel::instanceTokensEqual(QStringLiteral("same"), QStringLiteral("same")));
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("a"), QStringLiteral("b")));
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("short"), QStringLiteral("longer")));
}

QTEST_MAIN(TestInstanceControl)
#include "test_instance_control.moc"
