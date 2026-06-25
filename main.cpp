#include <QGuiApplication>
#include <QEvent>
#include <QFileOpenEvent>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTranslator>
#include <QUrl>
#include <QWindow>

#include "app/Backend.h"
#include "app/MacWindow.h"
#include "core/InstanceControl.h"
#include "vpn/vpn_helper_server.h"

// Install/replace the UI translation for `lang` ("ru" loads the bundled .qm;
// anything else falls back to the English source strings). Retranslates live.
static void applyLanguage(QGuiApplication &app, QQmlApplicationEngine &engine,
                          QTranslator *&tr, const QString &lang) {
    if (tr) {
        app.removeTranslator(tr);
        delete tr;
        tr = nullptr;
    }
    if (lang == QLatin1String("ru")) {
        tr = new QTranslator(&app);
        if (tr->load(QStringLiteral(":/i18n/freetunnel_ru.qm")))
            app.installTranslator(tr);
    }
    engine.retranslate();
}

// macOS delivers custom-scheme URLs via QFileOpenEvent (not argv). Buffer any
// URL that arrives before the engine is ready, then route it to the backend.
class UrlOpenFilter : public QObject {
public:
    Backend *backend = nullptr;
    QWindow *win = nullptr;
    QString pending;
    void ready(Backend *b, QWindow *w) {
        backend = b; win = w;
        if (!pending.isEmpty()) { apply(pending); pending.clear(); }
    }
protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() == QEvent::FileOpen) {
            const QString u = static_cast<QFileOpenEvent *>(e)->url().toString();
            if (!u.isEmpty()) { backend ? apply(u) : (void)(pending = u); }
            return true;
        }
        return QObject::eventFilter(o, e);
    }
    void apply(const QString &u) {
        backend->handleControl(u);
        if (win) { win->show(); win->raise(); win->requestActivate(); }
    }
};

// macOS ⌘Q posts QEvent::Quit before window close; prepare cleanup early so
// Main.qml onClosing sees shuttingDown. Linux/Windows DE Quit usually sends a
// close event instead — handled in Main.qml onClosing (custom ✕ uses hide()).
class QuitFilter : public QObject {
public:
    Backend *backend = nullptr;
protected:
    bool eventFilter(QObject *, QEvent *e) override {
        if (e->type() == QEvent::Quit && backend)
            backend->prepareQuit();
        return false;
    }
};

// Pull a control argument (deep link) out of argv, if present.
static QString controlArgFrom(int argc, char *argv[]) {
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a.startsWith(QStringLiteral("tt://")) || a.startsWith(QStringLiteral("freetunnel://")))
            return a;
    }
    return QString();
}

#ifndef _WIN32
#include <sys/resource.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
static void raise_fd_limit() {
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
        rlim_t target = 524288; // aim for 512K
#ifdef __APPLE__
        // On macOS, kern.maxfilesperproc is the real ceiling.
        int mfp = 0;
        size_t len = sizeof(mfp);
        if (sysctlbyname("kern.maxfilesperproc", &mfp, &len, nullptr, 0) == 0 && mfp > 0) {
            target = static_cast<rlim_t>(mfp);
        }
#endif
        if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < target) {
            target = rl.rlim_max;
        }
        if (rl.rlim_cur < target) {
            rl.rlim_cur = target;
            setrlimit(RLIMIT_NOFILE, &rl);
        }
    }
}
#else
static void raise_fd_limit() {} // no-op on Windows
#endif

#ifdef Q_OS_MACOS
static void setupMacDockIcon(QGuiApplication &app, Backend &backend)
{
    if (QOperatingSystemVersion::current().majorVersion() < 26) {
        app.setWindowIcon(QIcon(QStringLiteral(":/assets/logo.svg")));
        return;
    }
    auto renderDockIcon = [&app, &backend]() {
        const QString m = backend.themeMode();
        const bool darkUi = m == QLatin1String("dark")
                || (m == QLatin1String("system")
                    && app.styleHints()->colorScheme() == Qt::ColorScheme::Dark);
        constexpr int kCanvas = 1024;
        constexpr int kTileInset = 100;
        constexpr int kTileSize = kCanvas - 2 * kTileInset;
        constexpr int kCornerRadius = 185;
        const int kLogoSize = kTileSize * 700 / 944;
        const int kLogoOffset = kTileInset + (kTileSize - kLogoSize) / 2;

        QPixmap pm(kCanvas, kCanvas);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath tile;
        tile.addRoundedRect(QRectF(kTileInset, kTileInset, kTileSize, kTileSize),
                            kCornerRadius, kCornerRadius);
        painter.fillPath(tile, QColor(darkUi ? QStringLiteral("#ffffff")
                                             : QStringLiteral("#1c1c1e")));
        QSvgRenderer logo(darkUi ? QStringLiteral(":/assets/logo.svg")
                                 : QStringLiteral(":/assets/logo-light.svg"));
        logo.render(&painter, QRectF(kLogoOffset, kLogoOffset, kLogoSize, kLogoSize));
        painter.end();
        app.setWindowIcon(QIcon(pm));
    };
    renderDockIcon();
    QObject::connect(&backend, &Backend::settingsChanged, &app, renderDockIcon);
    QObject::connect(app.styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [renderDockIcon](Qt::ColorScheme) { renderDockIcon(); });
}
#endif

static void wireInstanceServer(QLocalServer *server, Backend &backend, QWindow *win,
                               const QString &instanceToken)
{
    QObject::connect(server, &QLocalServer::newConnection, server,
                     [server, &backend, win, instanceToken]() {
        QLocalSocket *c = server->nextPendingConnection();
        if (!c)
            return;
        QByteArray buf = c->readAll();
        while (c->state() == QLocalSocket::ConnectedState
               && buf.size() < 64 * 1024
               && c->waitForReadyRead(200))
            buf += c->readAll();
        buf += c->readAll();
        if (!freetunnel::localSocketPeerIsSameUser(c)) {
            c->deleteLater();
            return;
        }
        QString recvToken;
        QString cmd;
        if (!freetunnel::parseInstanceMessage(buf, &recvToken, &cmd)
                || instanceToken.isEmpty()
                || !freetunnel::instanceTokensEqual(recvToken, instanceToken)) {
            c->deleteLater();
            return;
        }
        c->deleteLater();
        backend.handleControl(cmd);
        if (win) {
            win->show();
            win->raise();
            win->requestActivate();
        }
    });
}

static void setupDockReopen(QGuiApplication &app, QWindow *win, bool &appQuitting)
{
    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app,
                     [win, &appQuitting](Qt::ApplicationState s) {
                         if (appQuitting)
                             return;
                         if (s == Qt::ApplicationActive && win && !win->isVisible()) {
                             win->show();
                             win->raise();
                             win->requestActivate();
                         }
                     });
}

int main(int argc, char *argv[]) {
    raise_fd_limit();
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

    // Privileged helper mode: headless process that runs the VPN core for the
    // user-level GUI (spawned elevated via VpnHelperClient). No GUI here.
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--helper"))
            return runVpnHelper(argc, argv);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("FreeTunnel"));
    app.setOrganizationName(QStringLiteral("FreeTunnel"));
    app.setApplicationDisplayName(QStringLiteral("FreeTunnel"));
#ifndef Q_OS_MACOS
    // Windows/Linux: use the multi-size .ico that matches the one embedded in the
    // .exe (no OS-imposed icon mask there). logo.png is a raster fallback. (macOS
    // is set up below, after the backend exists, so the Dock icon can follow the
    // app's theme.)
    QIcon winLinuxIcon(QStringLiteral(":/assets/logo.ico"));
    winLinuxIcon.addFile(QStringLiteral(":/assets/logo.png"));
    app.setWindowIcon(winLinuxIcon);
#endif
    // Keep running in the tray when the window is closed.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    const QString controlArg = controlArgFrom(argc, argv);
    const QString kInstanceKey = QStringLiteral("FreeTunnelInstance");

    // Single instance: if one is already running, hand it our control command
    // (e.g. freetunnel://toggle) and exit instead of opening a second window.
    if (freetunnel::forwardToRunningInstance(kInstanceKey, controlArg))
        return 0;

    QLocalServer::removeServer(kInstanceKey);
    QString instanceToken;
    if (!freetunnel::writeInstanceAuthToken(&instanceToken))
        instanceToken.clear();
    auto *server = new QLocalServer(&app);
    // Only the same user may connect to the single-instance socket, so another
    // local user can't push control commands (toggle/connect/import) into a
    // running session.
    server->setSocketOptions(QLocalServer::UserAccessOption);
    server->listen(kInstanceKey);

    UrlOpenFilter urlFilter;
    app.installEventFilter(&urlFilter); // catch macOS URL opens early

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
        freetunnel::removeInstanceAuthToken();
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    // UI language: apply the saved choice and re-apply live when it changes.
    QTranslator *translator = nullptr;
    applyLanguage(app, engine, translator, backend.language());
    QObject::connect(&backend, &Backend::languageChanged, &app,
                     [&app, &engine, &translator](const QString &lang) {
                         applyLanguage(app, engine, translator, lang);
                     });

    auto *win = qobject_cast<QWindow *>(engine.rootObjects().first());
#ifdef Q_OS_MACOS
    if (win)
        applyMacUnifiedTitlebar(win->winId()); // transparent, content-filled title bar
#endif
    urlFilter.ready(&backend, win); // flush any URL that arrived during startup
    if (!controlArg.isEmpty())
        backend.handleControl(controlArg);

    setupDockReopen(app, win, appQuitting);
    wireInstanceServer(server, backend, win, instanceToken);

    return app.exec();
}
