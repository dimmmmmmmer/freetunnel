// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace freetunnel::backend_config {

bool validateAddressList(const QString &addresses);
bool validateDnsList(const QString &dns);
// Both write owner-only and atomically (staged, then committed over the target).
bool writeConfigFile(const QString &target, const QByteArray &body);
bool storeConfigPassword(const QString &target, const QString &password);
// The client random auth key, the config's second secret, in the credential store
// under target. An empty key removes the entry, but only when previousAuthKey (what
// the store held for target before) says there is one, so a config without a key
// never touches the store. True once the store holds exactly authKey.
bool storeConfigAuthKey(const QString &target, const QString &authKey, const QString &previousAuthKey);
// For a config being made at a path that had no file before, and brings no key.
// Credentials are keyed by path, and a name is reused: an entry for this path is a
// leftover of an earlier config of that name (a removal the keyring refused, one
// made under 1.2.x, which knows nothing of these entries), and the new config
// would send its key to whatever server it names. Costs the store nothing when
// there is no entry.
void dropLeftoverAuthKey(const QString &target);
// Body + password + auth key as one all-or-nothing step: the destination is
// untouched unless the credential store accepted both, and when the file then
// cannot be written the store is put back to previousPassword and previousAuthKey,
// what it held for target before (empty: nothing). *errOut: "write"|"password"|"authkey".
bool saveConfigWithPassword(const QString &target, const QByteArray &body, const QString &password,
                            const QString &previousPassword, const QString &authKey,
                            const QString &previousAuthKey, QString *errOut);
// What to tell the user when that step failed on "password": the credential
// store refused it. Worded for the store this platform has; on Linux, for whether
// a keyring is there at all (@p storeIsThere) or is there and refused.
QString passwordNotStoredMessage(bool storeIsThere);
// The same when it was the auth key the store refused ("authkey").
QString authKeyNotStoredMessage();
// What to tell the user when the store could not be asked for the password (a
// connect, the editor, a link or an export that needs it), worded as
// passwordNotStoredMessage() is, for the store this platform has.
QString passwordNotReadMessage(bool storeIsThere);
// And when it was the auth key it could not be asked for.
QString authKeyNotReadMessage();
void updateStoredConfigList(QStringList &stored, const QString &oldPath, const QString &target);
// *errOut: "read"|"invalid"|"authkey" (a client_random_auth_key that is not whole bytes of hex)|
// "tlsprofile" (an [endpoint] tls_profile the core has no name for; *contentOut is set)|
// "certificate" (an [endpoint] certificate with no certificate in it, see certificatesOnly)|
// "export" (the TOML file an endpoint exports, see parseEndpointExport; *contentOut is set).
bool readValidatedImportContent(const QString &path, QString *contentOut, QString *errOut);
bool copyImportIntoAppConfigDir(const QString &content, const QString &sourcePath, QString *targetOut);

} // namespace freetunnel::backend_config
