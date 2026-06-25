// cppcheck-suppress-file missingIncludeSystem
#include "qt_trusttunnel_events.h"
#include "qt_trusttunnel_platform.h"

#include "net/network_manager.h"

#include <QList>

#include <chrono>

StateChangedPayload extractStateChangedPayload(ag::VpnStateChangedEvent *event)
{
    StateChangedPayload payload;
    if (!event)
        return payload;
    payload.state = event->state;
    if (event->state == ag::VPN_SS_WAITING_RECOVERY) {
        payload.errCode = event->waiting_recovery_info.error.code;
        if (event->waiting_recovery_info.error.text)
            payload.errText = QString::fromUtf8(event->waiting_recovery_info.error.text);
        return payload;
    }
    if (event->state == ag::VPN_SS_CONNECTED)
        return payload;
    payload.errCode = event->error.code;
    if (event->error.text)
        payload.errText = QString::fromUtf8(event->error.text);
    return payload;
}

QString recoveryReason(const QString &prefix, int errCode, const QString &errText)
{
    const QString detail = qt_trusttunnel_format_vpn_error(errCode, errText);
    if (detail.isEmpty())
        return QStringLiteral("%1: reconnecting").arg(prefix);
    return QStringLiteral("%1: %2").arg(prefix, detail);
}

static QString defaultDisconnectReason(std::chrono::steady_clock::time_point lastAttempt)
{
    if (lastAttempt == std::chrono::steady_clock::time_point{})
        return QStringLiteral("core disconnected");
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - lastAttempt)
                                   .count();
    if (elapsedMs >= 15000 && elapsedMs <= 45000) {
        return QStringLiteral("Connection failed: endpoint timed out (~%1s)")
                .arg(elapsedMs / 1000);
    }
    return QStringLiteral("core disconnected");
}

QString buildDisconnectReason(int errCode, const QString &errText, bool everConnected,
                              std::chrono::steady_clock::time_point lastAttempt)
{
    QString reason = qt_trusttunnel_format_vpn_error(errCode, errText);
    if (reason.isEmpty())
        return defaultDisconnectReason(lastAttempt);
    if (!everConnected)
        return QStringLiteral("Connection failed: %1").arg(reason);
    return reason;
}

size_t clientOutputBytes(ag::VpnClientOutputEvent *event)
{
    if (!event)
        return 0;
    size_t bytes = 0;
    for (size_t i = 0; i < event->packet.chunks_num; ++i)
        bytes += event->packet.chunks[i].iov_len;
    return bytes;
}

void emitCoreLogLines(const QByteArray &chunk, const std::function<void(const QString &)> &emitLine)
{
    for (const QByteArray &raw : chunk.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (!line.isEmpty())
            emitLine(QString::fromUtf8(line));
    }
}

#ifdef Q_OS_WIN
void pinWindowsPhysicalOutbound(uint32_t ifIndex)
{
    if (ifIndex != 0)
        ag::vpn_network_manager_set_outbound_interface(ifIndex);
}
#endif
