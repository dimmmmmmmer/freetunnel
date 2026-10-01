// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QLocalSocket>
#include <QByteArray>
#include <QString>
#include <QStringList>

class QObject;

namespace freetunnel {

QString instanceAuthFilePath();

/// Create a per-session token (0600 file) for second-instance IPC auth.
bool writeInstanceAuthToken(QString *tokenOut);

// The name of this user's single-instance socket, test override included. On
// Linux, a path in $XDG_RUNTIME_DIR when that is this user's own directory.
QString instanceServerName();

// The names a running instance may be listening on, newest first: this user's
// own, then the one name every build up to 1.2.2 shared between all users.
QStringList instanceServerNames();

// Clear a socket name that nothing listens on any more — the file a crashed
// instance leaves behind on Unix — and nothing else. Returns whether it did.
// Never anything on Windows, where a pipe goes away with its last handle.
bool removeStaleInstanceServer(const QString &socketName);

// Mark @p socketName as held by a running instance, for as long as @p owner lives
// or until releaseInstanceName(). Where a busy listener refuses a connection as a
// stale socket file does (macOS), this is what tells the two apart; elsewhere it
// does nothing. Returns whether the mark was taken.
bool claimInstanceName(QObject *owner, const QString &socketName);
void releaseInstanceName(QObject *owner);

// Forget the token that lets a second launch talk to this instance.
//
// With `onlyIfItMatches` given, it is removed only when the stored token is
// still that one. A quitting instance passes its own, so that an overlapping
// successor — the self-update path starts one deliberately — does not have the
// token it just wrote deleted out from under it.
void removeInstanceAuthToken(const QString &onlyIfItMatches = QString());

/// Remove legacy on-disk instance token when the credential store already holds it.
void sweepLegacyInstanceAuthFile();

bool readInstanceAuthToken(QString *tokenOut);

QByteArray formatInstanceMessage(const QString &token, const QString &payload);

bool parseInstanceMessage(const QByteArray &data, QString *tokenOut, QString *payloadOut);

bool instanceTokensEqual(const QString &a, const QString &b);

/// Which end of the connection this socket is. On Windows the API that names
/// the peer is a different one for each — asking the wrong one names this very
/// process, and the check then cannot fail. Unix answers either way.
enum class SocketEnd {
    WeConnected, ///< we opened it with connectToServer()
    WeAccepted,  ///< it came from QLocalServer::nextPendingConnection()
};

/// Defense-in-depth: verify the peer runs as the same user as this process.
bool localSocketPeerIsSameUser(QLocalSocket *socket, SocketEnd end);

/// What became of a command a launch tried to hand to a running instance.
enum class ForwardResult {
    Forwarded,   ///< an instance of ours took it; this launch is done
    NoInstance,  ///< none of ours is listening; this launch is to be the instance
    Unreachable, ///< ours is listening but could not be handed it; never start beside it
};

/// Forward a control command to an already-running instance, trying each name
/// in turn. The first is this user's own; only a listener there that is
/// provably this user's can make a launch give way without being handed the
/// command (Unreachable). The names after it are fallbacks (instanceServerNames()).
ForwardResult forwardToRunningInstance(const QStringList &socketNames, const QString &controlArg);

} // namespace freetunnel
