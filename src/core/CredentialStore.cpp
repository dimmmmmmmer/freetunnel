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
#if defined(FT_ENABLE_TEST_HOOKS)
#include <QMutex>
#endif

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

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#endif

namespace freetunnel {

Q_LOGGING_CATEGORY(lcCredentialStore, "freetunnel.credentialstore")

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

// What the Security framework said, for the log: the status and its own words.
QString macStatusText(OSStatus st)
{
    CFStringRef words = SecCopyErrorMessageString(st, nullptr);
    const QString text = words ? QString::fromCFString(words) : QString();
    if (words)
        CFRelease(words);
    return QStringLiteral("OSStatus %1 (%2)").arg(st).arg(text);
}

// errSecItemNotFound is the one status that means the Keychain was asked and
// holds nothing. A locked Keychain (errSecInteractionNotAllowed), a prompt the
// user denied or closed (errSecAuthFailed, errSecUserCanceled), no Keychain to
// ask (errSecNotAvailable, errSecNoSuchKeychain) and the rest are failures to ask:
// the item may be there.
StoredSecret macLookup(CFStringRef account)
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

    StoredSecret out;
    if (st == errSecItemNotFound) {
        out.status = SecretStatus::NotFound;
        return out;
    }
    if (st != errSecSuccess || !data) {
        out.status = SecretStatus::Error;
        out.detail = st == errSecSuccess ? QStringLiteral("success, but no data came with it")
                                         : macStatusText(st);
        if (data)
            CFRelease(data);
        return out;
    }

    const auto *bytes = CFDataGetBytePtr(data);
    const CFIndex len = CFDataGetLength(data);
    out.status = SecretStatus::Found;
    out.value = QString::fromUtf8(reinterpret_cast<const char *>(bytes), static_cast<int>(len));
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

// The file older builds kept a password in. A file is read, not asked: it holds
// the entry or it does not, and one that cannot be opened is not a source of
// anything either.
StoredSecret legacyFileLookup(const QString &key)
{
    StoredSecret out;
    QFile f(filePathForKey(key));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QString value = QString::fromUtf8(f.readAll());
    if (value.isEmpty())
        return out;
    out.status = SecretStatus::Found;
    out.value = value;
    return out;
}

bool deletePasswordFile(const QString &key)
{
    return QFile::remove(filePathForKey(key));
}

// The Linux store is the desktop Secret Service (GNOME Keyring, KWallet's Secret
// Service bridge, …), through libsecret when the build has it and secret-tool
// otherwise. With neither reaching a service, a new password is refused: the
// 0600 file is only read back (and moved out of) for passwords older builds left
// there, and only test builds still write it.
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

// The first line secret-tool said on stderr, which is the only place it says why.
QString firstStderrLine(QProcess &p)
{
    const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
    return err.section(QLatin1Char('\n'), 0, 0).trimmed();
}

// secret-tool's answer, or nothing when there is no secret-tool to ask. Its exit
// code 1 is both "no such item" and "could not ask". With no session bus it says
// why on stderr first, and an item that is not there prints nothing, which is how
// secretServiceAvailable() tells them apart too. A locked keyring is the third
// thing, and not told apart: when the unlock prompt is dismissed, secret-tool gets
// no password and no error, and exits 1 without a word, as for an item that is not
// there; killed while the prompt is still up, it is "did not answer in time". So a
// silent exit 1 is not proof that nothing is stored, and secretToolSearch() asks
// the question this one cannot.
// Not started, timed out, crashed, or any other exit: could not ask.
std::optional<StoredSecret> secretToolLookup(const QString &key)
{
    const QString tool = secretToolPath();
    if (tool.isEmpty())
        return std::nullopt;
    StoredSecret out;
    out.status = SecretStatus::Error;
    QProcess p;
    p.start(tool, {QStringLiteral("lookup"), QStringLiteral("service"),
                   credentialServiceName(), QStringLiteral("account"), key});
    if (!p.waitForStarted(3000)) {
        out.detail = QStringLiteral("secret-tool did not start");
        return out;
    }
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
        out.detail = QStringLiteral("secret-tool did not answer in time");
        return out;
    }
    if (p.exitStatus() != QProcess::NormalExit) {
        out.detail = QStringLiteral("secret-tool crashed");
        return out;
    }
    if (p.exitCode() == 0) {
        out.status = SecretStatus::Found;
        out.value = QString::fromUtf8(p.readAllStandardOutput()); // secret-tool emits no trailing newline
        return out;
    }
    const QString why = firstStderrLine(p);
    if (p.exitCode() == 1 && why.isEmpty()) {
        out.status = SecretStatus::NotFound;
        return out;
    }
    out.detail = QStringLiteral("secret-tool exit %1: %2").arg(p.exitCode()).arg(why);
    return out;
}

// Whether secret-tool can list an item stored for this entry: `search --all`, which
// without --unlock cannot raise a prompt and so names a locked item too (it prints
// the item, and says on stderr that it cannot get the secret). Used where libsecret
// cannot do the same search itself: a build without libsecret, or with one before
// 0.19. Exit 0 with nothing printed is "no such item", as `lookup`'s exit 1 is, and
// output is an item. Anything else, such as a secret-tool before `search`, a
// timeout or a crash, is no answer: empty. What it prints holds the secret of an
// item that is not locked; it is looked at for being there and never kept or logged.
std::optional<StoredSecret> secretToolSearch(const QString &key)
{
    const QString tool = secretToolPath();
    if (tool.isEmpty())
        return std::nullopt;
    QProcess p;
    p.start(tool, {QStringLiteral("search"), QStringLiteral("--all"), QStringLiteral("service"),
                   credentialServiceName(), QStringLiteral("account"), key});
    if (!p.waitForStarted(3000))
        return std::nullopt;
    if (!p.waitForFinished(5000)) {
        p.kill();
        p.waitForFinished(1000);
        return std::nullopt;
    }
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return std::nullopt;
    StoredSecret out; // NotFound
    if (!p.readAllStandardOutput().trimmed().isEmpty()) {
        out.status = SecretStatus::Error;
        out.detail = QStringLiteral("secret-tool lists an item for this entry that its lookup did not return");
    }
    return out;
}

// LinuxLookupSources::secretToolItems: libsecret's search where there is one,
// secret-tool's otherwise.
std::optional<StoredSecret> secretToolItemsLookup(const QString &key)
{
#if defined(FT_HAVE_LIBSECRET)
    if (const std::optional<StoredSecret> viaLibsecret = libsecretToolItemsLookup(key))
        return viaLibsecret;
#endif
    return secretToolSearch(key);
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

// Moves a password the legacy file holds into the OS store, and the file with it.
// False when it did not move: there is no store to take it, or the store refused.
static bool migrateLegacyFilePassword(const QString &key, const QString &fromFile)
{
    if (fromFile.isEmpty() || !CredentialStore::secureStorageAvailable())
        return false;
#if defined(FT_HAVE_LIBSECRET)
    if (libsecretStore(key, fromFile)) {
        deletePasswordFile(key);
        return true;
    }
#endif
    if (secretServiceStore(key, fromFile)) {
        deletePasswordFile(key);
        return true;
    }
    return false;
}

static StoredSecret lookupLinux(const QString &key)
{
    LinuxLookupSources sources;
#if defined(FT_ALLOW_INSECURE_CREDENTIAL_FALLBACK)
    sources.insecureFallbackBuild = true;
#endif
#if defined(FT_HAVE_LIBSECRET)
    sources.libsecret = libsecretLookup(key);
#endif
    // Each is asked only when what came before did not have the value. secret-tool
    // stays a second source beside libsecret for the items it stored itself, which
    // libsecret's schema does not match.
    bool inOsStore = sources.libsecret && sources.libsecret->found();
    if (!inOsStore) {
        sources.secretTool = secretToolLookup(key);
        inOsStore = sources.secretTool && sources.secretTool->found();
    }
    // Neither returned it. A locked item whose prompt was dismissed is not
    // returned by either, and secret-tool's silence is the same as for an item
    // that is not there; so, unless the answer is an error already, ask whether
    // an item secret-tool stored for it is there.
    if (!inOsStore) {
        const std::optional<StoredSecret> &decider =
                sources.libsecret ? sources.libsecret : sources.secretTool;
        if (!(decider && decider->failed()))
            sources.secretToolItems = secretToolItemsLookup(key);
    }
    if (!inOsStore)
        sources.legacyFile = legacyFileLookup(key);

    StoredSecret out = combineLinuxLookup(sources);
    // A value that only the legacy file had moves into the OS store where there is
    // one to take it, as it always has, and is no longer "from the file" if it did.
    if (out.found() && !inOsStore && migrateLegacyFilePassword(key, out.value))
        out.fromLegacyFile = false;
    return out;
}

#endif

} // namespace

QString CredentialStore::keyForConfigPath(const QString &absoluteConfigPath)
{
    return QFileInfo(absoluteConfigPath).absoluteFilePath();
}

QString CredentialStore::keyForConfigAuthKey(const QString &absoluteConfigPath)
{
    // Not under keyForSubscription()'s prefix, nor any path's: an absolute path
    // starts with a separator or a drive letter.
    return QStringLiteral("client-random-auth-key/") + QFileInfo(absoluteConfigPath).absoluteFilePath();
}

QString CredentialStore::keyForSubscription(const QString &sourceId)
{
    return QStringLiteral("subscription/") + sourceId;
}

QString CredentialStore::keyForInstanceToken()
{
    return QStringLiteral("__freetunnel_instance_auth__");
}

StoredSecret combineLinuxLookup(const LinuxLookupSources &sources)
{
    // A source that holds the entry wins: the OS ones first, then the file.
    if (sources.libsecret && sources.libsecret->found())
        return *sources.libsecret;
    if (sources.secretTool && sources.secretTool->found())
        return *sources.secretTool;
    if (sources.legacyFile.found()) {
        StoredSecret fromFile = sources.legacyFile;
        // Where the file is the store (a development build), it is not the legacy of
        // anything. Elsewhere it is what an older build left, and stays so until it
        // has moved into the OS store.
        fromFile.fromLegacyFile = !sources.insecureFallbackBuild;
        return fromFile;
    }
    // An item secret-tool stored is there, and neither lookup returned it (locked,
    // its prompt dismissed): not absent, whatever they said.
    if (sources.secretToolItems && sources.secretToolItems->failed()) {
        StoredSecret out;
        out.status = SecretStatus::Error;
        out.detail = sources.secretToolItems->detail;
        return out;
    }
    // Nobody holds it. libsecret says whether that is so, or whether it could not
    // look; secret-tool says it only when libsecret is not there, so that a
    // secret-tool that fails does not turn a clean "not found" into an error.
    const StoredSecret *decider = sources.libsecret ? &*sources.libsecret
                                : sources.secretTool ? &*sources.secretTool
                                                     : nullptr;
    if (decider) {
        StoredSecret out;
        out.status = decider->status;
        out.detail = decider->detail;
        return out;
    }
    // No OS source to ask. A build with the insecure fallback keeps its entries in
    // the file, which was read and has none. Every other build has nowhere to look,
    // which is not the same as nothing being there.
    StoredSecret out;
    if (!sources.insecureFallbackBuild) {
        out.status = SecretStatus::Error;
        out.detail = QStringLiteral("no Secret Service to ask (neither libsecret nor secret-tool)");
    }
    return out;
}

namespace {

// Every call below talks to the OS credential store, and every one of them
// BLOCKS: on macOS the request goes to securityd and does not return while the
// system is asking the user whether this build may touch the item; on Linux the
// Secret Service probe can wait up to five seconds, for the bus to start a
// keyring daemon or for secret-tool to answer.
// Called straight from the GUI thread — which is what opening the edit form,
// saving a config, copying a config link or exporting one used to do — that
// freezes the whole window, spinner and all.
//
// So run it on a worker and keep our own event loop turning while we wait. User
// input is excluded, so the app stays painted and responsive-looking but nothing
// can re-enter the operation that is already in flight. Off the GUI thread there
// is nothing to keep alive, so the call is made directly.
// Depth guard for the loop below. Only ever reached on the GUI thread — every
// other caller returns before touching it — so a plain bool needs no
// synchronisation. It lives out here rather than as a static local inside the
// template: a static local would be per-instantiation, so a QString-returning
// call would not see the flag a bool-returning one had set, and the guard would
// not hold between two different credential operations.
bool g_uiCallInFlight = false;

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
    if (g_uiCallInFlight)
        return fn();

    g_uiCallInFlight = true;
    const auto reset = qScopeGuard([] { g_uiCallInFlight = false; });

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

#if !defined(Q_OS_MACOS) && !defined(Q_OS_WIN)
bool secretServiceOnSessionBus()
{
    static const QString name = QStringLiteral("org.freedesktop.secrets");
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (bus.interface()->isServiceRegistered(name))
        return true;
    // Not running is not the same as not there: most keyrings are started on
    // first use, from a D-Bus .service file, which is what libsecret's first call
    // would do. Ask the bus to start it now. This fails where that call would — no
    // such service, or one that will not start — and is capped like the
    // secret-tool probe, so a daemon that hangs on start costs seconds rather
    // than the bus's own 25.
    QDBusMessage start = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("StartServiceByName"));
    start << name << 0u;
    return bus.call(start, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage;
}
#endif

#if defined(FT_ENABLE_TEST_HOOKS)
// Tests only. "absent": no store at all; "locked": a store that is there and
// refuses, as a keyring does whose unlock prompt was dismissed. Neither can be
// had from a real store on demand, on any platform. "present": a store that is
// there and takes what it is given, wherever this build keeps it, for a test of
// what needs one on a machine with no keyring.
static QByteArray testKeyring()
{
    return qgetenv("FT_TEST_KEYRING");
}
#endif

static bool doSecureStorageAvailable()
{
#if defined(FT_ENABLE_TEST_HOOKS)
    if (const QByteArray keyring = testKeyring(); !keyring.isEmpty())
        return keyring != "absent";
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    return true;
#else
#if defined(FT_HAVE_LIBSECRET)
    // libsecret is linked in, so the client is never what is missing; the service
    // can be. This used to answer from DBUS_SESSION_BUS_ADDRESS alone, but every
    // desktop session has a bus, keyring or not, so the Settings warning stayed
    // hidden on exactly the machines it is for and each save failed on its own.
    // Asked the same two ways doStorePassword() tries, in the same order.
    return secretServiceOnSessionBus() || secretServiceAvailable();
#endif
    return secretServiceAvailable();
#endif
}

static bool doStorePassword(const QString &key, const QString &password)
{
    if (key.isEmpty())
        return false;
#if defined(FT_ENABLE_TEST_HOOKS)
    if (const QByteArray keyring = testKeyring(); !keyring.isEmpty() && keyring != "present")
        return false;
#endif

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

static StoredSecret doLookup(const QString &key)
{
    if (key.isEmpty())
        return StoredSecret();

#if defined(Q_OS_MACOS)
    CFStringRef account = CFStringCreateWithCString(kCFAllocatorDefault,
                                                    key.toUtf8().constData(),
                                                    kCFStringEncodingUTF8);
    const StoredSecret out = macLookup(account);
    CFRelease(account);
    return out;
#elif defined(Q_OS_WIN)
    const std::wstring target = (winTargetPrefix() + key).toStdWString();
    PCREDENTIALW cred = nullptr;
    const BOOL read = CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred);
    // Straight after the call, before anything else can change it.
    const DWORD why = read ? ERROR_SUCCESS : GetLastError();
    StoredSecret out;
    if (!read || !cred) {
        if (!read && why == ERROR_NOT_FOUND)
            return out; // asked, and nothing is there
        // Any other failure is no answer: ERROR_NO_SUCH_LOGON_SESSION, a profile
        // that has not loaded, a service that is not running.
        out.status = SecretStatus::Error;
        out.detail = read ? QStringLiteral("success, but no credential came with it")
                          : QStringLiteral("Win32 error %1").arg(why);
        return out;
    }
    out.status = SecretStatus::Found;
    out.value = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
                                  static_cast<int>(cred->CredentialBlobSize));
    CredFree(cred);
    return out;
#else
    return lookupLinux(key);
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

bool migrateConfigPassword(const QString &configPath, SecretRefused *refused)
{
    if (refused)
        *refused = SecretRefused::None;
    const QString abs = QFileInfo(configPath).absoluteFilePath();
    const QString passwordEntry = CredentialStore::keyForConfigPath(abs);
    const QString authKeyEntry = CredentialStore::keyForConfigAuthKey(abs);
    ConfigToml c = parseConfigToml(readConfigText(abs));
    if (c.password.isEmpty() && c.clientRandomAuthKey.isEmpty())
        return true;

    // The one case where a refusal can come after something was written: the
    // password lands, then the key is refused (a denied Keychain prompt for an
    // item that is there, or a key over what Windows' Credential Manager holds).
    // What the password's entry held is read before it is overwritten, and only
    // then, so that the password can be put back; a config with one secret has
    // nothing written by the time its only store can fail.
    const bool both = !c.password.isEmpty() && !c.clientRandomAuthKey.isEmpty();
    QString previousPassword;
    if (both) {
        // An entry that cannot be read is not an empty one: the password stored
        // next would overwrite it, and the key's refusal would then delete it
        // for want of anything to put back. Nothing is touched; the file keeps
        // both secrets, and is migrated when the store can be read.
        const StoredSecret previous = CredentialStore::lookup(passwordEntry);
        if (previous.failed()) {
            if (refused)
                *refused = SecretRefused::Password;
            return false;
        }
        previousPassword = previous.value;
    }

    if (!c.password.isEmpty() && !CredentialStore::storePassword(passwordEntry, c.password)) {
        if (refused)
            *refused = SecretRefused::Password;
        return false;
    }
    // The second secret of the config, stored the same way before the file loses
    // it. A key that is already stored is not what the file holds any more, and
    // is not read back: the file's is the newer, as the password's is.
    if (!c.clientRandomAuthKey.isEmpty() && !CredentialStore::storePassword(authKeyEntry, c.clientRandomAuthKey)) {
        if (both) {
            if (previousPassword.isEmpty())
                CredentialStore::deletePassword(passwordEntry);
            else
                CredentialStore::storePassword(passwordEntry, previousPassword);
        }
        if (refused)
            *refused = SecretRefused::AuthKey;
        return false;
    }

    c.password.clear();
    c.clientRandomAuthKey.clear();
    return writeConfigText(abs, buildConfigToml(c));
}

ConfigSecrets readConfigSecrets(const QString &configPath, const ConfigToml &c)
{
    const QString abs = QFileInfo(configPath).absoluteFilePath();
    ConfigSecrets out;

    // The file's copy wins, and no store is asked: it is the newer, as in
    // migrateConfigPassword().
    bool passwordFromLegacyFile = false;
    out.password = c.password;
    if (out.password.isEmpty()) {
        const StoredSecret stored = CredentialStore::lookup(CredentialStore::keyForConfigPath(abs));
        if (stored.failed()) {
            out.unreadable = SecretUnreadable::Password;
            return out;
        }
        out.password = stored.value;
        passwordFromLegacyFile = stored.fromLegacyFile;
    }

    out.authKey = c.clientRandomAuthKey;
    if (!out.authKey.isEmpty())
        return out;
    const StoredSecret key = CredentialStore::lookup(CredentialStore::keyForConfigAuthKey(abs));
    if (key.found()) {
        out.authKey = key.value;
        return out;
    }
    if (!key.failed())
        return out; // a config that has no key
    // The store could not be asked. It is where a key would be only if it is where
    // the password is: every path that puts the secrets of a config in the store
    // puts both or neither (migrateConfigPassword(), saveConfigWithPassword(), an
    // import that is refused when the migration is), and a legacy file still on disk
    // means no store write for the config ever succeeded, as storing deletes it.
    // A config whose password is in its file, or was in the legacy file, never had
    // a key stored, so one that cannot be read is one that is not there.
    const bool storeIsHome = c.password.isEmpty() && !passwordFromLegacyFile;
    if (storeIsHome) {
        out.unreadable = SecretUnreadable::AuthKey;
        return out;
    }
    qCWarning(lcCredentialStore).noquote()
            << "no auth key read for" << QFileInfo(abs).fileName()
            << "- this config keeps its password outside the credential store, so it has none there";
    return out;
}

ConnectConfig buildConnectConfig(const QString &configPath, const QString &logLevel)
{
    const QString abs = QFileInfo(configPath).absoluteFilePath();
    migrateConfigPassword(abs);

    ConfigToml c = parseConfigToml(readConfigText(abs));
    const ConfigSecrets secrets = readConfigSecrets(abs, c);
    ConnectConfig out;
    // In this order: a password that cannot be read is said before the one that
    // is not there, and a key only once there is a password to send with it.
    if (secrets.unreadable == SecretUnreadable::Password) {
        out.problem = ConnectConfig::Problem::PasswordUnreadable;
        return out;
    }
    if (secrets.password.isEmpty()) {
        out.problem = ConnectConfig::Problem::NoPassword;
        return out;
    }
    if (secrets.unreadable == SecretUnreadable::AuthKey) {
        out.problem = ConnectConfig::Problem::AuthKeyUnreadable;
        return out;
    }
    c.password = secrets.password;
    c.clientRandomAuthKey = secrets.authKey;
    // With a key the core derives the client random from it, replaces whatever
    // client_random says and clears its mask - and warns about the one it ignores
    // on every TLS handshake and every ping, which the recovery loop of a kill
    // switch repeats for as long as it runs. The config on disk keeps the user's
    // value for the day the key goes; the core is only handed what it will use.
    // Not in buildConfigToml(): an export or a share link keeps both.
    if (!c.clientRandomAuthKey.isEmpty())
        c.clientRandom.clear();
    out.toml = buildConfigToml(c, logLevel);
    return out;
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

// A config imported by 1.2.3 or earlier can hold a TrustTunnel 1.3 subscription
// address, VPN password and all: those versions kept every table and key they did
// not know as the file had it, and migrateConfigPassword() moved the password out
// and left the address in. The import leaves it out now, but nothing rewrites a
// config whose password is already stored until it is edited, so it is taken out
// here. Only from the copies in the app's own folder, and not through a link: a
// config kept elsewhere is the user's own file, which TrustTunnel's own client may
// still read the address from.
//
// The same goes for whatever such a config holds beside its certificates, such as
// a private key pasted in with them: the import keeps only the certificates now
// (withCertificatesOnly), and the rest is taken out here, by the same rules. A
// block holding a whole chain, as 1.1.9 and earlier wrote one, holds nothing
// but certificates and stays as it is; a certificate with none left is emptied.
void sweepStoredSubscriptionAddresses(const QStringList &configPaths)
{
    const QString ownDir =
            QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).absoluteFilePath();
    for (const QString &path : configPaths) {
        const QFileInfo info(path);
        if (info.absolutePath() != ownDir || info.isSymLink())
            continue;
        bool removed = false;
        bool trimmed = false;
        const QString kept = withCertificatesOnly(
                withoutSubscription(readConfigText(info.absoluteFilePath()), &removed), &trimmed);
        if (removed || trimmed)
            writeConfigText(info.absoluteFilePath(), kept);
    }
}



// ---- public entry points -------------------------------------------------
// Thin wrappers so no caller can accidentally block the GUI thread on the
// credential store; see withoutFreezingTheUi above.

namespace {

#if defined(FT_ENABLE_TEST_HOOKS)
QMutex g_callHookMutex;
CredentialStore::CallHookForTest g_callHookForTest;
CredentialStore::ReadFailureForTest g_readFailureForTest;
CredentialStore::LookupOverrideForTest g_lookupOverrideForTest;

// Copies, called outside the lock, like the hook above.
bool readFailsForTest(const QString &key)
{
    CredentialStore::ReadFailureForTest hook;
    {
        const QMutexLocker lock(&g_callHookMutex);
        hook = g_readFailureForTest;
    }
    return hook && hook(key);
}

std::optional<StoredSecret> lookupOverrideForTest(const QString &key)
{
    CredentialStore::LookupOverrideForTest hook;
    {
        const QMutexLocker lock(&g_callHookMutex);
        hook = g_lookupOverrideForTest;
    }
    return hook ? hook(key) : std::nullopt;
}

// A copy, called outside the lock: the hook may itself use the store.
void callHookForTest(const char *call, const QString &key)
{
    CredentialStore::CallHookForTest hook;
    {
        const QMutexLocker lock(&g_callHookMutex);
        hook = g_callHookForTest;
    }
    if (hook)
        hook(call, key);
}
#else
void callHookForTest(const char *, const QString &) {}
#endif

} // namespace

#if defined(FT_ENABLE_TEST_HOOKS)
void CredentialStore::setCallHookForTest(CallHookForTest hook)
{
    const QMutexLocker lock(&g_callHookMutex);
    g_callHookForTest = std::move(hook);
}

void CredentialStore::setReadFailureForTest(ReadFailureForTest hook)
{
    const QMutexLocker lock(&g_callHookMutex);
    g_readFailureForTest = std::move(hook);
}

void CredentialStore::setLookupOverrideForTest(LookupOverrideForTest hook)
{
    const QMutexLocker lock(&g_callHookMutex);
    g_lookupOverrideForTest = std::move(hook);
}
#endif

bool CredentialStore::secureStorageAvailable()
{
    return withoutFreezingTheUi([] { return doSecureStorageAvailable(); });
}

bool CredentialStore::storePassword(const QString &key, const QString &password)
{
    callHookForTest("store", key);
    return withoutFreezingTheUi([&] { return doStorePassword(key, password); });
}

namespace {

// What an entry is, for the log, from its key: the kind, and whose it is where
// that is a config (its file name, never the folder it is in). Never a value.
QString describeEntry(const QString &key)
{
    static const QString authKeyPrefix = QStringLiteral("client-random-auth-key/");
    static const QString subscriptionPrefix = QStringLiteral("subscription/");
    if (key.startsWith(authKeyPrefix))
        return QStringLiteral("auth key of %1").arg(QFileInfo(key.mid(authKeyPrefix.size())).fileName());
    if (key.startsWith(subscriptionPrefix))
        return QStringLiteral("subscription address");
    if (key == CredentialStore::keyForInstanceToken())
        return QStringLiteral("instance token");
    if (QFileInfo(key).isAbsolute())
        return QStringLiteral("password of %1").arg(QFileInfo(key).fileName());
    return QStringLiteral("entry");
}

void logUnreadable(const QString &key, const StoredSecret &answer)
{
    qCWarning(lcCredentialStore).noquote()
            << "could not read the" << describeEntry(key) << "from the credential store:" << answer.detail;
}

} // namespace

StoredSecret CredentialStore::lookup(const QString &key)
{
    callHookForTest("load", key);
#if defined(FT_ENABLE_TEST_HOOKS)
    if (const std::optional<StoredSecret> overridden = lookupOverrideForTest(key))
        return *overridden;
    if (readFailsForTest(key)) {
        StoredSecret failed;
        failed.status = SecretStatus::Error;
        failed.detail = QStringLiteral("a test made this entry unreadable");
        logUnreadable(key, failed);
        return failed;
    }
#endif
    const StoredSecret answer = withoutFreezingTheUi([&] { return doLookup(key); });
    if (answer.failed())
        logUnreadable(key, answer);
    return answer;
}

bool CredentialStore::deletePassword(const QString &key)
{
    callHookForTest("delete", key);
    return withoutFreezingTheUi([&] { return doDeletePassword(key); });
}

} // namespace freetunnel
