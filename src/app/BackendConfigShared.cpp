// cppcheck-suppress-file missingIncludeSystem
#include "BackendConfigShared.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"

namespace freetunnel::backend_config {

bool validateAddressList(const QString &addresses)
{
    const QStringList addrs = addresses.split(QLatin1Char(','), Qt::SkipEmptyParts);
    return std::all_of(addrs.cbegin(), addrs.cend(), [](const QString &raw) {
        const QString a = raw.trimmed();
        const int colon = a.lastIndexOf(QLatin1Char(':'));
        bool portOk = false;
        const int port = colon >= 0 ? a.mid(colon + 1).toInt(&portOk) : 0;
        return colon > 0 && portOk && port >= 1 && port <= 65535;
    });
}

bool validateDnsList(const QString &dns)
{
    static const QRegularExpression dnsScheme(
        QStringLiteral("^(tls|https|quic|h3|sdns|udp|tcp)://"), QRegularExpression::CaseInsensitiveOption);
    const QStringList dnsList = dns.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                         Qt::SkipEmptyParts);
    return std::all_of(dnsList.cbegin(), dnsList.cend(), [&](const QString &raw) {
        const QString d = raw.trimmed();
        if (dnsScheme.match(d).hasMatch())
            return true;
        QString host = d;
        const int colon = host.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && !host.contains(QLatin1Char('[')) && host.count(QLatin1Char(':')) == 1)
            host = host.left(colon);
        return !QHostAddress(host).isNull();
    });
}

namespace {

// Config files are password-bearing — a created config until storeConfigPassword()
// runs, a deep-link import until migrateConfigPassword() moves the plaintext
// `password = "…"` line into the keychain. Creating them with default permissions
// and chmod'ing afterwards left a world-readable window, so set the mode on the
// (still unnamed) QSaveFile temp before a single byte is written, and treat a
// failure to do so as a failed write rather than ignoring it.
bool openOwnerOnly(QSaveFile *file)
{
    if (!file->open(QIODevice::WriteOnly))
        return false;
    if (!file->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        file->cancelWriting();
        return false;
    }
    return true;
}

bool writeAll(QSaveFile *file, const QByteArray &body)
{
    if (file->write(body) != body.size()) {
        file->cancelWriting();
        return false;
    }
    return true;
}

} // namespace

bool writeConfigFile(const QString &target, const QByteArray &body)
{
    QSaveFile file(target);
    if (!openOwnerOnly(&file) || !writeAll(&file, body))
        return false;
    return file.commit();
}

bool storeConfigPassword(const QString &target, const QString &password)
{
    const QString key = freetunnel::CredentialStore::keyForConfigPath(target);
    freetunnel::CredentialStore::storePassword(key, password);
    if (password.isEmpty())
        return true;
    // Compare the value, not just "something is there". When EDITING a config in
    // place the key already holds the PREVIOUS password, so a non-empty readback
    // proves nothing: a store that rejected the write would look like success and
    // the config would be committed with a stale credential, failing to connect
    // with no indication why.
    return freetunnel::CredentialStore::loadPassword(key) == password;
}

bool saveConfigWithPassword(const QString &target, const QByteArray &body, const QString &password,
                            QString *errOut)
{
    // Editing a config saves over the ORIGINAL file (ownerConfigPathForSave reuses
    // oldPath when the name is unchanged). The old order — truncate the file, then
    // delete it if the credential store refused — destroyed the user's config
    // outright whenever a Linux keyring was locked, leaving a ghost row in
    // configs.json. Stage the body instead and only replace the destination once
    // the password is verifiably in the store, so a credential failure can never
    // reach existing data on disk.
    QSaveFile file(target);
    if (!openOwnerOnly(&file) || !writeAll(&file, body)) {
        if (errOut)
            *errOut = QStringLiteral("write");
        return false;
    }
    if (!storeConfigPassword(target, password)) {
        file.cancelWriting(); // the config already on disk stays exactly as it was
        if (errOut)
            *errOut = QStringLiteral("password");
        return false;
    }
    if (!file.commit()) {
        if (errOut)
            *errOut = QStringLiteral("write");
        return false;
    }
    return true;
}

void updateStoredConfigList(QStringList &stored, const QString &oldPath, const QString &target)
{
    const int storedIdx = oldPath.isEmpty() ? -1 : stored.indexOf(oldPath);
    if (storedIdx >= 0) {
        stored[storedIdx] = target;
        stored.removeDuplicates();
    } else if (!stored.contains(target)) {
        stored.prepend(target);
    }
}

bool readValidatedImportContent(const QString &path, QString *contentOut, QString *errOut)
{
    QFile vf(path);
    if (!vf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errOut)
            *errOut = QStringLiteral("read");
        return false;
    }
    const QString content = QString::fromUtf8(vf.readAll());
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(content);
    if (c.addresses.trimmed().isEmpty() || c.username.trimmed().isEmpty()) {
        if (errOut)
            *errOut = QStringLiteral("invalid");
        return false;
    }
    if (contentOut)
        *contentOut = content;
    return true;
}

bool copyImportIntoAppConfigDir(const QString &content, const QString &sourcePath, QString *targetOut)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString stem = freetunnel::sanitizeConfigBaseName(QFileInfo(sourcePath).completeBaseName());
    QString target = freetunnel::uniqueOwnerConfigPath(stem);
    if (QFileInfo(target).absoluteFilePath() == QFileInfo(sourcePath).absoluteFilePath())
        target = freetunnel::uniqueOwnerConfigPath(stem + QStringLiteral("-copy"));
    if (!writeConfigFile(target, content.toUtf8()))
        return false;
    if (targetOut)
        *targetOut = target;
    return true;
}

} // namespace freetunnel::backend_config
