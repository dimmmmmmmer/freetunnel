// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace freetunnel::backend_config {

bool validateAddressList(const QString &addresses);
bool validateDnsList(const QString &dns);
// Both write owner-only and atomically (staged, then committed over the target).
bool writeConfigFile(const QString &target, const QByteArray &body);
bool storeConfigPassword(const QString &target, const QString &password);
// Body + password as one all-or-nothing step: the destination is untouched
// unless the credential store accepted the password. *errOut: "write"|"password".
bool saveConfigWithPassword(const QString &target, const QByteArray &body, const QString &password,
                            QString *errOut);
void updateStoredConfigList(QStringList &stored, const QString &oldPath, const QString &target);
bool readValidatedImportContent(const QString &path, QString *contentOut, QString *errOut);
bool copyImportIntoAppConfigDir(const QString &content, const QString &sourcePath, QString *targetOut);

} // namespace freetunnel::backend_config
