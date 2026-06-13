#pragma once

#include <QString>

QString pingConfigFile(const QString &path);
QString buildConfigSummaryHtml(const QString &path);
QString buildConfigValidationHtml(const QString &path);
QString loadLicenseText();
