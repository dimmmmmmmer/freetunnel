// cppcheck-suppress-file missingIncludeSystem
// Whether Linux has anywhere to keep a VPN password is a question about the
// session bus: is a Secret Service on it, or one the bus knows how to start. The
// libsecret build answered it from DBUS_SESSION_BUS_ADDRESS being set, which every
// desktop session has, keyring or not — so the Settings warning never appeared on
// a machine without one, and each save failed on its own instead.
//
// Every case runs on a dbus-daemon of the test's own, with a service directory of
// its own, so the answer depends on nothing this machine has installed or running.
// It is the one session bus this process ever uses (see initTestCase).
#include <QtTest>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <csignal>
#include <cstring>
#include <sys/prctl.h>

#include "core/CredentialStore.h"

using namespace freetunnel;

namespace {

const QString kSecrets = QStringLiteral("org.freedesktop.secrets");
const char kBeAKeyring[] = "--be-a-keyring";

// Run as a keyring by the private bus, from the .service file the activation case
// writes: claim the name, then leave. The bus answers the activation once the
// name is taken, so there is nothing to stay for, and nothing is left running.
int beAKeyring(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QDBusConnection bus =
            QDBusConnection::connectToBus(QDBusConnection::ActivationBus, QStringLiteral("starter"));
    return bus.isConnected() && bus.registerService(kSecrets) ? 0 : 1;
}

} // namespace

class TestSecretServiceProbe : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void aBusWithNoKeyringHasNoStorage();
    void aRunningKeyringIsFound();
    void aKeyringTheBusCanStartIsFound();
    void aKeyringThatWillNotStartIsNotThere();

private:
    // Have the bus offer a keyring started by @p exec, or none for an empty one.
    bool offerKeyring(const QString &exec);

    QTemporaryDir m_dir;
    QProcess m_daemon;
    QString m_address;
};

void TestSecretServiceProbe::initTestCase()
{
    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dbus-daemon"));
    if (daemon.isEmpty()) {
        // A reason to skip on a developer's machine, never on CI, where it would
        // quietly drop the only test of what the Settings warning rests on.
        if (qEnvironmentVariableIsSet("CI"))
            QFAIL("no dbus-daemon to run a private session bus on");
        QSKIP("no dbus-daemon to run a private session bus on");
    }
    QVERIFY(m_dir.isValid());
    QVERIFY(QDir(m_dir.path()).mkpath(QStringLiteral("services")));
    const QString config = m_dir.filePath(QStringLiteral("bus.conf"));
    QFile file(config);
    QVERIFY(file.open(QIODevice::WriteOnly));
    // Plain literals: moc cannot read past a raw string, and would lose every
    // class after it.
    file.write("<busconfig>\n"
               "  <type>session</type>\n"
               "  <listen>unix:tmpdir=/tmp</listen>\n"
               "  <auth>EXTERNAL</auth>\n"
               "  <servicedir>");
    file.write(QFile::encodeName(m_dir.filePath(QStringLiteral("services"))));
    file.write("</servicedir>\n"
               "  <policy context=\"default\">\n"
               "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
               "    <allow eavesdrop=\"true\"/>\n"
               "    <allow own=\"*\"/>\n"
               "  </policy>\n"
               "</busconfig>\n");
    file.close();
    // Gone with this process however it ends: a crash or a kill skips
    // cleanupTestCase, and would leave a daemon behind for good.
    m_daemon.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_daemon.start(daemon, {QStringLiteral("--config-file=") + config, QStringLiteral("--nofork"),
                            QStringLiteral("--print-address")});
    QVERIFY(m_daemon.waitForStarted(5000));
    while (!m_daemon.canReadLine() && m_daemon.waitForReadyRead(5000)) {}
    m_address = QString::fromUtf8(m_daemon.readLine()).trimmed();
    QVERIFY(!m_address.isEmpty());

    // The session bus is this process's for good once anything connects to it, so
    // it is pointed at the private one before anything does.
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_address.toUtf8());
    QVERIFY(QDBusConnection::sessionBus().isConnected());
}

void TestSecretServiceProbe::cleanupTestCase()
{
    m_daemon.terminate();
    m_daemon.waitForFinished(5000);
}

bool TestSecretServiceProbe::offerKeyring(const QString &exec)
{
    const QString path = m_dir.filePath(QStringLiteral("services/org.freedesktop.secrets.service"));
    QFile::remove(path);
    if (!exec.isEmpty()) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write("[D-BUS Service]\nName=org.freedesktop.secrets\nExec=");
        file.write(exec.toUtf8());
        file.write("\n");
    }
    // Read before it answers, so the next call sees the directory as it is now.
    const QDBusMessage reload = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("ReloadConfig"));
    return QDBusConnection::sessionBus().call(reload).type() == QDBusMessage::ReplyMessage;
}

// A session bus with nothing on it that keeps secrets: a tiling window manager
// with no keyring, a KDE install whose wallet does not speak Secret Service, a
// minimal install. The bus is there, so the old answer was yes.
void TestSecretServiceProbe::aBusWithNoKeyringHasNoStorage()
{
    QVERIFY(offerKeyring(QString()));
    QVERIFY2(!secretServiceOnSessionBus(), "a bare session bus was taken for a keyring");
    QVERIFY2(!CredentialStore::secureStorageAvailable(),
             "secure storage reported available with no Secret Service to keep it");
}

// A keyring already running — what a GNOME login starts. A connection of the
// test's own stands in for it: the probe asks about the name, and the name is
// all this claims.
void TestSecretServiceProbe::aRunningKeyringIsFound()
{
    QVERIFY(offerKeyring(QString()));
    const QString owner = QStringLiteral("running-keyring");
    QDBusConnection keyring = QDBusConnection::connectToBus(m_address, owner);
    const auto close = qScopeGuard([&] {
        keyring.unregisterService(kSecrets);
        QDBusConnection::disconnectFromBus(owner);
    });
    QVERIFY(keyring.registerService(kSecrets));

    QVERIFY(secretServiceOnSessionBus());
#if defined(FT_HAVE_LIBSECRET)
    QVERIFY(CredentialStore::secureStorageAvailable());
#endif
}

// A keyring that is installed and not yet running: the bus starts it the first
// time anything asks for it, as it would for libsecret's first save. Saying no
// here would show the warning on a machine where saving works.
void TestSecretServiceProbe::aKeyringTheBusCanStartIsFound()
{
    QVERIFY(offerKeyring(QCoreApplication::applicationFilePath() + QLatin1Char(' ')
                         + QLatin1String(kBeAKeyring)));
    QVERIFY2(secretServiceOnSessionBus(), "a keyring the bus can start was taken for none");
    // It leaves as soon as it has the name; let it go before the next case asks.
    QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(kSecrets));
}

// Installed, and broken: the bus knows how to start a keyring, and the keyring
// does not come up (here it exits at once). libsecret's first save would fail
// the same way, so this is no storage.
void TestSecretServiceProbe::aKeyringThatWillNotStartIsNotThere()
{
    const QString fails = QStandardPaths::findExecutable(QStringLiteral("false"));
    QVERIFY(!fails.isEmpty());
    QVERIFY(offerKeyring(fails));
    QVERIFY2(!secretServiceOnSessionBus(), "a keyring that cannot start was taken for one");
    QVERIFY(!CredentialStore::secureStorageAvailable());
}

int main(int argc, char *argv[])
{
    if (argc == 2 && std::strcmp(argv[1], kBeAKeyring) == 0)
        return beAKeyring(argc, argv);
    QCoreApplication app(argc, argv);
    TestSecretServiceProbe tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "test_secret_service_probe.moc"
