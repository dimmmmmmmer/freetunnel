// cppcheck-suppress-file missingIncludeSystem
#include "vpn/vpn_helper_launch.h"

#include <QCoreApplication>
#include <QFile>

#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

namespace freetunnel {

HelperLaunchConfig parseHelperLaunchArgs(const QStringList &args)
{
    HelperLaunchConfig cfg;
    for (int i = 1; i < args.size() - 1; ++i) {
        if (args[i] == QLatin1String("--port"))
            cfg.port = args[i + 1].toUShort();
        else if (args[i] == QLatin1String("--token-file"))
            cfg.token = readHelperTokenFile(args[i + 1], &cfg.ownerUid);
    }
    return cfg;
}

QString readHelperTokenFile(const QString &path, qint64 *ownerUidOut)
{
    if (ownerUidOut)
        *ownerUidOut = -1;
    if (path.isEmpty())
        return QString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
#ifndef Q_OS_WIN
    // Stat the descriptor we actually opened, not the name — the name could be
    // swapped between the two calls.
    struct stat st {};
    if (ownerUidOut && ::fstat(f.handle(), &st) == 0)
        *ownerUidOut = static_cast<qint64>(st.st_uid);
#endif
    const QString token = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    QFile::remove(path);
    return token;
}

} // namespace freetunnel
