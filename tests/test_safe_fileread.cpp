#include <QtTest>

#include "core/AppUiUtils.h"

#include <QDir>
#include <QTemporaryFile>

class TestSafeFileRead : public QObject {
    Q_OBJECT

private slots:
    void readsFileInHome();
    void readsFileUrlInHome();
    void rejectsSystemPaths();
    void rejectsOversized();
};

void TestSafeFileRead::readsFileInHome()
{
    QTemporaryFile tf(QDir::homePath() + QStringLiteral("/.ft-test-read-XXXXXX.pem"));
    tf.setAutoRemove(true);
    QVERIFY(tf.open());
    tf.write("-----BEGIN CERTIFICATE-----\nTEST\n");
    tf.close();

    const QString content = safeReadUserTextFile(tf.fileName());
    QVERIFY(content.contains(QStringLiteral("BEGIN CERTIFICATE")));
}

void TestSafeFileRead::readsFileUrlInHome()
{
    QTemporaryFile tf(QDir::homePath() + QStringLiteral("/.ft-test-read-XXXXXX.pem"));
    tf.setAutoRemove(true);
    QVERIFY(tf.open());
    tf.write("PEM");
    tf.close();

    const QUrl url = QUrl::fromLocalFile(tf.fileName());
    QCOMPARE(safeReadUserTextFile(url.toString()), QStringLiteral("PEM"));
}

void TestSafeFileRead::rejectsSystemPaths()
{
    QVERIFY(safeReadUserTextFile(QStringLiteral("/etc/passwd")).isEmpty());
#if defined(Q_OS_UNIX)
    QVERIFY(safeReadUserTextFile(QStringLiteral("/etc/hosts")).isEmpty());
#endif
}

void TestSafeFileRead::rejectsOversized()
{
    QTemporaryFile tf(QDir::homePath() + QStringLiteral("/.ft-test-big-XXXXXX.pem"));
    tf.setAutoRemove(true);
    QVERIFY(tf.open());
    tf.write(QByteArray(2 * 1024 * 1024, 'A'));
    tf.close();

    QVERIFY(safeReadUserTextFile(tf.fileName(), 1024 * 1024).isEmpty());
}

QTEST_MAIN(TestSafeFileRead)
#include "test_safe_fileread.moc"
