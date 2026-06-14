#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>
#include <QUrl>

#include "MockBackend.h"

// Local design preview: loads the QML UI against a mock backend and grabs a
// screenshot to PNG (run with QT_QPA_PLATFORM=offscreen). Not shipped.
int main(int argc, char **argv) {
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);

    MockBackend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);

    // Preview Russian UI with QML_PREVIEW_LANG=ru
    QTranslator tr;
    if (qgetenv("QML_PREVIEW_LANG") == "ru" && tr.load(QStringLiteral(":/i18n/freetunnel_ru.qm")))
        app.installTranslator(&tr);
    if (qEnvironmentVariableIsSet("QML_PREVIEW_THEME"))
        backend.setThemeMode(QString::fromLocal8Bit(qgetenv("QML_PREVIEW_THEME")));

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : QStringLiteral("/tmp/freetunnel_home.png");
    if (win && argc > 2)
        win->setProperty("currentPage", QString::fromLocal8Bit(argv[2]).toInt());
    if (win && argc > 3)
        win->setProperty("overlay", QString::fromLocal8Bit(argv[3]));
    QTimer::singleShot(2500, [&]() {
        if (win) {
            QImage img = win->grabWindow();
            img.save(out);
        }
        app.quit();
    });
    return app.exec();
}
