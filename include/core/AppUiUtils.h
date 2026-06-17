#pragma once

#include <QIcon>
#include <QString>

bool openHttpUrl(const QString &url);
QString shellEscape(QString s);
QString appleScriptEscape(QString s);
bool runElevatedShell(const QString &command, QString *errorText);
QIcon makeAppIcon();
