// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>

namespace freetunnel {

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
// Migrate/remove legacy plaintext credential files when secure storage is available.
void sweepLegacyPlaintextStorage();

} // namespace freetunnel
