// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QEvent>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QOperatingSystemVersion>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QQmlApplicationEngine>
#include <QStyleHints>
#include <QTranslator>
#include <QWindow>

#include "app/Backend.h"
#include "core/InstanceControl.h"

#ifdef Q_OS_MACOS
#include <QSvgRenderer>
#endif

#ifndef _WIN32
#include <sys/resource.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif

namespace freetunnel {

void applyLanguage(QGuiApplication &app, QQmlApplicationEngine &engine,
                   QTranslator *&tr, const QString &lang)
{
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

// cppcheck-suppress constParameter
QString controlArgFrom(int argc, char *argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a.startsWith(QStringLiteral("tt://")) || a.startsWith(QStringLiteral("freetunnel://")))
            return a;
    }
    return QString();
}

void raiseFdLimit()
{
#ifndef _WIN32
    struct rlimit rl{};
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        return;
    rlim_t target = 524288;
#ifdef __APPLE__
    int mfp = 0;
    size_t len = sizeof(mfp);
    if (sysctlbyname("kern.maxfilesperproc", &mfp, &len, nullptr, 0) == 0 && mfp > 0)
        target = static_cast<rlim_t>(mfp);
#endif
    if (rl.rlim_max != RLIM_INFINITY && rl.rlim_max < target)
        target = rl.rlim_max;
    if (rl.rlim_cur < target) {
        rl.rlim_cur = target;
        setrlimit(RLIMIT_NOFILE, &rl);
    }
#endif
}

#ifdef Q_OS_MACOS
void setupMacDockIcon(QGuiApplication &app, Backend &backend)
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
#else
void setupMacDockIcon(QGuiApplication &, Backend &) {}
#endif

void wireInstanceServer(QLocalServer *server, Backend &backend, QWindow *win,
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
        if (!localSocketPeerIsSameUser(c)) {
            c->deleteLater();
            return;
        }
        QString recvToken;
        QString cmd;
        if (!parseInstanceMessage(buf, &recvToken, &cmd)
                || instanceToken.isEmpty()
                || !instanceTokensEqual(recvToken, instanceToken)) {
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

void setupDockReopen(QGuiApplication &app, QWindow *win, bool &appQuitting)
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

} // namespace freetunnel

void UrlOpenFilter::ready(Backend *b, QWindow *w)
{
    backend = b;
    win = w;
    if (!pending.isEmpty()) {
        apply(pending);
        pending.clear();
    }
}

bool UrlOpenFilter::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::FileOpen) {
        const QString u = static_cast<QFileOpenEvent *>(e)->url().toString();
        if (!u.isEmpty()) {
            if (backend)
                apply(u);
            else
                pending = u;
        }
        return true;
    }
    return QObject::eventFilter(o, e);
}

void UrlOpenFilter::apply(const QString &u)
{
    backend->handleControl(u);
    if (win) {
        win->show();
        win->raise();
        win->requestActivate();
    }
}

bool QuitFilter::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::Quit && backend)
        backend->prepareQuit();
    return QObject::eventFilter(o, e);
}
