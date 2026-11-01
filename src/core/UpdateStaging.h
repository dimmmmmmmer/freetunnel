#pragma once
#include <QString>
class QFile;
namespace freetunnel::update_staging {
QString updateStagingDir();
bool prepareStagingDir(const QString &dir, QString *error);
bool createInstallerFile(QFile &file, const QString &path);
}
