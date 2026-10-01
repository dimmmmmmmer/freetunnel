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
    // A link can name its config at any length; one too long for a file name
    // failed to import. Cut it rather than refuse a link for its name.
    out.fileName = sanitizeFileName(clippedConfigName(name)) + QStringLiteral(".toml");
    const QString whole = sanitizeFileName(name) + QStringLiteral(".toml");
    if (whole != out.fileName)
        out.unclippedFileName = whole;
    if (!name.isEmpty())
        out.legacyFileName = legacyConfigBaseName(name) + QStringLiteral(".toml");
    out.tomlContent = deepLinkConfigToToml(*cfg);
    out.skipVerification = cfg->skipVerification;
    return out;
}

} // namespace freetunnel
