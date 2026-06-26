// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
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
    const QString kInstanceKey = QStringLiteral("FreeTunnelInstance");
    QLocalServer::removeServer(kInstanceKey);
    if (!writeInstanceAuthToken(instanceToken))
        instanceToken->clear();
    auto *server = new QLocalServer(&app);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    server->listen(kInstanceKey);
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

static void wireBackendLifecycle(QGuiApplication &app, Backend &backend, bool &appQuitting)
{
    auto *quitFilter = new QuitFilter(&app);
    quitFilter->backend = &backend;
    app.installEventFilter(quitFilter);

    QObject::connect(&backend, &Backend::aboutToShutdown, &app, [&appQuitting]() {
        appQuitting = true;
    });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::prepareQuit);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, []() {
        removeInstanceAuthToken();
    });
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

int runGuiApplication(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    applyAppBranding(app);

    const QString controlArg = controlArgFrom(argc, argv);
    if (forwardToRunningInstance(QStringLiteral("FreeTunnelInstance"), controlArg))
        return 0;

    QString instanceToken;
    QLocalServer *server = startSingleInstanceServer(app, &instanceToken);

    UrlOpenFilter urlFilter;
    app.installEventFilter(&urlFilter);

    Backend backend;
#ifdef Q_OS_MACOS
    setupMacDockIcon(app, backend);
#endif

    bool appQuitting = false;
    wireBackendLifecycle(app, backend, appQuitting);
#ifdef Q_OS_MACOS
    setupMacApplicationQuit(backend);
#endif

    QQmlApplicationEngine engine;
    QWindow *win = loadMainWindow(engine, backend);
    if (!win)
        return -1;

    QTranslator *translator = nullptr;
    wireLanguageChanges(app, engine, backend, translator);

#ifdef Q_OS_MACOS
    applyMacUnifiedTitlebar(win->winId());
    // The red close button hides to tray; everything else (⌘Q, Quit menu) quits.
    installMacWindowCloseToTray(win->winId(), [win]() { win->hide(); });
#endif
    urlFilter.ready(&backend, win);
    if (!controlArg.isEmpty())
        backend.handleControl(controlArg);

    setupDockReopen(app, win, appQuitting);
    wireInstanceServer(server, backend, win, instanceToken);

    return app.exec();
}

} // namespace freetunnel
