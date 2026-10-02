// cppcheck-suppress-file missingIncludeSystem
#include "core/InstanceControl.h"

#include "core/CredentialStore.h"

#include <QDir>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QObject>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QThread>

#include <optional>

#if defined(Q_OS_WIN)
// clang-format off
#include <windows.h>
#include <namedpipeapi.h>
#include <sddl.h>
// clang-format on
#else
#include <sys/stat.h>
#include <unistd.h>
#if defined(Q_OS_LINUX)
#include <sys/socket.h>
#include <sys/un.h>
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

namespace {

// The token in the fallback file, or empty when there is none.
QString readInstanceAuthFile()
{
    QFile f(instanceAuthFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    return QString::fromUtf8(f.readAll()).trimmed();
}

// The token where a second launch would find it: the store's, or the fallback
// file's when the store holds none.
QString storedInstanceAuthToken()
{
    const QString stored = CredentialStore::loadPassword(kInstanceAuthKey);
    return stored.isEmpty() ? readInstanceAuthFile() : stored;
}

bool storeInstanceAuthToken(const QString &token)
{
    // Prefer OS credential storage over a plaintext file (same-user malware can
    // still read it, but not by simply cat-ing a predictable path).
    if (CredentialStore::storePassword(kInstanceAuthKey, token)) {
        QFile::remove(instanceAuthFilePath()); // drop legacy file from older builds
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
    return true;
}

} // namespace

bool writeInstanceAuthToken(QString *tokenOut)
{
    const QString token = randomInstanceToken();
    if (!storeInstanceAuthToken(token))
        return false;
    if (tokenOut)
        *tokenOut = token;
    return true;
}

// The same token, never a new one: the listener checks every command against
// the one it was started with, so no other lets a launch in. Written only when
// it is not what a launch would read already, so an instance whose token is in
// place changes nothing.
bool restoreInstanceAuthToken(const QString &token)
{
    if (token.isEmpty())
        return false;
    if (storedInstanceAuthToken() == token)
        return true;
    return storeInstanceAuthToken(token);
}

namespace {

// The one name every build up to 1.2.2 listened on, whoever ran it.
const QString kSharedInstanceName = QStringLiteral("FreeTunnelInstance");

#if defined(Q_OS_WIN)
QByteArray processUserSid(HANDLE process); // below, with the pipe-peer checks
#endif

// Who this process runs as, as text that can go into a socket name: the uid on
// Unix, the SID on Windows. Empty when it cannot be told.
QString currentUserTag()
{
#if defined(Q_OS_WIN)
    QByteArray sid = processUserSid(::GetCurrentProcess());
    LPWSTR text = nullptr;
    if (sid.isEmpty() || ::ConvertSidToStringSidW(sid.data(), &text) == 0)
        return {};
    const QString tag = QString::fromWCharArray(text);
    ::LocalFree(text);
    return tag;
#else
    return QString::number(::getuid());
#endif
}

// The name in the temporary directory: this user's own, or the shared one when
// who this is cannot be told.
QString perUserTempName()
{
    const QString user = currentUserTag();
    return user.isEmpty() ? kSharedInstanceName : kSharedInstanceName + QLatin1Char('-') + user;
}

#if defined(Q_OS_LINUX)
// $XDG_RUNTIME_DIR when it is what the spec promises — a directory of this
// user's own that no other account may enter — and a socket path in it fits;
// empty otherwise.
QString privateRuntimeDir()
{
    const QByteArray dir = qgetenv("XDG_RUNTIME_DIR");
    struct stat st{};
    if (!dir.startsWith('/') || ::lstat(dir.constData(), &st) != 0 || !S_ISDIR(st.st_mode)
        || st.st_uid != ::getuid() || (st.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        return {};
    const QString clean = QDir::cleanPath(QFile::decodeName(dir));
    const qsizetype socketPath = QFile::encodeName(clean).size() + 1 + kSharedInstanceName.size();
    if (socketPath >= static_cast<qsizetype>(sizeof(sockaddr_un{}.sun_path)))
        return {};
    return clean;
}
#endif

// The test override, or empty when there is none.
//
// It matters during development: run a debug build from a checkout under the
// production name and it connects to whatever FreeTunnel the developer happens
// to have running — forwarding a command into their live app and then exiting
// as though it were the second instance.
QString testInstanceNameOverride()
{
#ifdef FT_ENABLE_TEST_HOOKS
    return qEnvironmentVariable("FT_TEST_INSTANCE_NAME");
#else
    return {};
#endif
}

} // namespace

// The name this user's single-instance socket listens on.
//
// Lives here rather than beside the listener because more than the listener
// needs it: a second launch looks for it first (instanceServerNames()), an
// AppImage update that cannot start the new build listens on it again, and a
// second copy of the string is a second thing to keep in step with the test
// override.
//
// One per user. A single name for the machine belonged to whoever started
// FreeTunnel first: a Windows pipe name is one namespace for every session, and
// on Linux the socket sat in the shared /tmp. Everyone else's launches found no
// instance of theirs there and could not listen on it either, so every launch
// and every link of theirs started another full copy.
//
// On Linux it goes in $XDG_RUNTIME_DIR, which every systemd or elogind session
// has. A name of this user's in /tmp is still one any account can create first,
// as a socket that turns everyone away or as a link to somewhere else; this
// directory no one else can enter. Without it, the name in /tmp is all there is.
// The temporary directory on macOS is the user's own already.
QString instanceServerName()
{
    const QString override = testInstanceNameOverride();
    if (!override.isEmpty())
        return override;
#if defined(Q_OS_LINUX)
    const QString runtime = privateRuntimeDir();
    if (!runtime.isEmpty())
        return runtime + QLatin1Char('/') + kSharedInstanceName;
#endif
    return perUserTempName();
}

// The names after this user's own are only ever forwarded to, never listened
// on, and what answers there has to pass the same checks as anything else. A
// listener there that lets no one in is not given way to
// (whatAFailedConnectMeans()).
//
// The shared name is tried for an update installed while an older FreeTunnel
// keeps running. That one listens on the shared name, and a launch of the new
// build that looked only for its own would find nothing and start a second copy
// beside it. On Linux the name in /tmp comes before it, for a FreeTunnel that
// was started where $XDG_RUNTIME_DIR was not set, as from a shell outside the
// desktop session.
QStringList instanceServerNames()
{
    const QString own = instanceServerName();
    if (own == kSharedInstanceName || !testInstanceNameOverride().isEmpty())
        return {own};
    QStringList names{own};
    const QString inTemp = perUserTempName();
    if (inTemp != own && inTemp != kSharedInstanceName)
        names << inTemp;
    names << kSharedInstanceName;
    return names;
}

namespace {

// Where QLocalSocket looks for @p socketName: the name itself when it is a path,
// otherwise a file of that name in the temporary directory.
QString socketFilePath(const QString &socketName)
{
    return socketName.startsWith(QLatin1Char('/')) ? socketName
                                                   : QDir::tempPath() + QLatin1Char('/') + socketName;
}

#if !defined(Q_OS_WIN)
// Whether the socket file at @p socketName is one this user bound. The file
// belongs to whoever bound it, and no other account can make one that is ours.
// Looked at itself rather than through a link, which another account can leave
// in /tmp pointing at something of this user's.
bool socketFileIsThisUsers(const QString &socketName)
{
    struct stat st{};
    return ::lstat(QFile::encodeName(socketFilePath(socketName)).constData(), &st) == 0
            && S_ISSOCK(st.st_mode) && st.st_uid == ::getuid();
}
#endif

// Whether a refused connection can come from a listener that is there but busy.
// macOS turns a connection away from a full backlog with the same refusal as one
// to a socket file nothing listens on; Linux leaves it waiting instead, and a
// pipe on Windows is never refused. The hook lets a test on Linux take the
// macOS path, which the system there never takes.
bool refusalCanBeABusyListener()
{
#if defined(Q_OS_MACOS)
    return true;
#elif defined(FT_ENABLE_TEST_HOOKS) && !defined(Q_OS_WIN)
    return qEnvironmentVariableIsSet("FT_TEST_REFUSAL_CAN_BE_BUSY");
#else
    return false;
#endif
}

QString claimFilePath(const QString &socketName)
{
    return socketFilePath(socketName) + QStringLiteral(".lock");
}

const QString kClaimObjectName = QStringLiteral("freetunnel-instance-name-claim");

// A running instance's claim on its name (claimInstanceName()), held for as long
// as this object lives.
class InstanceNameClaim : public QObject
{
public:
    InstanceNameClaim(const QString &path, QObject *owner) : QObject(owner), m_lock(path)
    {
        setObjectName(kClaimObjectName);
        // Stale only once the process that took it is gone, however long ago
        // that was: an instance runs for days.
        m_lock.setStaleLockTime(0);
    }
    bool take() { return m_lock.tryLock(0); }

private:
    QLockFile m_lock;
};

// Whether a process that is still running claims @p socketName. A claim left by
// one that crashed is cleared on the way, as QLockFile does with a stale lock.
bool aLiveInstanceClaims(const QString &socketName)
{
    QLockFile probe(claimFilePath(socketName));
    probe.setStaleLockTime(0);
    if (probe.tryLock(0)) {
        probe.unlock();
        return false;
    }
    return probe.error() == QLockFile::LockFailedError;
}

} // namespace

bool claimInstanceName(QObject *owner, const QString &socketName)
{
    if (owner == nullptr || !refusalCanBeABusyListener())
        return false;
    auto *claim = new InstanceNameClaim(claimFilePath(socketName), owner);
    if (claim->take())
        return true;
    delete claim;
    return false;
}

void releaseInstanceName(QObject *owner)
{
    if (owner != nullptr)
        qDeleteAll(owner->findChildren<QObject *>(kClaimObjectName, Qt::FindDirectChildrenOnly));
}

// Removing the name used to be unconditional at every start, and a start is not
// proof that nothing listens there, only that nothing of ours answered: the name
// can be held by a listener that is not ours to remove. Only a name that refuses
// the connection outright is stale — a socket file with no listener behind it —
// unless a live instance claims it: on macOS that refusal is also what a busy
// instance of ours gives (refusalCanBeABusyListener()).
//
// What this spares is narrow, and no test of the start-up can see it. On Unix,
// listen() with UserAccessOption renames its socket over whatever holds the name,
// so the start that follows takes the name either way; only a name that listen()
// then fails to take is left to its holder. A live instance of ours is kept from
// being taken over by the launch never listening beside it: a listener of ours
// that cannot be handed the command is Unreachable, and the launch exits.
bool removeStaleInstanceServer(const QString &socketName)
{
#if defined(Q_OS_WIN)
    // Nothing to remove: a pipe goes away with its last handle, and removeServer()
    // does nothing there. Asking would still cost time. Qt waits five seconds for
    // a pipe that stays busy, as one another account keeps taken does, and the
    // launch has already waited that long for it once.
    Q_UNUSED(socketName)
    return false;
#else
    QLocalSocket probe;
    probe.connectToServer(socketName);
    if (probe.waitForConnected(250) || probe.error() != QLocalSocket::ConnectionRefusedError)
        return false;
    if (refusalCanBeABusyListener() && aLiveInstanceClaims(socketName))
        return false;
    return QLocalServer::removeServer(socketName);
#endif
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
    //
    // Compared with the token where a second launch would find it: the store's,
    // or the fallback file's when the store holds none. Comparing with the store
    // alone never matched on Linux without a Secret Service, where the file is
    // the only copy, and the token stayed on disk after every quit.
    //
    // Comparing and deleting are still two calls to the store, and a token the
    // replacement writes between them goes all the same. That is why a running
    // instance puts its token back (restoreInstanceAuthToken()), and why one that
    // handed its name over does not come here at all.
    if (!onlyIfItMatches.isEmpty() && storedInstanceAuthToken() != onlyIfItMatches)
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
    // The fallback file: written when the store refused the token, or left by a
    // build from before the credential-store migration.
    const QString token = readInstanceAuthFile();
    if (token.isEmpty())
        return false;
    if (CredentialStore::secureStorageAvailable()
            && CredentialStore::storePassword(kInstanceAuthKey, token)) {
        QFile::remove(instanceAuthFilePath());
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

// This process was started by the user — from Explorer, or by a browser for a
// tt:// link — and so may set the foreground window. The running instance, about
// to be asked to bring its window forward, may not: Windows refuses
// SetForegroundWindow to a process that did not get the last input and only
// flashes its taskbar button. Pass the right on, to that process alone.
void letPipeServerTakeForeground(HANDLE pipe)
{
    ULONG serverPid = 0;
    if (::GetNamedPipeServerProcessId(pipe, &serverPid) != 0 && serverPid != 0)
        ::AllowSetForegroundWindow(static_cast<DWORD>(serverPid));
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

namespace {

// Whether what holds @p socketName, and did not let us in, is provably this
// user's: an instance of ours too busy to answer, which a launch must not start
// beside.
//
// The failure itself says only that something holds the name, not whose it is,
// and another account can hold even this user's name: by creating the pipe
// first on Windows, or the socket in the shared /tmp. Taken for ours whatever
// it was, a listener there that answers no one, its backlog kept full or its
// one pipe instance kept taken, made every launch exit, and FreeTunnel did not
// start at all.
bool heldByThisUser(const QString &socketName, QLocalSocket::LocalSocketError error)
{
#if defined(Q_OS_WIN)
    // A pipe that will not open cannot be asked who made it. But a listener of
    // ours keeps fifty instances of its pipe waiting for callers and opens
    // another for each it takes, so a pipe that stays busy for the five seconds
    // Qt waits on it is someone else's. Qt 6.8 reports that wait running out as
    // a connection error, not a timeout: while connecting, it reads the wait's
    // ERROR_SEM_TIMEOUT as ERROR_NO_DATA, a pipe being closed, which is no
    // instance to give way to either. A timeout is taken the same way, should a
    // later Qt call it one.
    Q_UNUSED(socketName)
    return error != QLocalSocket::ConnectionError && error != QLocalSocket::SocketTimeoutError;
#else
    Q_UNUSED(error)
    return socketFileIsThisUsers(socketName);
#endif
}

// Whether a refused connection to this user's own name came from a live instance
// of ours with a full backlog, rather than from a socket file nothing listens on.
// Only where the two look alike (macOS), and only with a running process's claim
// on the name to tell them apart.
bool refusedByABusyInstanceOfOurs(const QLocalSocket &probe, const QString &socketName,
                                  bool ownName)
{
    return ownName && probe.error() == QLocalSocket::ConnectionRefusedError
            && refusalCanBeABusyListener() && heldByThisUser(socketName, probe.error())
            && aLiveInstanceClaims(socketName);
}

// A full backlog drains as soon as the instance's event loop turns again, so a
// busy instance of ours is asked a few more times before it is given up on.
constexpr int kBusyRetries = 5;
constexpr unsigned long kBusyRetryPauseMs = 200;

// What a connection that failed says about whether an instance of ours runs.
// Only this user's own name can be given way to. The names after it are
// fallbacks: the one every build up to 1.2.2 shared, which every user's launch
// tries and any account can hold, and a failure there is no instance of ours.
ForwardResult whatAFailedConnectMeans(const QLocalSocket &probe, const QString &socketName,
                                      bool ownName)
{
    switch (probe.error()) {
    case QLocalSocket::ConnectionRefusedError: // a socket file nothing listens on
        return refusedByABusyInstanceOfOurs(probe, socketName, ownName) ? ForwardResult::Unreachable
                                                                        : ForwardResult::NoInstance;
    case QLocalSocket::ServerNotFoundError:    // no such name
    case QLocalSocket::SocketAccessError:      // a name another user holds
        return ForwardResult::NoInstance;
    default:
        break;
    }
    return ownName && heldByThisUser(socketName, probe.error()) ? ForwardResult::Unreachable
                                                                : ForwardResult::NoInstance;
}

// Connect to the instance listening on @p socketName. Nothing when it is there
// and ours; otherwise what the failure says about whether one is running.
std::optional<ForwardResult> connectToInstance(QLocalSocket &probe, const QString &socketName,
                                               bool ownName)
{
    probe.connectToServer(socketName);
    bool connected = probe.waitForConnected(250);
    for (int retry = 0; !connected && retry < kBusyRetries
         && refusedByABusyInstanceOfOurs(probe, socketName, ownName);
         ++retry) {
        QThread::msleep(kBusyRetryPauseMs);
        probe.abort();
        probe.connectToServer(socketName);
        connected = probe.waitForConnected(250);
    }
    if (!connected)
        return whatAFailedConnectMeans(probe, socketName, ownName);

    // The local-socket name lives in a world-writable namespace on Unix, so a
    // process of ANOTHER user could squat it before our real instance starts.
    // Sending the auth token there would leak it and make this launch exit as
    // if an instance were already running (silent startup DoS). Only talk to a
    // listener owned by the same user.
    if (!localSocketPeerIsSameUser(&probe, SocketEnd::WeConnected))
        return ForwardResult::NoInstance;
#if !defined(Q_OS_WIN)
    // Nor to one reached through a link. connect() follows it, so another
    // account can point a name in /tmp at any socket of this user's, such as the
    // session bus: that passes the peer check, takes the token and the link, and
    // this launch exits without FreeTunnel starting.
    if (!socketFileIsThisUsers(socketName))
        return ForwardResult::NoInstance;
#endif
    return std::nullopt;
}

ForwardResult sendToInstance(QLocalSocket &probe, const QString &token, const QString &controlArg)
{
#if defined(Q_OS_WIN)
    letPipeServerTakeForeground(reinterpret_cast<HANDLE>(probe.socketDescriptor()));
#endif
    const QString payload = controlArg.isEmpty() ? QStringLiteral("focus") : controlArg;
    const QByteArray msg = formatInstanceMessage(token, payload);
    if (probe.write(msg) != msg.size())
        return ForwardResult::Unreachable;
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
    return ForwardResult::Forwarded;
}

} // namespace

// "Could not hand it over" and "there is nothing to hand it to" used to be one
// answer, false, and the caller started a full instance on either. With an
// instance running that meant two copies driving one VPN — the second taking the
// socket name over, so the first could no longer be reached by anything. It
// happened whenever the token could not be read (a locked keyring, or an
// instance that never managed to store one) and whenever the connection was slow.
ForwardResult forwardToRunningInstance(const QStringList &socketNames, const QString &controlArg)
{
    // Read before any connection is open: the read can sit on a keyring prompt
    // for as long as the user takes over it, and the listener gives a connection
    // that says nothing three seconds before dropping it.
    QString token;
    const bool haveToken = readInstanceAuthToken(&token);
    for (qsizetype i = 0; i < socketNames.size(); ++i) {
        QLocalSocket probe;
        const bool ownName = i == 0; // see instanceServerNames()
        if (const std::optional<ForwardResult> failed =
                    connectToInstance(probe, socketNames.at(i), ownName)) {
            if (*failed == ForwardResult::NoInstance)
                continue;
            return *failed;
        }
        // Ours and listening. Without the token it can be told nothing, and
        // that is still no reason to start a second copy beside it.
        if (!haveToken)
            return ForwardResult::Unreachable;
        return sendToInstance(probe, token, controlArg);
    }
    return ForwardResult::NoInstance;
}

} // namespace freetunnel
