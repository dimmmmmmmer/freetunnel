// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QEvent>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class Backend;
class QGuiApplication;
class QLocalServer;
class QQmlApplicationEngine;
class QTranslator;
class QWindow;

class UrlOpenFilter : public QObject {
    Q_OBJECT
public:
    Backend *backend = nullptr;
    QWindow *win = nullptr;
    // Deep links delivered before ready(): the first waits in `pending`, any
    // further ones queue behind it. Keeping only the last one dropped imports
    // when macOS delivered several FileOpen events back-to-back at launch.
    QString pending;
    QStringList pendingMore;
    void ready(Backend *b, QWindow *w);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void apply(const QString &url);
};

class QuitFilter : public QObject {
    Q_OBJECT
public:
    explicit QuitFilter(QObject *parent = nullptr) : QObject(parent) {}
    Backend *backend = nullptr;

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
};

namespace freetunnel {

void applyLanguage(QGuiApplication &app, QQmlApplicationEngine &engine,
                   QTranslator *&translator, const QString &lang);

QString controlArgFrom(int argc, char *argv[]);

void raiseFdLimit();

void setupMacDockIcon(QGuiApplication &app, Backend &backend);

void wireInstanceServer(QLocalServer *server, Backend &backend, QWindow *win,
                        const QString &instanceToken);

// Returns the object that owns the reopen wiring, or nullptr where there is none
// (macOS reopens through the native Dock event instead). The caller must keep it
// alive no longer than @p appQuitting: the filter and the connections read that
// flag by address, and an event filter installed on the application outlives any
// scope but its own owner. Destroying the returned object unregisters everything.
QObject *setupDockReopen(QGuiApplication &app, QWindow *win, bool &appQuitting);
// Re-show the main window when the user activates the app from the dock/taskbar
// while it was hidden to the tray (macOS Dock, Linux panel, etc.).

// Everything runGuiApplication() builds and has to keep alive until exec()
// returns. Declaration order is destruction order reversed, and it matters:
// `engine` must go before `backend`, because the QML that engine owns holds
// `backend` as a context property and touches it on teardown.
struct GuiStartup {
    std::unique_ptr<UrlOpenFilter> urlFilter;
    std::unique_ptr<Backend> backend;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QLocalServer *server = nullptr; // parented to the application
    QWindow *win = nullptr;         // owned by the engine
    QTranslator *translator = nullptr;
    // Captured BY REFERENCE in the shutdown lambdas, so it has to outlive them —
    // which is the reason this is a member and not a local in the caller.
    bool appQuitting = false;
    // Declared after appQuitting so it is destroyed BEFORE it — it reads that flag
    // by address from an event filter installed on the application.
    std::unique_ptr<QObject> dockReopen;
    // The startup steps in the order they ran. The entry point is the one file
    // no test compiled, and both macOS bugs found in it were about what happens
    // before what; this lets a test say so out loud.
    QStringList trace;
};

// Build the application: everything runGuiApplication() does except constructing
// QGuiApplication and entering the event loop. Returns a value when the process
// should exit immediately with it (0 when the command was handed to a running
// instance, -1 when the QML failed to load), or nothing when it should exec().
std::optional<int> wireGuiApplication(QGuiApplication &app, int argc, char *argv[],
                                      GuiStartup *out);

int runGuiApplication(int argc, char *argv[]);

} // namespace freetunnel

