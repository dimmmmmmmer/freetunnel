// cppcheck-suppress-file missingIncludeSystem
#include <QString>

#include "app/AppStartup.h"
#include "vpn/vpn_helper_server.h"

int main(int argc, char *argv[])
{
    freetunnel::raiseFdLimit();
    // Required so Icon.qml can XHR-GET bundled qrc icons; every QML/XHR URL is
    // hardcoded in the binary — no untrusted local-file read path exists.
    qputenv("QML_XHR_ALLOW_FILE_READ", "1");
    qputenv("QML_DISABLE_DISK_CACHE", "1");

    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QLatin1String("--helper"))
            return runVpnHelper(argc, argv);
    }
    return freetunnel::runGuiApplication(argc, argv);
}
