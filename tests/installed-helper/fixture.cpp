// cppcheck-suppress-file missingIncludeSystem
#include "vpn/installed_helper.h"
#include "vpn/vpn_helper_launch.h"
#include <QCoreApplication>
#include <QThread>
#include <QStringList>

int main(int argc, char **argv)
{
    if (argc < 2) return 1;
    // Catch a joined-argument sudoers match escaping into ordinary CLI dispatch:
    // the real main would otherwise continue looking for --helper/GUI options.
    if (!freetunnel::installed_helper::isCommand(QString::fromLocal8Bit(argv[1]))) return 0;
    return freetunnel::installed_helper::runCommand(argc, argv, [&] {
        QCoreApplication app(argc, argv);
        const auto cfg = freetunnel::parseHelperLaunchArgs(app.arguments());
        if (!cfg.ok()) return 1;
        // Gives the integration test time to attempt an update while a session
        // owns the installed runtime. No tunnel, route or DNS is changed.
        QThread::sleep(3);
        return 0;
    });
}
