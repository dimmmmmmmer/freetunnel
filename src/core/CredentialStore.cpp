#include "CredentialStore.h"

#include "ConfigToml.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>

#if defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <wincred.h>
#endif

namespace freetunnel {

namespace {

#if defined(Q_OS_MACOS)
CFStringRef macServiceName()
{
    return CFSTR("com.freetunnel.app");
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
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
            + QStringLiteral("/credentials");
    QDir().mkpath(dir);
    return dir;
}

QString filePathForKey(const QString &key)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(credentialDir()).filePath(QString::fromLatin1(hash));
}

bool storePasswordFile(const QString &key, const QString &password)
{
    QFile f(filePathForKey(key));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(password.toUtf8());
    f.close();
    QFile::setPermissions(f.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

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
                   QStringLiteral("service"), QStringLiteral("com.freetunnel.app"),
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
                   QStringLiteral("com.freetunnel.app"), QStringLiteral("account"), key});
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
                   QStringLiteral("com.freetunnel.app"), QStringLiteral("account"), key});
    return p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}
#endif

} // namespace

QString CredentialStore::keyForConfigPath(const QString &absoluteConfigPath)
{
    return QFileInfo(absoluteConfigPath).absoluteFilePath();
}

bool CredentialStore::storePassword(const QString &key, const QString &password)
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
    const std::wstring target = (QStringLiteral("FreeTunnel/") + key).toStdWString();
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
    if (secretServiceStore(key, password)) {
        deletePasswordFile(key); // don't leave a plaintext-ish copy on disk
        return true;
    }
    // No desktop Secret Service (GNOME Keyring / KWallet) available — the
    // password can only be persisted as a 0600 plaintext file. Warn loudly so
    // the user understands the credential is not OS-encrypted at rest.
    qWarning("CredentialStore: no Secret Service available (install gnome-keyring "
             "or kwallet + secret-tool); storing VPN password as a plaintext "
             "0600 file. It is NOT encrypted at rest.");
    return storePasswordFile(key, password);
#endif
}

QString CredentialStore::loadPassword(const QString &key)
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
    const std::wstring target = (QStringLiteral("FreeTunnel/") + key).toStdWString();
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &cred) || !cred)
        return QString();
    const QString out = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob),
                                          static_cast<int>(cred->CredentialBlobSize));
    CredFree(cred);
    return out;
#else
    bool ok = false;
    const QString fromService = secretServiceLookup(key, &ok);
    if (ok && !fromService.isEmpty())
        return fromService;
    return loadPasswordFile(key); // fallback (or pre-Secret-Service migration)
#endif
}

bool CredentialStore::deletePassword(const QString &key)
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
    const std::wstring target = (QStringLiteral("FreeTunnel/") + key).toStdWString();
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != FALSE;
#else
    const bool fromService = secretServiceClear(key);
    const bool fromFile = deletePasswordFile(key);
    return fromService || fromFile;
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
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(toml.toUtf8());
    f.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
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

QString materializeConfigForConnect(const QString &configPath)
{
    const QString abs = QFileInfo(configPath).absoluteFilePath();
    migrateConfigPassword(abs);

    ConfigToml c = parseConfigToml(readConfigText(abs));
    if (!c.password.isEmpty())
        return abs;

    const QString key = CredentialStore::keyForConfigPath(abs);
    const QString stored = CredentialStore::loadPassword(key);
    if (stored.isEmpty())
        return QString();
    c.password = stored;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QTemporaryFile tf(dir + QStringLiteral("/.connect-XXXXXX.toml"));
    tf.setAutoRemove(false);
    if (!tf.open())
        return QString();
    tf.write(buildConfigToml(c).toUtf8());
    tf.close();
    QFile::setPermissions(tf.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return tf.fileName();
}

void removeMaterializedConfig(const QString &materializedPath)
{
    if (materializedPath.isEmpty())
        return;
    const QString absConfigDir = QFileInfo(
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).absoluteFilePath();
    const QFileInfo fi(materializedPath);
    // Exact parent-directory match (not a prefix) so a sibling like
    // "<config>-evil/.connect-x.toml" can't be coaxed into removal, and only
    // remove the password temp files we ourselves materialize.
    if (fi.absolutePath() != absConfigDir)
        return;
    if (fi.fileName().startsWith(QStringLiteral(".connect-")))
        QFile::remove(materializedPath);
}

void sweepStaleMaterializedConfigs()
{
    // A crash mid-connection can leave a .connect-XXXXXX.toml (which holds the
    // injected password) behind. None should exist at startup, so clear them.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir d(dir);
    const QStringList stale = d.entryList({QStringLiteral(".connect-*.toml")},
                                          QDir::Files | QDir::Hidden);
    for (const QString &name : stale)
        QFile::remove(d.filePath(name));
}

} // namespace freetunnel
