// cppcheck-suppress-file missingIncludeSystem
#include "CredentialStore.h"

#include "ConfigToml.h"
#include "CredentialStoreLibsecret.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QThread>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace freetunnel {

QString credentialServiceName()
{
#if defined(FT_ENABLE_TEST_HOOKS)
    static const QString name = [] {
        const QByteArray override = qgetenv("FT_TEST_CREDENTIAL_SERVICE");
        if (!override.isEmpty())
            return QString::fromUtf8(override);
        // Unique per PROCESS, not just per build. An item in the OS store carries
        // an ACL bound to the binary that created it, and test binaries are
        // rebuilt constantly — so touching an entry a previous build left behind
        // makes macOS raise an authorization dialog and the run hangs until a
        // human answers it. A per-process service can never name an older item.
        return QStringLiteral("com.freetunnel.app.test.%1")
                .arg(QCoreApplication::applicationPid());
    }();
    return name;
#else
    return QStringLiteral("com.freetunnel.app");
#endif
}

namespace {

#if defined(Q_OS_WIN)
QString winTargetPrefix()
{
#if defined(FT_ENABLE_TEST_HOOKS)
    return QStringLiteral("FreeTunnelTest/");
#else
    return QStringLiteral("FreeTunnel/");
#endif
}
#endif

#if defined(Q_OS_MACOS)
CFStringRef macServiceName()
{
    // Built once from credentialServiceName() and never released: a
    // process-lifetime constant the Security framework keeps referencing.
    static const CFStringRef name = credentialServiceName().toCFString();
    return name;
}

CFDictionaryRef macLookupQuery(CFStringRef account)
{
    const void *keys[] = { kSecClass, kSecAttrService, kSecAttrAccount };
    const void *values[] = { kSecClassGenericPassword, macServiceName(), account };
    return CFDictionaryCreate(kCFAllocatorDefault, keys, values, 3,
                              &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
}

bool macStorePassword(CFStringRef account, CFDataRef secret)
{
    CFDictionaryRef lookup = macLookupQuery(account);
    SecItemDelete(lookup);

    const void *keys[] = { kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData };
    const void *values[] = { kSecClassGenericPassword, macServiceName(), account, secret };
    CFDictionaryRef add = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 4,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    const OSStatus st = SecItemAdd(add, nullptr);
    CFRelease(add);
    CFRelease(lookup);
    return st == errSecSuccess;
}

QString macLoadPassword(CFStringRef account)
{
    CFDictionaryRef base = macLookupQuery(account);
    CFMutableDictionaryRef query =
            CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, base);
    CFRelease(base);
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);

    CFDataRef data = nullptr;
    const OSStatus st = SecItemCopyMatching(query, reinterpret_cast<CFTypeRef *>(&data));
    CFRelease(query);
    if (st != errSecSuccess || !data)
        return QString();

    const auto *bytes = CFDataGetBytePtr(data);
    const CFIndex len = CFDataGetLength(data);
    const QString out = QString::fromUtf8(reinterpret_cast<const char *>(bytes),
                                          static_cast<int>(len));
    CFRelease(data);
    return out;
}

bool macDeletePassword(CFStringRef account)
{
    CFDictionaryRef lookup = macLookupQuery(account);
    const OSStatus st = SecItemDelete(lookup);
    CFRelease(lookup);
    return st == errSecSuccess || st == errSecItemNotFound;
}
#endif

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
// File-backed fallback (Linux/other): only these helpers need a path on disk.

QString credentialDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString dir = base + QStringLiteral("/credentials");
    QDir().mkpath(dir);
    // Owner-only: the files inside are already 0600, but a 0700 dir also keeps
    // other local users from listing/stat-ing the per-config credential entries.
    // The parent gets the same treatment — mkpath() would otherwise create the
    // app config dir 0755 and leave the config .toml listing world-readable.
    QFile::setPermissions(base, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
    return dir;
}

QString filePathForKey(const QString &key)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(credentialDir()).filePath(QString::fromLatin1(hash));
}

#if defined(FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK)
// Open @p path for writing with 0600 already in place. QFile has no mode
// argument, and chmod-after-write leaves a window in which another local user
// can open the file while it still holds a plaintext password; on POSIX we hand
// QFile a descriptor that was created 0600, and O_NOFOLLOW so a symlink planted
// under the path can't redirect the write either.
bool openOwnerOnlyForWrite(QFile &f, const QString &path)
{
#if defined(Q_OS_UNIX)
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    if (!f.open(fd, QIODevice::WriteOnly | QIODevice::Truncate, QFile::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
#else
    // Windows: no mode bits on open — the file inherits the owner-only ACL of
    // the config directory, and we tighten it before anything is written.
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
#endif
    // An existing file keeps its old mode through O_CREAT, so still assert it —
    // and don't pretend the write is safe when we couldn't.
    if (!QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        f.close();
        return false;
    }
    return true;
}

bool storePasswordFile(const QString &key, const QString &password)
{
    const QString path = filePathForKey(key);
    QFile f(path);
    if (!openOwnerOnlyForWrite(f, path))
        return false;
    const QByteArray secret = password.toUtf8();
    const bool ok = f.write(secret) == secret.size();
    f.close();
    if (!ok)
        QFile::remove(path); // never leave a half-written password behind
    return ok;
}
#endif

QString loadPasswordFile(const QString &key)
{
    QFile f(filePathForKey(key));
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll());
}

bool deletePasswordFile(const QString &key)
{
    return QFile::remove(filePathForKey(key));
}

// Preferred Linux store: the desktop Secret Service (GNOME Keyring, KWallet's
// Secret Service bridge, …) via secret-tool. Falls back to the 0600 file when
// secret-tool or a running service isn't available.
QString secretToolPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("secret-tool"));
}

bool secretServiceStore(const QString &key, const QString &password)
{
    const QString tool = secretToolPath();
    if (tool.isEmpty())
        return false;
    QProcess p;
    p.start(tool, {QStringLiteral("store"), QStringLiteral("--label=FreeTunnel"),
                   QStringLiteral("service"), credentialServiceName(),
                   QStringLiteral("account"), key});
    if (!p.waitForStarted(3000))
        return false;
    p.write(password.toUtf8());
    p.closeWriteChannel();
    return p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

QString secretServiceLookup(const QString &key, bool *ok)
{
    *ok = false;
    const QString tool = secretToolPath();
    if (tool.isEmpty())
        return QString();
    QProcess p;
    p.start(tool, {QStringLiteral("lookup"), QStringLiteral("service"),
                   credentialServiceName(), QStringLiteral("account"), key});
    if (!p.waitForFinished(5000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return QString();
    *ok = true;
    return QString::fromUtf8(p.readAllStandardOutput()); // secret-tool emits no trailing newline
}

bool secretServiceClear(const QString &key)
{
    const QString tool = secretToolPath();
    if (tool.isEmpty())
        return false;
    QProcess p;
    p.start(tool, {QStringLiteral("clear"), QStringLiteral("service"),
                   credentialServiceName(), QStringLiteral("account"), key});
    return p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

bool secretServiceAvailable()
{
    const QString tool = secretToolPath();
    if (tool.isEmpty())
        return false;
    QProcess p;
    p.start(tool, {QStringLiteral("lookup"), QStringLiteral("service"),
                   credentialServiceName(), QStringLiteral("account"),
                   QStringLiteral("__freetunnel_probe__")});
    if (!p.waitForStarted(2000) || !p.waitForFinished(3000)
        || p.exitStatus() != QProcess::NormalExit)
        return false;
    // Exit 0 means the probe item was found, so the service clearly answered.
    if (p.exitCode() == 0)
        return true;
    // Exit 1 is ambiguous: it is also what a *working* service returns for "no
    // such item". With no session bus (headless, ssh, no keyring daemon)
    // secret-tool still starts and exits 1 — but it complains on stderr first,
    // while a plain "not found" prints nothing at all. Treating the exit code
    // alone as success reported storage that isn't there, which suppressed the
    // "install gnome-keyring/KWallet" warning and let the legacy plaintext
    // migration believe it had somewhere better to put the password.
    return p.readAllStandardError().trimmed().isEmpty();
}

static QString migrateLegacyFilePassword(const QString &key, const QString &fromFile)
{
    if (fromFile.isEmpty() || !CredentialStore::secureStorageAvailable())
        return fromFile;
#if defined(FT_HAVE_LIBSECRET)
    if (libsecretStore(key, fromFile)) {
        deletePasswordFile(key);
        return fromFile;
    }
#endif
    if (secretServiceStore(key, fromFile)) {
        deletePasswordFile(key);
        return fromFile;
    }
    return fromFile;
}

static QString loadPasswordLinux(const QString &key)
{
    bool ok = false;
#if defined(FT_HAVE_LIBSECRET)
    const QString fromLibsecret = libsecretLookup(key, &ok);
    if (ok && !fromLibsecret.isEmpty())
        return fromLibsecret;
    ok = false;
#endif
    const QString fromService = secretServiceLookup(key, &ok);
    if (ok && !fromService.isEmpty())
        return fromService;
    return migrateLegacyFilePassword(key, loadPasswordFile(key));
}

#endif

} // namespace

QString CredentialStore::keyForConfigPath(const QString &absoluteConfigPath)
{
    return QFileInfo(absoluteConfigPath).absoluteFilePath();
}

namespace {

// Every call below talks to the OS credential store, and every one of them
// BLOCKS: on macOS the request goes to securityd and does not return while the
// system is asking the user whether this build may touch the item; on Linux the
// Secret Service probe shells out to secret-tool and waits up to five seconds.
// Called straight from the GUI thread — which is what opening the edit form,
// saving a config, copying a config link or exporting one used to do — that
// freezes the whole window, spinner and all.
//
// So run it on a worker and keep our own event loop turning while we wait. User
// input is excluded, so the app stays painted and responsive-looking but nothing
// can re-enter the operation that is already in flight. Off the GUI thread there
// is nothing to keep alive, so the call is made directly.
// Depth guard for the loop below. Only ever touched on the GUI thread, and only
// between the check and the reset, so a plain bool is enough.
inline bool &uiCallInFlight()
{
    static bool inFlight = false;
    return inFlight;
}

template <typename Fn>
auto withoutFreezingTheUi(Fn &&fn) -> decltype(fn())
{
    using Result = decltype(fn());
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread())
        return fn();
    // Already inside the loop below, re-entered from a queued call it dispatched.
    // Run directly rather than stacking a second loop on top of the first: the
    // window is already in its waiting state, so there is no responsiveness left
    // to preserve, and nesting depth stays bounded at one no matter what the
    // dispatched code does.
    if (uiCallInFlight())
        return fn();

    uiCallInFlight() = true;
    const auto reset = qScopeGuard([] { uiCallInFlight() = false; });

    Result result{};
    QEventLoop loop;
    QThread *worker = QThread::create([&result, &fn]() { result = fn(); });
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    worker->wait();
    delete worker;
    return result;
}

} // namespace

static bool doSecureStorageAvailable()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return true;
#else
#if defined(FT_HAVE_LIBSECRET)
    if (secretServiceAvailable())
        return true;
    // libsecret works when a session D-Bus is present even if secret-tool is absent.
    return qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS");
#endif
    return secretServiceAvailable();
#endif
}

static bool doStorePassword(const QString &key, const QString &password)
{
    if (key.isEmpty())
        return false;

#if defined(Q_OS_MACOS)
    const QByteArray secretBytes = password.toUtf8();
    CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
                                                    key.toUtf8().constData(),
                                                    kCFStringEncodingUTF8);
    CFDataRef secret = CFDataCreate(kCFAllocatorDefault,
                                     reinterpret_cast<const UInt8 *>(secretBytes.constData()),
                                     secretBytes.size());
    const bool ok = macStorePassword(account, secret);
    CFRelease(secret);
    CFRelease(account);
    return ok;
#elif defined(Q_OS_WIN)
    const std::wstring target = (winTargetPrefix() + key).toStdWString();
    const QByteArray secret = password.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = static_cast<DWORD>(secret.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(L"FreeTunnel");
    return CredWriteW(&cred, 0) != FALSE;
#else
#if defined(FT_HAVE_LIBSECRET)
    if (libsecretStore(key, password)) {
        deletePasswordFile(key);
        return true;
    }
#endif
    if (secretServiceStore(key, password)) {
        deletePasswordFile(key); // don't leave a plaintext-ish copy on disk
        return true;
    }
#if defined(FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK)
    qWarning("CredentialStore: no Secret Service — storing password as plaintext "
             "(FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK)");
    return storePasswordFile(key, password);
#endif
    qWarning("CredentialStore: no Secret Service available (install gnome-keyring "
             "or kwallet + secret-tool); refusing to store VPN password insecurely.");
    return false;
#endif
}

static QString doLoadPassword(const QString &key)
{
    if (key.isEmpty())
        return QString();

#if defined(Q_OS_MACOS)
    CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
                                                    key.toUtf8().constData(),
                                                    kCFStringEncodingUTF8);
    const QString out = macLoadPassword(account);
    CFRelease(account);
    return out;
#elif defined(Q_OS_WIN)
    const std::wstring target = (winTargetPrefix() + key).toStdWString();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred)
        return QString();
    const QString out = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
                                          static_cast<int>(cred->CredentialBlobSize));
    CredFree(cred);
    return out;
#else
    return loadPasswordLinux(key);
#endif
}

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
static bool deletePasswordLinuxAll(const QString &key)
{
#if defined(FT_HAVE_LIBSECRET)
    const bool fromLibsecret = libsecretClear(key);
#endif
    const bool fromService = secretServiceClear(key);
    const bool fromFile = deletePasswordFile(key);
#if defined(FT_HAVE_LIBSECRET)
    return fromLibsecret || fromService || fromFile;
#else
    return fromService || fromFile;
#endif
}
#endif

static bool doDeletePassword(const QString &key)
{
    if (key.isEmpty())
        return false;

#if defined(Q_OS_MACOS)
    CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
                                                    key.toUtf8().constData(),
                                                    kCFStringEncodingUTF8);
    const bool ok = macDeletePassword(account);
    CFRelease(account);
    return ok;
#elif defined(Q_OS_WIN)
    const std::wstring target = (winTargetPrefix() + key).toStdWString();
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
#else
    return deletePasswordLinuxAll(key);
#endif
}

static QString readConfigText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

static bool writeConfigText(const QString &path, const QString &toml)
{
    // Atomic on purpose. migrateConfigPassword() rewrites the user's LIVE config,
    // and by the time it does the password has already moved into the credential
    // store and been stripped from the text — so a truncate-in-place that then
    // fails half way (full disk, I/O error, power loss) leaves a config with no
    // server details AND no password in it. Stage and rename instead.
    //
    // The rename also replaces a symlink sitting at the target rather than
    // writing through it, so dropping the O_NOFOLLOW open loses nothing here.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    // Owner-only before a single byte is written: this file carries the config
    // during the window where the password may still be inline.
    if (!f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        f.cancelWriting();
        return false;
    }
    const QByteArray data = toml.toUtf8();
    if (f.write(data) != data.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

bool migrateConfigPassword(const QString &configPath)
{
    const QString abs = QFileInfo(configPath).absoluteFilePath();
    const QString key = CredentialStore::keyForConfigPath(abs);
    ConfigToml c = parseConfigToml(readConfigText(abs));
    if (c.password.isEmpty())
        return true;

    if (!CredentialStore::storePassword(key, c.password))
        return false;

    c.password.clear();
    return writeConfigText(abs, buildConfigToml(c));
}

QString buildConnectConfigToml(const QString &configPath, const QString &logLevel)
{
    const QString abs = QFileInfo(configPath).absoluteFilePath();
    migrateConfigPassword(abs);

    ConfigToml c = parseConfigToml(readConfigText(abs));
    if (c.password.isEmpty())
        c.password = CredentialStore::loadPassword(CredentialStore::keyForConfigPath(abs));
    if (c.password.isEmpty())
        return QString();
    return buildConfigToml(c, logLevel);
}

void sweepStaleMaterializedConfigs()
{
    // Older versions materialized password-injected .connect-XXXXXX.toml temp
    // files for connecting; a crash could leave one behind. The connect path
    // is in-memory-only now, but keep sweeping so upgrades clean old leftovers.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir d(dir);
    const QStringList stale = d.entryList({QStringLiteral(".connect-*.toml")},
                                          QDir::Files | QDir::Hidden);
    for (const QString &name : stale)
        QFile::remove(d.filePath(name));
}

void sweepLegacyPlaintextStorage()
{
    sweepStaleMaterializedConfigs();
}



// ---- public entry points -------------------------------------------------
// Thin wrappers so no caller can accidentally block the GUI thread on the
// credential store; see withoutFreezingTheUi above.

bool CredentialStore::secureStorageAvailable()
{
    return withoutFreezingTheUi([] { return doSecureStorageAvailable(); });
}

bool CredentialStore::storePassword(const QString &key, const QString &password)
{
    return withoutFreezingTheUi([&] { return doStorePassword(key, password); });
}

QString CredentialStore::loadPassword(const QString &key)
{
    return withoutFreezingTheUi([&] { return doLoadPassword(key); });
}

bool CredentialStore::deletePassword(const QString &key)
{
    return withoutFreezingTheUi([&] { return doDeletePassword(key); });
}

} // namespace freetunnel
