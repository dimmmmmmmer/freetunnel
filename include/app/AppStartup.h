// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QEvent>
#include <QObject>
#include <QString>
#include <QStringList>

class Backend;
class QGuiApplication;
class QLocalServer;
class QQmlApplicationEngine;
class QTranslator;
class QWindow;

namespace freetunnel {

void applyLanguage(QGuiApplication &app, QQmlApplicationEngine &engine,
                   QTranslator *&translator, const QString &lang);

QString controlArgFrom(int argc, char *argv[]);

void raiseFdLimit();

void setupMacDockIcon(QGuiApplication &app, Backend &backend);

void wireInstanceServer(QLocalServer *server, Backend &backend, QWindow *win,
                        const QString &instanceToken);

void setupDockReopen(QGuiApplication &app, QWindow *win, bool &appQuitting);
// Re-show the main window when the user activates the app from the dock/taskbar
// while it was hidden to the tray (macOS Dock, Linux panel, etc.).

int runGuiApplication(int argc, char *argv[]);

} // namespace freetunnel

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
