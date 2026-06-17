#include "CredentialStore.h"

#include "ConfigToml.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    CFMutableDictionaryRef query =
            CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, macLookupQuery(account));
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
    return loadPasswordFile(key);
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
    return deletePasswordFile(key);
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
    if (!QFileInfo(materializedPath).absoluteFilePath().startsWith(absConfigDir))
        return;
    if (materializedPath.contains(QStringLiteral(".connect-")))
        QFile::remove(materializedPath);
}

} // namespace freetunnel
