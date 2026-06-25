// cppcheck-suppress-file missingIncludeSystem
#include "ControlCommand.h"

namespace freetunnel {

ControlCommand parseControlCommand(const QString &raw) {
    QString c = raw.trimmed();
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

} // namespace freetunnel
