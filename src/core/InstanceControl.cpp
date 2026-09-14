// cppcheck-suppress-file missingIncludeSystem
#include "core/InstanceControl.h"

#include "core/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QStandardPaths>

#if defined(Q_OS_WIN)
// clang-format off
#include <windows.h>
#include <namedpipeapi.h>
// clang-format on
#else
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
    // Zero-pad each 64-bit half to a fixed 16 hex chars: QString::number(…, 16)
    // drops leading-zero nibbles, which would let the token length vary between
    // runs and shave bits off the worst case. Matches the helper IPC token.
    return QStringLiteral("%1%2")
            .arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'))
            .arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'));
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
    // Restrict BEFORE the token is written, not after: open() honours the umask,
    // which on most desktops leaves the file world-readable, so tightening it
    // afterwards leaves a window in which any local user can read a token that
    // grants control over this instance. An empty file leaking is harmless; the
    // token must never exist on disk at wider permissions than 0600.
    if (!f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        f.remove();
        return false;
    }
    if (f.write(token.toUtf8()) != token.size()) {
        f.remove();
        return false;
    }
    if (tokenOut)
        *tokenOut = token;
    return true;
}

// The name this application's single-instance socket listens on.
//
// Lives here rather than beside the listener because more than the listener
// needs it: the self-update path has to stop owning the socket before it starts
// the replacement, and a second copy of the string is a second thing to keep in
// step with the test override below.
//
// The override matters during development: run a debug build from a checkout
// under the production name and it connects to whatever FreeTunnel the developer
// happens to have running — forwarding a command into their live app and then
// exiting as though it were the second instance. It also leaves a socket in a
// namespace shared with every other process on the machine.
QString instanceServerName()
{
#ifdef FT_ENABLE_TEST_HOOKS
    const QByteArray override = qgetenv("FT_TEST_INSTANCE_NAME");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
#endif
    return QStringLiteral("FreeTunnelInstance");
}

void removeInstanceAuthToken(const QString &onlyIfItMatches)
{
    // A quitting instance must not delete a token that is no longer its own.
    // The self-update path deliberately overlaps two processes: the replacement
    // writes its own token at startup, and this runs from the old one's
    // aboutToQuit, which can land afterwards. Deleting then leaves the new
    // instance with no token at all — reachable by nothing, so every later
    // deep link starts a second copy instead of being forwarded, until the
    // next restart.
    if (!onlyIfItMatches.isEmpty()
        && CredentialStore::loadPassword(kInstanceAuthKey) != onlyIfItMatches)
        return;
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

#if defined(Q_OS_WIN)
namespace {

// The SID of the user a process is running as, or empty when it cannot be read.
QByteArray processUserSid(HANDLE process)
{
    HANDLE token = nullptr;
    if (::OpenProcessToken(process, TOKEN_QUERY, &token) == 0)
        return {};
    DWORD size = 0;
    ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray sid;
    if (size > 0) {
        QByteArray buffer(static_cast<int>(size), Qt::Uninitialized);
        if (::GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != 0) {
            const auto *user = reinterpret_cast<const TOKEN_USER *>(buffer.constData());
            if (user->User.Sid != nullptr && ::IsValidSid(user->User.Sid) != 0) {
                sid = QByteArray(reinterpret_cast<const char *>(user->User.Sid),
                                 static_cast<int>(::GetLengthSid(user->User.Sid)));
            }
        }
    }
    ::CloseHandle(token);
    return sid;
}

// Whether the process on the OTHER end of this pipe runs as the user we do.
//
// Which end to ask is not a detail. GetNamedPipeServerProcessId names the
// process that created the pipe, which is the peer only when this handle is the
// CLIENT end. Asked on a handle from CreateNamedPipe — the one Qt hands back
// from nextPendingConnection() — it names this very process, so the comparison
// is with ourselves and cannot fail. The check then passed for every inbound
// connection whoever opened it, which is the whole thing it exists to refuse.
bool pipePeerIsSameUser(HANDLE pipe, bool weAreTheServer)
{
    if (pipe == nullptr || pipe == INVALID_HANDLE_VALUE)
        return false;
    ULONG peerPid = 0;
    const BOOL asked = weAreTheServer ? ::GetNamedPipeClientProcessId(pipe, &peerPid)
                                      : ::GetNamedPipeServerProcessId(pipe, &peerPid);
    if (asked == 0 || peerPid == 0)
        return false;
    HANDLE peer = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                static_cast<DWORD>(peerPid));
    if (peer == nullptr)
        return false;
    const QByteArray theirs = processUserSid(peer);
    ::CloseHandle(peer);
    const QByteArray ours = processUserSid(::GetCurrentProcess());
    return !ours.isEmpty() && ours == theirs;
}

} // namespace
#endif

bool localSocketPeerIsSameUser(QLocalSocket *socket, SocketEnd end)
{
    if (!socket)
        return false;
#if defined(Q_OS_WIN)
    // Ask who is on the other end, rather than assuming.
    //
    // What stood here was "QLocalServer::UserAccessOption restricts the named
    // pipe to the same user", and returned true for any connected socket. That
    // is true of the pipe this application CREATES and says nothing about this
    // side, which opens a name another user may have created first: Qt opens it
    // with CreateFile and default security, checking nothing about the server.
    // The caller then sends the instance token and the contents of a tt:// link
    // — a VPN username and password — to whoever answered, and exits as though
    // an instance were already running, so the application never starts.
    //
    // Failing closed is deliberate at every step. A process belonging to another
    // user normally cannot even be opened for query, and that refusal is the
    // answer.
    return pipePeerIsSameUser(reinterpret_cast<HANDLE>(socket->socketDescriptor()),
                              end == SocketEnd::WeAccepted);
#else
    // Unix answers the same question from either end: SO_PEERCRED and getpeereid
    // both describe the peer, not the socket's role.
    Q_UNUSED(end)
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

    // The local-socket name lives in a world-writable namespace on Unix, so a
    // process of ANOTHER user could squat it before our real instance starts.
    // Sending the auth token there would leak it and make this launch exit as
    // if an instance were already running (silent startup DoS). Only talk to a
    // listener owned by the same user.
    if (!localSocketPeerIsSameUser(&probe, SocketEnd::WeConnected))
        return false;

    const QString payload = controlArg.isEmpty() ? QStringLiteral("focus") : controlArg;
    const QByteArray msg = formatInstanceMessage(token, payload);
    if (probe.write(msg) != msg.size())
        return false;
    probe.flush();
    probe.waitForBytesWritten(300);
    probe.disconnectFromServer();
    // Wait for the close to finish. On Windows the named-pipe write is completed
    // asynchronously, so waitForBytesWritten() above can return with the payload
    // still queued; disconnectFromServer() then only *starts* the close, and this
    // process exits immediately afterwards (runGuiApplication returns 0), taking
    // the unsent bytes with it. Short commands got through and a tt:// deep link —
    // the longest thing this channel carries, and the one where losing it means a
    // link the user clicked does nothing at all — did not.
    probe.waitForDisconnected(3000);
    return true;
}

} // namespace freetunnel
