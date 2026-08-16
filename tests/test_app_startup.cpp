// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTranslator>

#include "app/AppStartup.h"
#include "app/Backend.h"
#include "core/AppSettings.h"
#include "core/ConfigStore.h"
#include "core/DeepLink.h"
#include "core/InstanceControl.h"

class TestAppStartup : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void controlArgFromArgv();
    void urlOpenFilterBuffersUntilReady();
    void quitFilterEmitsShutdown();
    void prepareQuitRequestsApplicationQuit();
    void applyLanguageLoadsRussian();
    void wireInstanceServerForwardsCommand();
    void wireInstanceServerIgnoresWrongToken();
    void wireInstanceServerWithoutTokenRefusesEveryCommand();

private:
    // Every test gets its own socket so a listener left over from the previous
    // one can never be the thing that answers — an assertion about "the command
    // did not arrive" is worthless if it might have arrived somewhere else.
    static QString instanceSocketName(const QString &suffix);
    static void sendInstanceMessage(const QString &socketName, const QByteArray &msg);
};

void TestAppStartup::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("AppStartupTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QStandardPaths::setTestModeEnabled(true);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/ft-app-startup-test");
    QDir().mkpath(dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    saveAppSettings(AppSettings{});
    saveStoredConfigs({});
}

void TestAppStartup::controlArgFromArgv()
{
    char arg0[] = "FreeTunnel";
    char toggle[] = "freetunnel://toggle";
    char tt[] = "tt://?abc";
    char other[] = "--foo";
    char *argvToggle[] = {arg0, toggle, nullptr};
    char *argvTt[] = {arg0, tt, nullptr};
    char *argvNone[] = {arg0, other, nullptr};

    QCOMPARE(freetunnel::controlArgFrom(2, argvToggle), QStringLiteral("freetunnel://toggle"));
    QCOMPARE(freetunnel::controlArgFrom(2, argvTt), QStringLiteral("tt://?abc"));
    QVERIFY(freetunnel::controlArgFrom(2, argvNone).isEmpty());
}

void TestAppStartup::urlOpenFilterBuffersUntilReady()
{
    UrlOpenFilter filter;
    qApp->installEventFilter(&filter);
    Backend backend;

    QFileOpenEvent ev(QUrl(QStringLiteral("freetunnel://toggle")));
    QCoreApplication::sendEvent(qApp, &ev);
    QCOMPARE(filter.pending, QStringLiteral("freetunnel://toggle"));

    filter.ready(&backend, nullptr);
    QVERIFY(filter.pending.isEmpty());
}

void TestAppStartup::quitFilterEmitsShutdown()
{
    Backend backend;
    QuitFilter filter;
    filter.backend = &backend;
    qApp->installEventFilter(&filter);
    QSignalSpy spy(&backend, &Backend::aboutToShutdown);

    QCoreApplication::postEvent(qApp, new QEvent(QEvent::Quit));
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);
}

void TestAppStartup::prepareQuitRequestsApplicationQuit()
{
    Backend backend;
    QSignalSpy spy(&backend, &Backend::aboutToShutdown);
    backend.quitApplication();
    QCOMPARE(spy.count(), 1);
}

// applyLanguage() swallows a failed QTranslator::load() by design, so "it did
// not crash" says nothing: a missing or unregistered :/i18n/freetunnel_ru.qm
// leaves the app fully English while every switch in the UI reports success.
// Assert against the catalog itself instead. The expected text is derived from
// the translation rather than hardcoded in Cyrillic here, so the test keeps
// working when a wording changes and still fails when nothing was loaded.
void TestAppStartup::applyLanguageLoadsRussian()
{
    QQmlApplicationEngine engine;
    QTranslator *translator = nullptr;

    const QString sourceText = QStringLiteral("Connected");
    QCOMPARE(QCoreApplication::translate("Backend", "Connected"), sourceText);

    freetunnel::applyLanguage(*qGuiApp, engine, translator, QStringLiteral("ru"));
    QVERIFY2(translator != nullptr, "applyLanguage(\"ru\") created no translator at all");
    const QString russian = QCoreApplication::translate("Backend", "Connected");
    QVERIFY2(russian != sourceText,
             "the Russian catalog did not take effect: Backend strings are still English");
    QVERIFY2(russian.at(0).script() == QChar::Script_Cyrillic,
             "the loaded catalog is not the Russian one");

    // Switching back must uninstall it: leaving the previous catalog installed
    // would make the language switch one-way in a running app.
    freetunnel::applyLanguage(*qGuiApp, engine, translator, QStringLiteral("en"));
    QVERIFY2(translator == nullptr, "the Russian translator outlived the switch back to English");
    QCOMPARE(QCoreApplication::translate("Backend", "Connected"), sourceText);
}

QString TestAppStartup::instanceSocketName(const QString &suffix)
{
    return QStringLiteral("freetunnel-app-startup-%1-%2")
            .arg(QCoreApplication::applicationPid())
            .arg(suffix);
}

// Perform the second-instance handshake the way the real launcher does: connect,
// write one framed message, then sit still. handleInstanceConnection()
// deliberately does not act on the bytes as they arrive — it buffers them and
// waits kInstanceMessageIdleMs (250 ms) of silence before deciding the message
// is complete — so the wait afterwards is what makes a later "nothing happened"
// assertion mean anything. Without it the test would simply be outrunning the
// delivery timer and would stay green no matter how the auth gate behaves.
// Long enough that a command which was going to be delivered has been. Proving a
// negative needs this: without it, "the spy is empty" only says the listener had
// not got round to it yet, which would be just as green if the auth gate were gone.
static void waitOutInstanceDelivery()
{
    QTest::qWait(3000); // 12x the listener's 250 ms idle delay
}

void TestAppStartup::sendInstanceMessage(const QString &socketName, const QByteArray &msg)
{
    QLocalSocket client;
    client.connectToServer(socketName);
    QVERIFY(client.waitForConnected(2000));
    QCOMPARE(client.write(msg), static_cast<qint64>(msg.size()));
    // Send it exactly the way forwardToRunningInstance() does, and for the same
    // reason: on Windows a QLocalSocket is a named pipe whose write is completed
    // asynchronously, so it is still queued here — waitForBytesWritten() does not
    // drive it and its answer means nothing, which is why the production code
    // ignores the result too. disconnectFromServer() is the part that matters: it
    // flushes what is pending and closes, and the listener's deliver path is
    // driven by that disconnect. Letting the socket die with the scope instead
    // dropped the write on Windows and delivered nothing.
    client.flush();
    client.waitForBytesWritten(2000);
    client.disconnectFromServer();
    // No fixed wait here. Delivery is deliberately deferred by the listener
    // (kInstanceMessageIdleMs, 250 ms) and then has to cross the event loop, so
    // how long it takes depends on how loaded the machine is — a hardcoded second
    // was enough on this developer box and not always enough on a Windows runner.
    // Callers expecting something to arrive use QTRY_*; callers expecting silence
    // wait out the delay explicitly with waitOutInstanceDelivery().
    QTest::qWait(50);
}

// The authorised path: a peer holding the session token gets its command run —
// and run VERBATIM, which is the part worth proving. Asserting merely that
// "something happened" would stay green if the listener substituted a fixed verb
// for whatever arrived, and most of the control verbs are indistinguishable from
// the outside (connect and toggle both answer "Select a config first" with no
// configs stored). An import link is the exception: only ControlAction::ImportLink
// produces deepLinkImportConfirmationRequired, and the signal carries the link
// back, so the payload can be compared byte for byte with what was sent.
void TestAppStartup::wireInstanceServerForwardsCommand()
{
    Backend backend;
    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("good"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    freetunnel::wireInstanceServer(&server, backend, nullptr, QStringLiteral("tok"));

    freetunnel::DeepLinkConfig cfg;
    cfg.hostname = QStringLiteral("vpn.example.com");
    cfg.addresses = {QStringLiteral("1.2.3.4:443")};
    cfg.username = QStringLiteral("u");
    cfg.password = QStringLiteral("pw");
    cfg.name = QStringLiteral("Forwarded");
    const QString link = freetunnel::encodeDeepLink(cfg);
    QVERIFY(link.startsWith(QStringLiteral("tt://?")));

    QSignalSpy imports(&backend, &Backend::deepLinkImportConfirmationRequired);
    QVERIFY(imports.isValid());
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(QStringLiteral("tok"), link));

    QTRY_COMPARE_WITH_TIMEOUT(imports.count(), 1, 10000);
    // The link the Backend was asked to import is the one that went over the
    // socket — not a truncated, re-encoded or substituted one.
    QCOMPARE(imports.at(0).at(1).toString(), link);

    // A second, different verb through the same listener: if the payload were
    // being replaced by a constant, both messages would produce the same effect.
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      QStringLiteral("tok"),
                                      QStringLiteral("freetunnel://connect")));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
    QCOMPARE(imports.count(), 1); // still one — connect is not an import
    QCOMPARE(errors.at(0).at(0).toString(), Backend::tr("Select a config first"));
}

// This is the whole single-instance auth boundary. The local socket lives in a
// namespace every process of this user can reach, so a browser extension host, a
// compromised Electron app or an npm postinstall script can connect to it at
// will; the session token is the only thing that stops such a peer from sending
// "freetunnel://disconnect" to silently drop the tunnel, or a tt:// link to
// install the attacker's server config. The forged message below is perfectly
// framed and carries a real command precisely so that nothing but the token
// comparison can reject it.
void TestAppStartup::wireInstanceServerIgnoresWrongToken()
{
    Backend backend;
    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("wrongtoken"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    // Same LENGTH as the real token, differing in one byte. A forged token of a
    // different length only ever reaches the size check at the top of
    // instanceTokensEqual and returns before the byte comparison — so gutting that
    // comparison would leave a length-mismatch test green while any same-length
    // guess walked straight in.
    const QString realToken = QStringLiteral("0123456789abcdef");
    const QString forgedToken = QStringLiteral("0123456789abcdeF");
    QCOMPARE(forgedToken.size(), realToken.size());

    freetunnel::wireInstanceServer(&server, backend, nullptr, realToken);

    QSignalSpy spy(&backend, &Backend::errorOccurred);
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      forgedToken,
                                      QStringLiteral("freetunnel://connect")));
    waitOutInstanceDelivery();
    QCOMPARE(spy.count(), 0);

    // Same listener, same command, correct token. Without this second half a
    // green result above would equally describe a listener that ignores
    // everything, a Backend that never emits, or a test that simply never
    // reached the socket — the contrast is what makes the silence evidence.
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      realToken, QStringLiteral("freetunnel://connect")));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
}

// startSingleInstanceServer() clears the token when writeInstanceAuthToken()
// fails (no Secret Service and an unwritable config dir), and then still listens
// on the socket. That degraded instance has nothing to authenticate against, so
// it must fail closed and refuse every command. Failing open here would make the
// attacker's best case out of the user's worst one.
void TestAppStartup::wireInstanceServerWithoutTokenRefusesEveryCommand()
{
    Backend backend;
    QLocalServer unarmed;
    const QString unarmedName = instanceSocketName(QStringLiteral("notoken"));
    QLocalServer::removeServer(unarmedName);
    unarmed.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(unarmed.listen(unarmedName));
    freetunnel::wireInstanceServer(&unarmed, backend, nullptr, QString());

    QSignalSpy spy(&backend, &Backend::errorOccurred);
    sendInstanceMessage(unarmedName, freetunnel::formatInstanceMessage(
                                             QStringLiteral("any-token"),
                                             QStringLiteral("freetunnel://connect")));
    waitOutInstanceDelivery();
    QCOMPARE(spy.count(), 0);

    // An empty token on the wire must not be treated as matching the empty
    // stored one: "neither side has a token" is the one case where a naive
    // equality check would hand over control to an unauthenticated peer.
    sendInstanceMessage(unarmedName, QByteArray("\nfreetunnel://connect"));
    waitOutInstanceDelivery();
    QCOMPARE(spy.count(), 0);

    // Control: the identical command on a listener that does hold a token is
    // accepted by this same Backend, so the two silences above are about the
    // missing token and not about an observable that never fires.
    QLocalServer armed;
    const QString armedName = instanceSocketName(QStringLiteral("notoken-control"));
    QLocalServer::removeServer(armedName);
    armed.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(armed.listen(armedName));
    freetunnel::wireInstanceServer(&armed, backend, nullptr, QStringLiteral("tok"));

    sendInstanceMessage(armedName, freetunnel::formatInstanceMessage(
                                           QStringLiteral("tok"),
                                           QStringLiteral("freetunnel://connect")));
    QCOMPARE(spy.count(), 1);
}

QTEST_MAIN(TestAppStartup)
#include "test_app_startup.moc"
