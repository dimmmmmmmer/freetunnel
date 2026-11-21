// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>
#include <QImageReader>
#include <QPainter>
#include <QScopeGuard>
#include <QSvgRenderer>

class TestQtSvgLimits : public QObject {
    Q_OBJECT
private slots:
    void blurBufferRespectsImageAllocationLimit() {
        const int previousLimit = QImageReader::allocationLimit();
        const auto restoreLimit = qScopeGuard([previousLimit] {
            QImageReader::setAllocationLimit(previousLimit);
        });
        QImageReader::setAllocationLimit(1);
        // The ARGB image fits in 1 MiB, but the blur's uint64_t buffer does not.
        QSvgRenderer renderer(QByteArray(
            "<svg xmlns='http://www.w3.org/2000/svg' width='400' height='400' viewBox='0 0 400 400'>"
            "<defs><filter id='blur' filterUnits='userSpaceOnUse' x='0' y='0' width='400' height='400'>"
            "<feGaussianBlur stdDeviation='1'/></filter></defs>"
            "<rect width='400' height='400' fill='red' filter='url(#blur)'/></svg>"));
        QVERIFY(renderer.isValid());
        QImage image(400, 400, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        QTest::ignoreMessage(QtWarningMsg, "The requested filter buffer is too big, ignoring");
        renderer.render(&painter);
    }
};

QTEST_MAIN(TestQtSvgLimits)
#include "test_qt_svg_limits.moc"
