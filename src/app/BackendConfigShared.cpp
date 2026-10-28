// cppcheck-suppress-file missingIncludeSystem
#include "BackendConfigShared.h"

#include <QCoreApplication>
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
#include "vpn/trusttunnel_tls_profiles.h"

namespace freetunnel::backend_config {

bool validateAddressList(const QString &addresses)
{
    const QStringList addrs = addresses.split(QLatin1Char(','), Qt::SkipEmptyParts);
    return std::all_of(addrs.cbegin(), addrs.cend(),
                       [](const QString &raw) { return freetunnel::isValidEndpointAddress(raw); });
}

bool validateDnsList(const QString &dns)
{
    static const QRegularExpression dnsScheme(
        QStringLiteral("^(tls|https|quic|h3|sdns|udp|tcp)://"), QRegularExpression::CaseInsensitiveOption);
    // Split as the config is written (see splitDnsList), so what passes here is
    // what the core gets.
    const QStringList dnsList = freetunnel::splitDnsList(dns);
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

void restorePassword(const QString &target, const QString &previous)
{
    const QString key = freetunnel::CredentialStore::keyForConfigPath(target);
    if (previous.isEmpty())
        freetunnel::CredentialStore::deletePassword(key);
    else
        freetunnel::CredentialStore::storePassword(key, previous);
}

// The same for the auth key, which a save touched only when it changed anything
// (see storeConfigAuthKey), so a save that did not is not put back either.
void restoreAuthKey(const QString &target, const QString &previous)
{
    const QString key = freetunnel::CredentialStore::keyForConfigAuthKey(target);
    if (previous.isEmpty())
        freetunnel::CredentialStore::deletePassword(key);
    else
        freetunnel::CredentialStore::storePassword(key, previous);
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

bool storeConfigAuthKey(const QString &target, const QString &authKey, const QString &previousAuthKey)
{
    const QString key = freetunnel::CredentialStore::keyForConfigAuthKey(target);
    if (authKey.isEmpty()) {
        // A config without a key has no entry to touch, and costs the store
        // nothing. One that had a key and has none now must lose it: left in the
        // store, it would be sent to whatever server the config names next.
        if (previousAuthKey.isEmpty())
            return true;
        freetunnel::CredentialStore::deletePassword(key);
        return freetunnel::CredentialStore::loadPassword(key).isEmpty();
    }
    freetunnel::CredentialStore::storePassword(key, authKey);
    // The value, not just "something is there", as for the password.
    return freetunnel::CredentialStore::loadPassword(key) == authKey;
}

void dropLeftoverAuthKey(const QString &target)
{
    freetunnel::CredentialStore::deletePassword(freetunnel::CredentialStore::keyForConfigAuthKey(target));
}

bool saveConfigWithPassword(const QString &target, const QByteArray &body, const QString &password,
                            const QString &previousPassword, const QString &authKey,
                            const QString &previousAuthKey, QString *errOut)
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
    if (!storeConfigAuthKey(target, authKey, previousAuthKey)) {
        // Both secrets or neither: the password is already in, and the config on
        // disk is the old one, which goes with the old password.
        restorePassword(target, previousPassword);
        restoreAuthKey(target, previousAuthKey);
        file.cancelWriting();
        if (errOut)
            *errOut = QStringLiteral("authkey");
        return false;
    }
    if (!file.commit()) {
        // The other way round from the case above, and as bad: the store holds the
        // new password while the file is still the old config, perhaps with
        // another username, and every connect fails until the next good save.
        // Put the store back to what the file on disk goes with.
        restorePassword(target, previousPassword);
        if (!authKey.isEmpty() || !previousAuthKey.isEmpty())
            restoreAuthKey(target, previousAuthKey);
        if (errOut)
            *errOut = QStringLiteral("write");
        return false;
    }
    return true;
}

QString authKeyNotStoredMessage()
{
    // One wording for every store: the cause is the same as for the password, and
    // said there in the terms of each, but a key has no length limit worth
    // naming, and this is the rarer message.
    return QCoreApplication::translate("Backend",
                                       "Could not store the auth key in the system's credential store. If it "
                                       "asked for access or to be unlocked, allow that and try again.");
}

QString passwordNotStoredMessage(bool storeIsThere)
{
    // Said in the terms of the store that refused. This used to be the Linux
    // advice everywhere, so a Windows or macOS user, most of them, was told to
    // install gnome-keyring. On macOS the cause is usually a Keychain prompt
    // that was denied or closed, and the next attempt asks again. On Windows the
    // one refusal known to happen is a password over the 2560 bytes Credential
    // Manager holds (CRED_MAX_CREDENTIAL_BLOB_SIZE), which another try does not
    // change. On Linux a keyring that is there and still refused was locked, its
    // unlock prompt dismissed; telling that user to install one was wrong.
#if defined(Q_OS_MACOS)
    Q_UNUSED(storeIsThere);
    return QCoreApplication::translate("Backend",
                                       "Could not store the VPN password in the Keychain. If macOS "
                                       "asked for access to the Keychain, allow it and try again.");
#elif defined(Q_OS_WIN)
    Q_UNUSED(storeIsThere);
    return QCoreApplication::translate("Backend",
                                       "Could not store the VPN password in Windows Credential Manager. "
                                       "It holds at most 2560 bytes, so a longer password cannot be "
                                       "saved.");
#else
    if (storeIsThere) {
        return QCoreApplication::translate("Backend",
                                           "Could not store the VPN password in your keyring. If it "
                                           "asked to be unlocked, unlock it and try again.");
    }
    return QCoreApplication::translate("Backend", "Could not store the VPN password securely. Install "
                                                  "gnome-keyring or KWallet, then try again.");
#endif
}

QString passwordNotReadMessage(bool storeIsThere)
{
    // The same four cases as passwordNotStoredMessage(), for a read. A store that
    // is there and could not be read from was locked (Linux), asked the user and
    // was refused (macOS), or was not reachable (Windows).
#if defined(Q_OS_MACOS)
    Q_UNUSED(storeIsThere);
    return QCoreApplication::translate("Backend",
                                       "Could not read the VPN password from the Keychain. If macOS "
                                       "asked for access to the Keychain, allow it and try again.");
#elif defined(Q_OS_WIN)
    Q_UNUSED(storeIsThere);
    return QCoreApplication::translate("Backend",
                                       "Could not read the VPN password from Windows Credential "
                                       "Manager. Try again.");
#else
    if (storeIsThere) {
        return QCoreApplication::translate("Backend",
                                           "Could not read the VPN password from your keyring. If it "
                                           "asked to be unlocked, unlock it and try again.");
    }
    return QCoreApplication::translate("Backend",
                                       "Could not read the VPN password: secure credential storage is "
                                       "unavailable. Install gnome-keyring or KWallet, then try again.");
#endif
}

QString authKeyNotReadMessage()
{
    // One wording for every store, as for authKeyNotStoredMessage().
    return QCoreApplication::translate("Backend",
                                       "Could not read the auth key from the system's credential store. If "
                                       "it asked for access or to be unlocked, allow that and try again.");
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
        // The endpoint's own export has no [endpoint]: its keys are at the top,
        // to be pasted under one. It is imported as a link is, so its text is
        // handed back to be asked about (importEndpointExport) rather than refused.
        if (errOut)
            *errOut = freetunnel::parseEndpointExport(content) ? QStringLiteral("export") : QStringLiteral("invalid");
        if (contentOut)
            *contentOut = content;
        return false;
    }
    // The core drops a key it cannot decode and connects without it, and a server
    // that checks one then turns the client away with nothing to say why, so a
    // key that is not whole bytes of hex is refused here. The empty key the
    // endpoint's export always writes is no key.
    if (!freetunnel::isValidClientRandomAuthKey(freetunnel::endpointClientRandomAuthKey(content))) {
        if (errOut)
            *errOut = QStringLiteral("authkey");
        return false;
    }
    // Core 1.3 refuses the whole config for a tls_profile it has no name for, and
    // says so only in a log the user does not see; a file with one would import
    // and then never connect. The content is handed back for the message to name it.
    const QString profile = freetunnel::endpointTlsProfile(content);
    if (!profile.isEmpty() && !freetunnel::isKnownTlsProfile(profile.toStdString())) {
        if (errOut)
            *errOut = QStringLiteral("tlsprofile");
        if (contentOut)
            *contentOut = content;
        return false;
    }
    // A certificate with no certificate in it is refused, as a link's is: the
    // file meant to pin the server, and imported without the pin, a self-signed
    // server would fail at the handshake with nothing to say why.
    if (!c.certificate.trimmed().isEmpty() && freetunnel::certificatesDer(c.certificate).isEmpty()) {
        if (errOut)
            *errOut = QStringLiteral("certificate");
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
    // Clipped: the source's own name fits its directory, not always with the
    // "-2" or the staging suffix a copy here can need.
    const QString stem = freetunnel::sanitizeConfigBaseName(
            freetunnel::clippedConfigName(QFileInfo(sourcePath).completeBaseName()));
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
