#pragma once

#include <QIcon>
#include <QString>

QString shellEscape(QString s);
QString appleScriptEscape(QString s);
bool runElevatedShell(const QString &command, QString *errorText);
QIcon makeAppIcon();
