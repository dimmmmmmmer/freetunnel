// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRectF>
#include <QTranslator>
#include <QUrl>
#include <QWindow>
#ifdef Q_OS_MACOS
#include <QAction>
#endif

#include <memory>

#include "app/Backend.h"
#include "app/MacWindow.h"
#if defined(Q_OS_WIN) && defined(FT_HAVE_QWINDOWKIT)
#include "app/WindowsChrome.h"

#include <QQuickWindow>
#endif
#include "core/InstanceControl.h"

namespace freetunnel {

static void applyAppBranding(QGuiApplication &app)
{
    app.setApplicationName(QStringLiteral("FreeTunnel"));
    app.setOrganizationName(QStringLiteral("FreeTunnel"));
    app.setApplicationDisplayName(QStringLiteral("FreeTunnel"));
#ifndef Q_OS_MACOS
    // macOS uses logo.icns from the bundle; setWindowIcon() there overrides the Dock
    // icon when the window opens (see setupMacDockIcon).
    QIcon winLinuxIcon(QStringLiteral(":/assets/logo.ico"));
    winLinuxIcon.addFile(QStringLiteral(":/assets/logo.png"));
    app.setWindowIcon(winLinuxIcon);
#endif
    QGuiApplication::setQuitOnLastWindowClosed(false);
}


// Hand the command to an instance already running, if there is one. Returns the
// exit code when this launch is done, nothing when it is to be the instance.
static std::optional<int> handToRunningInstance(const QString &controlArg, QStringList &trace)
{
    switch (forwardToRunningInstance(instanceServerNames(), controlArg)) {
    case ForwardResult::Forwarded:
        trace << QStringLiteral("forwarded-to-running-instance");
        return 0;
    case ForwardResult::Unreachable:
        // Ours is running and could not be told. Never start beside it: two
        // copies would drive one VPN, and this one would take the socket name
        // over and leave that one reachable by nothing.
        qWarning("FreeTunnel is already running but did not take the command; "
                 "not starting a second copy");
        trace << QStringLiteral("instance-unreachable");
        return 1;
    case ForwardResult::NoInstance:
        break;
    }
    return std::nullopt;
}

static QLocalServer *startSingleInstanceServer(QGuiApplication &app, QString *instanceToken)
{
    const QString kInstanceKey = freetunnel::instanceServerName();
    // Only what a crashed instance left behind (see removeStaleInstanceServer()).
    removeStaleInstanceServer(kInstanceKey);
    if (!writeInstanceAuthToken(instanceToken))
        instanceToken->clear();
    QLocalServer *server = newInstanceServer(&app);
    // After the stale name is cleared, which a claim of our own would stop.
    claimInstanceName(server, kInstanceKey);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(kInstanceKey)) {
        // Not fatal (the app works without single-instance forwarding), but a
        // persistent failure here usually means another user squats the name.
        qWarning("Single-instance server failed to listen on '%s': %s",
                 qPrintable(kInstanceKey), qPrintable(server->errorString()));
    }
    return server;
}

static QWindow *loadMainWindow(QQmlApplicationEngine &engine, Backend &backend)
{
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return nullptr;
    return qobject_cast<QWindow *>(engine.rootObjects().first());
}

static void wireLanguageChanges(QGuiApplication &app, QQmlApplicationEngine &engine,
                                Backend &backend, QTranslator *&translator)
{
    // Backend is told after each change too: it words some things in C++ and
    // keeps them, and the first of those are worded while the window loads,
    // before this installs any translator at all.
    applyLanguage(app, engine, translator, backend.language());
    backend.retranslate();
    QObject::connect(&backend, &Backend::languageChanged, &app,
                     [&app, &engine, &translator, &backend](const QString &lang) {
                         applyLanguage(app, engine, translator, lang);
                         backend.retranslate();
                     });
}

// Returns the quit filter, which the caller owns. Not parented to the application:
// it holds a Backend* and reads a bool by address, and an event filter installed on
// the application outlives every scope but its own owner's. In the shipped app that
// is invisible because the process exits moments later; anything that builds the
// application and returns — a test — leaves a filter installed over freed memory,
// ready to swallow the next Quit event and dereference what is gone. Same shape as
// setupDockReopen(), same fix.
static QuitFilter *wireBackendLifecycle(QGuiApplication &app, Backend &backend, bool &appQuitting,
                                        QLocalServer *server, const QString &instanceToken)
{
    auto *quitFilter = new QuitFilter();
    quitFilter->backend = &backend;
    app.installEventFilter(quitFilter);

    // Bound to the filter's lifetime for the same reason: this lambda captures
    // appQuitting by reference.
    QObject::connect(&backend, &Backend::aboutToShutdown, quitFilter, [&appQuitting]() {
        appQuitting = true;
    });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::prepareQuit);
    auto handedOver = std::make_shared<bool>(false);
    QObject::connect(&backend, &Backend::instanceNameHandedOver, &app,
                     [handedOver]() { *handedOver = true; });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app,
                     [listener = QPointer<QLocalServer>(server), instanceToken, handedOver]() {
        // Stop listening before the token goes. Left listening until the
        // application is destroyed, it would be found by a launch in between with
        // no token left to show, which would take this for an instance it cannot
        // reach and exit with nothing on screen. With the name gone it starts.
        if (listener) {
            listener->close();
            releaseInstanceName(listener);
        }
        // Ours, and only ours: the self-update path leaves a successor running.
        // Once the name is handed to it, not even a token that compares as ours:
        // the successor writes its own over it, and one written between the
        // comparison and the deletion would be deleted.
        if (!*handedOver)
            removeInstanceAuthToken(instanceToken);
    });
    return quitFilter;
}

#ifdef Q_OS_MACOS
static void setupMacApplicationQuit(Backend &backend)
{
    // Replace the platform Quit item (Завершить / ⌘Q) so it calls our shutdown path
    // instead of QCoreApplication::quit(), which our onClosing handler would cancel.
    auto *quitAction = new QAction(QCoreApplication::translate("App", "Quit"), &backend);
    quitAction->setMenuRole(QAction::QuitRole);
    QObject::connect(quitAction, &QAction::triggered, &backend, &Backend::quitApplication);
}
#endif

// The native macOS window setup, lifted out of the wiring so that function stays
// readable — it is a handful of calls that only exist on one platform and only make
// sense once the window does.
static void setupMacWindow(QWindow *win, bool *appQuitting)
{
#ifdef Q_OS_MACOS
    installMacStatusItemCrashGuard();
    applyMacUnifiedTitlebar(win->winId());
    // Tell the QML where the traffic lights really are, and keep telling it: the
    // buttons are laid out once the window is on screen, and full screen hides
    // them. See macWindowControlsRect() for why this is asked, not assumed.
    const auto publishControls = [win]() {
        const MacRect r = macWindowControlsRect(win->winId());
        win->setProperty("macControlsRect", QRectF(r.x, r.y, r.width, r.height));
    };
    publishControls();
    QObject::connect(win, &QWindow::visibleChanged, win, publishControls);
    QObject::connect(win, &QWindow::widthChanged, win, publishControls);
    QObject::connect(win, &QWindow::windowStateChanged, win, publishControls);
    // And whether the app is hidden (⌘H, Hide Others). AppKit orders the window out
    // for that without Qt hearing of it, so the window still counts as visible and
    // the QML would take itself to be in view. See Main.qml's inView.
    const QPointer<QWindow> guard(win);
    installMacApplicationHiddenHandler([guard](bool hidden) {
        if (guard)
            guard->setProperty("macAppHidden", hidden);
    });
    // The red close button hides to tray; everything else (⌘Q, Quit menu) quits.
    installMacWindowCloseToTray(win->winId(), [win]() { freetunnel::hideWindowToTray(win); });
    // Bring the hidden window back only on a real Dock-icon click — not on every
    // app activation (status-bar clicks, Cmd-Tab), which used to re-open it. And in
    // whatever state it was in: show() is showNormal(), and every Dock click took
    // a zoomed window out of zoom and a full-screen one out of full screen.
    installMacDockReopenHandler([win, appQuitting]() {
        if (*appQuitting)
            return;
        freetunnel::bringWindowForward(win);
    });
#else
    Q_UNUSED(win);
    Q_UNUSED(appQuitting);
#endif
}

// The engine the main window will be loaded into, with the desktop it follows
// already in its context.
static void createDesktopAndEngine(GuiStartup *out)
{
    out->desktop = std::make_unique<freetunnel::DesktopChrome>();
    // Before the QML is loaded, so the first frame already has the desktop's
    // buttons and its light or dark. Not under offscreen, which has no desktop to
    // follow — and is what the tests run on, which must not take on the look of
    // whatever desktop they happen to run under.
    if (QGuiApplication::platformName() != QLatin1String("offscreen"))
        out->desktop->followDesktop();
    out->engine = std::make_unique<QQmlApplicationEngine>();
    out->engine->rootContext()->setContextProperty(QStringLiteral("desktop"), out->desktop.get());
}

// What came in before there was a window to act on it: the links the URL filter
// held, then the command this launch was started with.
static void deliverDeferredControl(GuiStartup *out, Backend &backend, const QString &controlArg)
{
    out->urlFilter->ready(&backend, out->win);
    if (!controlArg.isEmpty())
        backend.handleControl(controlArg);
}

std::optional<int> wireGuiApplication(QGuiApplication &app, int argc, char *argv[],
                                      GuiStartup *out)
{
    // Each step is named as it happens. The order is the thing worth pinning: this
    // file had no test at all, and both macOS defects found in it were about what
    // ran before what, not about what any one line did.
    const auto step = [out](const char *name) { out->trace << QLatin1String(name); };

    applyAppBranding(app);
    step("branding");

    const QString controlArg = controlArgFrom(argc, argv);
    // Reads the credential store, which spins a nested event loop on the GUI
    // thread — so by the time anything below runs, the event loop has already
    // turned. Code after this point must not assume otherwise; assuming it is
    // exactly how the Dock-reopen handler came to be registered too late.
    if (const std::optional<int> exitNow = handToRunningInstance(controlArg, out->trace))
        return exitNow;
    step("forward-check");

    QString instanceToken;
    out->server = startSingleInstanceServer(app, &instanceToken);
    step("instance-server");

    out->urlFilter = std::make_unique<UrlOpenFilter>();
    app.installEventFilter(out->urlFilter.get());
    step("url-filter");

    out->backend = std::make_unique<Backend>();
    Backend &backend = *out->backend;
#ifdef Q_OS_MACOS
    setupMacDockIcon(app, backend);
#endif
    step("backend");

    out->quitFilter.reset(
            wireBackendLifecycle(app, backend, out->appQuitting, out->server, instanceToken));
#ifdef Q_OS_MACOS
    setupMacApplicationQuit(backend);
#endif
    step("lifecycle");

    createDesktopAndEngine(out);
#if defined(Q_OS_WIN) && defined(FT_HAVE_QWINDOWKIT)
    // Only on the real Windows platform: under offscreen the window's id is a
    // counter, not an HWND, and the agent would hand it to Win32 as one.
    const bool windowsAgent = QGuiApplication::platformName() == QLatin1String("windows");
    if (windowsAgent)
        out->engine->setInitialProperties({{QStringLiteral("windowsAgent"), true}});
#endif
    out->win = loadMainWindow(*out->engine, backend);
    if (!out->win) {
        step("qml-failed");
        return -1;
    }
    step("qml-loaded");

    wireLanguageChanges(app, *out->engine, backend, out->translator);
    step("language");

    setupMacWindow(out->win, &out->appQuitting);
    step("mac-window");

#if defined(Q_OS_WIN) && defined(FT_HAVE_QWINDOWKIT)
    if (windowsAgent) {
        setupWindowsChrome(qobject_cast<QQuickWindow *>(out->win));
        step("windows-chrome");
    }
#endif

    deliverDeferredControl(out, backend, controlArg);
    step("deferred-control");

    out->dockReopen.reset(setupDockReopen(app, out->win, out->appQuitting));
    step("dock-reopen");

    wireInstanceServer(out->server, backend, out->win, instanceToken);
    step("instance-wired");

    return std::nullopt;
}

int runGuiApplication(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    GuiStartup startup;
    if (const std::optional<int> exitNow = wireGuiApplication(app, argc, argv, &startup))
        return *exitNow;
    return app.exec();
}

} // namespace freetunnel
