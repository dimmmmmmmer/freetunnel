// cppcheck-suppress-file missingIncludeSystem
#include "core/ConfigImport.h"

#include <QCoreApplication>
#include <QDateTime>

#include "core/ConfigPaths.h"
#include "core/ConfigToml.h"
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

QString subscriptionNotKeptNote()
{
    return QCoreApplication::translate("ConfigImport",
                                       "Subscriptions aren't supported yet: this server won't update "
                                       "itself, and its subscription address isn't saved.");
}

QString certificateExtraIgnoredNote()
{
    return QCoreApplication::translate("ConfigImport", "Extra data in the certificate was ignored.");
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

namespace {

// A server's certificate field once only its certificates are kept: their DER,
// one after another, whether the field held anything at all, and whether
// anything besides them was left out of it.
struct KeptCertificates {
    QByteArray der;
    bool given = false;
    bool dropped = false;
};

// What a link and an endpoint's export both go through once read, so a server is
// refused, named and written the same way whichever way it arrived, and in the
// same order. `subscription` says whether it came with a subscription address.
// Split out of prepareDeepLinkImport().
std::optional<PreparedImport> prepareServer(DeepLinkConfig cfg, bool subscription,
                                            const KeptCertificates &certificates, QString *error,
                                            LinkRefusal *refusal)
{
    // A link that is only a subscription address names no server until the
    // subscription is fetched, and fetching one is later work. One that carries
    // its server as well imports that server, without the address (below).
    if (subscription && !deepLinkHasStaticServer(cfg))
        return refuse(LinkRefusal::SubscriptionOnly, subscriptionOnlyRefusal(), error, refusal);
    // The certificates alone go into the config (deepLinkConfigToToml). A link
    // whose certificate field holds none at all meant to pin the server all the
    // same, and without a pin a self-signed server would fail later, at the
    // handshake, with nothing to say why; so it is refused as damaged, as the
    // parser refuses bytes that do not split into elements.
    if (certificates.given && certificates.der.isEmpty())
        return refuse(LinkRefusal::Damaged, QCoreApplication::translate("DeepLink", "malformed certificate"),
                      error, refusal);
    cfg.certificate = certificates.der;
    QString name = cfg.name.trimmed();
    if (name.isEmpty()) {
        name = cfg.hostname.trimmed();
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
    // deepLinkConfigToToml() does not write the subscription address, so it, and
    // the password in it, go no further than this. The auth key is written, for
    // the import to move to the credential store with the password.
    out.tomlContent = deepLinkConfigToToml(cfg);
    out.skipVerification = cfg.skipVerification;
    out.subscriptionNotKept = subscription;
    out.certificateExtraIgnored = certificates.dropped;
    return out;
}

// The server an endpoint's export names, as a link carries one. Its addresses
// and DNS servers are lists here, and the client random keeps the export's
// spelling for the import to check, as a link's does.
DeepLinkConfig serverOfExport(const EndpointExport &exported, const QString &fallbackName)
{
    const ConfigToml &c = exported.config;
    DeepLinkConfig cfg;
    cfg.hostname = c.hostname;
    for (const QString &address : c.addresses.split(QLatin1Char(','))) {
        if (!address.trimmed().isEmpty())
            cfg.addresses << address.trimmed();
    }
    cfg.username = c.username;
    cfg.password = c.password;
    cfg.customSni = c.customSni;
    cfg.hasIpv6 = c.allowIpv6;
    cfg.skipVerification = c.skipVerification;
    cfg.antiDpi = c.antiDpi;
    // As buildConfigToml() writes it: anything but http3 is http2.
    cfg.upstreamProtocol = c.protocol == QLatin1String("http3") ? UpstreamProtocol::Http3 : UpstreamProtocol::Http2;
    cfg.dnsUpstreams = splitDnsList(c.dns);
    cfg.clientRandomPrefix = c.clientRandom;
    cfg.clientRandomAuthKey = exported.clientRandomAuthKey;
    cfg.name = exported.name.isEmpty() ? fallbackName.trimmed() : exported.name;
    return cfg;
}

// Whether the export names a server to connect to, as any config file has to: an
// address and a username. One with a subscription address may leave them to the
// subscription, and is refused for that instead (prepareServer). Judged on the
// server as the link carries it, with empty addresses left out: `addresses =
// [""]` names none, and the link made from it would be refused only after the
// user had said yes to it.
bool namesAServer(const DeepLinkConfig &cfg, bool subscription)
{
    return subscription || (!cfg.addresses.isEmpty() && !cfg.username.trimmed().isEmpty());
}

} // namespace

std::optional<PreparedImport> prepareDeepLinkImport(const QString &link, QString *error,
                                                    LinkRefusal *refusal) {
    if (refusal)
        *refusal = LinkRefusal::Damaged;
    auto cfg = parseDeepLink(link, error);
    if (!cfg) {
        return std::nullopt;
    }
    KeptCertificates certificates;
    certificates.given = !cfg->certificate.isEmpty();
    certificates.der = deepLinkCertificates(cfg->certificate, &certificates.dropped);
    auto out = prepareServer(*cfg, !cfg->subscriptionUrl.isEmpty(), certificates, error, refusal);
    if (out)
        out->link = link;
    return out;
}

// Damage first, as a link's parser refuses it before the rules that follow:
// a server the file does not name, then a client random or an auth key the core
// cannot use.
std::optional<PreparedImport> prepareEndpointExportImport(const QString &toml, const QString &fallbackName,
                                                          QString *error, LinkRefusal *refusal)
{
    if (refusal)
        *refusal = LinkRefusal::Damaged;
    const std::optional<EndpointExport> exported = parseEndpointExport(toml);
    DeepLinkConfig cfg = exported ? serverOfExport(*exported, fallbackName) : DeepLinkConfig();
    if (!exported || !namesAServer(cfg, exported->subscription))
        return refuse(LinkRefusal::Damaged,
                      QCoreApplication::translate("ConfigImport", "missing address or credentials"), error,
                      refusal);
    if (!isValidClientRandom(clientRandomForCore(cfg.clientRandomPrefix)))
        return refuse(LinkRefusal::Damaged,
                      QCoreApplication::translate("DeepLink", "malformed client_random value"), error, refusal);
    // As a link's tag 0x0F is: whole bytes of hex, or a key the core would do
    // without. The endpoint writes "" for none, which is no key.
    if (!isValidClientRandomAuthKey(cfg.clientRandomAuthKey))
        return refuse(LinkRefusal::Damaged,
                      QCoreApplication::translate("DeepLink", "malformed client_random_auth_key value"), error,
                      refusal);
    KeptCertificates certificates;
    certificates.given = !exported->config.certificate.trimmed().isEmpty();
    certificates.der = certificatesDer(exported->config.certificate, &certificates.dropped);
    auto out = prepareServer(cfg, exported->subscription, certificates, error, refusal);
    if (!out)
        return out;
    out->fromFile = true;
    // Version 1, or 2 with an auth key (0x0F), with the name (0x0C) and the
    // certificates alone, and never the subscription address (see encodeDeepLink).
    cfg.certificate = certificates.der;
    out->link = encodeDeepLink(cfg);
    return out;
}

// The server is a config's, as an export's is, so it is read as an export's:
// named after the document, or after its hostname when it gives no name.
std::optional<PreparedImport> prepareSubscriptionImport(const ConfigToml &server, const QString &password,
                                                        const QString &authKey, QString *error)
{
    EndpointExport asExport;
    asExport.config = server;
    asExport.config.password = password;
    asExport.clientRandomAuthKey = authKey.trimmed();
    asExport.name = server.name.trimmed();
    KeptCertificates certificates;
    certificates.given = !server.certificate.trimmed().isEmpty();
    certificates.der = certificatesDer(server.certificate, &certificates.dropped);
    return prepareServer(serverOfExport(asExport, QString()), false, certificates, error, nullptr);
}

} // namespace freetunnel
