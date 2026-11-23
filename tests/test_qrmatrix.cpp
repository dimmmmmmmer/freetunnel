// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCryptographicHash>

#include "core/QrMatrix.h"

// What the Configs page's "Show QR code" draws (Backend::configQrCode): the modules
// of a config's deep link, which the Android app's Scan QR code reads back.
class TestQrMatrix : public QObject {
    Q_OBJECT

private slots:
    void aLinkGivesTheCodeTheOtherQrcodegenPortsGive();
    void everyCodeHasItsFinderPatterns();
    void aTextTooLongForAnyCodeGivesNone();
    void nothingToEncodeGivesNone();
};

// tt:// with a whole server (vpn.example.com, 1.2.3.4:443, alice, secret), as the
// desktop writes it. Nayuki's Python port of the same library (qrcodegen 1.8.0 on
// PyPI), encode_text(link, Ecc.LOW), gives a 33-module code whose modules, as
// '1'/'0' row by row, hash to this; the Java port the Android app uses is the
// same algorithm, so a phone that reads one reads the other.
void TestQrMatrix::aLinkGivesTheCodeTheOtherQrcodegenPortsGive()
{
    const QrMatrix::Matrix code = QrMatrix::encode(QStringLiteral(
            "tt://?AAEBAQ92cG4uZXhhbXBsZS5jb20CCzEuMi4zLjQ6NDQzBQVhbGljZQYGc2VjcmV0"));
    QCOMPARE(code.size, 33);
    QCOMPARE(code.modules.size(), 33 * 33);
    QCOMPARE(QCryptographicHash::hash(code.modules.toLatin1(), QCryptographicHash::Sha256).toHex(),
             QByteArray("a05335d280a77678919954e7c0b88f7dee8e6905f69129560f31353a9257fa1c"));
}

// The three 7x7 squares a scanner finds a code by, in the corners but the bottom
// right: a dark ring, a light ring and a dark 3x3 middle.
void TestQrMatrix::everyCodeHasItsFinderPatterns()
{
    for (const QString &text : {QStringLiteral("a"), QString(500, QLatin1Char('x')),
                                QStringLiteral("tt://?") + QString(1200, QLatin1Char('A'))}) {
        const QrMatrix::Matrix code = QrMatrix::encode(text);
        QVERIFY(code.size >= 21 && code.size <= 177);
        QCOMPARE((code.size - 17) % 4, 0);
        QCOMPARE(code.modules.size(), code.size * code.size);
        const auto dark = [&](int x, int y) { return code.modules.at(y * code.size + x) == QLatin1Char('1'); };
        for (const QPoint corner : {QPoint(0, 0), QPoint(code.size - 7, 0), QPoint(0, code.size - 7)}) {
            for (int y = 0; y < 7; ++y) {
                for (int x = 0; x < 7; ++x) {
                    const int ring = qMax(qAbs(x - 3), qAbs(y - 3));
                    QCOMPARE(dark(corner.x() + x, corner.y() + y), ring != 2);
                }
            }
        }
    }
}

// 2953 bytes is all version 40 holds at L. A deep link with a long certificate
// chain can be longer: the page says to export .toml then.
void TestQrMatrix::aTextTooLongForAnyCodeGivesNone()
{
    QCOMPARE(QrMatrix::encode(QString(2953, QLatin1Char('a'))).size, 177);
    const QrMatrix::Matrix none = QrMatrix::encode(QString(2954, QLatin1Char('a')));
    QCOMPARE(none.size, 0);
    QVERIFY(none.modules.isEmpty());
}

void TestQrMatrix::nothingToEncodeGivesNone()
{
    QCOMPARE(QrMatrix::encode(QString()).size, 0);
    // A NUL would end the C string the library reads, and the code would hold less.
    QCOMPARE(QrMatrix::encode(QStringLiteral("tt://?a") + QChar(0) + QStringLiteral("b")).size, 0);
}

QTEST_GUILESS_MAIN(TestQrMatrix)
#include "test_qrmatrix.moc"
