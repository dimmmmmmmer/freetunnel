// cppcheck-suppress-file missingIncludeSystem
#include "SubscriptionController.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QUuid>

#include <algorithm>
#include <iterator>

#include "core/CredentialStore.h"

using freetunnel::CredentialStore;
using freetunnel::StoredSecret;
using freetunnel::SubscriptionRecord;
using freetunnel::SubscriptionRefreshPolicy;

namespace {

// The scheduler wakes this often and compares wall-clock times, so a refresh
// that fell due while the computer slept is made at the next check. No config
// is fetched automatically more often than this either.
constexpr qint64 kCheckSeconds = 10 * 60;
// Added to the refresh interval, and how long after start-up the first check
// comes: the configs of many users do not all fetch in the same minute.
constexpr qint64 kMaxJitterSeconds = 10 * 60;
// Waits after one, two and three failures in a row; after that, the interval.
constexpr qint64 kBackoffSeconds[] = {10 * 60, 30 * 60, 2 * 60 * 60};
// How long an import waits for the user, from the question before the fetch.
constexpr qint64 kImportSeconds = 10 * 60;
// Fetches at a time, imports and refreshes together.
constexpr int kMaxFetches = 2;

const QString kTokenPrefix = QStringLiteral("subscription-import:");

} // namespace

SubscriptionController::SubscriptionController(const QString &appVersion, QObject *parent)
    : QObject(parent)
    , m_appVersion(appVersion)
{
    m_timer.setInterval(static_cast<int>(kCheckSeconds * 1000));
    connect(&m_timer, &QTimer::timeout, this, &SubscriptionController::tick);
    m_firstCheck.setSingleShot(true);
    connect(&m_firstCheck, &QTimer::timeout, this, [this]() {
        tick();
        m_timer.start();
    });
}

void SubscriptionController::start(const QStringList &configPaths, bool scheduling)
{
    m_records = freetunnel::loadSubscriptions();
    bool removed = false;
    for (const QString &path : m_records.keys()) {
        if (configPaths.contains(path))
            continue;
        const QString sourceId = m_records.take(path).sourceId;
        dropAddressUnlessShared(sourceId);
        removed = true;
    }
    if (removed)
        save();
    if (scheduling)
        m_firstCheck.start(static_cast<int>(jitter() * 1000));
}

std::optional<SubscriptionRecord> SubscriptionController::record(const QString &path) const
{
    const auto it = m_records.constFind(path);
    if (it == m_records.constEnd())
        return std::nullopt;
    return *it;
}

QString SubscriptionController::pathWithIdentity(const QString &identity, const QStringList &among) const
{
    for (const QString &path : among) {
        const auto it = m_records.constFind(path);
        if (it != m_records.constEnd() && !identity.isEmpty() && it->identity == identity)
            return path;
    }
    return QString();
}

QString SubscriptionController::storeAddress(const QString &url)
{
    const QString sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString key = CredentialStore::keyForSubscription(sourceId);
#ifdef FT_ENABLE_TEST_HOOKS
    m_storedForTest.append(sourceId);
#endif
    if (CredentialStore::storePassword(key, url)) {
        // The value read back, not just something there, and not a read that failed.
        const StoredSecret stored = CredentialStore::lookup(key);
        if (stored.found() && stored.value == url)
            return sourceId;
    }
    CredentialStore::deletePassword(key);
    return QString();
}

void SubscriptionController::deleteAddress(const QString &sourceId)
{
    if (!sourceId.isEmpty())
        CredentialStore::deletePassword(CredentialStore::keyForSubscription(sourceId));
}

// Two records never share an entry unless subscriptions.json was edited by hand;
// then the other one keeps it.
void SubscriptionController::dropAddressUnlessShared(const QString &sourceId)
{
    const bool shared = std::any_of(m_records.cbegin(), m_records.cend(),
                                    [&sourceId](const SubscriptionRecord &r) { return r.sourceId == sourceId; });
    if (!shared)
        deleteAddress(sourceId);
}

StoredSecret SubscriptionController::address(const QString &path) const
{
    const auto it = m_records.constFind(path);
    if (it == m_records.constEnd())
        return StoredSecret();
    return CredentialStore::lookup(CredentialStore::keyForSubscription(it->sourceId));
}

bool SubscriptionController::link(const QString &path, const QString &sourceId, const SubscriptionImport &import,
                                  bool given)
{
    SubscriptionRecord record;
    record.sourceId = sourceId;
    record.origin = import.origin;
    record.identity = import.identity;
    if (given)
        markFailure(record, import.failureCode, import.failurePolicy);
    else
        markDocument(record, import.hint, true);
    const std::optional<SubscriptionRecord> previous = this->record(path);
    m_records.insert(path, record);
    if (!freetunnel::saveSubscriptions(m_records)) {
        if (previous)
            m_records.insert(path, *previous);
        else
            m_records.remove(path);
        return false;
    }
    if (previous && previous->sourceId != sourceId)
        dropAddressUnlessShared(previous->sourceId);
    emit statusChanged(path);
    return true;
}

bool SubscriptionController::forget(const QString &path)
{
    abortFetch(path);
    m_held.remove(path);
    m_acceptedHostnames.remove(path);
    if (!m_records.contains(path))
        return false;
    const QString sourceId = m_records.take(path).sourceId;
    save();
    dropAddressUnlessShared(sourceId);
    emit statusChanged(path);
    return true;
}

// The address's entry is the subscription's, not the path's: nothing moves in the
// credential store. A fetch for the old path is let go, and its result with it.
void SubscriptionController::rename(const QString &from, const QString &to)
{
    if (from == to || !m_records.contains(from))
        return;
    abortFetch(from);
    m_records.insert(to, m_records.take(from));
    if (m_held.contains(from))
        m_held.insert(to, m_held.take(from));
    if (m_acceptedHostnames.contains(from))
        m_acceptedHostnames.insert(to, m_acceptedHostnames.take(from));
    save();
    emit statusChanged(from);
    emit statusChanged(to);
}

void SubscriptionController::save()
{
    if (!freetunnel::saveSubscriptions(m_records))
        qWarning("SubscriptionController: subscriptions.json could not be written");
}

qint64 SubscriptionController::now() const
{
    return m_clock ? m_clock() : QDateTime::currentSecsSinceEpoch();
}

qint64 SubscriptionController::jitter() const
{
    return m_random ? std::clamp<qint64>(m_random(kMaxJitterSeconds), 0, kMaxJitterSeconds)
                    : QRandomGenerator::global()->bounded(kMaxJitterSeconds + 1);
}

// The interval is the operator's, when there is a hint, with jitter on top.
void SubscriptionController::markDocument(SubscriptionRecord &r, std::optional<qint64> hint,
                                          bool resumeAutomatic) const
{
    const qint64 t = now();
    r.lastFetchedAt = r.lastAttemptAt = t;
    r.lastStatus = QStringLiteral("ok");
    r.failures = 0;
    r.hintSeconds = hint.value_or(0);
    r.nextAttemptAt = t + freetunnel::subscriptionRefreshInterval(hint) + jitter();
    if (resumeAutomatic)
        r.autoRefresh = true;
}

// Sooner at first after what may pass, then at the interval; never again by
// itself after what only the user can fix.
void SubscriptionController::markFailure(SubscriptionRecord &r, const QString &code,
                                         SubscriptionRefreshPolicy policy) const
{
    const qint64 t = now();
    r.lastAttemptAt = t;
    r.lastStatus = code;
    ++r.failures;
    if (policy == SubscriptionRefreshPolicy::ManualOnly) {
        r.autoRefresh = false;
        return;
    }
    if (policy == SubscriptionRefreshPolicy::Backoff && r.failures <= static_cast<int>(std::size(kBackoffSeconds))) {
        r.nextAttemptAt = t + kBackoffSeconds[r.failures - 1];
        return;
    }
    const std::optional<qint64> hint = r.hintSeconds > 0 ? std::optional<qint64>(r.hintSeconds) : std::nullopt;
    r.nextAttemptAt = t + freetunnel::subscriptionRefreshInterval(hint) + jitter();
}

void SubscriptionController::noteDocument(const QString &path, std::optional<qint64> hint, bool resumeAutomatic,
                                          const QString &status)
{
    const auto it = m_records.find(path);
    if (it == m_records.end())
        return;
    markDocument(*it, hint, resumeAutomatic);
    it->lastStatus = status;
    save();
    emit statusChanged(path);
}

void SubscriptionController::noteFailure(const QString &path, const QString &code, SubscriptionRefreshPolicy policy)
{
    const auto it = m_records.find(path);
    if (it == m_records.end())
        return;
    markFailure(*it, code, policy);
    save();
    emit statusChanged(path);
}

void SubscriptionController::noteApplied(const QString &path, const HeldSubscriptionUpdate &update,
                                         const QString &summary)
{
    m_held.remove(path);
    m_acceptedHostnames.remove(path);
    const auto it = m_records.find(path);
    if (it != m_records.end())
        it->heldHostname.clear();
    noteDocument(path, update.hint, update.manual);
    if (!summary.isEmpty())
        emit updated(path, summary);
}

void SubscriptionController::refreshSoon(const QString &path)
{
    const auto it = m_records.find(path);
    if (it != m_records.end() && it->autoRefresh) {
        it->nextAttemptAt = now();
        save();
    }
}

void SubscriptionController::hold(const QString &path, const HeldSubscriptionUpdate &update)
{
    m_held.insert(path, update);
    emit statusChanged(path);
}

void SubscriptionController::holdForHostname(const QString &path, const HeldSubscriptionUpdate &update,
                                             const QString &oldHostname)
{
    const auto it = m_records.find(path);
    if (it == m_records.end())
        return;
    m_held.insert(path, update);
    m_acceptedHostnames.remove(path);
    it->heldHostname = update.document.hostname;
    save();
    emit hostnameChangeHeld(path, oldHostname, update.document.hostname);
    emit statusChanged(path);
}

std::optional<HeldSubscriptionUpdate> SubscriptionController::held(const QString &path) const
{
    const auto it = m_held.constFind(path);
    if (it == m_held.constEnd())
        return std::nullopt;
    return *it;
}

bool SubscriptionController::answerHostname(const QString &path, bool accept)
{
    const auto it = m_records.find(path);
    if (it == m_records.end() || it->heldHostname.isEmpty())
        return false;
    if (accept) {
        m_acceptedHostnames.insert(path, it->heldHostname);
    } else {
        m_held.remove(path);
        it->autoRefresh = false;
    }
    it->heldHostname.clear();
    save();
    emit statusChanged(path);
    return true;
}

bool SubscriptionController::hostnameAccepted(const QString &path, const QString &hostname) const
{
    const auto it = m_acceptedHostnames.constFind(path);
    return it != m_acceptedHostnames.constEnd() && freetunnel::sameSubscriptionHost(*it, hostname);
}

void SubscriptionController::setBusy(const QString &path, bool busy)
{
    if (busy)
        m_busy.insert(path);
    else
        m_busy.remove(path);
}

// A new import takes the place of one still waiting, and its fetch.
QString SubscriptionController::open(SubscriptionImport import, const QVariantMap &details)
{
    if (m_import)
        close(m_import->token);
    import.token = kTokenPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
    import.expiresAt = now() + kImportSeconds;
    m_import = std::move(import);
    const QString token = m_import->token;
    emit consentRequired(token, details);
    return token;
}

SubscriptionImport *SubscriptionController::pendingImport(const QString &token)
{
    if (!m_import || m_import->token != token)
        return nullptr;
    if (now() > m_import->expiresAt) {
        close(token);
        return nullptr;
    }
    return &*m_import;
}

void SubscriptionController::close(const QString &token)
{
    if (!m_import || m_import->token != token)
        return;
    abortFetch(token);
    m_import.reset();
}

bool SubscriptionController::isImportToken(const QString &text)
{
    return text.startsWith(kTokenPrefix);
}

bool SubscriptionController::fetch(const QString &key, const freetunnel::SubscriptionFetchRequest &request,
                                   bool manual)
{
    if (isFetching(key))
        return false;
    Fetch job;
    job.key = key;
    job.manual = manual;
    job.request = request;
    m_queued.append(job);
    pump();
    return true;
}

bool SubscriptionController::isFetching(const QString &key) const
{
    const auto byKey = [&key](const Fetch &f) { return f.key == key; };
    return std::any_of(m_queued.cbegin(), m_queued.cend(), byKey)
            || std::any_of(m_running.cbegin(), m_running.cend(), byKey);
}

void SubscriptionController::abortFetch(const QString &key)
{
    m_queued.removeIf([&key](const Fetch &f) { return f.key == key; });
    for (qsizetype i = m_running.size() - 1; i >= 0; --i) {
        if (m_running.at(i).key != key)
            continue;
        freetunnel::SubscriptionFetcher *fetcher = m_running.takeAt(i).fetcher;
        fetcher->disconnect(this);
        fetcher->abort();
        fetcher->deleteLater();
    }
    pump();
}

void SubscriptionController::pump()
{
    while (m_running.size() < kMaxFetches && !m_queued.isEmpty()) {
        Fetch job = m_queued.takeFirst();
        job.fetcher = m_factory ? m_factory(this) : new freetunnel::SubscriptionFetcher(m_appVersion, this);
        freetunnel::SubscriptionFetcher *fetcher = job.fetcher;
        connect(fetcher, &freetunnel::SubscriptionFetcher::finished, this,
                [this, fetcher](const freetunnel::SubscriptionFetchResult &result) { onFetched(fetcher, result); });
        m_running.append(job);
        fetcher->start(job.request);
    }
}

void SubscriptionController::onFetched(freetunnel::SubscriptionFetcher *fetcher,
                                       const freetunnel::SubscriptionFetchResult &result)
{
    const auto it = std::find_if(m_running.begin(), m_running.end(),
                                 [fetcher](const Fetch &f) { return f.fetcher == fetcher; });
    if (it == m_running.end())
        return;
    const Fetch job = *it;
    m_running.erase(it);
    fetcher->disconnect(this);
    fetcher->deleteLater();
    emit fetchFinished(job.key, job.manual, result);
    pump();
}

// A config is due once its time has come, unless it fetched less than a check
// ago, is fetching or being applied, or has automatic refresh off. Backend
// decides whether the refresh may run now.
void SubscriptionController::tick()
{
    const qint64 t = now();
    for (const QString &path : m_records.keys()) {
        const auto it = m_records.constFind(path);
        if (it == m_records.constEnd() || !it->autoRefresh || it->nextAttemptAt > t
                || t - it->lastAttemptAt < kCheckSeconds || isFetching(path) || isBusy(path))
            continue;
        emit refreshDue(path);
    }
}
