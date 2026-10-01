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
    // Opened as a link, through the operating system's URL handler, rather than
    // run as a command. Any web page can open a link, so one that would turn the
    // VPN off is asked about first (Backend::handleControl); a command someone
    // runs — a script, a Stream Deck button — is theirs and acts at once.
    bool fromLink = false;
};

// The argument the URL-handler registrations put before the URL: the
// installer's freetunnel:// and tt:// entries on Windows, the .desktop files on
// Linux. It is how a link the system opened is told from a command someone ran
// with the same URL. (macOS hands links over as events, never on the command
// line; see UrlOpenFilter.)
inline constexpr QLatin1String kUrlHandlerArg("--url-handler");

// The control string for a URL that arrived as a link: the URL behind a mark
// that parseControlCommand() reads back as fromLink. The mark travels with the
// string, so a link forwarded to the running instance is still a link there.
QString linkControlString(const QString &url);

// Parse a raw control string, e.g. "freetunnel://toggle", "tt://?<...>",
// "focus", "", or any of them marked by linkControlString(). Accepts an optional
// "freetunnel://" scheme prefix and is case-insensitive for the verb.
ControlCommand parseControlCommand(const QString &raw);

} // namespace freetunnel
