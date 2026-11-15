// cppcheck-suppress-file missingIncludeSystem
#include <QString>

#include "app/AppStartup.h"
#include "vpn/vpn_helper_server.h"
#if defined(Q_OS_UNIX)
#include "vpn/installed_helper.h"
#endif

int main(int argc, char *argv[])
{
    freetunnel::raiseFdLimit();
#if defined(Q_OS_UNIX)
    if (argc > 1 && freetunnel::installed_helper::isCommand(QString::fromLocal8Bit(argv[1])))
        return freetunnel::installed_helper::runCommand(argc, argv, [&] { return runVpnHelper(argc, argv); });
#endif
    // Icon.qml reads bundled SVGs through backend.readBundledText (qrc-only),
    // so QML XHR local-file access stays disabled engine-wide.
    qputenv("QML_DISABLE_DISK_CACHE", "1");

    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
#if defined(Q_OS_WIN)
        // The helper service, as the installer registers it: started by the
        // service control manager, never by hand.
        if (arg == QLatin1String("--service"))
            return runVpnHelperService(argc, argv);
#endif
        if (arg == QLatin1String("--helper"))
            return runVpnHelper(argc, argv);
    }
    return freetunnel::runGuiApplication(argc, argv);
}
