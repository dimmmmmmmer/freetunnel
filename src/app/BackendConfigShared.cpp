// cppcheck-suppress-file missingIncludeSystem
#include "BackendConfigShared.h"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
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

bool writeConfigFile(const QString &target, const QByteArray &body)
{
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(body);
    file.close();
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool storeConfigPassword(const QString &target, const QString &password)
{
    freetunnel::CredentialStore::storePassword(freetunnel::CredentialStore::keyForConfigPath(target),
                                               password);
    if (!password.isEmpty()
        && freetunnel::CredentialStore::loadPassword(
                   freetunnel::CredentialStore::keyForConfigPath(target))
                   .isEmpty()) {
        QFile::remove(target);
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
