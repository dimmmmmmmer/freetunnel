// cppcheck-suppress-file missingIncludeSystem
// TrustTunnel subscriptions in Backend, behind the hidden experimental/subscriptions
// setting: adding a config from one, with the user's consent before anything is
// fetched; refreshing it; and keeping the link through the config's renames,
// replacements and removal. SubscriptionController keeps the records, the
// fetches and when each is due; what a document does to a config and to the
// tunnel is decided here. A refresh never changes which config is active, and
// never connects or disconnects, but to reconnect a connected tunnel to the
// server it changed.
#include "app/Backend.h"

#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTimer>
#include <QUrl>

#include "BackendConfigShared.h"
#include "SubscriptionController.h"
#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "core/Subscription.h"
#include "core/SubscriptionFetcher.h"

using freetunnel::LinkRefusal;
using freetunnel::PreparedImport;
using freetunnel::SubscriptionDocError;
using freetunnel::SubscriptionFetchError;
using freetunnel::SubscriptionField;

namespace {

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readAll());
}

// What the editor would refuse to save, a server's part of it: a subscription's
// is checked the same way before anything of it is written.
bool isUsableServer(const freetunnel::ConfigToml &c)
{
    return freetunnel::backend_config::validateAddressList(c.addresses)
            && freetunnel::backend_config::validateDnsList(c.dns) && freetunnel::isValidClientRandom(c.clientRandom);
}

// What a change list says in a log line, in the config file's own words: the
// server's name and addresses with their values, which the log shows anyway
// when connecting, and the rest by name alone.
QString changeSummary(const QList<freetunnel::SubscriptionChange> &changes)
{
    QStringList parts;
    for (const freetunnel::SubscriptionChange &c : changes) {
        switch (c.field) {
        case SubscriptionField::Hostname:
            parts << QStringLiteral("hostname %1 → %2").arg(c.oldValue, c.newValue);
            break;
        case SubscriptionField::Address:
            parts << QStringLiteral("addresses %1 → %2").arg(c.oldValue, c.newValue);
            break;
        case SubscriptionField::Username:
            parts << QStringLiteral("username");
            break;
        case SubscriptionField::Password:
            parts << QStringLiteral("password");
            break;
        case SubscriptionField::Certificate:
            parts << QStringLiteral("certificate");
            break;
        case SubscriptionField::CustomSni:
            parts << QStringLiteral("custom_sni");
            break;
        case SubscriptionField::ClientRandom:
            parts << QStringLiteral("client_random");
            break;
        }
    }
    return parts.join(QStringLiteral(", "));
}

// The config a document makes, checked as the editor checks one.
std::optional<PreparedImport> serverOfDocument(const freetunnel::SubscriptionDocument &document)
{
    const freetunnel::SubscriptionApplied made =
            freetunnel::applySubscription({}, {}, document, freetunnel::SubscriptionApplyMode::Creation);
    if (!isUsableServer(made.config))
        return std::nullopt;
    std::optional<PreparedImport> out = freetunnel::prepareSubscriptionImport(made.config, made.password);
    if (out)
        out->certificateExtraIgnored = out->certificateExtraIgnored || document.extraPemIgnored;
    return out;
}

QString hostKey(const QString &hostname)
{
    const QByteArray ace = QUrl::toAce(hostname);
    return (ace.isEmpty() ? hostname : QString::fromLatin1(ace)).toLower();
}

} // namespace

void Backend::startSubscriptions()
{
    m_subscriptions = new SubscriptionController(appVersion(), this);
    connect(m_subscriptions, &SubscriptionController::fetchFinished, this, &Backend::onSubscriptionFetched);
    connect(m_subscriptions, &SubscriptionController::refreshDue, this, [this](const QString &path) {
        if (automaticRefreshAllowed(path))
            startSubscriptionRefresh(path, false);
    });
    m_subscriptions->start(m_paths, m_settings.subscriptions_enabled);
}

// ---- adding a config from a subscription ---------------------------------

// A link with a subscription address and nothing else this version refuses (an
// auth key, a damaged server). False sends the link on as it would go without
// subscriptions, and what it says about it is said there.
bool Backend::offerSubscriptionLink(const QString &link)
{
    if (!m_settings.subscriptions_enabled)
        return false;
    const std::optional<freetunnel::DeepLinkConfig> cfg = freetunnel::parseDeepLink(link);
    if (!cfg || cfg->subscriptionUrl.isEmpty())
        return false;
    LinkRefusal why = LinkRefusal::Damaged;
    const std::optional<PreparedImport> given = freetunnel::prepareDeepLinkImport(link, nullptr, &why);
    if (!given && why != LinkRefusal::SubscriptionOnly)
        return false;
    // What the fetch trusts is what the link pins, read as the link's import reads it.
    offerSubscription(cfg->subscriptionUrl, freetunnel::parseConfigToml(freetunnel::deepLinkConfigToToml(*cfg)),
                      given ? &*given : nullptr, false);
    return true;
}

// The same for the TOML file an endpoint exports.
bool Backend::offerSubscriptionExport(const QString &content, const QString &fileName)
{
    if (!m_settings.subscriptions_enabled)
        return false;
    const std::optional<freetunnel::EndpointExport> exported = freetunnel::parseEndpointExport(content);
    if (!exported || exported->subscriptionUrl.isEmpty())
        return false;
    LinkRefusal why = LinkRefusal::Damaged;
    const std::optional<PreparedImport> given =
            freetunnel::prepareEndpointExportImport(content, fileName, nullptr, &why);
    if (!given && why != LinkRefusal::SubscriptionOnly)
        return false;
    offerSubscription(exported->subscriptionUrl, exported->config, given ? &*given : nullptr, true);
    return true;
}

bool Backend::beginSubscriptionImport(const QString &url)
{
    return m_settings.subscriptions_enabled && offerSubscription(url.trimmed(), {}, nullptr, false);
}

// Step one, with nothing fetched: whoever made the link chose the address, and
// fetching it unasked would tell them when the link was opened, and from where.
bool Backend::offerSubscription(const QString &url, const freetunnel::ConfigToml &trustSource,
                                const PreparedImport *given, bool fromFile)
{
    const std::optional<freetunnel::SubscriptionUrl> parsed = freetunnel::parseSubscriptionUrl(url);
    if (!parsed) {
        emit errorOccurred(
                freetunnel::subscriptionFailureText(SubscriptionFetchError::BadUrl, SubscriptionDocError::Invalid));
        return false;
    }
    // The address holds the VPN password, and the credential store is the one
    // place it may be kept.
    if (!freetunnel::CredentialStore::secureStorageAvailable()) {
        emit errorOccurred(tr("Secure credential storage is unavailable, so the subscription can't be added: "
                              "its address holds the VPN password."));
        return false;
    }
    SubscriptionImport import;
    import.url = url;
    import.origin = freetunnel::redactUrl(parsed->requestUrl);
    import.identity = parsed->identity;
    import.trustSource = trustSource;
    if (given)
        import.given = *given;
    const QString originAndPath = freetunnel::redactUrl(parsed->requestUrl, freetunnel::RedactKeep::OriginAndPath);
    const QVariantMap details{
            {QStringLiteral("origin"), originAndPath},
            {QStringLiteral("server"), given ? freetunnel::parseConfigToml(given->tomlContent).hostname : QString()},
            {QStringLiteral("fromFile"), fromFile},
            {QStringLiteral("skipVerification"), given && given->skipVerification},
            {QStringLiteral("mixedScripts"),
             given && freetunnel::nameMixesScripts(QFileInfo(given->fileName).completeBaseName())},
    };
    m_subscriptions->open(std::move(import), details);
    return true;
}

bool Backend::fetchSubscriptionForImport(const QString &token)
{
    SubscriptionImport *import = m_subscriptions->pendingImport(token);
    if (!m_settings.subscriptions_enabled || !import || import->fetching || import->fetched)
        return false;
    import->fetching = true;
    import->fetchFailed = false;
    return m_subscriptions->fetch(token, freetunnel::subscriptionRequestFor(import->url, import->trustSource), true);
}

void Backend::cancelSubscriptionImport(const QString &token)
{
    m_subscriptions->close(token);
}

void Backend::onSubscriptionFetched(const QString &key, bool manual, const freetunnel::SubscriptionFetchResult &result)
{
    if (SubscriptionController::isImportToken(key)) {
        onSubscriptionImportFetched(key, result);
        return;
    }
    if (!m_subscriptions->isLinked(key)) // unlinked or removed while it fetched
        return;
    if (!result.document) {
        const QString code = freetunnel::subscriptionFetchCode(result.error, result.documentError.kind);
        m_subscriptions->noteFailure(key, code, freetunnel::refreshPolicyFor(result.error, result.documentError.kind));
        logSubscription(key, tr("Subscription %1 did not update “%2”: %3"), code);
        return;
    }
    HeldSubscriptionUpdate update;
    update.document = *result.document;
    update.hint = result.hint;
    update.manual = manual;
    applySubscriptionUpdate(key, update);
}

// Step two: the question every link is asked, about the server the subscription
// names, or, when the fetch failed, about the server given with it, with why.
// Asked with the import's token in place of a link: the address stays here.
void Backend::onSubscriptionImportFetched(const QString &token, const freetunnel::SubscriptionFetchResult &result)
{
    SubscriptionImport *import = m_subscriptions->pendingImport(token);
    if (!import)
        return;
    import->fetching = false;
    QString why = freetunnel::subscriptionFailureText(result.error, result.documentError.kind);
    QString code = freetunnel::subscriptionFetchCode(result.error, result.documentError.kind);
    freetunnel::SubscriptionRefreshPolicy policy =
            freetunnel::refreshPolicyFor(result.error, result.documentError.kind);
    const QString updatesFrom = tr("Updates from %1").arg(import->origin);
    const QString existing = m_subscriptions->pathWithIdentity(import->identity, m_paths);
    if (result.document) {
        if (std::optional<PreparedImport> fetched = serverOfDocument(*result.document)) {
            fetched->link = token;
            fetched->existingPath = existing;
            import->fetched = fetched;
            import->hint = result.hint;
            QStringList notes{updatesFrom};
            const QString givenHost =
                    import->given ? freetunnel::parseConfigToml(import->given->tomlContent).hostname : QString();
            if (!givenHost.isEmpty() && hostKey(givenHost) != hostKey(result.document->hostname))
                notes << tr("The server given with it is %1.").arg(givenHost);
            askToImport(*fetched, notes);
            return;
        }
        why = freetunnel::subscriptionFailureText(SubscriptionFetchError::Document, SubscriptionDocError::Invalid);
        code = freetunnel::subscriptionFetchCode(SubscriptionFetchError::Document, SubscriptionDocError::Invalid);
        policy = freetunnel::SubscriptionRefreshPolicy::Backoff;
    }
    import->fetchFailed = true;
    import->failureCode = code;
    import->failurePolicy = policy;
    if (!import->given) {
        m_subscriptions->close(token);
        emit errorOccurred(why);
        return;
    }
    PreparedImport given = *import->given;
    given.link = token;
    given.existingPath = existing;
    // It is linked, so the note that it would not be is not said.
    given.subscriptionNotKept = false;
    askToImport(given, {why,
                        tr("The server given with the subscription can be added now, and updates from it later."),
                        updatesFrom});
}

// Through the question's own answer: the server the subscription named, or
// the one given with it once the fetch failed.
bool Backend::confirmPendingSubscriptionImport(const QString &token, bool replaceExisting)
{
    const SubscriptionImport *import = m_subscriptions->pendingImport(token);
    return confirmSubscriptionImport(token, replaceExisting, import && !import->fetched);
}

// In this order, so whatever fails leaves nothing behind it: the address goes
// into the credential store, read back; the config is written as any link's is;
// then the link between them. A config whose link cannot be saved stays, and
// does not update.
bool Backend::confirmSubscriptionImport(const QString &token, bool replaceExisting, bool useLinkFallback)
{
    if (!m_settings.subscriptions_enabled)
        return false;
    const SubscriptionImport *pending = m_subscriptions->pendingImport(token);
    if (!pending) {
        emit errorOccurred(tr("That import has expired. Open the link again."));
        return false;
    }
    // A copy: what follows runs the credential store's event loop.
    const SubscriptionImport import = *pending;
    const std::optional<PreparedImport> &server = useLinkFallback ? import.given : import.fetched;
    if (!server || (useLinkFallback && !import.fetchFailed))
        return false;
    m_subscriptions->close(token);
    PreparedImport prepared = *server;
    prepared.existingPath = m_subscriptions->pathWithIdentity(import.identity, m_paths);
    const QString target = importTargetFor(prepared, replaceExisting);
    const QString sourceId = m_subscriptions->storeAddress(import.url);
    if (sourceId.isEmpty()) {
        reportPasswordNotStored();
        return false;
    }
    if (!importPreparedDeepLink(prepared, target)) {
        m_subscriptions->deleteAddress(sourceId);
        return false;
    }
    if (!m_subscriptions->link(target, sourceId, import, useLinkFallback)) {
        m_subscriptions->deleteAddress(sourceId);
        emit errorOccurred(tr("Config added: %1. It won't update from its subscription, which couldn't be saved.")
                                   .arg(nameForPath(target)));
    }
    return true;
}

// ---- refreshing a linked config ------------------------------------------

bool Backend::refreshSubscription(const QString &path)
{
    return m_settings.subscriptions_enabled && m_subscriptions->isLinked(path) && startSubscriptionRefresh(path, true);
}

// The fetch trusts what the config pins, as it is now: after a refresh that
// moved the server elsewhere, the subscription's own host and the system's CAs.
bool Backend::startSubscriptionRefresh(const QString &path, bool manual)
{
    if (m_subscriptions->isFetching(path) || m_subscriptions->isBusy(path))
        return false;
    const QString url = m_subscriptions->address(path);
    // Asked of the credential store, which runs an event loop meanwhile.
    if (!m_subscriptions->isLinked(path) || m_subscriptions->isFetching(path))
        return false;
    if (url.isEmpty()) {
        // A locked keyring answers nothing, and may be open next time.
        m_subscriptions->noteFailure(path, QStringLiteral("no_address"),
                                     freetunnel::SubscriptionRefreshPolicy::Backoff);
        return false;
    }
    const freetunnel::ConfigToml current = freetunnel::parseConfigToml(readText(path));
    return m_subscriptions->fetch(path, freetunnel::subscriptionRequestFor(url, current), manual);
}

// Skipped, and not counted as a failure, while it could not get through: with
// the kill switch on, a tunnel that is coming up blocks everything else. And
// never by itself for a config that hides its server's name behind a custom SNI:
// a fetch has to send the real name, in the clear.
bool Backend::automaticRefreshAllowed(const QString &path) const
{
    if (!m_settings.subscriptions_enabled || (m_settings.killswitch_enabled && m_connecting))
        return false;
    return freetunnel::parseConfigToml(readText(path)).customSni.trimmed().isEmpty();
}

// What a document fetched for a linked config does to it: nothing when nothing
// changed; held for the user when it names another server; held while the
// tunnel is being built from it or torn down, or while the editor has it open;
// otherwise written as the editor writes a config, TOML and password together,
// and a connected tunnel is rebuilt with it.
void Backend::applySubscriptionUpdate(const QString &path, const HeldSubscriptionUpdate &update)
{
    // Already being applied, from under the credential store's event loop: asked
    // again at the next check rather than written over the one under way.
    if (m_subscriptions->isBusy(path)) {
        m_subscriptions->refreshSoon(path);
        return;
    }
    m_subscriptions->setBusy(path, true);
    const auto done = qScopeGuard([this, path]() { m_subscriptions->setBusy(path, false); });
    const freetunnel::ConfigToml current = freetunnel::parseConfigToml(readText(path));
    QString password = freetunnel::CredentialStore::loadPassword(freetunnel::CredentialStore::keyForConfigPath(path));
    if (password.isEmpty())
        password = current.password;
    if (!m_subscriptions->isLinked(path))
        return;
    const freetunnel::SubscriptionApplied next = freetunnel::applySubscription(
            current, password, update.document, freetunnel::SubscriptionApplyMode::Refresh);
    if (!isUsableServer(next.config)) {
        const QString code =
                freetunnel::subscriptionFetchCode(SubscriptionFetchError::Document, SubscriptionDocError::Invalid);
        m_subscriptions->noteFailure(path, code, freetunnel::SubscriptionRefreshPolicy::Backoff);
        logSubscription(path, tr("Subscription %1 did not update “%2”: %3"), code);
        return;
    }
    // Nothing to write: the time of the fetch moves, and that is all.
    if (next.changes.isEmpty()) {
        m_subscriptions->noteApplied(path, update, QString());
        return;
    }
    const QString summary = changeSummary(next.changes);
    if (next.hostnameChanged && !m_subscriptions->hostnameAccepted(path, update.document.hostname)) {
        m_subscriptions->holdForHostname(path, update, current.hostname);
        m_subscriptions->noteDocument(path, update.hint, false, QStringLiteral("held_hostname"));
        logSubscription(path, tr("Subscription %1 has an update for “%2”, held: %3"), summary);
        return;
    }
    if (subscriptionUpdateWaits(path)) {
        m_subscriptions->hold(path, update);
        m_subscriptions->noteDocument(path, update.hint, update.manual, QStringLiteral("held"));
        logSubscription(path, tr("Subscription %1 has an update for “%2”, held: %3"), summary);
        return;
    }
    // A failure on either leaves both as they were (saveConfigWithPassword).
    QString saveErr;
    if (!freetunnel::backend_config::saveConfigWithPassword(path, freetunnel::buildConfigToml(next.config).toUtf8(),
                                                            next.password, password, &saveErr)) {
        const QString code = QStringLiteral("not_saved");
        m_subscriptions->noteFailure(path, code, freetunnel::SubscriptionRefreshPolicy::Backoff);
        logSubscription(path, tr("Subscription %1 did not update “%2”: %3"), code);
        return;
    }
    m_subscriptions->noteApplied(path, update, summary);
    logSubscription(path, tr("Subscription %1 updated “%2”: %3"), summary);
    if (path == m_activePath && m_connected)
        reconnectActiveConfig();
}

// The active config while its tunnel is between states, or the config the
// editor has open, whose form would otherwise show what the file no longer says.
bool Backend::subscriptionUpdateWaits(const QString &path) const
{
    if (path == m_configOpenInEditor)
        return true;
    return path == m_activePath
            && (m_connecting || m_disconnecting || m_reapplying || m_awaitingToml || m_pendingReconnect);
}

// Later, from the event loop: this is called from the VPN client's own state
// callback, and applying can rebuild the tunnel and read the credential store.
void Backend::flushHeldSubscriptionUpdates()
{
    if (!m_subscriptions || m_subscriptions->heldPaths().isEmpty())
        return;
    QTimer::singleShot(0, this, [this]() {
        for (const QString &path : m_subscriptions->heldPaths()) {
            const std::optional<HeldSubscriptionUpdate> held = m_subscriptions->held(path);
            const std::optional<freetunnel::SubscriptionRecord> record = m_subscriptions->record(path);
            if (held && record && record->heldHostname.isEmpty() && !subscriptionUpdateWaits(path))
                applySubscriptionUpdate(path, *held);
        }
    });
}

bool Backend::answerHostnameChange(const QString &path, bool accept)
{
    if (!m_subscriptions->answerHostname(path, accept))
        return false;
    if (!accept)
        return true;
    // The document went with a restart: fetched again, it is applied as accepted.
    if (m_subscriptions->held(path))
        flushHeldSubscriptionUpdates();
    else if (m_settings.subscriptions_enabled)
        startSubscriptionRefresh(path, true);
    return true;
}

void Backend::setConfigOpenInEditor(const QString &path)
{
    m_configOpenInEditor = path;
    flushHeldSubscriptionUpdates();
}

// ---- keeping the link through what happens to its config -------------------

bool Backend::detachSubscription(const QString &path)
{
    return m_subscriptions->forget(path);
}

void Backend::forgetSubscription(const QString &path)
{
    m_subscriptions->forget(path);
}

// A linked config's server is its subscription's: an editor save keeps what
// the file and the credential store hold for it, so a form opened before a
// refresh does not write the old server back. The password stays the form's
// when the store gives none, rather than none.
bool Backend::keepSubscriptionOwnedFields(const QString &path, const QString &fileText, const QString &storedPassword,
                                          freetunnel::ConfigToml *ct, QString *password) const
{
    if (!m_settings.subscriptions_enabled || path.isEmpty() || !m_subscriptions->isLinked(path))
        return false;
    const freetunnel::ConfigToml file = freetunnel::parseConfigToml(fileText);
    ct->hostname = file.hostname;
    ct->addresses = file.addresses;
    ct->username = file.username;
    ct->certificate = file.certificate;
    ct->customSni = file.customSni;
    ct->clientRandom = file.clientRandom;
    if (!storedPassword.isEmpty())
        *password = storedPassword;
    return true;
}

// One line in the log: `line` names the subscription by its origin (%1), never
// by its address, then the config (%2) and `detail` (%3).
void Backend::logSubscription(const QString &path, const QString &line, const QString &detail)
{
    const std::optional<freetunnel::SubscriptionRecord> record = m_subscriptions->record(path);
    appendLog(QStringLiteral("INFO"), line.arg(record ? record->origin : QString(), nameForPath(path), detail));
}
