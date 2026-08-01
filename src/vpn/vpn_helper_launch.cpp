// cppcheck-suppress-file missingIncludeSystem
#include "vpn/vpn_helper_launch.h"

#include <QCoreApplication>
#include <QFile>

namespace freetunnel {

HelperLaunchConfig parseHelperLaunchArgs(const QStringList &args)
{
    HelperLaunchConfig cfg;
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args[i] == QLatin1String("--port"))
            cfg.port = args[i + 1].toUShort();
        else if (args[i] == QLatin1String("--token-file"))
            cfg.token = readHelperTokenFile(args[i + 1]);
    }
    return cfg;
}

QString readHelperTokenFile(const QString &path)
{
    if (path.isEmpty())
        return QString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QString token = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    QFile::remove(path);
    return token;
}

} // namespace freetunnel
