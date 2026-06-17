#pragma once

#include <QString>

namespace freetunnel {

// Stores VPN config passwords outside the on-disk TOML (macOS Keychain, Windows
// Credential Manager, Linux owner-only credential file).
class CredentialStore {
public:
    static QString keyForConfigPath(const QString &absoluteConfigPath);
    static bool storePassword(const QString &key, const QString &password);
    static QString loadPassword(const QString &key);
    static bool deletePassword(const QString &key);
};

// Move inline passwords into the credential store and strip them from TOML.
bool migrateConfigPassword(const QString &configPath);

// Build a helper-readable config path (temp file with password injected when needed).
QString materializeConfigForConnect(const QString &configPath);
void removeMaterializedConfig(const QString &materializedPath);
// Delete any leftover materialized configs (e.g. from a crash) — call at startup.
void sweepStaleMaterializedConfigs();

} // namespace freetunnel
