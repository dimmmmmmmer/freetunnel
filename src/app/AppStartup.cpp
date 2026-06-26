// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QEvent>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QWindow>

#include "app/Backend.h"
#include "core/InstanceControl.h"

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
void setupMacDockIcon(QGuiApplication &, Backend &)
{
    // macOS uses CFBundleIconFile (logo.icns). setWindowIcon() overrides it once the
    // window is shown, so the Dock icon visibly swaps on open/close — keep the bundle
    // icon authoritative (regression fix after AppStartup refactor).
}
#else
void setupMacDockIcon(QGuiApplication &, Backend &) {}
#endif

static void raiseMainWindow(QWindow *win)
{
    if (!win)
        return;
    if (!win->isVisible())
        win->show();
    win->raise();
    win->requestActivate();
}

namespace {

class HiddenWindowReopenFilter : public QObject {
public:
    explicit HiddenWindowReopenFilter(QObject *parent = nullptr) : QObject(parent) {}
    QWindow *win = nullptr;
    bool *appQuitting = nullptr;

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (!win || !appQuitting || *appQuitting)
            return false;
        // ApplicationActivate is delivered to QGuiApplication (taskbar/dock on Windows/Linux).
        if (e->type() == QEvent::ApplicationActivate) {
            raiseMainWindow(win);
            return false;
        }
        if (o != win)
            return false;
        switch (e->type()) {
        case QEvent::WindowActivate:
        case QEvent::Show:
            raiseMainWindow(win);
            break;
        default:
            break;
        }
        return false;
    }
};

} // namespace

QByteArray readLocalSocketPayload(QLocalSocket *c)
{
    QByteArray buf = c->readAll();
    while (c->state() == QLocalSocket::ConnectedState && buf.size() < 64 * 1024
           && c->waitForReadyRead(200))
        buf += c->readAll();
    buf += c->readAll();
    return buf;
}

void handleInstanceConnection(QLocalSocket *c, Backend &backend, QWindow *win,
                              const QString &instanceToken)
{
    if (!c)
        return;
    const QByteArray buf = readLocalSocketPayload(c);
    if (!localSocketPeerIsSameUser(c)) {
        c->deleteLater();
        return;
    }
    QString recvToken;
    QString cmd;
    if (!parseInstanceMessage(buf, &recvToken, &cmd) || instanceToken.isEmpty()
            || !instanceTokensEqual(recvToken, instanceToken)) {
        c->deleteLater();
        return;
    }
    c->deleteLater();
    backend.handleControl(cmd);
    raiseMainWindow(win);
}

void wireInstanceServer(QLocalServer *server, Backend &backend, QWindow *win,
                        const QString &instanceToken)
{
    QObject::connect(server, &QLocalServer::newConnection, server,
                     [server, &backend, win, instanceToken]() {
                         handleInstanceConnection(server->nextPendingConnection(), backend, win,
                                                  instanceToken);
                     });
}

void setupDockReopen(QGuiApplication &app, QWindow *win, bool &appQuitting)
{
    if (!win)
        return;

    auto *filter = new HiddenWindowReopenFilter(&app);
    filter->win = win;
    filter->appQuitting = &appQuitting;
    app.installEventFilter(filter);
    win->installEventFilter(filter);

    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &app,
                     [win, &appQuitting](Qt::ApplicationState s) {
                         if (appQuitting)
                             return;
                         if (s == Qt::ApplicationActive)
                             raiseMainWindow(win);
                     });

    // Panel/taskbar can activate a hidden window without changing application state.
    QObject::connect(win, &QWindow::activeChanged, win, [win, &appQuitting]() {
        if (appQuitting || !win->isActive())
            return;
        raiseMainWindow(win);
    });
    QObject::connect(&app, &QGuiApplication::focusWindowChanged, &app,
                     [win, &appQuitting](QWindow *focus) {
                         if (appQuitting || focus != win)
                             return;
                         raiseMainWindow(win);
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
    freetunnel::raiseMainWindow(win);
}

bool QuitFilter::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::Quit && backend) {
        backend->quitApplication();
        return true;
    }
    return QObject::eventFilter(o, e);
}
