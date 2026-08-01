// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QtGlobal>

#include <QString>

namespace freetunnel {

struct HelperLaunchConfig {
    quint16 port = 0;
    QString token;
    bool ok() const { return port != 0 && !token.isEmpty(); }
};

/// Parse `--helper --port P --token-file F` arguments (reads and deletes token file).
HelperLaunchConfig parseHelperLaunchArgs(const QStringList &args);

/// Read a one-time token from a helper launch file and remove the file.
QString readHelperTokenFile(const QString &path);

} // namespace freetunnel
