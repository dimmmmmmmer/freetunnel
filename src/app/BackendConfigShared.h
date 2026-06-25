#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace freetunnel::backend_config {

bool validateAddressList(const QString &addresses);
bool validateDnsList(const QString &dns);
bool writeConfigFile(const QString &target, const QByteArray &body);
bool storeConfigPassword(const QString &target, const QString &password);
void updateStoredConfigList(QStringList &stored, const QString &oldPath, const QString &target);
bool readValidatedImportContent(const QString &path, QString *contentOut, QString *errOut);
bool copyImportIntoAppConfigDir(const QString &content, const QString &sourcePath, QString *targetOut);

} // namespace freetunnel::backend_config
