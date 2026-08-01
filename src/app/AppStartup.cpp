// cppcheck-suppress-file missingIncludeSystem
#include "app/AppStartup.h"

#include <QEvent>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QTranslator>
#include <QWindow>

#include <memory>

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

namespace {

// An instance message is "token\npayload" with no terminator, so the only end
// markers are the peer's disconnect, a gap in the stream, or the size cap.
constexpr int kInstanceMessageMax = 64 * 1024;
constexpr int kInstanceMessageIdleMs = 250;  // no more bytes for this long → done
constexpr int kInstanceMessageDeadlineMs = 3000; // hard stop for a peer that drips

} // namespace

// Collect the message without blocking: the old waitForReadyRead(200) loop froze
// the GUI thread for up to 200 ms per chunk (64 KB worth) while a same-user peer
// took its time. Buffer on readyRead instead and deliver from the event loop.
void handleInstanceConnection(QLocalSocket *c, Backend &backend, QWindow *win,
                              const QString &instanceToken)
{
    if (!c)
        return;
    if (!localSocketPeerIsSameUser(c)) {
        c->deleteLater();
        return;
    }
    auto buf = std::make_shared<QByteArray>();
    auto delivered = std::make_shared<bool>(false);
    auto *idle = new QTimer(c);
    idle->setSingleShot(true);
    idle->setInterval(kInstanceMessageIdleMs);

    Backend *be = &backend;
    auto deliver = [c, buf, delivered, be, win, instanceToken]() {
        if (*delivered)
            return;
        *delivered = true;
        *buf += c->readAll();
        c->deleteLater();
        QString recvToken;
        QString cmd;
        if (!parseInstanceMessage(*buf, &recvToken, &cmd) || instanceToken.isEmpty()
                || !instanceTokensEqual(recvToken, instanceToken))
            return;
        be->handleControl(cmd);
        raiseMainWindow(win);
    };

    QObject::connect(idle, &QTimer::timeout, c, deliver);
    QObject::connect(c, &QLocalSocket::readyRead, c, [c, buf, idle, deliver]() {
        *buf += c->readAll();
        if (buf->size() >= kInstanceMessageMax) {
            deliver();
            return;
        }
        idle->start();
    });
    QObject::connect(c, &QLocalSocket::disconnected, c, deliver);
    QTimer::singleShot(kInstanceMessageDeadlineMs, c, deliver);
    idle->start();
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

#ifdef Q_OS_MACOS
    // macOS reopens via the native Dock-icon Apple Event (installMacDockReopenHandler,
    // wired in runGuiApplication), which fires ONLY on a Dock click — not on
    // status-bar clicks or Cmd-Tab. The broad app-activation handlers below would
    // re-show the menu-bar-hidden window on any activation (the reported bug), so
    // they are macOS-excluded. Windows/Linux keep them: there the window is
    // minimized to the taskbar (not hidden) and these drive taskbar reopen.
    Q_UNUSED(app);
    Q_UNUSED(appQuitting);
#else
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
#endif
}

} // namespace freetunnel

void UrlOpenFilter::ready(Backend *b, QWindow *w)
{
    backend = b;
    win = w;
    const QStringList queued = pendingMore;
    pendingMore.clear();
    if (!pending.isEmpty()) {
        const QString first = pending;
        pending.clear();
        apply(first);
    }
    for (const QString &u : queued)
        apply(u);
}

bool UrlOpenFilter::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::FileOpen) {
        const QString u = static_cast<QFileOpenEvent *>(e)->url().toString();
        if (!u.isEmpty()) {
            if (backend)
                apply(u);
            else if (pending.isEmpty())
                pending = u;
            else
                pendingMore << u; // several links can land before ready()
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
