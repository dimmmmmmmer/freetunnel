// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "core/InstanceControl.h"
#include "core/CredentialStore.h"

class TestInstanceControl : public QObject {
    Q_OBJECT

private slots:
    void roundTripMessage();
    void rejectsBadMessage();
    void tokenFileRoundTrip();
    void legacyInstanceAuthFileMigratesWhenSecure();
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

void TestInstanceControl::legacyInstanceAuthFileMigratesWhenSecure()
{
#if !defined(Q_OS_LINUX)
    QSKIP("AppConfigLocation override is Linux-only in this test");
#endif
    if (!freetunnel::CredentialStore::secureStorageAvailable())
        QSKIP("Secure storage required to verify legacy instance-auth migration");

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    qputenv("XDG_CONFIG_HOME", tmp.path().toUtf8());

    const QString legacyToken = QStringLiteral("legacy-token-abc");
    const QString path = freetunnel::instanceAuthFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(legacyToken.toUtf8());
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    QString read;
    QVERIFY(freetunnel::readInstanceAuthToken(&read));
    QCOMPARE(read, legacyToken);
    QVERIFY(!QFileInfo::exists(path));

    freetunnel::removeInstanceAuthToken();
}

void TestInstanceControl::rejectsMismatchedToken()
{
    QVERIFY(freetunnel::instanceTokensEqual(QStringLiteral("same"), QStringLiteral("same")));
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("a"), QStringLiteral("b")));
    QVERIFY(!freetunnel::instanceTokensEqual(QStringLiteral("short"), QStringLiteral("longer")));
}

QTEST_MAIN(TestInstanceControl)
#include "test_instance_control.moc"
