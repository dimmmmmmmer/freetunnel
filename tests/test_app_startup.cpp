// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QScopeGuard>

#include <QElapsedTimer>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QLibraryInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTranslator>

#include <atomic>
#include <memory>

#include "app/AppStartup.h"
#include "app/Backend.h"
#include "core/AppSettings.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/ControlCommand.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "core/InstanceControl.h"
#include "helper_ipc_mock_server.h"

class TestAppStartup : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void controlArgFromArgv();
    void aUrlHandlerLaunchIsALink();
    void urlOpenFilterBuffersUntilReady();
    void aLinkOpenedAtLaunchIsAskedAboutInTheWindow();
    void quitFilterEmitsShutdown();
    void prepareQuitRequestsApplicationQuit();
    void applyLanguageLoadsRussian();
    void applyLanguageLoadsQtsOwnRussianToo();
    void qtsOwnRussianIsFoundWhereWindowsAndMacShipIt();
    void guiWiringBuildsTheAppInAKnownOrder();
    void wireInstanceServerForwardsCommand();
    void aSecondLaunchBringsTheWindowBack();
    void aForwardedToggleLeavesTheWindowAlone();
    void aForwardedLinkThatWouldDisconnectAsksInTheWindow();
    void aMessageSplitAcrossChunksStillArrives();
    void wireInstanceServerIgnoresWrongToken();
    void wireInstanceServerSurvivesASlowFirstChunk();
    void aMessageSurvivesAStallInTheTurnThatAcceptsIt();
    void aLaunchWaitsForABusyInstanceToTakeItsMessage();
    void aLaunchSaysWhenItsMessageWasNotTaken();
    void aHandOverGivesUpWhenTheBudgetRunsOut();
    void aLaunchToAnInstanceThatIsQuittingStartsInstead();
    void wireInstanceServerWithoutTokenRefusesEveryCommand();
    void aLinkSentWhileStartingIsHandled();
    void aSecondLaunchNeverStartsBesideALiveInstance();
    void aLaunchWhileQuittingFindsNoInstance();
    void wireInstanceServerHandsTheListenerToBackend();
    void aTokenDeletedAsTheInstanceStartsIsPutBack();
    void aLaunchWithNoTokenToShowHasItPutBack();
    void aQuitAfterHandingTheNameOverLeavesTheToken();

private:
    // Every test gets its own socket so a listener left over from the previous
    // one can never be the thing that answers — an assertion about "the command
    // did not arrive" is worthless if it might have arrived somewhere else.
    static QString instanceSocketName(const QString &suffix);
    static void sendInstanceMessage(const QString &socketName, const QByteArray &msg);
    // A config of its own, stored and made the active one, deleted with the guard.
    static QString storeActiveConfig(const QString &hostname, bool withPassword);
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
    // Every Backend checks for updates 1.2 s after it is made, and with nothing
    // set that is a real HTTPS request to api.github.com. Part of it runs on
    // this thread: the proxy lookup and the TLS setup, which can take seconds on
    // a fresh CI machine. Here it goes to a port nothing listens on, over plain
    // HTTP, and fails at once. A background check fails silently.
    qputenv("FT_GITHUB_API_BASE", "http://127.0.0.1:9");
}

// Backend::quitApplication() queues the application's exit, and three tests
// here call it. The exit ran in whatever came next and emitted aboutToQuit,
// which the application does once. From then on every event loop entered in
// this binary returned at once, nested ones and QSignalSpy::wait() included, so
// later tests ran in a state no running FreeTunnel is ever in. No test leaves
// a queued call on the application behind.
void TestAppStartup::cleanup()
{
    QCoreApplication::removePostedEvents(qApp, QEvent::MetaCall);
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

// The URL handler starts FreeTunnel as "FreeTunnel --url-handler <url>", and a
// command someone runs is the same URL without it. Only the first may be asked
// about, so the difference has to survive into the control string.
void TestAppStartup::aUrlHandlerLaunchIsALink()
{
    using freetunnel::ControlAction;
    using freetunnel::controlArgFrom;
    using freetunnel::parseControlCommand;
    char arg0[] = "FreeTunnel";
    char handler[] = "--url-handler";
    char disconnect[] = "freetunnel://disconnect";
    char *argvLink[] = {arg0, handler, disconnect, nullptr};
    char *argvMarkAfter[] = {arg0, disconnect, handler, nullptr};
    char *argvCommand[] = {arg0, disconnect, nullptr};
    char *argvMenu[] = {arg0, handler, nullptr};

    const auto link = parseControlCommand(controlArgFrom(3, argvLink));
    QCOMPARE(link.action, ControlAction::Disconnect);
    QVERIFY(link.fromLink);
    QVERIFY(parseControlCommand(controlArgFrom(3, argvMarkAfter)).fromLink);

    const auto command = parseControlCommand(controlArgFrom(2, argvCommand));
    QCOMPARE(command.action, ControlAction::Disconnect);
    QVERIFY(!command.fromLink);

    // The menu entry is the same line with no URL: an ordinary start.
    QVERIFY(controlArgFrom(2, argvMenu).isEmpty());
}

QString TestAppStartup::storeActiveConfig(const QString &hostname, bool withPassword)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QTemporaryFile file(QDir(base).filePath(hostname + QStringLiteral("-XXXXXX.toml")));
    file.setAutoRemove(false);
    if (!file.open())
        return QString();
    freetunnel::ConfigToml cfg;
    cfg.hostname = hostname;
    cfg.addresses = QStringLiteral("203.0.113.60:443");
    cfg.username = QStringLiteral("user");
    file.write(freetunnel::buildConfigToml(cfg).toUtf8());
    file.close();
    if (withPassword
        && !freetunnel::CredentialStore::storePassword(file.fileName(), QStringLiteral("secret")))
        return QString();
    saveStoredConfigs({file.fileName()});
    AppSettings settings = loadAppSettings();
    settings.last_config_path = file.fileName();
    saveAppSettings(settings);
    return file.fileName();
}

// macOS opens a freetunnel:// link by handing the URL to the app as an event,
// starting the app first if it is not running. With "Connect on startup" on, a
// page that opens freetunnel://disconnect would keep the VPN off: that is asked,
// in a window brought up for it, and until it is answered nothing is called off.
void TestAppStartup::aLinkOpenedAtLaunchIsAskedAboutInTheWindow()
{
    const QString config = storeActiveConfig(QStringLiteral("fileopen-server"), false);
    QVERIFY(!config.isEmpty());
    AppSettings settings = loadAppSettings();
    settings.auto_connect_on_start = true;
    saveAppSettings(settings);
    auto restore = qScopeGuard([&] {
        saveAppSettings(AppSettings{});
        saveStoredConfigs({});
        QFile::remove(config);
    });

    UrlOpenFilter filter;
    qApp->installEventFilter(&filter);
    QWindow window;
    window.resize(200, 200);
    window.show();
    window.setWindowStates(Qt::WindowMinimized);
    // Delivered before the app is ready, as at a launch the link caused.
    QFileOpenEvent ev(QUrl(QStringLiteral("freetunnel://disconnect")));
    QCoreApplication::sendEvent(qApp, &ev);

    Backend backend; // "Connect on startup" is armed from here
    QSignalSpy asked(&backend, &Backend::deepLinkDisconnectConfirmationRequired);
    filter.ready(&backend, &window);
    QCOMPARE(asked.count(), 1);
    QVERIFY2(!(window.windowStates() & Qt::WindowMinimized),
             "the question was put in a window left minimised");
    // What a yes does, and it keeps the auto-connect from starting anything here.
    backend.disconnectVpn();
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

// Qt's own words — the drawn file dialog's Open, Save and Cancel, the macOS
// application menu — come from Qt's catalogues, which were never loaded: the
// dialog had a Russian title over English buttons.
void TestAppStartup::applyLanguageLoadsQtsOwnRussianToo()
{
    const QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (!QFile::exists(dir + QStringLiteral("/qtbase_ru.qm")))
        QSKIP("this Qt was installed without its translations");
    QQmlApplicationEngine engine;
    QTranslator *translator = nullptr;
    QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancel"));
    freetunnel::applyLanguage(*qGuiApp, engine, translator, QStringLiteral("ru"));
    const QString russian = QCoreApplication::translate("QPlatformTheme", "Cancel");
    QVERIFY2(!russian.isEmpty() && russian.at(0).script() == QChar::Script_Cyrillic, qPrintable(russian));
    freetunnel::applyLanguage(*qGuiApp, engine, translator, QStringLiteral("en"));
    QCOMPARE(QCoreApplication::translate("QPlatformTheme", "Cancel"), QStringLiteral("Cancel"));
}

// The packaged app has nothing in Qt's own translations path on Windows and macOS.
// Windows ships windeployqt's merged catalogue as translations\qt_ru.qm beside
// FreeTunnel.exe; the macOS bundle has qtbase_ru.qm in Contents/Resources/
// translations, beside Contents/MacOS. Laid out that way here, around a Qt
// translations path that holds nothing. A copy of qtbase_ru.qm stands in for the
// merged file, which holds the same messages.
void TestAppStartup::qtsOwnRussianIsFoundWhereWindowsAndMacShipIt()
{
    const QString qtbase = QLibraryInfo::path(QLibraryInfo::TranslationsPath)
            + QStringLiteral("/qtbase_ru.qm");
    if (!QFile::exists(qtbase))
        QSKIP("this Qt was installed without its translations");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString nothingHere = root.filePath(QStringLiteral("qt-translations"));
    QVERIFY(QDir().mkpath(nothingHere));
    const QString windowsApp = root.filePath(QStringLiteral("FreeTunnel"));
    QVERIFY(QDir().mkpath(windowsApp + QStringLiteral("/translations")));
    QVERIFY(QFile::copy(qtbase, windowsApp + QStringLiteral("/translations/qt_ru.qm")));
    const QString macBundle = root.filePath(QStringLiteral("FreeTunnel.app/Contents"));
    QVERIFY(QDir().mkpath(macBundle + QStringLiteral("/MacOS")));
    QVERIFY(QDir().mkpath(macBundle + QStringLiteral("/Resources/translations")));
    QVERIFY(QFile::copy(qtbase, macBundle + QStringLiteral("/Resources/translations/qtbase_ru.qm")));

    const auto cancel = []() { return QCoreApplication::translate("QPlatformTheme", "Cancel"); };
    QCOMPARE(cancel(), QStringLiteral("Cancel"));
    for (const QString &appDir : {windowsApp, macBundle + QStringLiteral("/MacOS")}) {
        {
            QTranslator owner; // its children, the catalogues, go with it
            QVERIFY2(freetunnel::installQtCatalogues(*qGuiApp, &owner, QStringLiteral("ru"),
                                                     freetunnel::qtCatalogueDirs(nothingHere, appDir)),
                     qPrintable(appDir));
            const QString russian = cancel();
            QVERIFY2(!russian.isEmpty() && russian.at(0).script() == QChar::Script_Cyrillic,
                     qPrintable(appDir + QStringLiteral(": ") + russian));
        }
        QCOMPARE(cancel(), QStringLiteral("Cancel"));
    }
    QTranslator owner;
    QVERIFY(!freetunnel::installQtCatalogues(*qGuiApp, &owner, QStringLiteral("ru"),
                                             freetunnel::qtCatalogueDirs(nothingHere, nothingHere)));
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

// One write of a second launch, after a pause of its own.
struct TimedChunk {
    int pauseMs;
    QByteArray bytes;
};

// The second launch's side of the handshake, on a thread of its own, as in a
// second process: connect to @p socketName, then pause before each chunk and
// write it. Each chunk is taken by the listener before the next. The last one
// goes through handOverInstanceMessage(), exactly as a launch sends, which also
// closes. Meanwhile this thread turns its event loop, as a running FreeTunnel
// does, and that is where the listener accepts and reads. True when every chunk
// was taken, and the last one well within the launch's budget: with the
// listener free, a hand-over that took long is one that waited for nothing.
static bool sendFromAnotherThread(const QString &socketName, const QList<TimedChunk> &chunks,
                                  int budgetMs = freetunnel::kInstanceHandOverMs)
{
    std::atomic<bool> taken{false};
    std::atomic<qint64> handOverMs{-1};
    std::unique_ptr<QThread> sender(QThread::create([&socketName, &chunks, budgetMs, &taken,
                                                     &handOverMs] {
        QLocalSocket client;
        client.connectToServer(socketName);
        if (!client.waitForConnected(2000))
            return;
        for (qsizetype i = 0; i < chunks.size(); ++i) {
            QThread::msleep(static_cast<unsigned long>(chunks.at(i).pauseMs));
            const QByteArray &bytes = chunks.at(i).bytes;
            if (i + 1 == chunks.size()) {
                QElapsedTimer timer;
                timer.start();
                taken = freetunnel::handOverInstanceMessage(client, bytes, budgetMs)
                        == freetunnel::HandOver::Taken;
                handOverMs = timer.elapsed();
                return;
            }
            if (client.write(bytes) != bytes.size())
                return;
            client.flush();
            while (client.state() == QLocalSocket::ConnectedState && client.bytesToWrite() > 0) {
                if (!client.waitForBytesWritten(budgetMs))
                    return;
            }
        }
    }));
    sender->start();
    // Every wait on the sending side has a limit of its own, so this ends.
    const bool finished = QTest::qWaitFor([&sender] { return sender->isFinished(); }, 30000);
    sender->wait(); // a QThread destroyed while it runs takes the process down
    return finished && taken && handOverMs < budgetMs / 2;
}

// Send one message the way a second launch does (sendFromAnotherThread()).
//
// This used to send from this very thread, and that was not what a launch
// does. On Windows a QLocalServer's pipe has no buffer, so a write completes
// only once the listener has read it, and the listener reads from this thread's
// event loop. That loop did not turn until the sender's waitForBytesWritten()
// and waitForDisconnected() had both run out: seven seconds for every message.
// Everything that came due meanwhile then ran in one turn with the accept. That
// is the turn in which the listener's deadline could fire before the bytes it
// had read were counted, and a message was lost now and then
// (aMessageSurvivesAStallInTheTurnThatAcceptsIt).
//
// No fixed wait afterwards. Delivery is deliberately deferred by the listener
// (kInstanceMessageIdleMs, 250 ms) and then has to cross the event loop, so how
// long it takes depends on how loaded the machine is. Callers expecting
// something to arrive use QTRY_*; callers expecting silence wait out the delay
// explicitly with waitOutInstanceDelivery().
void TestAppStartup::sendInstanceMessage(const QString &socketName, const QByteArray &msg)
{
    QVERIFY2(sendFromAnotherThread(socketName, {{0, msg}}), "the listener never took the message");
}

// The entry point was the one file no test compiled, and both macOS defects found
// in it were about what ran before what — a Dock handler registered at a moment
// that looked early and was not, and window setup that needs a window to exist.
// This walks the real wiring and pins the sequence, so a reorder has to be
// deliberate rather than incidental.
//
// It is a smoke test, and worth being honest about its reach: it would not have
// caught either bug by itself, because neither was visible from the order alone.
// What it gives is the first coverage this file has ever had, and a tripwire on
// the property both bugs lived in.
void TestAppStartup::guiWiringBuildsTheAppInAKnownOrder()
{
#ifdef Q_OS_MACOS
    // The macOS branch of the wiring hands win->winId() to AppKit, and under the
    // offscreen platform plugin there is no NSView behind that handle — the
    // process dies dereferencing it (SIGSEGV at address 0x1 on the CI runner).
    // Exercising it needs a real window server, which no headless job has. The
    // ordering this pins is identical on every platform, so it is checked where it
    // can be, rather than contorting the app to be testable against a window that
    // does not exist.
    QSKIP("the macOS window setup needs a real window server, not offscreen");
#else
    // A name of our own. With the production one this drives the real
    // single-instance path against whatever is listening on the machine — a
    // FreeTunnel the developer is running, or a socket an earlier run of this very
    // test left behind — and then reports success by exiting, which is how it
    // started failing here.
    const QByteArray instanceName =
            QByteArrayLiteral("FreeTunnelStartupTest-") + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("FT_TEST_INSTANCE_NAME", instanceName);
    QLocalServer::removeServer(QString::fromLatin1(instanceName));
    auto dropName = qScopeGuard([&] {
        qunsetenv("FT_TEST_INSTANCE_NAME");
        QLocalServer::removeServer(QString::fromLatin1(instanceName));
    });

    freetunnel::GuiStartup startup;
    // The listener is parented to the application, so it outlives this test
    // unless it is deleted here. Left behind, it went on listening with whatever
    // it had accepted and the deadlines armed for it, and its handlers hold the
    // Backend and window this test destroys. Deleted before them: this guard is
    // declared after startup.
    const auto dropListener = qScopeGuard([&startup] {
        delete startup.server;
        startup.server = nullptr;
    });
    char arg0[] = "freetunnel";
    char *argv[] = {arg0, nullptr};
    int argc = 1;

    const std::optional<int> exitNow =
            freetunnel::wireGuiApplication(*qApp, argc, argv, &startup);

    QVERIFY2(!exitNow.has_value(),
             qPrintable(QStringLiteral("wiring asked to exit with %1; trace: %2")
                                .arg(exitNow.value_or(0))
                                .arg(startup.trace.join(QLatin1Char(' ')))));

    // The QML has to have produced a window: "-1, could not load Main.qml" is the
    // failure this file returns for a broken UI, and nothing else notices it.
    QVERIFY(startup.win != nullptr);
    QVERIFY(startup.backend != nullptr);
    QVERIFY(startup.engine != nullptr);
    QVERIFY(startup.urlFilter != nullptr);
    QVERIFY(startup.server != nullptr);

    const QStringList expected{
            QStringLiteral("branding"),      QStringLiteral("forward-check"),
            QStringLiteral("instance-server"), QStringLiteral("url-filter"),
            QStringLiteral("backend"),       QStringLiteral("lifecycle"),
            QStringLiteral("qml-loaded"),    QStringLiteral("language"),
            QStringLiteral("mac-window"),    QStringLiteral("deferred-control"),
            QStringLiteral("dock-reopen"),   QStringLiteral("instance-wired")};
    QCOMPARE(startup.trace, expected);

    // Two orderings the rest of the file depends on, stated so a future edit that
    // breaks them fails here rather than at runtime on one platform:
    // the window must exist before anything that configures it,
    QVERIFY(startup.trace.indexOf(QStringLiteral("qml-loaded"))
            < startup.trace.indexOf(QStringLiteral("mac-window")));
    // and the listener must not accept commands before the Backend can serve them.
    QVERIFY(startup.trace.indexOf(QStringLiteral("backend"))
            < startup.trace.indexOf(QStringLiteral("instance-wired")));

    // The listener is the one that checks a peer as it accepts it
    // (newInstanceServer()). A plain QLocalServer checks it only once the
    // connection is handed on, and a link sent while FreeTunnel was starting
    // was gone by then and refused; nothing but this tells the two apart once
    // the wiring is done.
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(instanceName));
    QVERIFY(probe.waitForConnected(2000));
    QTRY_VERIFY_WITH_TIMEOUT(!startup.server->findChildren<QLocalSocket *>().isEmpty(), 10000);
    QVERIFY(startup.server->findChildren<QLocalSocket *>().constFirst()
                    ->property("freetunnelPeerIsSameUser").isValid());
    probe.abort();

    // Shutdown must be reachable from what was wired: the quit path is what the
    // tray item, ⌘Q and the window's close button all end up calling.
    QSignalSpy shutdown(startup.backend.get(), &Backend::aboutToShutdown);
    startup.backend->quitApplication();
    QCOMPARE(shutdown.count(), 1);
    QVERIFY(startup.backend->applicationClosingDown());
#endif
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
    // Watched from the start so a failure can say WHICH of the two ways this can
    // go wrong happened: the link never reached handleControl (both spies stay
    // empty), or it reached it and was rejected (errorOccurred carries the reason).
    // A bare count comparison reports neither, which cost several CI rounds.
    QSignalSpy errors(&backend, &Backend::errorOccurred);
    QVERIFY(errors.isValid());
    // Counting only — the listener's own handler is what consumes the connection.
    // Without this a failure cannot say whether the server ever accepted anything,
    // which is the difference between "the peer never got the bytes out" and "the
    // listener took the connection and then dropped the message".
    QSignalSpy conns(&server, &QLocalServer::newConnection);
    QVERIFY(conns.isValid());
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(QStringLiteral("tok"), link));

    // Hand-rolled rather than QTRY_*, because a QTRY that times out fails on the
    // spot and the informative assertion below never runs — which is how the last
    // round produced a diagnostic nobody got to read.
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 10000 && imports.count() == 0 && errors.count() == 0)
        QTest::qWait(50);

    QVERIFY2(imports.count() == 1,
             qPrintable(QStringLiteral("no import confirmation: connections=%1 imports=%2 "
                                       "errors=%3 first=%4 linkBytes=%5")
                                .arg(conns.count())
                                .arg(imports.count())
                                .arg(errors.count())
                                .arg(errors.isEmpty() ? QStringLiteral("<none>")
                                                      : errors.at(0).at(0).toString())
                                .arg(link.size())));
    // The link the Backend was asked to import is the one that went over the
    // socket — not a truncated, re-encoded or substituted one.
    QCOMPARE(imports.at(0).at(1).toString(), link);

    // A second, different verb through the same listener: if the payload were
    // being replaced by a constant, both messages would produce the same effect.
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      QStringLiteral("tok"),
                                      QStringLiteral("freetunnel://connect")));
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10000);
    QCOMPARE(imports.count(), 1); // still one — connect is not an import
    QCOMPARE(errors.at(0).at(0).toString(), Backend::tr("Select a config first"));
}

// A plain second launch sends "focus": the user asking for the window, from
// another process. A window our close button minimised has to come back — a
// raise alone left it minimised.
void TestAppStartup::aSecondLaunchBringsTheWindowBack()
{
    Backend backend;
    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("focus"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    QWindow window;
    window.resize(200, 200);
    window.show();
    window.setWindowStates(Qt::WindowMinimized);
    freetunnel::wireInstanceServer(&server, backend, &window, QStringLiteral("tok"));
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(QStringLiteral("tok"), QStringLiteral("focus")));
    QTRY_VERIFY_WITH_TIMEOUT(!(window.windowStates() & Qt::WindowMinimized), 10000);
    QVERIFY(window.isVisible());
}

// freetunnel://toggle comes from a keyboard shortcut, a script or a Stream Deck,
// and acts silently, as the in-app hotkeys do. Bringing the window up for it took
// focus from whatever the user was typing into.
void TestAppStartup::aForwardedToggleLeavesTheWindowAlone()
{
    Backend backend;
    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("toggle"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    QWindow window;
    window.resize(200, 200);
    window.show();
    window.setWindowStates(Qt::WindowMinimized);
    freetunnel::wireInstanceServer(&server, backend, &window, QStringLiteral("tok"));
    // With no config to connect, the toggle answers with an error: the sign that
    // the command arrived and was acted on.
    QSignalSpy handled(&backend, &Backend::errorOccurred);
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(QStringLiteral("tok"),
                                                                QStringLiteral("freetunnel://toggle")));
    QTRY_VERIFY_WITH_TIMEOUT(handled.count() > 0, 10000);
    QCoreApplication::processEvents();
    QVERIFY2(window.windowStates() & Qt::WindowMinimized, "a toggle brought the window up");
}

// The same toggle forwarded as a link, from a second launch the URL handler
// started, while the VPN is up: that would turn it off on a web page's say-so,
// so it is a question, and the window comes up for it.
void TestAppStartup::aForwardedLinkThatWouldDisconnectAsksInTheWindow()
{
    const QString config = storeActiveConfig(QStringLiteral("forwarded-link-server"), true);
    QVERIFY(!config.isEmpty());
    const QString token = QStringLiteral("app-startup-helper-token");
    MockHelperServer helper(token);
    QVERIFY(helper.listen());
    qputenv("FT_TEST_HELPER_PORT", QByteArray::number(helper.port()));
    qputenv("FT_TEST_HELPER_TOKEN", token.toUtf8());
    auto restore = qScopeGuard([&] {
        qunsetenv("FT_TEST_HELPER_PORT");
        qunsetenv("FT_TEST_HELPER_TOKEN");
        freetunnel::CredentialStore::deletePassword(config);
        saveAppSettings(AppSettings{});
        saveStoredConfigs({});
        QFile::remove(config);
    });

    Backend backend;
    backend.connectVpn();
    QVERIFY(QTest::qWaitFor([&]() { return backend.connected(); }, 10000));

    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("linktoggle"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));
    QWindow window;
    window.resize(200, 200);
    window.show();
    window.setWindowStates(Qt::WindowMinimized);
    freetunnel::wireInstanceServer(&server, backend, &window, QStringLiteral("tok"));

    QSignalSpy asked(&backend, &Backend::deepLinkDisconnectConfirmationRequired);
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      QStringLiteral("tok"),
                                      freetunnel::linkControlString(
                                              QStringLiteral("freetunnel://toggle"))));
    QTRY_COMPARE_WITH_TIMEOUT(asked.count(), 1, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(!(window.windowStates() & Qt::WindowMinimized), 10000);
    QVERIFY2(backend.connected() && !backend.disconnecting(),
             "a forwarded link turned the VPN off without asking");
    backend.prepareQuit();
}

// A peer whose first bytes are slow to arrive must still be heard. The listener
// arms its idle timer when the connection opens, so a quiet first quarter-second
// used to fire the delivery path against an empty buffer, mark the message
// delivered, and drop everything that arrived afterwards — silently, because a
// message that fails to parse is simply ignored.
//
// The gap here is deliberate and only slightly over the 250 ms idle interval; on
// a loaded machine a real peer hits it by itself, which is how this was found —
// a tt:// link forwarded to a running instance on Windows did nothing at all, no
// error and no import prompt, while shorter commands on the same socket worked.
void TestAppStartup::wireInstanceServerSurvivesASlowFirstChunk()
{
    Backend backend;
    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("slowchunk"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    freetunnel::wireInstanceServer(&server, backend, nullptr, QStringLiteral("tok"));

    QSignalSpy spy(&backend, &Backend::errorOccurred);
    const QByteArray msg = freetunnel::formatInstanceMessage(
            QStringLiteral("tok"), QStringLiteral("freetunnel://connect"));
    // Connected, then quiet for longer than kInstanceMessageIdleMs (250 ms) and
    // less than the three-second deadline.
    QVERIFY(sendFromAnotherThread(name, {{400, msg}}));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
    QCOMPARE(spy.at(0).at(0).toString(), Backend::tr("Select a config first"));
}

// A listener wired as at startup, on a name of this test's own, with the
// Backend that runs what it is sent.
struct WiredListener {
    Backend backend;
    std::unique_ptr<QLocalServer> server{freetunnel::newInstanceServer(nullptr)};
    QString name;
};

static bool wireListener(WiredListener &w, const QString &name)
{
    w.name = name;
    QLocalServer::removeServer(name);
    w.server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!w.server->listen(name))
        return false;
    freetunnel::wireInstanceServer(w.server.get(), w.backend, nullptr, QStringLiteral("tok"));
    return true;
}

// A second launch's hand-over, on a thread of its own: connect to @p name and
// hand @p message over within @p budgetMs. connected is set once the connection
// is open, result once the hand-over is done.
struct LaunchThread {
    std::atomic<bool> connected{false};
    std::atomic<freetunnel::HandOver> result{freetunnel::HandOver::Refused};
    std::atomic<qint64> tookMs{-1};
    std::unique_ptr<QThread> thread;

    LaunchThread(const QString &name, const QByteArray &message,
                 int budgetMs = freetunnel::kInstanceHandOverMs)
    {
        thread.reset(QThread::create([this, name, message, budgetMs] {
            QLocalSocket client;
            client.connectToServer(name);
            if (!client.waitForConnected(2000))
                return;
            connected = true;
            QElapsedTimer timer;
            timer.start();
            result = freetunnel::handOverInstanceMessage(client, message, budgetMs);
            tookMs = timer.elapsed();
        }));
        thread->start();
    }
    ~LaunchThread() { thread->wait(); } // a QThread destroyed while it runs takes the process down
    bool finishedWhileTurningTheLoop(int ms)
    {
        return QTest::qWaitFor([this] { return thread->isFinished(); }, ms);
    }
};

static QByteArray connectMessage()
{
    return freetunnel::formatInstanceMessage(QStringLiteral("tok"),
                                             QStringLiteral("freetunnel://connect"));
}

// The listener's three-second deadline is for a peer that says nothing, and it
// fired for one that had said everything. On Windows the pipe reader takes the
// peer's bytes off the pipe as the connection is accepted, but they are counted
// only once an event posted for the next turn of the event loop runs. When
// something slow ran in the same turn after the accept, the deadline came due
// first, found nothing to read and dropped the whole message. The Windows CI
// runners lost a forwarded tt:// link that way now and then. Here the slow
// thing is made to run in exactly that turn.
void TestAppStartup::aMessageSurvivesAStallInTheTurnThatAcceptsIt()
{
    WiredListener w;
    QVERIFY(wireListener(w, instanceSocketName(QStringLiteral("stall"))));
    QSignalSpy spy(&w.backend, &Backend::errorOccurred);

    LaunchThread launch(w.name, connectMessage());
    // The connection is opened while this thread turns no event loop, so its
    // accept waits in the queue. The stall is queued behind it. Both run in the
    // next turn: the accept, which reads the message and arms the deadline, and
    // then the stall, which runs past the deadline.
    for (int i = 0; i < 5000 && !launch.connected; ++i)
        QThread::msleep(1);
    QVERIFY2(launch.connected, "the launch did not connect in time; the turn was not set up");
    QThread::msleep(50); // and the listener's side of it has been signalled
    QMetaObject::invokeMethod(qApp, [] { QThread::msleep(3500); }, Qt::QueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 15000);
    QCOMPARE(spy.at(0).at(0).toString(), Backend::tr("Select a config first"));
    QVERIFY(launch.finishedWhileTurningTheLoop(15000));
    QCOMPARE(launch.result.load(), freetunnel::HandOver::Taken);
}

// A second launch waits for the running instance to take what it sends. On
// Windows a write is taken only when the instance reads it, and the instance
// reads from its event loop. A launch used to give up after about three seconds,
// say the command had been handed over, and exit, and the bytes went with it. A
// link clicked while FreeTunnel was starting, or busy for a moment, did nothing.
// Here the instance is busy for longer than that.
void TestAppStartup::aLaunchWaitsForABusyInstanceToTakeItsMessage()
{
    WiredListener w;
    QVERIFY(wireListener(w, instanceSocketName(QStringLiteral("busy"))));
    QSignalSpy spy(&w.backend, &Backend::errorOccurred);

    LaunchThread launch(w.name, connectMessage());
    QThread::msleep(4500); // busy: no turn of the event loop at all

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
    QCOMPARE(spy.at(0).at(0).toString(), Backend::tr("Select a config first"));
    QVERIFY(launch.finishedWhileTurningTheLoop(15000));
    QCOMPARE(launch.result.load(), freetunnel::HandOver::Taken);
}

// And when the instance is busy for longer than a launch waits, the launch says
// the message was not taken. It used to report it handed over and exit, and
// the bytes it still held went with it. Unreachable keeps it from starting a
// second copy beside the instance, and says what happened.
void TestAppStartup::aLaunchSaysWhenItsMessageWasNotTaken()
{
    WiredListener w;
    QVERIFY(wireListener(w, instanceSocketName(QStringLiteral("nottaken"))));
    QSignalSpy spy(&w.backend, &Backend::errorOccurred);

    LaunchThread launch(w.name, connectMessage(), 1000);
    QThread::msleep(2500); // busy for longer than this launch's 1 s budget
    QVERIFY(launch.finishedWhileTurningTheLoop(15000));
#ifdef Q_OS_WIN
    // The bytes never left the launch, so the instance has nothing to run.
    QCOMPARE(launch.result.load(), freetunnel::HandOver::NotTaken);
    waitOutInstanceDelivery();
    QCOMPARE(spy.count(), 0);
#else
    // The kernel buffers the write, so it is taken at once and arrives.
    QCOMPARE(launch.result.load(), freetunnel::HandOver::Taken);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
#endif
}

// The budget is the whole wait, on every platform: a message the instance
// never reads is given up on when it runs out, not before and not long after.
// Too big for any socket buffer, so it waits on Unix too.
void TestAppStartup::aHandOverGivesUpWhenTheBudgetRunsOut()
{
    QLocalServer server; // never accepts: this thread turns no event loop
    const QString name = instanceSocketName(QStringLiteral("budget"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    {
        LaunchThread launch(name, QByteArray(8 * 1024 * 1024, 'x'), 600);
        launch.thread->wait();
        QCOMPARE(launch.result.load(), freetunnel::HandOver::NotTaken);
        QVERIFY2(launch.tookMs >= 550, qPrintable(QString::number(launch.tookMs)));
        QVERIFY2(launch.tookMs < 5000, qPrintable(QString::number(launch.tookMs)));
    }
}

// A launch whose message is refused unread asks again, once. FreeTunnel closes
// its listener as it quits, connections it has not accepted yet included, so a
// launch made just then used to report an instance it could not reach and start
// nothing. The name is gone when it asks again, and it starts instead. On Unix
// the kernel takes the write before the listener closes, so the launch counts
// it as handed over: an instance that quits before reading drops it.
void TestAppStartup::aLaunchToAnInstanceThatIsQuittingStartsInstead()
{
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    const auto dropToken = qScopeGuard([] { freetunnel::removeInstanceAuthToken(); });
    const QString name = instanceSocketName(QStringLiteral("quitting"));
    QLocalServer::removeServer(name);

    // The quitting instance: it listens, accepts nothing, and closes its
    // listener half a second after it started.
    std::atomic<bool> listening{false};
    std::unique_ptr<QThread> instance(QThread::create([&name, &listening] {
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server.listen(name))
            return;
        listening = true;
        QThread::msleep(500);
        server.close();
    }));
    instance->start();
    const auto joinInstance = qScopeGuard([&instance] { instance->wait(); });
    for (int i = 0; i < 5000 && !listening; ++i)
        QThread::msleep(1);
    QVERIFY(listening);

    const freetunnel::ForwardResult result =
            freetunnel::forwardToRunningInstance({name}, QStringLiteral("freetunnel://connect"));
#ifdef Q_OS_WIN
    QCOMPARE(result, freetunnel::ForwardResult::NoInstance);
#else
    QCOMPARE(result, freetunnel::ForwardResult::Forwarded);
#endif
}

// A control message is not framed by length, so the listener has to decide when it
// has all of it: a gap between chunks, the peer closing, or the size cap. Nothing
// covered the gap path, and it is the one a real sender hits — a deep link is the
// longest thing this channel carries, and on a loaded machine its bytes do not
// necessarily arrive in one piece.
//
// The pause here is under the 250 ms idle interval, so the two chunks are one
// message. A pause longer than that is a different case: the listener would then
// deliver the first half on its own, which parses as a token plus a truncated
// command and is reported as an error rather than silently obeyed.
void TestAppStartup::aMessageSplitAcrossChunksStillArrives()
{
    Backend backend;
    QLocalServer server;
    const QString name = instanceSocketName(QStringLiteral("split"));
    QLocalServer::removeServer(name);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server.listen(name));

    freetunnel::wireInstanceServer(&server, backend, nullptr, QStringLiteral("tok"));

    const QByteArray msg = freetunnel::formatInstanceMessage(
            QStringLiteral("tok"), QStringLiteral("freetunnel://connect"));
    // Split after the token line, so each half is meaningless alone.
    const int cut = msg.indexOf('\n') + 3;
    QVERIFY(cut > 0 && cut < msg.size());

    QSignalSpy spy(&backend, &Backend::errorOccurred);
    // A real gap between the halves, comfortably under the idle interval.
    QVERIFY(sendFromAnotherThread(name, {{0, msg.left(cut)}, {80, msg.mid(cut)}}));

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
    QCOMPARE(spy.at(0).at(0).toString(), Backend::tr("Select a config first"));
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
    // Waited for, like every other delivery in this file. sendInstanceMessage()
    // returns 50 ms after the disconnect, and a plain comparison here only held
    // while the listener happened to finish inside that: it fails as soon as the
    // delivery runs late — a loaded runner, or a Windows pipe whose disconnect is
    // seen after the idle timer rather than before it.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
}

// The listener is up from early in startup and wired only at its end. A second
// launch in between, a link clicked while FreeTunnel starts, connects, sends and
// is gone before the wiring, as a real one is: within microseconds. Its peer was
// checked only once it was handed on, by which time the socket had let go of its
// descriptor, so it was refused and the link was lost. And taking one connection
// per signal after that kept the queue a connection behind for the whole
// session: each later launch was handed the one before it, and its own command
// waited for the next.
void TestAppStartup::aLinkSentWhileStartingIsHandled()
{
    Backend backend;
    std::unique_ptr<QLocalServer> server(freetunnel::newInstanceServer(nullptr));
    const QString name = instanceSocketName(QStringLiteral("early"));
    QLocalServer::removeServer(name);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server->listen(name));

    // Sent in full, and gone, while nothing is wired yet.
    QSignalSpy accepted(server.get(), &QLocalServer::newConnection);
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      QStringLiteral("tok"),
                                      QStringLiteral("freetunnel://connect")));
    QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 10000);
    QTest::qWait(200); // and the listener has seen it go

    QSignalSpy spy(&backend, &Backend::errorOccurred);
    freetunnel::wireInstanceServer(server.get(), backend, nullptr, QStringLiteral("tok"));
    // Its command runs once there is someone to run it, and without waiting out
    // the listener's three-second deadline for a peer that sends nothing more.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1500);

    // And the next launch is not handed that connection in place of its own.
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      QStringLiteral("tok"),
                                      QStringLiteral("freetunnel://connect")));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 10000);
}

// A second launch that finds FreeTunnel running but cannot hand it the command —
// the token unreadable behind a locked keyring, or never stored — used to start
// in full anyway: two copies driving one VPN, the second taking the socket name
// over so that the first could no longer be reached by anything. It exits now,
// before building anything, and the running one keeps its name.
void TestAppStartup::aSecondLaunchNeverStartsBesideALiveInstance()
{
    const QByteArray instanceName = QByteArrayLiteral("FreeTunnelBesideTest-")
            + QByteArray::number(QCoreApplication::applicationPid());
    const QString name = QString::fromLatin1(instanceName);
    qputenv("FT_TEST_INSTANCE_NAME", instanceName);
    QLocalServer::removeServer(name);
    auto dropName = qScopeGuard([&] {
        qunsetenv("FT_TEST_INSTANCE_NAME");
        QLocalServer::removeServer(name);
    });

#if defined(Q_OS_LINUX)
    // The token is looked for in the application's config directory, and the
    // wiring renames the application to FreeTunnel: under test mode a directory
    // every test process that wires the app shares, where a token another one
    // left would answer for this test. This one gets a directory of its own.
    QTemporaryDir config;
    QVERIFY(config.isValid());
    const QByteArray configHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QStandardPaths::setTestModeEnabled(false);
    auto restorePaths = qScopeGuard([configHome] {
        QStandardPaths::setTestModeEnabled(true);
        if (configHome.isEmpty())
            qunsetenv("XDG_CONFIG_HOME");
        else
            qputenv("XDG_CONFIG_HOME", configHome);
    });
#endif
    QLocalServer running;
    running.setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(running.listen(name));
    freetunnel::removeInstanceAuthToken(); // its token cannot be read

    freetunnel::GuiStartup startup;
    char arg0[] = "freetunnel";
    char *argv[] = {arg0, nullptr};
    int argc = 1;
    const std::optional<int> exitNow =
            freetunnel::wireGuiApplication(*qApp, argc, argv, &startup);

    QVERIFY2(exitNow.has_value(),
             qPrintable(startup.trace.join(QLatin1Char(' '))));
    QCOMPARE(*exitNow, 1);
    QCOMPARE(startup.trace,
             QStringList({QStringLiteral("branding"), QStringLiteral("instance-unreachable")}));
    QVERIFY(startup.server == nullptr);
    QVERIFY(startup.backend == nullptr);

    QLocalSocket probe;
    probe.connectToServer(name);
    QVERIFY2(probe.waitForConnected(2000), "the running instance lost its socket name");
}

// Quitting removes the instance's token, while the listener stayed up until the
// application was destroyed. A launch in between reached it with no token to
// show, took it for an instance it could not reach and exited with nothing on
// screen. The listener closes first now, so that launch finds no instance and
// starts.
//
// Where a busy listener is refused like a stale socket file (macOS, or Linux
// with the test hook), the running instance claims its name, and gives the claim
// up with the listener: the launch that starts then is the one to hold it.
void TestAppStartup::aLaunchWhileQuittingFindsNoInstance()
{
#ifdef Q_OS_MACOS
    QSKIP("the macOS window setup needs a real window server, not offscreen");
#else
    const QByteArray instanceName = QByteArrayLiteral("FreeTunnelQuitTest-")
            + QByteArray::number(QCoreApplication::applicationPid());
    qputenv("FT_TEST_INSTANCE_NAME", instanceName);
    qputenv("FT_TEST_REFUSAL_CAN_BE_BUSY", "1");
    QLocalServer::removeServer(QString::fromLatin1(instanceName));
    auto dropName = qScopeGuard([&] {
        qunsetenv("FT_TEST_INSTANCE_NAME");
        qunsetenv("FT_TEST_REFUSAL_CAN_BE_BUSY");
        QLocalServer::removeServer(QString::fromLatin1(instanceName));
    });
    QObject successor;

    freetunnel::GuiStartup startup;
    const auto dropListener = qScopeGuard([&startup] { // see guiWiringBuildsTheAppInAKnownOrder
        delete startup.server;
        startup.server = nullptr;
    });
    char arg0[] = "freetunnel";
    char *argv[] = {arg0, nullptr};
    int argc = 1;
    QVERIFY(!freetunnel::wireGuiApplication(*qApp, argc, argv, &startup).has_value());
    QVERIFY(startup.server->isListening());
#ifndef Q_OS_WIN
    QVERIFY2(!freetunnel::claimInstanceName(&successor, QString::fromLatin1(instanceName)),
             "the running instance does not claim its name");
#endif

    // What leaving the event loop emits; a test has no loop of its own to leave.
    QVERIFY(QMetaObject::invokeMethod(qApp, "aboutToQuit"));

    QCOMPARE(freetunnel::forwardToRunningInstance(freetunnel::instanceServerNames(), QString()),
             freetunnel::ForwardResult::NoInstance);
#ifndef Q_OS_WIN
    QVERIFY2(freetunnel::claimInstanceName(&successor, QString::fromLatin1(instanceName)),
             "a quitting instance keeps its claim on the name");
#endif
#endif
}

// Replacing a running AppImage closes this listener for the new build and, if
// that build does not start, listens on it again (Backend::applyLinuxUpdate).
// Backend is handed it here. It used to look for it among the application's
// children, which finds nothing, or the wrong listener, without a word.
void TestAppStartup::wireInstanceServerHandsTheListenerToBackend()
{
    Backend backend;
    QLocalServer server;
    QCOMPARE(backend.instanceServer(), nullptr);
    freetunnel::wireInstanceServer(&server, backend, nullptr, QStringLiteral("tok"));
    QCOMPARE(backend.instanceServer(), &server);
}

// An AppImage update has the old FreeTunnel start the new one and quit. As it
// quits it compares the stored token with its own and then deletes it, in two
// calls to the store, and the new one's token, written in between, went with
// it. Every launch and link after that found FreeTunnel running with no token
// to show it and gave up, until FreeTunnel was restarted. A few seconds after
// the listener is wired the instance looks again, and puts its token back.
void TestAppStartup::aTokenDeletedAsTheInstanceStartsIsPutBack()
{
#ifdef Q_OS_MACOS
    QSKIP("the macOS window setup needs a real window server, not offscreen");
#else
    const QString name = instanceSocketName(QStringLiteral("token-start"));
    qputenv("FT_TEST_INSTANCE_NAME", name.toUtf8());
    QLocalServer::removeServer(name);
    auto dropName = qScopeGuard([&] {
        qunsetenv("FT_TEST_INSTANCE_NAME");
        QLocalServer::removeServer(name);
    });
    freetunnel::GuiStartup startup;
    const auto dropListener = qScopeGuard([&startup] { // see guiWiringBuildsTheAppInAKnownOrder
        delete startup.server;
        startup.server = nullptr;
    });
    char arg0[] = "freetunnel";
    char *argv[] = {arg0, nullptr};
    QVERIFY(!freetunnel::wireGuiApplication(*qApp, 1, argv, &startup).has_value());
    QString token;
    QVERIFY(freetunnel::readInstanceAuthToken(&token));

    freetunnel::removeInstanceAuthToken(); // the old FreeTunnel's, just after it was written
    QString stored;
    QVERIFY(!freetunnel::readInstanceAuthToken(&stored));
    // With no launch to set it off.
    QTRY_VERIFY_WITH_TIMEOUT(freetunnel::readInstanceAuthToken(&stored) && stored == token,
                             15000);
#endif
}

// And whenever a launch could not show the token, however late the old
// FreeTunnel deleted it: that launch has given up already, but the next one is
// handed its command. Only the launch can set it off here, the later look being
// put off past the end of the test.
void TestAppStartup::aLaunchWithNoTokenToShowHasItPutBack()
{
    qputenv("FT_TEST_TOKEN_RECHECK_MS", "600000");
    auto unsetHook = qScopeGuard([] { qunsetenv("FT_TEST_TOKEN_RECHECK_MS"); });
    Backend backend;
    std::unique_ptr<QLocalServer> server(freetunnel::newInstanceServer(nullptr));
    const QString name = instanceSocketName(QStringLiteral("token-launch"));
    QLocalServer::removeServer(name);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    QVERIFY(server->listen(name));
    QString token;
    QVERIFY(freetunnel::writeInstanceAuthToken(&token));
    freetunnel::wireInstanceServer(server.get(), backend, nullptr, token);

    freetunnel::removeInstanceAuthToken();
    QCOMPARE(freetunnel::forwardToRunningInstance({name}, QString()),
             freetunnel::ForwardResult::Unreachable);
    QString stored;
    QTRY_VERIFY_WITH_TIMEOUT(freetunnel::readInstanceAuthToken(&stored) && stored == token,
                             10000);

    QSignalSpy spy(&backend, &Backend::errorOccurred);
    sendInstanceMessage(name, freetunnel::formatInstanceMessage(
                                      stored, QStringLiteral("freetunnel://connect")));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 10000);
}

// Replacing a running AppImage hands the name to the new build once that has
// started (Backend::applyLinuxUpdate), and the new build writes a token of its
// own. Quitting then deleted the stored token if it compared as ours, and the
// new build's, written between the comparison and the deletion, went with it.
// Once the name is handed over, quitting leaves the token alone, and a listener
// closed for the new build does not put its token back over the new build's.
void TestAppStartup::aQuitAfterHandingTheNameOverLeavesTheToken()
{
#ifdef Q_OS_MACOS
    QSKIP("the macOS window setup needs a real window server, not offscreen");
#else
    const QString name = instanceSocketName(QStringLiteral("handover"));
    qputenv("FT_TEST_INSTANCE_NAME", name.toUtf8());
    QLocalServer::removeServer(name);
    auto dropName = qScopeGuard([&] {
        qunsetenv("FT_TEST_INSTANCE_NAME");
        QLocalServer::removeServer(name);
    });
    freetunnel::GuiStartup startup;
    const auto dropListener = qScopeGuard([&startup] { // see guiWiringBuildsTheAppInAKnownOrder
        delete startup.server;
        startup.server = nullptr;
    });
    char arg0[] = "freetunnel";
    char *argv[] = {arg0, nullptr};
    QVERIFY(!freetunnel::wireGuiApplication(*qApp, 1, argv, &startup).has_value());
    QString token;
    QVERIFY(freetunnel::readInstanceAuthToken(&token));

    // What applyLinuxUpdate does once the new build has started.
    startup.server->close();
    emit startup.backend->instanceNameHandedOver();
    QVERIFY(QMetaObject::invokeMethod(qApp, "aboutToQuit"));
    QString stored;
    QVERIFY2(freetunnel::readInstanceAuthToken(&stored), "quitting deleted the token");
    QCOMPARE(stored, token);

    QString newBuilds;
    QVERIFY(freetunnel::writeInstanceAuthToken(&newBuilds));
    freetunnel::keepInstanceTokenStored(startup.server, token);
    QVERIFY(freetunnel::readInstanceAuthToken(&stored));
    QCOMPARE(stored, newBuilds);
#endif
}

QTEST_MAIN(TestAppStartup)
#include "test_app_startup.moc"
