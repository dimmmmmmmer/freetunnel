// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTranslator>
#include <QUrl>
#include <QWindow>

#include "app/Backend.h"
#include "app/MacWindow.h"
#include "core/InstanceControl.h"

namespace freetunnel {

int runGuiApplication(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("FreeTunnel"));
    app.setOrganizationName(QStringLiteral("FreeTunnel"));
    app.setApplicationDisplayName(QStringLiteral("FreeTunnel"));
#ifndef Q_OS_MACOS
    QIcon winLinuxIcon(QStringLiteral(":/assets/logo.ico"));
    winLinuxIcon.addFile(QStringLiteral(":/assets/logo.png"));
    app.setWindowIcon(winLinuxIcon);
#endif
    QGuiApplication::setQuitOnLastWindowClosed(false);

    const QString controlArg = controlArgFrom(argc, argv);
    const QString kInstanceKey = QStringLiteral("FreeTunnelInstance");

    if (forwardToRunningInstance(kInstanceKey, controlArg))
        return 0;

    QLocalServer::removeServer(kInstanceKey);
    QString instanceToken;
    if (!writeInstanceAuthToken(&instanceToken))
        instanceToken.clear();
    auto *server = new QLocalServer(&app);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    server->listen(kInstanceKey);

    UrlOpenFilter urlFilter;
    app.installEventFilter(&urlFilter);

    Backend backend;

#ifdef Q_OS_MACOS
    setupMacDockIcon(app, backend);
#endif

    QuitFilter quitFilter;
    quitFilter.backend = &backend;
    app.installEventFilter(&quitFilter);

    bool appQuitting = false;
    QObject::connect(&backend, &Backend::aboutToShutdown, &app, [&appQuitting]() {
        appQuitting = true;
    });
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &backend, &Backend::prepareQuit);
    QObject::connect(&app, &QGuiApplication::aboutToQuit, &app, []() {
        removeInstanceAuthToken();
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    QTranslator *translator = nullptr;
    applyLanguage(app, engine, translator, backend.language());
    QObject::connect(&backend, &Backend::languageChanged, &app,
                     [&app, &engine, &translator](const QString &lang) {
                         applyLanguage(app, engine, translator, lang);
                     });

    auto *win = qobject_cast<QWindow *>(engine.rootObjects().first());
#ifdef Q_OS_MACOS
    if (win)
        applyMacUnifiedTitlebar(win->winId());
#endif
    urlFilter.ready(&backend, win);
    if (!controlArg.isEmpty())
        backend.handleControl(controlArg);

    setupDockReopen(app, win, appQuitting);
    wireInstanceServer(server, backend, win, instanceToken);

    return app.exec();
}

} // namespace freetunnel
