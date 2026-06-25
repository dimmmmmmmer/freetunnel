// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QEvent>
#include <QObject>
#include <QString>

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
    QString pending;
    void ready(Backend *b, QWindow *w);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void apply(const QString &url);
};

class QuitFilter : public QObject {
    Q_OBJECT
public:
    Backend *backend = nullptr;

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
};
