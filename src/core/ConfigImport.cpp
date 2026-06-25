// cppcheck-suppress-file missingIncludeSystem
#include "core/ConfigImport.h"

#include <QDateTime>

#include "core/ConfigPaths.h"
#include "core/DeepLink.h"

namespace freetunnel {

static QString sanitizeFileName(const QString &name) {
    return sanitizeConfigBaseName(name.isEmpty() ? QString() : name, QStringLiteral("imported"));
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
    out.fileName = sanitizeFileName(name) + QStringLiteral(".toml");
    out.tomlContent = deepLinkConfigToToml(*cfg);
    return out;
}

} // namespace freetunnel
