// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QUrl>

#include "BackendConfigShared.h"
#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigStore.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"

bool Backend::importFromClipboard()
{
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        emit errorOccurred(tr("Clipboard is empty"));
        return false;
    }
    if (text.startsWith(QStringLiteral("tt://")) || text.contains(QStringLiteral("tt=")))
        return importDeepLink(text);
    emit errorOccurred(tr("No tt:// link in the clipboard"));
    return false;
}

bool Backend::finalizeImportedConfig(const QString &target, bool hadNoActive)
{
    if (!freetunnel::migrateConfigPassword(target)) {
        QFile::remove(target);
        emit errorOccurred(tr("Could not store the VPN password securely. Install "
                             "gnome-keyring or KWallet, then try again."));
        return false;
    }
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored.prepend(target);
        saveStoredConfigs(stored);
    }
    reloadConfigs();
    if (hadNoActive) {
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
        emit configChanged();
    }
    emit configImported(nameForPath(target));
    return true;
}

bool Backend::importFile(const QString &path)
{
    QString p = path;
    if (p.startsWith(QStringLiteral("file://")))
        p = QUrl(p).toLocalFile();
    if (!QFileInfo::exists(p)) {
        emit errorOccurred(tr("File not found: %1").arg(p));
        return false;
    }
    QString content;
    QString importErr;
    if (!freetunnel::backend_config::readValidatedImportContent(p, &content, &importErr)) {
        if (importErr == QLatin1String("read"))
            emit errorOccurred(tr("Could not read the file"));
        else
            emit errorOccurred(tr("Not a valid TrustTunnel config (missing address or credentials)"));
        return false;
    }
    QString target;
    if (!freetunnel::backend_config::copyImportIntoAppConfigDir(content, p, &target)) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    return finalizeImportedConfig(target, m_activePath.isEmpty());
}

bool Backend::importPreparedDeepLink(const freetunnel::PreparedImport &prepared)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    // The file name comes from the link, so writing it straight would let a link
    // silently replace an existing config of the same name: the row already sits
    // in configs.json (no new entry appears) while its TOML and keychain entry now
    // point at the link author's server. Always take a fresh path, like the file
    // import path does.
    const QString target = freetunnel::uniqueOwnerConfigPath(
            QFileInfo(prepared.fileName).completeBaseName());
    if (!freetunnel::backend_config::writeConfigFile(target, prepared.tomlContent.toUtf8())) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    return finalizeImportedConfig(target, m_activePath.isEmpty());
}

bool Backend::importDeepLink(const QString &link)
{
    QString err;
    auto prepared = freetunnel::prepareDeepLinkImport(link, &err);
    if (!prepared) {
        emit errorOccurred(tr("Link error: %1").arg(err));
        return false;
    }
    // Every deep link is attacker-reachable: a web page can invoke the scheme
    // handler unattended, and an imported config becomes the active one (and the
    // connect target) when the user has none — so "freetunnel://connect" right
    // after would route everything through the link author's server. Confirmation
    // is therefore mandatory for ALL links, not only the ones that also turn off
    // certificate verification.
    emit deepLinkImportConfirmationRequired(
            prepared->skipVerification
                    ? tr("This link disables server certificate verification. "
                         "Only import configs from sources you trust.")
                    : tr("Add a VPN server from this link? All your traffic can be routed "
                         "through it — only import configs from sources you trust."),
            link);
    return false;
}

bool Backend::confirmDeepLinkImport(const QString &link)
{
    QString err;
    auto prepared = freetunnel::prepareDeepLinkImport(link, &err);
    if (!prepared) {
        emit errorOccurred(tr("Link error: %1").arg(err));
        return false;
    }
    return importPreparedDeepLink(*prepared);
}
