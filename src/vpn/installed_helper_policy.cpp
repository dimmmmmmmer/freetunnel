// cppcheck-suppress-file missingIncludeSystem
#include "vpn/installed_helper.h"

#include "core/ReleaseSigning.h"
#include "core/ReleaseVerify.h"
#include "core/VersionCompare.h"
#include <QFileInfo>
#include <QRegularExpression>

namespace freetunnel::installed_helper {

#if defined(FT_INSTALLED_HELPER_TEST_KEY) && !defined(FT_ENABLE_TEST_HOOKS)
#error "The helper fixture key must never be compiled into a production build"
#endif

QString rootDirectory()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("/Library/PrivilegedHelperTools/org.freetunnel.helper");
#else
    return QStringLiteral("/opt/freetunnel-helper");
#endif
}

QString executableRelativePath()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("FreeTunnel.app/Contents/MacOS/FreeTunnel");
#else
    return QStringLiteral("bin/FreeTunnel");
#endif
}

QString assetName()
{
#ifdef Q_OS_MACOS
    return QStringLiteral("freetunnel-helper-macos-universal.tar.gz");
#else
    return QStringLiteral("freetunnel-helper-linux-x86_64.tar.gz");
#endif
}

QString executablePath()
{
    return rootDirectory() + QStringLiteral("/current/runtime/") + executableRelativePath();
}

bool validUserId(const QString &text, quint32 *uid)
{
    static const QRegularExpression decimal(QStringLiteral("\\A[1-9][0-9]{0,9}\\z"));
    bool ok = false;
    const quint32 value = text.toUInt(&ok);
    if (!ok || !decimal.match(text).hasMatch() || value == 0 || value == UINT32_MAX)
        return false;
    if (uid)
        *uid = value;
    return true;
}

QString sudoRule(quint32 uid)
{
    if (uid == 0 || uid == UINT32_MAX)
        return {};
    // A numeric UID is a sudoers user, never text interpolated from an account
    // name. This fixed command cannot launch the GUI or accept arbitrary flags:
    // runCommand dispatches its first argument before main's ordinary modes.
    return QStringLiteral("#%1 ALL=(root) NOPASSWD:NOSETENV: %2 --installed-helper *\n")
            .arg(uid).arg(executablePath());
}

bool validStartArguments(const QStringList &args)
{
    if (args.size() != 4 || args[0] != QLatin1String("--port")
        || args[2] != QLatin1String("--token-file"))
        return false;
    bool ok = false;
    const uint port = args[1].toUInt(&ok);
    return ok && port >= 49152 && port <= 65535
            && QFileInfo(args[3]).isAbsolute()
            && QFileInfo(args[3]).fileName().startsWith(QLatin1String(".fthelper-"));
}

bool verifyArchive(const QString &archive, const QByteArray &sums, const QByteArray &signature,
                   const QString &currentVersion, bool allowSameVersion, QString *error)
{
    const auto refuse = [error](const QString &why) {
        if (error) *error = why;
        return false;
    };
#ifdef FT_INSTALLED_HELPER_TEST_KEY
    const QByteArray key("-----BEGIN PUBLIC KEY-----\n"
                         "MCowBQYDK2VwAyEAl/2X8WjH5cSx7grWmqx1lBiAYld85dLQgohY2tlDJ0k=\n"
                         "-----END PUBLIC KEY-----\n");
#else
    const QByteArray key(kReleaseSigningPublicKeyPem);
#endif
    if (sums.isEmpty() || sums.size() > 65536 || signature.size() != 64
        || !verifyEd25519Signature(sums, signature, key))
        return refuse(QStringLiteral("The helper update has no valid release signature."));
    const QString version = versionFromSums(sums);
    static const QRegularExpression release(QStringLiteral("\\A[0-9]+\\.[0-9]+\\.[0-9]+\\z"));
    if (!release.match(version).hasMatch()
        || !(isVersionNewer(currentVersion, version) || (allowSameVersion && version == currentVersion)))
        return refuse(QStringLiteral("The helper update would downgrade the installed version."));
    if (!verifyFileAgainstSums(archive, sums, assetName()))
        return refuse(QStringLiteral("The helper archive does not match the signed release."));
    return true;
}

} // namespace freetunnel::installed_helper
