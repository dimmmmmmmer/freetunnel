// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <QTemporaryFile>

#include "core/CredentialStore.h"
#include "core/ConfigToml.h"

using namespace freetunnel;

class TestCredentialStore : public QObject {
    Q_OBJECT

private slots:
    void init();
    void storeLoadDelete();
    void migrateStripsPassword();
    void keepsTheEventLoopRunningWhileItWaits();
    void worksOffTheMainThread();
    void reentryDoesNotStackEventLoops();
};

void TestCredentialStore::init()
{
    // Hermetic: standard paths point away from the real user scope, and under
    // FT_ENABLE_TEST_HOOKS credentialServiceName() is unique to this process, so
    // nothing here can name — let alone overwrite — a real entry.
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CredentialStoreTest"));
    QStandardPaths::setTestModeEnabled(true);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir credDir(dir + QStringLiteral("/credentials"));
    if (credDir.exists()) {
        for (const QFileInfo &fi : credDir.entryInfoList(QDir::Files))
            QFile::remove(fi.absoluteFilePath());
    }
}

void TestCredentialStore::storeLoadDelete()
{
    const QString key = QStringLiteral("/tmp/test-config.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));
    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QVERIFY(CredentialStore::deletePassword(key));
    QVERIFY(CredentialStore::loadPassword(key).isEmpty());
}

void TestCredentialStore::migrateStripsPassword()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    // A plain file, not a QTemporaryFile: migrateConfigPassword() now commits by
    // renaming over the target, and on Windows that cannot replace a path a live
    // QTemporaryFile still owns. Production configs are plain files anyway.
    const QString path = QDir(base).filePath(
            QStringLiteral("migrate-%1.toml").arg(QCoreApplication::applicationPid()));
    QFile::remove(path);
    QFile tf(path);
    QVERIFY(tf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ConfigToml c;
    c.hostname = "h.example";
    c.addresses = "1.2.3.4:443";
    c.username = "u";
    c.password = "pw";
    tf.write(buildConfigToml(c).toUtf8());
    tf.close();

    QVERIFY(migrateConfigPassword(path));

    QFile rf(path);
    QVERIFY(rf.open(QIODevice::ReadOnly));
    ConfigToml out = parseConfigToml(QString::fromUtf8(rf.readAll()));
    QVERIFY(out.password.isEmpty());
    QCOMPARE(CredentialStore::loadPassword(CredentialStore::keyForConfigPath(path)),
             QStringLiteral("pw"));

    // Leave nothing behind: the credential entry is real (in the test-only
    // service) and the file is ours.
    CredentialStore::deletePassword(CredentialStore::keyForConfigPath(path));
    rf.close();
    QFile::remove(path);
}

// The OS credential store BLOCKS its caller: on macOS the request goes to
// securityd, which does not answer while the system is asking the user to
// authorize this binary. Called straight from the GUI thread that meant a frozen
// window with a spinning cursor — reported from a real session, on the edit
// form, on save, and on copying a config link.
//
// Rather than time anything (the store answers in microseconds when it is not
// asking a human, so a duration assert would prove nothing), assert the property
// that makes the freeze impossible: while the call is in flight, the calling
// thread's event loop keeps delivering. A queued call posted first is therefore
// dispatched BEFORE loadPassword() returns. A direct, blocking implementation
// cannot do that — it never reaches the event loop.
void TestCredentialStore::keepsTheEventLoopRunningWhileItWaits()
{
    const QString key = QStringLiteral("/tmp/test-responsive.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));

    bool painted = false;
    QMetaObject::invokeMethod(qApp, [&painted]() { painted = true; }, Qt::QueuedConnection);
    QVERIFY2(!painted, "the queued call must still be pending when the store is entered");

    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QVERIFY2(painted, "the UI thread was blocked for the whole credential call");

    QVERIFY(CredentialStore::deletePassword(key));
}

// ...and the same call off the main thread must still work, since that is the
// path the connect sequence takes. There is no UI to keep alive there, so it
// runs directly — the wrapper must not deadlock waiting on an event loop that
// nobody is running.
void TestCredentialStore::worksOffTheMainThread()
{
    const QString key = QStringLiteral("/tmp/test-worker.toml");
    QString loaded;
    bool stored = false;
    bool deleted = false;
    QThread *worker = QThread::create([&]() {
        stored = CredentialStore::storePassword(key, QStringLiteral("from-worker"));
        loaded = CredentialStore::loadPassword(key);
        deleted = CredentialStore::deletePassword(key);
    });
    worker->start();
    QVERIFY2(worker->wait(10000), "a credential call off the main thread hung");
    delete worker;

    QVERIFY(stored);
    QCOMPARE(loaded, QStringLiteral("from-worker"));
    QVERIFY(deleted);
}

// Keeping the UI painted means running an event loop, and an event loop can
// dispatch a queued call that asks the credential store for something else. That
// must not open a second nested loop on top of the first: nesting has to stay
// bounded no matter what the dispatched code does.
//
// Observable as: a call posted from inside the re-entrant call is NOT dispatched
// before it returns. If the inner call had spun its own loop, it would be.
void TestCredentialStore::reentryDoesNotStackEventLoops()
{
    const QString key = QStringLiteral("/tmp/test-reentry.toml");
    QVERIFY(CredentialStore::storePassword(key, QStringLiteral("s3cret")));

    bool innerDispatched = false;
    bool sawInnerDispatchedTooEarly = false;
    QString reentrantResult;

    QMetaObject::invokeMethod(
            qApp,
            [&]() {
                QMetaObject::invokeMethod(
                        qApp, [&innerDispatched]() { innerDispatched = true; },
                        Qt::QueuedConnection);
                reentrantResult = CredentialStore::loadPassword(key); // re-entry
                sawInnerDispatchedTooEarly = innerDispatched;
            },
            Qt::QueuedConnection);

    QCOMPARE(CredentialStore::loadPassword(key), QStringLiteral("s3cret"));
    QCOMPARE(reentrantResult, QStringLiteral("s3cret")); // the re-entrant call still works
    QVERIFY2(!sawInnerDispatchedTooEarly,
             "the re-entrant call spun its own event loop — nesting is unbounded");

    QVERIFY(CredentialStore::deletePassword(key));
}

QTEST_MAIN(TestCredentialStore)
#include "test_credentialstore.moc"
