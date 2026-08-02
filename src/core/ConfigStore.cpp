// cppcheck-suppress-file missingIncludeSystem
#include "ConfigStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString configDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    // Owner-only: the .toml configs next to this index hold hostnames, usernames
    // and certificates, so don't leave the directory readable to other users
    // (mkpath creates 0755). Same rule as the credentials subdirectory.
    QFile::setPermissions(base, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner);
    return base;
}

// Last-resort recovery: rebuild the list from the .toml files sitting in the app
// config directory. The index is only an index — losing it used to make every
// server disappear from the UI even though the configs themselves were still on
// disk, with nothing to rediscover them.
QStringList rediscoverConfigs() {
    QDir d(configDir());
    QStringList out;
    for (const QString &name : d.entryList({QStringLiteral("*.toml")}, QDir::Files, QDir::Name))
        out.push_back(d.filePath(name));
    return out;
}

} // namespace

QString storagePath() {
    return configDir() + "/configs.json";
}

QStringList loadStoredConfigs() {
    const QString path = storagePath();
    QFile f(path);
    if (!f.exists())
        return {}; // first run: nothing stored and nothing to recover from
    if (!f.open(QIODevice::ReadOnly))
        return rediscoverConfigs();
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    // Done reading. Closing here is not tidiness: the recovery below rewrites
    // this very path through QSaveFile, and on Windows a rename cannot replace a
    // file that is still open — the re-anchor would fail silently and every
    // start would have to rediscover again.
    f.close();
    if (!doc.isArray()) {
        // The file is there but unreadable as JSON — a truncated or half-written
        // index (crash / full disk mid-save). An *empty* array is a legitimate
        // "user removed every config" and is left alone; damage is not.
        const QStringList recovered = rediscoverConfigs();
        if (!recovered.isEmpty())
            saveStoredConfigs(recovered); // re-anchor the index so it stops being damaged
        return recovered;
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
    // Write via a temp file + atomic rename (QSaveFile) rather than truncating
    // the live index: a crash or a full disk halfway through used to leave a
    // zero-length configs.json and take the whole server list with it.
    const QString path = storagePath();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    // Index of config paths — keep owner-only to match the configs themselves,
    // and set it on the temp file so the permissions are already in place when
    // the rename publishes it.
    if (!f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        f.cancelWriting();
        return;
    }
    const QByteArray json = QJsonDocument(arr).toJson(QJsonDocument::Indented);
    if (f.write(json) != json.size()) {
        f.cancelWriting();
        return;
    }
    f.commit();
}
