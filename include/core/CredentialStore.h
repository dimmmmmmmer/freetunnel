// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

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

// What asking the credential store for an entry came to. "There is nothing under
// that name" and "the store could not be asked" are different answers, and
// callers act on the difference: a config whose auth key could not be read must
// not connect as one that has none, nor be saved over, nor shared without it.
enum class SecretStatus {
    Found,    // the entry is there; StoredSecret::value is what it holds
    NotFound, // the store was asked, and holds nothing under that name
    Error,    // the store could not be asked: a locked keyring, no Secret Service,
              // a denied prompt, a timeout. The entry may well be there.
};

struct StoredSecret {
    SecretStatus status = SecretStatus::NotFound;
    QString value; // Found only; empty otherwise
    // Linux only: the value came from the 0600 file an older build wrote, and the
    // move from it into the OS store did not happen (there was no store to take
    // it). Such a config's secrets live in the file and not in a store, which
    // is what readConfigSecrets() asks about. Never set in builds with
    // FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK, where that file is the store.
    bool fromLegacyFile = false;
    // Error only: what the backend said, for the log (an OSStatus, a Win32 code,
    // a GError, secret-tool's exit code). Never a value, and not for the user.
    QString detail;

    bool found() const { return status == SecretStatus::Found; }
    bool notFound() const { return status == SecretStatus::NotFound; }
    bool failed() const { return status == SecretStatus::Error; }
};

// What each place a Linux lookup can ask answered, for combineLinuxLookup().
struct LinuxLookupSources {
    // Empty: that source is not there to be asked (libsecret is not linked into
    // this build, secret-tool is not installed).
    std::optional<StoredSecret> libsecret;
    std::optional<StoredSecret> secretTool;
    // The file older builds kept passwords in. Read rather than asked, so it only
    // ever answers Found or NotFound.
    StoredSecret legacyFile;
    // A build with FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK: that file is the store.
    bool insecureFallbackBuild = false;
};

// How the Linux sources add up to one answer. Pure, so that every platform tests
// it:
//  - A source that holds the entry wins; the OS ones before the file.
//  - Otherwise libsecret decides when it is linked, and secret-tool when it is
//    not, so a failing secret-tool never turns a clean libsecret "not found" into
//    an error, and a failing libsecret is an error whatever secret-tool says
//    about an item it does not have.
//  - With no OS source to ask at all, a build that has no insecure fallback cannot
//    say whether an entry is there (Error: every shipped build); one that has
//    the fallback is answered by the file (NotFound).
StoredSecret combineLinuxLookup(const LinuxLookupSources &sources);

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
    // The entry the single-instance token is kept in (InstanceControl). Not a
    // config's, and not the other two kinds either.
    static QString keyForInstanceToken();
    /// True when the OS can encrypt credentials at rest (always on macOS/Windows).
    static bool secureStorageAvailable();
    static bool storePassword(const QString &key, const QString &password);
    // Asks the store for the entry under key. Found, NotFound, or Error when the
    // store could not be asked; see SecretStatus. Every Error is logged
    // (freetunnel.credentialstore), without a value.
    static StoredSecret lookup(const QString &key);
    // TRANSITIONAL: what lookup() says with the difference thrown away.
    static QString loadPassword(const QString &key) { return lookup(key).value; }
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
    // Tests only. lookup() answers Error, without asking the OS store, for each
    // key the hook returns true for: a store that cannot be read, such as a locked
    // keyring, for the entries chosen (the password readable and the key not, say).
    // Nothing else changes; FT_TEST_KEYRING keeps its meaning.
    using ReadFailureForTest = std::function<bool(const QString &key)>;
    static void setReadFailureForTest(ReadFailureForTest hook);
    // Tests only. lookup() answers what the hook returns, without asking the OS
    // store, for each key it returns a value for. For an answer that no store on
    // the machine can be made to give, such as a password found only in the legacy
    // file (StoredSecret::fromLegacyFile) on a build that has the insecure fallback.
    using LookupOverrideForTest = std::function<std::optional<StoredSecret>(const QString &key)>;
    static void setLookupOverrideForTest(LookupOverrideForTest hook);
#endif
};

// Which secret the credential store would not take, when a migration failed. Also
// set (Password) when the entry that migration had to read first could not be read.
enum class SecretRefused { None, Password, AuthKey };

// Move the inline password and client random auth key into the credential store
// and strip them from TOML. Both are stored before either is stripped, and the
// file is rewritten only when the store has taken what was in it; a false return
// leaves the file as it was. When the store takes the password and then refuses the
// key, the password goes back to what the store held for this config (deleted when
// it held none), so a config the caller keeps or puts back does not find another
// provider's password at its path. (A file that then cannot be rewritten is a
// different failure, and is not undone here.) What the password's entry held has to
// be read for that, before anything is written: when it cannot be read, nothing is
// touched and the password is the one refused, as a deleted entry could not be put
// back.
// *refused says which of the two the store refused.
bool migrateConfigPassword(const QString &configPath, SecretRefused *refused = nullptr);

// Which of a config's two secrets could not be read from the credential store.
enum class SecretUnreadable { None, Password, AuthKey };

// A config's two secrets as the app uses them, from the one rule for reading them.
struct ConfigSecrets {
    QString password;
    QString authKey;
    // Not None: that secret could not be read, and what is in the other fields
    // must not be used as the config's own (a connect, a link or a save made from
    // it would leave the secret out as if the config had none). Everything but
    // the secret read before the failure is empty.
    SecretUnreadable unreadable = SecretUnreadable::None;
};

struct ConfigToml;
// The file's own copy of a secret wins over the store's, as migrateConfigPassword()
// treats it: it is the newer, and no store is asked for it. Otherwise the store is
// asked, and:
//  - the password: found, or none (""), or unreadable (stops there);
//  - the auth key: found, or none (""), or, when the store could not be asked,
//    unreadable if the store is where this config keeps its secrets. That is so
//    when the file holds no password of its own and the password did not come from
//    the legacy file (ConfigSecrets, StoredSecret::fromLegacyFile): every path that
//    puts a secret in the store puts both or neither. Otherwise the config keeps
//    its secrets elsewhere, was never given a key there, and is read as one
//    without (a keyless config on a Linux machine with no keyring still connects).
// c is the config's parsed file.
ConfigSecrets readConfigSecrets(const QString &configPath, const ConfigToml &c);

// A connect config for the helper, or why there is none.
struct ConnectConfig {
    enum class Problem {
        None,
        PasswordUnreadable, // the store could not be asked for the password
        NoPassword,         // the config has none
        AuthKeyUnreadable,  // the store could not be asked for the key the config has
    };
    QString toml; // empty unless problem is None
    Problem problem = Problem::None;
};

// Build a helper-readable config TOML with the password and the client random
// auth key injected (in-memory only). The password and key are read the same way
// as everywhere (readConfigSecrets), and when either is not there to be read the
// result is no TOML at all: one without a secret the config has would connect
// without it.
// logLevel sets the core's verbosity ("warn" by default; "info" for debug logs).
ConnectConfig buildConnectConfig(const QString &configPath,
                                 const QString &logLevel = QStringLiteral("warn"));
// TRANSITIONAL: the connect config without the reason there is none.
inline QString buildConnectConfigToml(const QString &configPath,
                                      const QString &logLevel = QStringLiteral("warn"))
{
    return buildConnectConfig(configPath, logLevel).toml;
}
// Delete any leftover materialized configs (crash leftovers from versions that
// wrote password-injected temp files) — call at startup.
void sweepStaleMaterializedConfigs();
// Called at startup. Today that is the materialized-config sweep above and nothing
// more: a legacy per-config password file moves into the store when lookup()
// next reads it, and the legacy instance-auth file is InstanceControl's to sweep.
void sweepLegacyPlaintextStorage();
// Takes the TrustTunnel 1.3 subscription address out of each of these configs that
// still holds one (see withoutSubscription), and whatever is not a certificate out
// of its certificate (see withCertificatesOnly), when the config is a copy in the
// app's own folder. Called at startup, with the stored configs.
void sweepStoredSubscriptionAddresses(const QStringList &configPaths);

} // namespace freetunnel
