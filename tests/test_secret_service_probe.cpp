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

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QCryptographicHash>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

const char kBeASecretService[] = "--be-a-secret-service";

// ---- A Secret Service that holds what a test gives it --------------------------
//
// Enough of org.freedesktop.Secret for the real libsecret to search it, read from it
// and ask it to unlock, which is what a keyring that is locked does and no real one
// can be made to do on demand: gnome-keyring asks a prompter that has no display here.
// Run by the test as a process of its own (libsecret's calls block the one that makes
// them), on the test's private bus. The items are read from a file on every call, so a
// test changes what it holds by writing the file.
//
//   [{"locked": bool, "secret": "...", "attributes": {"name": "value", ...}}, ...]
//
// A locked item stays locked: unlocking it is a prompt, and the prompt is dismissed,
// as when a user closes the keyring's password dialog.
struct FakeSecret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const FakeSecret &secret)
{
    arg.beginStructure();
    arg << secret.session << secret.parameters << secret.value << secret.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, FakeSecret &secret)
{
    arg.beginStructure();
    arg >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    arg.endStructure();
    return arg;
}

class FakeSecretService : public QDBusVirtualObject {
public:
    explicit FakeSecretService(const QString &itemsFile) : m_itemsFile(itemsFile) {}

    QString introspect(const QString &) const override { return QString(); }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.type() != QDBusMessage::MethodCallMessage)
            return false;
        const QString root = QStringLiteral("/org/freedesktop/secrets");
        const QString service = QStringLiteral("org.freedesktop.Secret.Service");
        const QString prompt = QStringLiteral("org.freedesktop.Secret.Prompt");
        const QString promptPath = root + QStringLiteral("/prompt/u1");
        const QString member = message.member();
        if (message.interface() == QLatin1String("org.freedesktop.DBus.Properties")
            && member == QLatin1String("GetAll")) {
            return connection.send(message.createReply(QVariant(properties(message))));
        }
        if (message.path() == root && message.interface() == service) {
            if (member == QLatin1String("OpenSession")) {
                // Plain only: a client that asks for more is told, and asks again.
                if (message.arguments().value(0).toString() != QLatin1String("plain")) {
                    return connection.send(message.createErrorReply(
                            QDBusError::NotSupported, QStringLiteral("only plain sessions")));
                }
                return connection.send(message.createReply(
                        QVariantList{QVariant::fromValue(QDBusVariant(QString())),
                                     QVariant::fromValue(QDBusObjectPath(root + QStringLiteral("/session/s1")))}));
            }
            if (member == QLatin1String("SearchItems")) {
                const auto wanted = qdbus_cast<QMap<QString, QString>>(message.arguments().value(0));
                QList<QDBusObjectPath> unlocked;
                QList<QDBusObjectPath> locked;
                const QJsonArray items = readItems();
                for (int i = 0; i < items.size(); ++i) {
                    const QJsonObject item = items.at(i).toObject();
                    const QJsonObject have = item.value(QStringLiteral("attributes")).toObject();
                    bool matches = true;
                    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it)
                        matches = matches && have.value(it.key()).toString() == it.value();
                    if (!matches)
                        continue;
                    (item.value(QStringLiteral("locked")).toBool() ? locked : unlocked) << itemPath(i);
                }
                return connection.send(message.createReply(
                        QVariantList{QVariant::fromValue(unlocked), QVariant::fromValue(locked)}));
            }
            if (member == QLatin1String("GetSecrets")) {
                const auto wanted = qdbus_cast<QList<QDBusObjectPath>>(message.arguments().value(0));
                const QDBusObjectPath session = qvariant_cast<QDBusObjectPath>(message.arguments().value(1));
                const QJsonArray items = readItems();
                QDBusArgument secrets;
                secrets.beginMap(QMetaType::fromType<QDBusObjectPath>(), QMetaType::fromType<FakeSecret>());
                for (int i = 0; i < items.size(); ++i) {
                    const QJsonObject item = items.at(i).toObject();
                    if (item.value(QStringLiteral("locked")).toBool() || !wanted.contains(itemPath(i)))
                        continue;
                    secrets.beginMapEntry();
                    secrets << itemPath(i)
                            << FakeSecret{session, QByteArray(),
                                          item.value(QStringLiteral("secret")).toString().toUtf8(),
                                          QStringLiteral("text/plain")};
                    secrets.endMapEntry();
                }
                secrets.endMap();
                return connection.send(message.createReply(QVariant::fromValue(secrets)));
            }
            if (member == QLatin1String("Unlock")) {
                // Nothing unlocks without the user, who is asked.
                return connection.send(message.createReply(
                        QVariantList{QVariant::fromValue(QList<QDBusObjectPath>()),
                                     QVariant::fromValue(QDBusObjectPath(promptPath))}));
            }
        }
        if (message.path() == promptPath && message.interface() == prompt
            && member == QLatin1String("Prompt")) {
            if (!connection.send(message.createReply()))
                return false;
            // The user closed the dialog: dismissed, and nothing unlocked (libsecret
            // reads the result as a list of object paths whatever happened).
            QDBusMessage done = QDBusMessage::createSignal(promptPath, prompt, QStringLiteral("Completed"));
            done << true << QVariant::fromValue(QDBusVariant(QVariant::fromValue(QList<QDBusObjectPath>())));
            return connection.send(done);
        }
        return false;
    }

private:
    static QDBusObjectPath itemPath(int index)
    {
        return QDBusObjectPath(QStringLiteral("/org/freedesktop/secrets/collection/login/%1").arg(index + 1));
    }

    QJsonArray readItems() const
    {
        QFile file(m_itemsFile);
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).array() : QJsonArray();
    }

    // What a proxy reads when it is made: the service's collections (none), an item's
    // state, and for anything else an empty set, which is a valid answer.
    QVariantMap properties(const QDBusMessage &message) const
    {
        const QString what = message.arguments().value(0).toString();
        QVariantMap out;
        if (message.path() == QLatin1String("/org/freedesktop/secrets")
            && what == QLatin1String("org.freedesktop.Secret.Service")) {
            out.insert(QStringLiteral("Collections"), QVariant::fromValue(QList<QDBusObjectPath>()));
            return out;
        }
        if (what != QLatin1String("org.freedesktop.Secret.Item"))
            return out;
        const QJsonArray items = readItems();
        for (int i = 0; i < items.size(); ++i) {
            if (itemPath(i).path() != message.path())
                continue;
            const QJsonObject item = items.at(i).toObject();
            QDBusArgument attributes;
            attributes.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QString>());
            const QJsonObject have = item.value(QStringLiteral("attributes")).toObject();
            for (auto it = have.begin(); it != have.end(); ++it) {
                attributes.beginMapEntry();
                attributes << it.key() << it.value().toString();
                attributes.endMapEntry();
            }
            attributes.endMap();
            out.insert(QStringLiteral("Locked"), item.value(QStringLiteral("locked")).toBool());
            out.insert(QStringLiteral("Attributes"), QVariant::fromValue(attributes));
            out.insert(QStringLiteral("Label"), QStringLiteral("FreeTunnel test item"));
            out.insert(QStringLiteral("Created"), QVariant::fromValue(quint64(1)));
            out.insert(QStringLiteral("Modified"), QVariant::fromValue(quint64(1)));
        }
        return out;
    }

    QString m_itemsFile;
};

// Run by the test: claim the name on the session bus, serve the file's items, and stay
// until the test ends the process.
int beASecretService(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<FakeSecret>();
    FakeSecretService service(QString::fromLocal8Bit(argv[2]));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()
        || !bus.registerVirtualObject(QStringLiteral("/org/freedesktop/secrets"), &service,
                                      QDBusConnection::SubPath)
        || !bus.registerService(kSecrets)) {
        return 1;
    }
    return app.exec();
}

} // namespace

class TestSecretServiceProbe : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();
    void aBusWithNoKeyringHasNoStorage();
    void aRunningKeyringIsFound();
    void aKeyringTheBusCanStartIsFound();
    void aKeyringThatWillNotStartIsNotThere();
    void aBareBusCannotBeAskedForAnEntry();
    void aKeyringThatClaimsTheNameAndLeavesCannotBeAsked();
    void aLegacyFileWithNoKeyringIsFoundAndSaysSo();
    void anItemTheKeyringHoldsUnlockedIsFound();
    void aLockedItemWhoseUnlockWasDismissedIsNotAbsent();
    void aLockedItemSecretToolStoredIsNotAbsent();
    void anItemSecretToolStoredIsFoundThroughIt();
    void aSecretToolThatFailsDoesNotMakeNothingStoredAnError();
    void secretToolAloneListsALockedItemItsLookupCannotSee();

private:
    // Have the bus offer a keyring started by @p exec, or none for an empty one.
    bool offerKeyring(const QString &exec);
    // A Secret Service of the fake kind above holding @p items, until the test case ends.
    bool runKeyring(const QJsonArray &items);
    void stopKeyring();
    // PATH holds a secret-tool that answers as FAKE_TOOL_LOOKUP and FAKE_TOOL_SEARCH
    // say (see the script), or none.
    bool useSecretTool(const QString &lookup, const QString &search = QString());
    void useNoSecretTool();
    // An item of the fake keyring, under the attributes libsecret's own schema gives
    // an entry, or under the ones secret-tool stores one by.
    static QJsonObject item(const QMap<QString, QString> &attributes, bool locked,
                            const QString &secret = QString());
    static QMap<QString, QString> ownAttributes(const QString &key);
    static QMap<QString, QString> secretToolAttributes(const QString &key);
    static void libsecretCannotSearch();

    QTemporaryDir m_dir;
    QProcess m_daemon;
    QString m_address;
    QProcess m_keyring;
    QByteArray m_path;
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
    m_path = qgetenv("PATH");
    // The legacy password files of the last case live under the app's config
    // folder, which test mode keeps away from the real one.
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SecretServiceProbeTest"));
    QStandardPaths::setTestModeEnabled(true);
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
    stopKeyring();
    m_daemon.terminate();
    m_daemon.waitForFinished(5000);
}

void TestSecretServiceProbe::cleanup()
{
    qputenv("PATH", m_path);
    qunsetenv("FAKE_TOOL_LOOKUP");
    qunsetenv("FAKE_TOOL_SEARCH");
}

// One process for all the cases that use it: libsecret keeps the connection it made
// to the first one by the bus's name for it, and answers "not provided by any .service
// files" to a second one that takes over the well-known name. What it holds is
// the file, which it reads at every call.
bool TestSecretServiceProbe::runKeyring(const QJsonArray &items)
{
    const QString file = m_dir.filePath(QStringLiteral("items.json"));
    QFile out(file);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(QJsonDocument(items).toJson());
    out.close();
    if (m_keyring.state() != QProcess::NotRunning)
        return true;
    if (!offerKeyring(QString()))
        return false;
    m_keyring.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    m_keyring.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_keyring.start(QCoreApplication::applicationFilePath(),
                    {QString::fromLatin1(kBeASecretService), file});
    if (!m_keyring.waitForStarted(5000))
        return false;
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    for (int waited = 0; waited < 5000 && !bus->isServiceRegistered(kSecrets); waited += 50)
        QTest::qWait(50);
    return bus->isServiceRegistered(kSecrets);
}

void TestSecretServiceProbe::stopKeyring()
{
    if (m_keyring.state() == QProcess::NotRunning)
        return;
    m_keyring.terminate();
    m_keyring.waitForFinished(5000);
    // Gone from the bus before the next case asks, or it would be asked.
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    for (int waited = 0; waited < 5000 && bus->isServiceRegistered(kSecrets); waited += 50)
        QTest::qWait(50);
}

bool TestSecretServiceProbe::useSecretTool(const QString &lookup, const QString &search)
{
    const QString dir = m_dir.filePath(QStringLiteral("tool"));
    if (!QDir().mkpath(dir))
        return false;
    QFile script(QDir(dir).filePath(QStringLiteral("secret-tool")));
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    // Plain literals: moc cannot read past a raw string.
    script.write("#!/bin/sh\n"
                 "case \"$1\" in\n"
                 "  lookup)\n"
                 "    case \"$FAKE_TOOL_LOOKUP\" in\n"
                 "      found) printf 'tool-value'; exit 0;;\n"
                 "      silent) exit 1;;\n"
                 "      nobus) echo 'secret-tool: Cannot autolaunch D-Bus without X11 $DISPLAY' >&2; exit 1;;\n"
                 "      other) echo 'usage: secret-tool' >&2; exit 2;;\n"
                 "      hang) exec /bin/sleep 30;;\n"
                 "    esac;;\n"
                 "  search)\n"
                 "    case \"$FAKE_TOOL_SEARCH\" in\n"
                 "      listed) echo 'secret-tool: Cannot get secret of a locked object' >&2\n"
                 "              printf '[/1]\\nlabel = FreeTunnel\\nattribute.service = s\\n'; exit 0;;\n"
                 "      empty) exit 0;;\n"
                 "      unsupported) echo 'usage: secret-tool search'; exit 2;;\n"
                 "      hang) exec /bin/sleep 30;;\n"
                 "    esac;;\n"
                 "esac\n"
                 "exit 99\n");
    script.close();
    if (!script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return false;
    qputenv("PATH", QFile::encodeName(dir));
    qputenv("FAKE_TOOL_LOOKUP", lookup.toUtf8());
    qputenv("FAKE_TOOL_SEARCH", search.toUtf8());
    return !QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty();
}

void TestSecretServiceProbe::useNoSecretTool()
{
    const QString dir = m_dir.filePath(QStringLiteral("no-tool"));
    QDir().mkpath(dir);
    qputenv("PATH", QFile::encodeName(dir));
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

// Whether a store that cannot be asked is told apart from one that holds nothing:
// a build with the insecure fallback keeps its passwords in the file and answers
// from it, so the cases below are about the builds that are shipped.
static bool buildHasTheInsecureFallback()
{
#if defined(FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK)
    return true;
#else
    return false;
#endif
}

// The bus is there and nothing on it keeps secrets. Asked for an entry, the answer
// is not "nothing is stored": a config's auth key may be in a keyring that is not
// running, and reading its absence as no key would connect without it.
void TestSecretServiceProbe::aBareBusCannotBeAskedForAnEntry()
{
    if (buildHasTheInsecureFallback())
        QSKIP("a build with the insecure fallback is answered by its file");
    QVERIFY(offerKeyring(QString()));
    const StoredSecret answer = CredentialStore::lookup(QStringLiteral("/tmp/ft-probe-bare-bus.toml"));
    QVERIFY2(answer.failed(), "a bus with no keyring was taken for a keyring that holds nothing");
    QVERIFY(answer.value.isEmpty());
}

// A keyring that takes the name and is gone before it answers anything: the
// name was there, and the store still cannot be asked.
void TestSecretServiceProbe::aKeyringThatClaimsTheNameAndLeavesCannotBeAsked()
{
    if (buildHasTheInsecureFallback())
        QSKIP("a build with the insecure fallback is answered by its file");
    QVERIFY(offerKeyring(QCoreApplication::applicationFilePath() + QLatin1Char(' ')
                         + QLatin1String(kBeAKeyring)));
    const StoredSecret answer = CredentialStore::lookup(QStringLiteral("/tmp/ft-probe-keyring-leaves.toml"));
    QVERIFY2(answer.failed(), "a keyring that left was taken for one that holds nothing");
    QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(kSecrets));
}

// A password an older build left in its 0600 file, and no keyring to move it into:
// it is read, and said to be from the file, which is how a config that keeps its
// secrets there is told from one that keeps them in a store.
void TestSecretServiceProbe::aLegacyFileWithNoKeyringIsFoundAndSaysSo()
{
    if (buildHasTheInsecureFallback())
        QSKIP("a build with the insecure fallback never calls its file legacy");
    QVERIFY(offerKeyring(QString()));
    const QString key = QStringLiteral("/tmp/ft-probe-legacy.toml");
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/credentials");
    QVERIFY(QDir().mkpath(dir));
    const QString file = QDir(dir).filePath(QString::fromLatin1(
            QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()));
    QFile legacy(file);
    QVERIFY(legacy.open(QIODevice::WriteOnly | QIODevice::Truncate));
    legacy.write("legacy-password");
    legacy.close();
    const auto remove = qScopeGuard([&] { QFile::remove(file); });

    const StoredSecret answer = CredentialStore::lookup(key);
    QVERIFY2(answer.found(), "a password in the legacy file was not found with no keyring around");
    QCOMPARE(answer.value, QStringLiteral("legacy-password"));
    QVERIFY(answer.fromLegacyFile);
    QVERIFY2(QFile::exists(file), "the file went though nothing took its password");
}

// The cases that read from a locked keyring need a libsecret that can search. A
// machine without one skips them; CI is built with one (libsecret-1-dev, before
// 0.19 there is none), and a CI that lost it, to a dependency that is found quietly
// or a definition that went missing, must not go green on skipped cases.
void TestSecretServiceProbe::libsecretCannotSearch()
{
    if (qEnvironmentVariableIsSet("CI"))
        QTest::qFail("this build has no libsecret that can search (FT_HAVE_LIBSECRET, FT_LIBSECRET_HAS_SEARCH)",
                     __FILE__, __LINE__);
    else
        QTest::qSkip("needs a libsecret that can search", __FILE__, __LINE__);
}

QJsonObject TestSecretServiceProbe::item(const QMap<QString, QString> &attributes, bool locked,
                                         const QString &secret)
{
    QJsonObject have;
    for (auto it = attributes.cbegin(); it != attributes.cend(); ++it)
        have.insert(it.key(), it.value());
    return QJsonObject{{QStringLiteral("locked"), locked},
                       {QStringLiteral("secret"), secret},
                       {QStringLiteral("attributes"), have}};
}

QMap<QString, QString> TestSecretServiceProbe::ownAttributes(const QString &key)
{
    return {{QStringLiteral("account"), key}, {QStringLiteral("xdg:schema"), credentialServiceName()}};
}

QMap<QString, QString> TestSecretServiceProbe::secretToolAttributes(const QString &key)
{
    return {{QStringLiteral("service"), credentialServiceName()}, {QStringLiteral("account"), key}};
}

// The keyring is there, and an entry is in it, unlocked: libsecret reads it. The
// control for the cases below, which are all this fake keyring refusing: without it
// they would pass for a fake that refuses everything.
void TestSecretServiceProbe::anItemTheKeyringHoldsUnlockedIsFound()
{
#if defined(FT_HAVE_LIBSECRET) && defined(FT_LIBSECRET_HAS_SEARCH)
    useNoSecretTool();
    const QString key = QStringLiteral("/tmp/ft-probe-unlocked.toml");
    QVERIFY(runKeyring({item(ownAttributes(key), false, QStringLiteral("stored-pw"))}));
    const StoredSecret answer = CredentialStore::lookup(key);
    QVERIFY2(answer.found(), qPrintable(answer.detail));
    QCOMPARE(answer.value, QStringLiteral("stored-pw"));
    QVERIFY(CredentialStore::lookup(QStringLiteral("/tmp/ft-probe-other.toml")).notFound());
#else
    libsecretCannotSearch();
#endif
}

// What a locked keyring looks like once the user closes its password dialog:
// libsecret gets nothing back and no error, the same as for an entry that is not
// there. It is not absent, and a config whose auth key is in it must not connect
// without the key, nor be shared without it.
void TestSecretServiceProbe::aLockedItemWhoseUnlockWasDismissedIsNotAbsent()
{
#if defined(FT_HAVE_LIBSECRET) && defined(FT_LIBSECRET_HAS_SEARCH)
    useNoSecretTool();
    const QString key = QStringLiteral("/tmp/ft-probe-locked.toml");
    QVERIFY(runKeyring({item(ownAttributes(key), true, QStringLiteral("stored-pw"))}));
    const StoredSecret answer = CredentialStore::lookup(key);
    QVERIFY2(answer.failed(), "a locked entry whose prompt was dismissed was read as absent");
    QVERIFY2(answer.detail.contains(QLatin1String("could not be unlocked")), qPrintable(answer.detail));
    QVERIFY(answer.value.isEmpty());
    // Another entry, in the same locked keyring: nothing there, and no prompt for it.
    QVERIFY(CredentialStore::lookup(QStringLiteral("/tmp/ft-probe-other.toml")).notFound());
#else
    libsecretCannotSearch();
#endif
}

// The same for what secret-tool stored, which is how a password of 1.0.0 is kept
// (libsecret's schema does not match it). secret-tool exits 1 without a word for a
// locked item whose prompt was dismissed, and is killed by the timeout when the
// prompt is still up; neither is "nothing stored".
void TestSecretServiceProbe::aLockedItemSecretToolStoredIsNotAbsent()
{
#if defined(FT_HAVE_LIBSECRET) && defined(FT_LIBSECRET_HAS_SEARCH)
    const QString key = QStringLiteral("/tmp/ft-probe-locked-tool.toml");
    QVERIFY(runKeyring({item(secretToolAttributes(key), true, QStringLiteral("stored-pw"))}));
    for (const QString &lookup : {QStringLiteral("silent"), QStringLiteral("hang"), QString()}) {
        if (lookup.isEmpty())
            useNoSecretTool();
        else
            QVERIFY(useSecretTool(lookup));
        const StoredSecret answer = CredentialStore::lookup(key);
        QVERIFY2(answer.failed(), qPrintable(QStringLiteral("secret-tool: ") + lookup));
        QVERIFY2(answer.detail.contains(QLatin1String("secret-tool stored")), qPrintable(answer.detail));
        QVERIFY(answer.value.isEmpty());
        // Another entry in the same locked keyring: nothing there (the killed one is
        // not asked again, for the seconds it costs).
        if (lookup == QLatin1String("silent"))
            QVERIFY(CredentialStore::lookup(QStringLiteral("/tmp/ft-probe-other.toml")).notFound());
    }
#else
    libsecretCannotSearch();
#endif
}

// An item secret-tool stored that can be read is read, by secret-tool: nothing about
// the search above takes it away.
void TestSecretServiceProbe::anItemSecretToolStoredIsFoundThroughIt()
{
#if defined(FT_HAVE_LIBSECRET) && defined(FT_LIBSECRET_HAS_SEARCH)
    const QString key = QStringLiteral("/tmp/ft-probe-unlocked-tool.toml");
    QVERIFY(runKeyring({item(secretToolAttributes(key), false, QStringLiteral("stored-pw"))}));
    QVERIFY(useSecretTool(QStringLiteral("found")));
    const StoredSecret answer = CredentialStore::lookup(key);
    QVERIFY2(answer.found(), qPrintable(answer.detail));
    QCOMPARE(answer.value, QStringLiteral("tool-value"));
#else
    libsecretCannotSearch();
#endif
}

// libsecret has been asked and says nothing is stored under the name, and no item
// of secret-tool's is there either; a secret-tool that then fails in whatever way
// (it is not what decides) leaves that answer as it is, as it always has.
void TestSecretServiceProbe::aSecretToolThatFailsDoesNotMakeNothingStoredAnError()
{
#if defined(FT_HAVE_LIBSECRET) && defined(FT_LIBSECRET_HAS_SEARCH)
    QVERIFY(runKeyring({item(ownAttributes(QStringLiteral("/tmp/ft-probe-else.toml")), true)}));
    for (const QString &lookup : {QStringLiteral("nobus"), QStringLiteral("other"), QStringLiteral("silent")}) {
        QVERIFY(useSecretTool(lookup));
        const StoredSecret answer = CredentialStore::lookup(QStringLiteral("/tmp/ft-probe-nothing.toml"));
        QVERIFY2(answer.notFound(), qPrintable(lookup + QLatin1Char(' ') + answer.detail));
    }
#else
    libsecretCannotSearch();
#endif
}

// Without libsecret there is only secret-tool, and its lookup cannot say that an
// item is there but locked; its search, which cannot prompt, can.
void TestSecretServiceProbe::secretToolAloneListsALockedItemItsLookupCannotSee()
{
#if !defined(FT_HAVE_LIBSECRET)
    const QString key = QStringLiteral("/tmp/ft-probe-tool-alone.toml");
    struct Case {
        const char *lookup;
        const char *search;
        SecretStatus want;
        const char *detail;
    };
    const QList<Case> cases = {
        {"found", "", SecretStatus::Found, ""},
        {"silent", "empty", SecretStatus::NotFound, ""},
        {"silent", "listed", SecretStatus::Error, "lists an item"},
        // A secret-tool with no `search`, one that fails (here printing as it does),
        // or one that does not answer is no answer: as before.
        {"silent", "unsupported", SecretStatus::NotFound, ""},
        {"silent", "", SecretStatus::NotFound, ""},
        {"silent", "hang", SecretStatus::NotFound, ""},
        // Its lookup said why it could not ask, which is an error without asking more.
        {"nobus", "empty", SecretStatus::Error, "Cannot autolaunch"},
        {"other", "empty", SecretStatus::Error, "exit 2"},
        {"hang", "empty", SecretStatus::Error, "did not answer in time"},
    };
    for (const Case &c : cases) {
        const QString name = QStringLiteral("lookup %1, search %2")
                                     .arg(QLatin1String(c.lookup), QLatin1String(c.search));
        QVERIFY2(useSecretTool(QLatin1String(c.lookup), QLatin1String(c.search)), qPrintable(name));
        const StoredSecret answer = CredentialStore::lookup(key);
        QVERIFY2(answer.status == c.want, qPrintable(name + QLatin1String(": ") + answer.detail));
        QVERIFY2(answer.detail.contains(QLatin1String(c.detail)), qPrintable(name + QLatin1String(": ") + answer.detail));
        if (c.want == SecretStatus::Found)
            QCOMPARE(answer.value, QStringLiteral("tool-value"));
        else
            QVERIFY2(answer.value.isEmpty(), qPrintable(name));
    }
#else
    QSKIP("a build with libsecret searches with libsecret");
#endif
}

int main(int argc, char *argv[])
{
    if (argc == 2 && std::strcmp(argv[1], kBeAKeyring) == 0)
        return beAKeyring(argc, argv);
    if (argc == 3 && std::strcmp(argv[1], kBeASecretService) == 0)
        return beASecretService(argc, argv);
    QCoreApplication app(argc, argv);
    TestSecretServiceProbe tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "test_secret_service_probe.moc"
