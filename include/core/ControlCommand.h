// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Pure, UI/core-independent parsing of control commands that arrive via deep
// link, second-instance forwarding, or the tray. Kept separate from Backend so
// it can be unit-tested without the VPN core.

#include <QString>

namespace freetunnel {

enum class ControlAction {
    None,       // empty / "focus" / unrecognised — caller just raises the window
    ImportLink, // a tt:// config-import link (payload = the full link)
    Toggle,
    Connect,
    Disconnect,
};

struct ControlCommand {
    ControlAction action = ControlAction::None;
    QString payload; // set for ImportLink
};

// Parse a raw control string, e.g. "freetunnel://toggle", "tt://?<...>",
// "focus", or "". Accepts an optional "freetunnel://" scheme prefix and is
// case-insensitive for the verb.
ControlCommand parseControlCommand(const QString &raw);

} // namespace freetunnel
