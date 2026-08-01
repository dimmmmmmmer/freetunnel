// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QString>

namespace vpn_helper {

// Proof of knowing the one-time token, over a nonce the verifier chose.
//
// The GUI used to open with {"cmd":"hello","token":T} and treat whatever
// answered as the helper. But the helper only listens AFTER the elevation
// prompt is dismissed — seconds to a minute — so any local process able to
// pre-bind the port received the token and then the inline config TOML, which
// carries the user's VPN password, and could report "Connected" while nothing
// was tunnelled at all. Both sides now prove knowledge of the token over the
// other side's nonce before anything sensitive is sent, and neither ever puts
// the token itself on the wire.
inline QString authProof(const QString &token, const QString &role, const QString &nonce)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256);
    mac.setKey(token.toUtf8());
    mac.addData(role.toUtf8());
    mac.addData(QByteArrayLiteral(":"));
    mac.addData(nonce.toUtf8());
    return QString::fromLatin1(mac.result().toHex());
}

inline constexpr char kHelperRole[] = "helper";
inline constexpr char kGuiRole[] = "gui";

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

// Connect payloads carry full TOML (certs can be large).
inline constexpr int kMaxIpcLineBytes = 512 * 1024;

} // namespace vpn_helper
