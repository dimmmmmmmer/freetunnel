#include <QtTest>

#include <QTemporaryFile>

#include "core/ReleaseVerify.h"

class TestReleaseVerify : public QObject {
    Q_OBJECT

private slots:
    void parseSums();
    void verifyMatch();
    void verifyMismatch();
};

void TestReleaseVerify::parseSums()
{
    const QByteArray sums = "abc123  freetunnel-linux-x86_64.AppImage\n"
                            "def456  freetunnel-linux-x86_64.deb\n";
    QCOMPARE(expectedSha256FromSums(sums, QStringLiteral("freetunnel-linux-x86_64.AppImage")),
             QStringLiteral("abc123"));
    QVERIFY(expectedSha256FromSums(sums, QStringLiteral("missing")).isEmpty());
}

void TestReleaseVerify::verifyMatch()
{
    QTemporaryFile tf;
    QVERIFY(tf.open());
    tf.write("payload");
    tf.close();

    const QByteArray sums = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08  test.bin\n";
    // hash of "test" not "payload" — use actual hash of payload
    const QString hex = sha256HexOfFile(tf.fileName());
    const QByteArray realSums = (hex + "  test.bin\n").toLatin1();
    QVERIFY(verifyFileAgainstSums(tf.fileName(), realSums, QStringLiteral("test.bin")));
}

void TestReleaseVerify::verifyMismatch()
{
    QTemporaryFile tf;
    QVERIFY(tf.open());
    tf.write("payload");
    tf.close();
    const QByteArray sums = "deadbeef  test.bin\n";
    QVERIFY(!verifyFileAgainstSums(tf.fileName(), sums, QStringLiteral("test.bin")));
}

QTEST_MAIN(TestReleaseVerify)
#include "test_releaseverify.moc"
