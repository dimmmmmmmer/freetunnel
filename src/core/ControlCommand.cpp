// cppcheck-suppress-file missingIncludeSystem
#include "ControlCommand.h"

namespace freetunnel {

namespace {

// What linkControlString() puts in front of the URL. No URL begins with it (a
// scheme has no space in it) and no verb does, so only a marked string reads as
// a link.
constexpr QLatin1String kLinkMark("link ");

ControlCommand parseUnmarked(QString c) {
    if (c.isEmpty() || c.compare(QLatin1String("focus"), Qt::CaseInsensitive) == 0)
        return {};

    // Config-import links keep their full URI as the payload.
    if (c.startsWith(QLatin1String("tt://")))
        return {ControlAction::ImportLink, c};

    // Optional app scheme prefix; the verb itself is case-insensitive and may
    // carry stray slashes (e.g. "freetunnel://toggle/").
    if (c.startsWith(QLatin1String("freetunnel://"), Qt::CaseInsensitive))
        c = c.mid(QStringLiteral("freetunnel://").size());
    c = c.remove('/').toLower();

    if (c == QLatin1String("toggle"))
        return {ControlAction::Toggle, {}};
    if (c == QLatin1String("connect"))
        return {ControlAction::Connect, {}};
    if (c == QLatin1String("disconnect"))
        return {ControlAction::Disconnect, {}};
    return {};
}

} // namespace

QString linkControlString(const QString &url) {
    return kLinkMark + url.trimmed();
}

ControlCommand parseControlCommand(const QString &raw) {
    const QString c = raw.trimmed();
    // The mark first; what follows it is read exactly as it would be unmarked.
    const bool fromLink = c.startsWith(kLinkMark);
    ControlCommand cmd = parseUnmarked(fromLink ? c.mid(kLinkMark.size()).trimmed() : c);
    cmd.fromLink = fromLink;
    return cmd;
}

} // namespace freetunnel
