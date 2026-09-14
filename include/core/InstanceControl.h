// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QLocalSocket>
#include <QByteArray>
#include <QString>

namespace freetunnel {

QString instanceAuthFilePath();

/// Create a per-session token (0600 file) for second-instance IPC auth.
bool writeInstanceAuthToken(QString *tokenOut);

// The name of this application's single-instance socket, test override included.
QString instanceServerName();

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

/// Forward a control command to an already-running instance; returns false if none.
bool forwardToRunningInstance(const QString &socketName, const QString &controlArg);

} // namespace freetunnel
