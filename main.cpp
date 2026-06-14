#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QUrl>

#include "app/Backend.h"

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

int main(int argc, char *argv[]) {
    raise_fd_limit();
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("FreeTunnel"));
    app.setOrganizationName(QStringLiteral("FreeTunnel"));
    app.setApplicationDisplayName(QStringLiteral("FreeTunnel"));
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/logo.png")));

    Backend backend;

    // Import an official deep link passed on the command line (tt://...).
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QStringLiteral("tt://")))
            backend.importDeepLink(arg);
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
