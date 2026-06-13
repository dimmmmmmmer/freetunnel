#include "ConfigStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QStandardPaths>

QString storagePath() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    return base + "/configs.json";
}

QStringList loadStoredConfigs() {
    QFile f(storagePath());
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) {
        return {};
    }
    QStringList out;
    for (const QJsonValue &v : doc.array()) {
        if (v.isString()) {
            out.push_back(v.toString());
        }
    }
    out.removeDuplicates();
    return out;
}

void saveStoredConfigs(const QStringList &configs) {
    QJsonArray arr;
    for (const QString &c : configs) {
        arr.append(c);
    }
    QFile f(storagePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    }
}
