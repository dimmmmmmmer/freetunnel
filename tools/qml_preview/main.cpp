#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <QTranslator>
#include <QUrl>

#include "MockBackend.h"

// Local design preview: mock backend + PNG grab (QT_QPA_PLATFORM=offscreen).
// Usage: preview [out.png] [page] [scenario]
//   scenario: empty | config | connecting | disconnecting | connected
//   or set QML_PREVIEW_SCENARIO / QML_PREVIEW_LANG=ru / QML_PREVIEW_THEME=dark
int main(int argc, char **argv) {
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QML_XHR_ALLOW_FILE_READ", "1"); // let Icon recolor qrc SVGs via XHR
    QGuiApplication app(argc, argv);

    MockBackend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);

    QTranslator tr;
    if (qgetenv("QML_PREVIEW_LANG") == "ru" && tr.load(QStringLiteral(":/i18n/freetunnel_ru.qm")))
        app.installTranslator(&tr);
    if (qEnvironmentVariableIsSet("QML_PREVIEW_THEME"))
        backend.setThemeMode(QString::fromLocal8Bit(qgetenv("QML_PREVIEW_THEME")));

    const QByteArray scenario = qgetenv("QML_PREVIEW_SCENARIO");
    if (!scenario.isEmpty())
        backend.applyPreviewScenario(QString::fromUtf8(scenario));
    else if (argc > 3)
        backend.applyPreviewScenario(QString::fromLocal8Bit(argv[3]));

    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    const QString out = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                 : QStringLiteral("/tmp/freetunnel_home.png");
    if (win && argc > 2)
        win->setProperty("currentPage", QString::fromLocal8Bit(argv[2]).toInt());
    if (win && qEnvironmentVariableIsSet("QML_PREVIEW_OVERLAY"))
        win->setProperty("overlay", QString::fromLocal8Bit(qgetenv("QML_PREVIEW_OVERLAY")));
    QTimer::singleShot(1500, win, [win, out]() {
        if (win) {
            QImage img = win->grabWindow();
            img.save(out);
        }
        QCoreApplication::quit();
    });
    return app.exec();
}
