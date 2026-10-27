// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>
#include <QStringList>

#if defined(FT_ENABLE_TEST_HOOKS)
#include <functional>
#endif

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
    // The entry a config's client random auth key (TrustTunnel 1.3) is kept in: a
    // second secret beside the password, under the same path with a prefix that
    // no absolute path starts with, so it is never another config's password
    // entry. The store's password calls below hold any secret under any key.
    // Written only for a config that has a key, so a config without one costs the
    // store nothing.
    static QString keyForConfigAuthKey(const QString &absoluteConfigPath);
    // The entry a TrustTunnel subscription's address is kept in, by the source id
    // its record names (SubscriptionRecord). Keyed by the subscription, not by its
    // config, so a rename moves nothing in the store; and never an absolute path,
    // so never a config's own entry.
    static QString keyForSubscription(const QString &sourceId);
    /// True when the OS can encrypt credentials at rest (always on macOS/Windows).
    static bool secureStorageAvailable();
    static bool storePassword(const QString &key, const QString &password);
    static QString loadPassword(const QString &key);
    static bool deletePassword(const QString &key);

#if defined(FT_ENABLE_TEST_HOOKS)
    // Tests only. Called first by each of the three calls above, on the thread
    // that makes it, with what the call is ("store", "load" or "delete") and its
    // key. On the GUI thread the store's own event loop turns while the OS store
    // answers, so what the hook lets happen there is what can happen meanwhile,
    // such as the helper reporting a state; on a worker it can hold the call, as a
    // Keychain prompt does.
    using CallHookForTest = std::function<void(const char *call, const QString &key)>;
    static void setCallHookForTest(CallHookForTest hook);
#endif
};

// Move the inline password and client random auth key into the credential store
// and strip them from TOML. Both are stored before either is stripped, and the
// file is rewritten only when the store has taken what was in it; a false return
// leaves the file as it was.
bool migrateConfigPassword(const QString &configPath);

// Build a helper-readable config TOML with the password and the client random
// auth key injected (in-memory only).
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
// Takes the TrustTunnel 1.3 subscription address out of each of these configs that
// still holds one (see withoutSubscription), and whatever is not a certificate out
// of its certificate (see withCertificatesOnly), when the config is a copy in the
// app's own folder. Called at startup, with the stored configs.
void sweepStoredSubscriptionAddresses(const QStringList &configPaths);

} // namespace freetunnel
