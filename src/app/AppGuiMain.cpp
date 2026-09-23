// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QRectF>
#include <QTranslator>
#include <QUrl>
#include <QWindow>
#ifdef Q_OS_MACOS
#include <QAction>
#endif

#include "app/Backend.h"
#include "app/MacWindow.h"
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


static QLocalServer *startSingleInstanceServer(QGuiApplication &app, QString *instanceToken)
{
    const QString kInstanceKey = freetunnel::instanceServerName();
    QLocalServer::removeServer(kInstanceKey);
    if (!writeInstanceAuthToken(instanceToken))
        instanceToken->clear();
    auto *server = new QLocalServer(&app);
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
                                const Backend &backend, QTranslator *&translator)
{
    applyLanguage(app, engine, translator, backend.language());
    QObject::connect(&backend, &Backend::languageChanged, &app,
                     [&app, &engine, &translator](const QString &lang) {
                         applyLanguage(app, engine, translator, lang);
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
                                        const QString &instanceToken)
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
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, [instanceToken]() {
        // Ours, and only ours: the self-update path leaves a successor running.
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
// readable — it is four calls that only exist on one platform and only make sense
// once the window does.
static void setupMacWindow(QWindow *win, bool *appQuitting)
{
#ifdef Q_OS_MACOS
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
    // The red close button hides to tray; everything else (⌘Q, Quit menu) quits.
    installMacWindowCloseToTray(win->winId(), [win]() { win->hide(); });
    // Bring the hidden window back only on a real Dock-icon click — not on every
    // app activation (status-bar clicks, Cmd-Tab), which used to re-open it.
    installMacDockReopenHandler([win, appQuitting]() {
        if (*appQuitting)
            return;
        win->show();
        win->raise();
        win->requestActivate();
    });
#else
    Q_UNUSED(win);
    Q_UNUSED(appQuitting);
#endif
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
    if (forwardToRunningInstance(freetunnel::instanceServerName(), controlArg)) {
        step("forwarded-to-running-instance");
        return 0;
    }
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

    out->quitFilter.reset(wireBackendLifecycle(app, backend, out->appQuitting, instanceToken));
#ifdef Q_OS_MACOS
    setupMacApplicationQuit(backend);
#endif
    step("lifecycle");

    out->engine = std::make_unique<QQmlApplicationEngine>();
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

    out->urlFilter->ready(&backend, out->win);
    if (!controlArg.isEmpty())
        backend.handleControl(controlArg);
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
