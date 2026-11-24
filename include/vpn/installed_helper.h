// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>
#include <QStringList>
#include <functional>

namespace freetunnel::installed_helper {

QString rootDirectory();
QString executableRelativePath();
QString assetName();
QString executablePath();
QString sudoRule(quint32 uid);
bool validUserId(const QString &text, quint32 *uid);
bool validStartArguments(const QStringList &args);

// The archive must be authenticated before anything is extracted. The signed
// release version and the executable in that archive are checked independently.
bool verifyHelperManifest(const QByteArray &sums, const QByteArray &signature, bool allowBundledKey);
bool verifyArchive(const QString &archive, const QByteArray &sums, const QByteArray &signature,
                   const QString &currentVersion, bool allowSameVersion, QString *error);

// CLI entry points run before Qt GUI/plugin initialization. Only --installed-helper
// is granted by sudoers; installation/removal still require an explicit elevation.
bool isCommand(const QString &arg);
int runCommand(int argc, char **argv, const std::function<int()> &runSession);

} // namespace freetunnel::installed_helper
