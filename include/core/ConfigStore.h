#pragma once

#include <QString>
#include <QStringList>

QString storagePath();
QStringList loadStoredConfigs();
void saveStoredConfigs(const QStringList &configs);
