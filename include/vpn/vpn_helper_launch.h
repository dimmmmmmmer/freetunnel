// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QtGlobal>

#include <QString>

namespace freetunnel {

struct HelperLaunchConfig {
    quint16 port = 0;
    QString token;
    // Owner of the token file, i.e. the unprivileged user that launched us. The
    // helper runs as root and must not write wherever the GUI asks, so paths it
    // is handed over IPC are required to belong to this user. -1 = unknown.
    qint64 ownerUid = -1;
    bool ok() const { return port != 0 && !token.isEmpty(); }
};

/// Parse `--helper --port P --token-file F` arguments (reads and deletes token file).
HelperLaunchConfig parseHelperLaunchArgs(const QStringList &args);

/// Read a one-time token from a helper launch file and remove the file.
/// When ownerUidOut is given it receives the file's owner uid (-1 if unknown).
QString readHelperTokenFile(const QString &path, qint64 *ownerUidOut = nullptr);

} // namespace freetunnel
