#pragma once

#include <QByteArray>
#include <QString>

namespace freetunnel {

QString instanceAuthFilePath();

/// Create a per-session token (0600 file) for second-instance IPC auth.
bool writeInstanceAuthToken(QString *tokenOut);

void removeInstanceAuthToken();

bool readInstanceAuthToken(QString *tokenOut);

QByteArray formatInstanceMessage(const QString &token, const QString &payload);

bool parseInstanceMessage(const QByteArray &data, QString *tokenOut, QString *payloadOut);

bool instanceTokensEqual(const QString &a, const QString &b);

/// Forward a control command to an already-running instance; returns false if none.
bool forwardToRunningInstance(const QString &socketName, const QString &controlArg);

} // namespace freetunnel
