// cppcheck-suppress-file missingIncludeSystem
#include <QString>

#include "app/AppStartup.h"
#include "vpn/vpn_helper_server.h"

int main(int argc, char *argv[])
{
    freetunnel::raiseFdLimit();
    // Icon.qml reads bundled SVGs through backend.readBundledText (qrc-only),
    // so QML XHR local-file access stays disabled engine-wide.
    qputenv("QML_DISABLE_DISK_CACHE", "1");

    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--helper"))
            return runVpnHelper(argc, argv);
    }
    return freetunnel::runGuiApplication(argc, argv);
}
