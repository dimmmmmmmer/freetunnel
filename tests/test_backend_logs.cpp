// cppcheck-suppress-file missingIncludeSystem
// The log the user reads, copies and sends when something goes wrong.
//
// Log ownership changed on this branch: the core no longer writes into this file
// from inside the helper, so THIS process is now the only writer, CORE lines
// have to be persisted here or they exist only in memory, and clearing no longer
// has to wait for a disconnect. None of that was covered — and the failure mode
// is silent, because a log that quietly stops recording still looks like a log.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "app/Backend.h"
#include "app/LogModel.h"

class TestBackendLogs : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void startupRestoresTheOnDiskTail();
    void onlyTheMostRecentLinesAreRestored();
    void unparsableLinesSurviveVerbatim();
    void aHugeLogIsTrimmedToItsTail();
    void loggingDisabledWritesNothingAnywhere();
    void clearLogsEmptiesTheViewAndTheFile();
    void theLogFileIsOwnerOnly();

private:
    QString logPath() const;
    static void writeLog(const QString &path, const QByteArray &body);

    QTemporaryDir m_home;
};

void TestBackendLogs::initTestCase()
{
    QVERIFY(m_home.isValid());
    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    qputenv("XDG_DATA_HOME", m_home.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BackendLogsTest"));
}

QString TestBackendLogs::logPath() const
{
    return QDir(m_home.path()).filePath(QStringLiteral("freetunnel-test.log"));
}

void TestBackendLogs::init()
{
    // The app reads its settings through a default-constructed QSettings, which
    // takes the organization/application names set above — and, in test mode,
    // lands in the test scope rather than the user's real preferences.
    QSettings s;
    s.clear();
    // Pin the log somewhere this test owns, so nothing depends on the platform's
    // AppDataLocation and no real log can be read or truncated.
    s.setValue(QStringLiteral("logs/path"), logPath());
    s.setValue(QStringLiteral("logs/enabled"), true);
    s.sync();
    QFile::remove(logPath());
}

void TestBackendLogs::writeLog(const QString &path, const QByteArray &body)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(body), qint64(body.size()));
}

// History has to survive a restart: the view is rebuilt from the file, splitting
// each line back into time / level / message.
void TestBackendLogs::startupRestoresTheOnDiskTail()
{
    writeLog(logPath(),
             QByteArrayLiteral("2026-07-01 10:00:00\tINFO\tearlier session\n"
                               "2026-07-01 10:00:01\tERROR\thandshake failed\n"));

    Backend backend;
    const QString text = backend.logText();
    QVERIFY2(text.contains(QStringLiteral("earlier session")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("handshake failed")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("ERROR")), qPrintable(text));
    // The time column is the time from the file, not the time of the restart.
    QVERIFY2(text.contains(QStringLiteral("10:00:01")), qPrintable(text));
    // And startup still records that it started, after the restored history.
    QVERIFY2(text.contains(QStringLiteral("started")), qPrintable(text));
}

// A long-running install accumulates megabytes. Loading all of it into the view
// at startup would stall the window before it ever appears.
void TestBackendLogs::onlyTheMostRecentLinesAreRestored()
{
    QByteArray body;
    for (int i = 0; i < 500; ++i) {
        body += QStringLiteral("2026-07-01 10:00:00\tINFO\tline %1\n").arg(i).toUtf8();
    }
    writeLog(logPath(), body);

    Backend backend;
    auto *model = qobject_cast<LogModel *>(backend.logModel());
    QVERIFY(model);
    QVERIFY2(model->count() <= 210, qPrintable(QString::number(model->count())));

    const QString text = backend.logText();
    QVERIFY2(text.contains(QStringLiteral("line 499")), "the newest line was dropped");
    QVERIFY2(!text.contains(QStringLiteral("line 0\n")), "an ancient line was restored");
}

// The file also holds the trim marker and, historically, core lines that were
// not tab-separated. Anything that does not split into three columns still has
// to reach the user rather than vanishing.
void TestBackendLogs::unparsableLinesSurviveVerbatim()
{
    writeLog(logPath(),
             QByteArrayLiteral("... (older log entries trimmed) ...\n"
                               "2026-07-01 10:00:00\tINFO\tregular line\n"));

    Backend backend;
    const QString text = backend.logText();
    QVERIFY2(text.contains(QStringLiteral("older log entries trimmed")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("regular line")), qPrintable(text));
}

// The cap exists so the log cannot fill the disk. It runs at startup and hourly;
// this exercises the startup path.
void TestBackendLogs::aHugeLogIsTrimmedToItsTail()
{
    QByteArray body;
    const QByteArray line = QByteArrayLiteral("2026-07-01 10:00:00\tINFO\t")
            + QByteArray(200, 'x') + QByteArrayLiteral("\n");
    while (body.size() < 6 * 1024 * 1024)
        body += line;
    body += QByteArrayLiteral("2026-07-01 10:00:00\tINFO\tthe very last line\n");
    writeLog(logPath(), body);
    const qint64 before = QFileInfo(logPath()).size();

    Backend backend;
    const qint64 after = QFileInfo(logPath()).size();
    QVERIFY2(after < before, "the oversized log was not trimmed");
    QVERIFY2(after < 3 * 1024 * 1024, qPrintable(QString::number(after)));

    // Trimming drops the OLDEST entries — the recent ones are the useful ones.
    QFile f(logPath());
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray kept = f.readAll();
    QVERIFY2(kept.startsWith("... (older log entries trimmed) ..."), kept.left(60).constData());
    QVERIFY2(kept.contains("the very last line"), "the trim threw away the newest entries");
}

// Turning logging off has to stop the on-disk record too, not just the view:
// that switch is how a user says "do not keep this on my machine".
void TestBackendLogs::loggingDisabledWritesNothingAnywhere()
{
    QSettings s;
    s.setValue(QStringLiteral("logs/enabled"), false);
    s.sync();

    Backend backend;
    auto *model = qobject_cast<LogModel *>(backend.logModel());
    QVERIFY(model);
    QCOMPARE(model->count(), 0);
    QVERIFY(backend.logText().isEmpty());
    QVERIFY2(!QFileInfo::exists(logPath()), "a log file was created while logging was off");
}

void TestBackendLogs::clearLogsEmptiesTheViewAndTheFile()
{
    writeLog(logPath(), QByteArrayLiteral("2026-07-01 10:00:00\tINFO\tsomething private\n"));

    Backend backend;
    QVERIFY(backend.logText().contains(QStringLiteral("something private")));

    backend.clearLogs();
    auto *model = qobject_cast<LogModel *>(backend.logModel());
    QVERIFY(model);
    QCOMPARE(model->count(), 0);
    QVERIFY(backend.logText().isEmpty());
    // Nothing else holds this file open any more, so Clear must actually remove
    // it — leaving the old contents on disk is the bug it used to have.
    QVERIFY2(!QFileInfo::exists(logPath()), "clearLogs left the on-disk log behind");
}

// The log names servers, and can name domains. It is not for other local users.
void TestBackendLogs::theLogFileIsOwnerOnly()
{
    Backend backend;
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(logPath()), 5000);
    const QFile::Permissions perms = QFile::permissions(logPath());
    QVERIFY2(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)),
             "the log file is readable by other local users");
}

QTEST_MAIN(TestBackendLogs)
#include "test_backend_logs.moc"
