// cppcheck-suppress-file missingIncludeSystem
#include "core/ConfigImport.h"

#include <QCoreApplication>
#include <QDateTime>

#include "core/ConfigPaths.h"
#include "core/DeepLink.h"

namespace freetunnel {

static QString sanitizeFileName(const QString &name) {
    return sanitizeConfigBaseName(name.isEmpty() ? QString() : name, QStringLiteral("imported"));
}

QString subscriptionOnlyRefusal()
{
    return QCoreApplication::translate("ConfigImport",
                                       "Subscription links aren't supported yet. Ask your provider for "
                                       "a regular link, or update FreeTunnel when subscriptions arrive.");
}

QString newerCoreRefusal()
{
    return QCoreApplication::translate("ConfigImport",
                                       "This server needs a newer TrustTunnel core than this version of "
                                       "FreeTunnel uses.");
}

QString subscriptionNotKeptNote()
{
    return QCoreApplication::translate("ConfigImport",
                                       "Subscriptions aren't supported yet: this server won't update "
                                       "itself, and its subscription address isn't saved.");
}

static std::nullopt_t refuse(LinkRefusal why, const QString &message, QString *error,
                             LinkRefusal *refusal)
{
    if (error)
        *error = message;
    if (refusal)
        *refusal = why;
    return std::nullopt;
}

std::optional<PreparedImport> prepareDeepLinkImport(const QString &link, QString *error,
                                                    LinkRefusal *refusal) {
    if (refusal)
        *refusal = LinkRefusal::Damaged;
    auto cfg = parseDeepLink(link, error);
    if (!cfg) {
        return std::nullopt;
    }
    // A server that checks the auth key turns this core away at the TLS
    // handshake, and with nothing in the app to say why. First, because it is
    // the last word: a subscription would hand out the same server.
    if (!cfg->clientRandomAuthKey.isEmpty())
        return refuse(LinkRefusal::NeedsNewerCore, newerCoreRefusal(), error, refusal);
    // A link that is only a subscription address names no server until the
    // subscription is fetched, and fetching one is later work. One that carries
    // its server as well imports that server, without the address (below).
    if (!cfg->subscriptionUrl.isEmpty() && !deepLinkHasStaticServer(*cfg))
        return refuse(LinkRefusal::SubscriptionOnly, subscriptionOnlyRefusal(), error, refusal);
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
    // deepLinkConfigToToml() writes neither of the version 2 fields, so the
    // address, and the password in it, go no further than this.
    out.tomlContent = deepLinkConfigToToml(*cfg);
    out.skipVerification = cfg->skipVerification;
    out.subscriptionNotKept = !cfg->subscriptionUrl.isEmpty();
    return out;
}

} // namespace freetunnel
