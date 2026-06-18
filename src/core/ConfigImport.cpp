#include "core/ConfigImport.h"

#include <QDateTime>

#include "core/DeepLink.h"

namespace freetunnel {

static QString sanitizeFileName(const QString &name) {
    QString safe;
    for (const QChar &c : name) {
        safe += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('_');
    }
    if (safe.isEmpty()) {
        safe = QStringLiteral("imported-%1").arg(QDateTime::currentSecsSinceEpoch());
    }
    if (!safe.endsWith(QLatin1String(".toml"))) {
        safe += QLatin1String(".toml");
    }
    return safe;
}

std::optional<PreparedImport> prepareDeepLinkImport(const QString &link, QString *error) {
    auto cfg = parseDeepLink(link, error);
    if (!cfg) {
        return std::nullopt;
    }
    QString name = cfg->name.trimmed();
    if (name.isEmpty()) {
        name = cfg->hostname.trimmed();
    }
    PreparedImport out;
    out.fileName = sanitizeFileName(name);
    out.tomlContent = deepLinkConfigToToml(*cfg);
    return out;
}

} // namespace freetunnel
