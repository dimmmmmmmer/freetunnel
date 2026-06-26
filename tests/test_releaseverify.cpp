// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QTemporaryFile>

#include "core/ReleaseVerify.h"

class TestReleaseVerify : public QObject {
    Q_OBJECT

private slots:
    void parseSums();
    void verifyMatch();
    void verifyMismatch();
#if __has_include(<openssl/evp.h>)
    void ed25519Valid();
    void ed25519Invalid();
#endif
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

#if __has_include(<openssl/evp.h>)
void TestReleaseVerify::ed25519Valid()
{
    static const char kPubPem[] =
        "-----BEGIN PUBLIC KEY-----\n"
        "MCowBQYDK2VwAyEAdSdq79YO2Q2DAi/R23X7qCa5qsL3EVwG3Kb064ajl38=\n"
        "-----END PUBLIC KEY-----\n";
    const QByteArray manifest("abc123  test.AppImage\n");
    const QByteArray sig = QByteArray::fromHex(
        "d57a0fcaee75b77602433ee06e0f8b55cacd9113fe5969068b24211a0f11ec4"
        "fdf3503b8c06c3b4be0fcd547d773250c634ca96df804b15f5336f355a6563008");
    QVERIFY(verifyEd25519Signature(manifest, sig, kPubPem));
}

void TestReleaseVerify::ed25519Invalid()
{
    static const char kPubPem[] =
        "-----BEGIN PUBLIC KEY-----\n"
        "MCowBQYDK2VwAyEAdSdq79YO2Q2DAi/R23X7qCa5qsL3EVwG3Kb064ajl38=\n"
        "-----END PUBLIC KEY-----\n";
    const QByteArray manifest("tampered  test.AppImage\n");
    const QByteArray sig = QByteArray::fromHex(
        "d57a0fcaee75b77602433ee06e0f8b55cacd9113fe5969068b24211a0f11ec4"
        "fdf3503b8c06c3b4be0fcd547d773250c634ca96df804b15f5336f355a6563008");
    QVERIFY(!verifyEd25519Signature(manifest, sig, kPubPem));
}
#endif

QTEST_MAIN(TestReleaseVerify)
#include "test_releaseverify.moc"
