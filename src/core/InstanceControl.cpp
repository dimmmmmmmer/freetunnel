// cppcheck-suppress-file missingIncludeSystem
#include "core/InstanceControl.h"

#include "core/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QStandardPaths>

#if !defined(Q_OS_WIN)
#include <unistd.h>
#if defined(Q_OS_LINUX)
#include <sys/socket.h>
#elif defined(Q_OS_MACOS)
// getpeereid() — declared in unistd.h on macOS
#endif
#endif

namespace freetunnel {

namespace {

const QString kInstanceAuthKey = QStringLiteral("__freetunnel_instance_auth__");

QString randomInstanceToken()
{
    return QString::number(QRandomGenerator::system()->generate64(), 16)
            + QString::number(QRandomGenerator::system()->generate64(), 16);
}

} // namespace

QString instanceAuthFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/instance-auth");
}

bool writeInstanceAuthToken(QString *tokenOut)
{
    const QString token = randomInstanceToken();
    // Prefer OS credential storage over a plaintext file (same-user malware can
    // still read it, but not by simply cat-ing a predictable path).
    if (CredentialStore::storePassword(kInstanceAuthKey, token)) {
        QFile::remove(instanceAuthFilePath()); // drop legacy file from older builds
        if (tokenOut)
            *tokenOut = token;
        return true;
    }
    // Fallback when Linux has no Secret Service — keep single-instance working.
    const QString path = instanceAuthFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(token.toUtf8()) != token.size()) {
        f.remove();
        return false;
    }
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (tokenOut)
        *tokenOut = token;
    return true;
}

void removeInstanceAuthToken()
{
    CredentialStore::deletePassword(kInstanceAuthKey);
    QFile::remove(instanceAuthFilePath());
}

void sweepLegacyInstanceAuthFile()
{
    // Only a leftover legacy plaintext file ever needs sweeping. Check that first
    // so a normal startup doesn't do a blocking keychain read — on macOS that can
    // raise a keychain unlock/ACL prompt and freeze the UI at launch just to learn
    // there was nothing to delete.
    const QString legacyPath = instanceAuthFilePath();
    if (!QFile::exists(legacyPath))
        return;
    if (!CredentialStore::loadPassword(kInstanceAuthKey).isEmpty())
        QFile::remove(legacyPath);
}

bool readInstanceAuthToken(QString *tokenOut)
{
    if (!tokenOut)
        return false;
    const QString fromStore = CredentialStore::loadPassword(kInstanceAuthKey);
    if (!fromStore.isEmpty()) {
        *tokenOut = fromStore;
        QFile::remove(instanceAuthFilePath()); // drop stale legacy file
        return true;
    }
    // Legacy plaintext file from builds before credential-store migration.
    const QString path = instanceAuthFilePath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString token = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    if (token.isEmpty())
        return false;
    if (CredentialStore::secureStorageAvailable()
            && CredentialStore::storePassword(kInstanceAuthKey, token)) {
        QFile::remove(path);
    }
    *tokenOut = token;
    return true;
}

QByteArray formatInstanceMessage(const QString &token, const QString &payload)
{
    return token.toUtf8() + '\n' + payload.toUtf8();
}

bool parseInstanceMessage(const QByteArray &data, QString *tokenOut, QString *payloadOut)
{
    if (!tokenOut || !payloadOut)
        return false;
    const int nl = data.indexOf('\n');
    if (nl <= 0)
        return false;
    *tokenOut = QString::fromUtf8(data.left(nl));
    *payloadOut = QString::fromUtf8(data.mid(nl + 1));
    return !tokenOut->isEmpty();
}

bool instanceTokensEqual(const QString &a, const QString &b)
{
    const QByteArray ba = a.toUtf8();
    const QByteArray bb = b.toUtf8();
    if (ba.size() != bb.size())
        return false;
    char diff = 0;
    for (int i = 0; i < ba.size(); ++i)
        diff |= static_cast<char>(ba[i] ^ bb[i]);
    return diff == 0;
}

bool localSocketPeerIsSameUser(QLocalSocket *socket)
{
    if (!socket)
        return false;
#if defined(Q_OS_WIN)
    // QLocalServer::UserAccessOption restricts the named pipe to the same user.
    return socket->state() == QLocalSocket::ConnectedState;
#else
    const qintptr fd = socket->socketDescriptor();
    if (fd < 0)
        return false;
#if defined(Q_OS_LINUX)
    ucred cred{};
    socklen_t len = sizeof(cred);
    if (getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0)
        return false;
    return cred.uid == getuid();
#elif defined(Q_OS_MACOS)
    uid_t uid = 0;
    gid_t gid = 0;
    if (getpeereid(static_cast<int>(fd), &uid, &gid) != 0)
        return false;
    return uid == getuid();
#else
    return true;
#endif
#endif
}

bool forwardToRunningInstance(const QString &socketName, const QString &controlArg)
{
    QString token;
    if (!readInstanceAuthToken(&token))
        return false;

    QLocalSocket probe;
    probe.connectToServer(socketName);
    if (!probe.waitForConnected(250))
        return false;

    const QString payload = controlArg.isEmpty() ? QStringLiteral("focus") : controlArg;
    const QByteArray msg = formatInstanceMessage(token, payload);
    if (probe.write(msg) != msg.size())
        return false;
    probe.flush();
    probe.waitForBytesWritten(300);
    probe.disconnectFromServer();
    return true;
}

} // namespace freetunnel
