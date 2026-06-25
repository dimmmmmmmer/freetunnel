// cppcheck-suppress-file missingIncludeSystem
#pragma once

#ifdef _WIN32
#ifndef IOVEC_DEFINED_QT
#define IOVEC_DEFINED_QT
struct iovec {
    void *iov_base;
    size_t iov_len;
};
#endif
#endif

#include "vpn/trusttunnel/client.h"
#include "vpn/vpn.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <chrono>
#include <functional>

struct StateChangedPayload {
    ag::VpnSessionState state = ag::VPN_SS_DISCONNECTED;
    int errCode = 0;
    QString errText;
};

StateChangedPayload extractStateChangedPayload(ag::VpnStateChangedEvent *event);
QString recoveryReason(const QString &prefix, int errCode, const QString &errText);
QString buildDisconnectReason(int errCode, const QString &errText, bool everConnected,
                              std::chrono::steady_clock::time_point lastAttempt);
size_t clientOutputBytes(ag::VpnClientOutputEvent *event);
void emitCoreLogLines(const QByteArray &chunk, const std::function<void(const QString &)> &emitLine);

#ifdef _WIN32
void pinWindowsPhysicalOutbound(uint32_t ifIndex);
#endif
