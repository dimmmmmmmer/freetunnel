// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

// Name the OS credential stores are keyed by (Keychain service, libsecret schema,
// Credential Manager target prefix). Test builds resolve to a separate name: it is
// the only isolation the OS stores offer — QStandardPaths test mode and
// XDG_CONFIG_HOME redirect files only, so without this a test run reads, rewrites
// and deletes the real app's secrets.
QString credentialServiceName();

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
// Whether the session bus has a Secret Service on it, or can start one: the part
// of "can libsecret store a password" that linking libsecret in does not answer.
// Asks the bus only, so it looks nothing up and cannot raise an unlock prompt.
bool secretServiceOnSessionBus();
#endif

// Stores VPN config passwords outside the on-disk TOML (macOS Keychain, Windows
// Credential Manager, Linux libsecret / Secret Service). Plaintext file fallback is
// disabled for new passwords — see secureStorageAvailable().
class CredentialStore {
public:
    static QString keyForConfigPath(const QString &absoluteConfigPath);
    /// True when the OS can encrypt credentials at rest (always on macOS/Windows).
    static bool secureStorageAvailable();
    static bool storePassword(const QString &key, const QString &password);
    static QString loadPassword(const QString &key);
    static bool deletePassword(const QString &key);
};

// Move inline passwords into the credential store and strip them from TOML.
bool migrateConfigPassword(const QString &configPath);

// Build a helper-readable config TOML with password injected (in-memory only).
// logLevel sets the core's verbosity ("warn" by default; "info" for debug logs).
QString buildConnectConfigToml(const QString &configPath,
                               const QString &logLevel = QStringLiteral("warn"));
// Delete any leftover materialized configs (crash leftovers from versions that
// wrote password-injected temp files) — call at startup.
void sweepStaleMaterializedConfigs();
// Called at startup. Today that is the materialized-config sweep above and nothing
// more: a legacy per-config password file moves into the store when loadPassword()
// next reads it, and the legacy instance-auth file is InstanceControl's to sweep.
void sweepLegacyPlaintextStorage();

} // namespace freetunnel
