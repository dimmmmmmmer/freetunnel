#pragma once

#include <QByteArray>
#include <QString>

namespace vpn_helper {

// Constant-time comparison for the one-time helper auth token.
inline bool tokensEqual(const QString &a, const QString &b)
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

inline constexpr int kMaxReadBuffer = 65536;

} // namespace vpn_helper
