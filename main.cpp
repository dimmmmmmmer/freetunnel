// cppcheck-suppress-file missingIncludeSystem
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QWindow>

#include "app/AppStartup.h"
#include "app/Backend.h"
#include "app/MacWindow.h"
#include "core/InstanceControl.h"
#include "vpn/vpn_helper_server.h"

int main(int argc, char *argv[])
{
    freetunnel::raiseFdLimit();
    // Required so Icon.qml can XHR-GET the bundled SVGs (qrc:/icons/*) and
    // recolor them — Qt denies XMLHttpRequest reads of local *and* qrc schemes
    // without this. Safe here because every QML document and every XHR URL is a
    // hardcoded qrc: path baked into the binary: no remote/untrusted QML is ever
    // loaded, so there is no attacker-controlled local-file read path.
    qputenv("QML_XHR_ALLOW_FILE_READ", "1");
    // After an in-place upgrade the on-disk QML cache can disagree with the
    // embedded qrc timestamps and leave stale/broken bindings (blank pages,
    // wrong icon colours). Always compile from the bundled qrc.
    qputenv("QML_DISABLE_DISK_CACHE", "1");

    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--helper"))
            return runVpnHelper(argc, argv);

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

    const QString controlArg = freetunnel::controlArgFrom(argc, argv);
    const QString kInstanceKey = QStringLiteral("FreeTunnelInstance");

    if (freetunnel::forwardToRunningInstance(kInstanceKey, controlArg))
        return 0;

    QLocalServer::removeServer(kInstanceKey);
    QString instanceToken;
    if (!freetunnel::writeInstanceAuthToken(&instanceToken))
        instanceToken.clear();
    auto *server = new QLocalServer(&app);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    server->listen(kInstanceKey);

    UrlOpenFilter urlFilter;
    app.installEventFilter(&urlFilter);

    Backend backend;

#ifdef Q_OS_MACOS
    freetunnel::setupMacDockIcon(app, backend);
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
        freetunnel::removeInstanceAuthToken();
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    QTranslator *translator = nullptr;
    freetunnel::applyLanguage(app, engine, translator, backend.language());
    QObject::connect(&backend, &Backend::languageChanged, &app,
                     [&app, &engine, &translator](const QString &lang) {
                         freetunnel::applyLanguage(app, engine, translator, lang);
                     });

    auto *win = qobject_cast<QWindow *>(engine.rootObjects().first());
#ifdef Q_OS_MACOS
    if (win)
        applyMacUnifiedTitlebar(win->winId());
#endif
    urlFilter.ready(&backend, win);
    if (!controlArg.isEmpty())
        backend.handleControl(controlArg);

    freetunnel::setupDockReopen(app, win, appQuitting);
    freetunnel::wireInstanceServer(server, backend, win, instanceToken);

    return app.exec();
}
