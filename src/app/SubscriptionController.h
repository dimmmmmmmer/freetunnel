// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The TrustTunnel subscriptions behind the hidden experimental/subscriptions
// setting: which configs update from one (subscriptions.json, and each address in
// the credential store), the import waiting for the user to agree to a fetch, the
// fetches in flight, the updates fetched and not applied yet, and when each linked
// config is refreshed next. Owned by Backend, which decides what a document does
// to a config and to the tunnel (BackendSubscriptions.cpp). Nothing here writes a
// config, and none of it is exposed to QML.

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <functional>
#include <optional>

#include "core/ConfigImport.h"
#include "core/ConfigToml.h"
#include "core/Subscription.h"
#include "core/SubscriptionFetcher.h"
#include "core/SubscriptionStore.h"

// A subscription being added, from the question before any fetch to the user's
// answer to what it fetched. Kept in memory only, and for ten minutes at most.
struct SubscriptionImport {
    QString token;    // names it to the window in place of the address (isImportToken)
    QString url;      // with its userinfo
    QString origin;   // redactUrl() of the address
    QString identity; // SubscriptionUrl::identity
    // The server the link or file gives besides the subscription, as it would be
    // added without it, and what a fetch for the subscription trusts: its name, its
    // first address and its certificate (subscriptionRequestFor).
    std::optional<freetunnel::PreparedImport> given;
    freetunnel::ConfigToml trustSource;
    qint64 expiresAt = 0;
    bool fetching = false;
    // What the fetch gave: the server the subscription names, or why there is
    // none, with when to try again, for adding the given server instead.
    std::optional<freetunnel::PreparedImport> fetched;
    std::optional<qint64> hint;
    bool fetchFailed = false;
    QString failureCode;
    freetunnel::SubscriptionRefreshPolicy failurePolicy = freetunnel::SubscriptionRefreshPolicy::Backoff;
};

// A document fetched for a linked config and not applied yet.
struct HeldSubscriptionUpdate {
    freetunnel::SubscriptionDocument document;
    std::optional<qint64> hint;
    bool manual = false; // the user asked for the refresh that fetched it
};

class SubscriptionController : public QObject {
    Q_OBJECT
public:
    explicit SubscriptionController(const QString &appVersion, QObject *parent = nullptr);

    // Reads subscriptions.json and removes each record whose config is not among
    // configPaths, with its address: what a crash or a failed save left behind.
    // Refreshes are scheduled only when `scheduling`.
    void start(const QStringList &configPaths, bool scheduling);

    // ---- configs linked to a subscription
    bool isLinked(const QString &path) const { return m_records.contains(path); }
    std::optional<freetunnel::SubscriptionRecord> record(const QString &path) const;
    // The first of `among` linked to the subscription with this identity.
    QString pathWithIdentity(const QString &identity, const QStringList &among) const;
    // A new entry in the credential store holding `url`, read back to be sure, or
    // empty when the store refused it.
    QString storeAddress(const QString &url);
    void deleteAddress(const QString &sourceId);
    QString address(const QString &path) const;
    // Links `path` to what `import` added, as fetched or, when `given`, as the link
    // or file gave it after the fetch failed. False when subscriptions.json could
    // not be written, and nothing is linked.
    bool link(const QString &path, const QString &sourceId, const SubscriptionImport &import, bool given);
    // Unlinks it: its fetch, its held update, its record and its address go.
    bool forget(const QString &path);
    void rename(const QString &from, const QString &to);

    // ---- what a refresh gave
    void noteDocument(const QString &path, std::optional<qint64> hint, bool resumeAutomatic,
                      const QString &status = QStringLiteral("ok"));
    void noteFailure(const QString &path, const QString &code, freetunnel::SubscriptionRefreshPolicy policy);
    // Written to the config, or nothing to write when `summary` is empty.
    void noteApplied(const QString &path, const HeldSubscriptionUpdate &update, const QString &summary);
    // Due at the next check, for a result that came while another was applied.
    void refreshSoon(const QString &path);

    // ---- updates fetched and not applied yet
    void hold(const QString &path, const HeldSubscriptionUpdate &update);
    // Held until the user accepts the server it names, or refuses it.
    void holdForHostname(const QString &path, const HeldSubscriptionUpdate &update, const QString &oldHostname);
    std::optional<HeldSubscriptionUpdate> held(const QString &path) const;
    QStringList heldPaths() const { return m_held.keys(); }
    // Accepted: what it names is applied, should it be named again. Refused: the
    // update is dropped, and only the user's own refresh fetches from then on.
    bool answerHostname(const QString &path, bool accept);
    bool hostnameAccepted(const QString &path, const QString &hostname) const;
    // A config being applied, which a nested event loop may otherwise reach again.
    void setBusy(const QString &path, bool busy);
    bool isBusy(const QString &path) const { return m_busy.contains(path); }

    // ---- the import waiting for the user, one at a time
    QString open(SubscriptionImport import, const QVariantMap &details);
    SubscriptionImport *pendingImport(const QString &token); // nullptr once closed or expired
    void close(const QString &token);
    static bool isImportToken(const QString &text);

    // ---- fetches, at most kMaxFetches at a time; `key` is a config's path or a token
    bool fetch(const QString &key, const freetunnel::SubscriptionFetchRequest &request, bool manual);
    bool isFetching(const QString &key) const;
    void abortFetch(const QString &key);

    qint64 now() const;

signals:
    // Step one of an import: before anything is fetched, the user is asked whether
    // to. `details` has the origin and path of the address ("origin"), the server
    // given with it ("server", empty for none), "fromFile", and the warnings
    // ("skipVerification", "mixedScripts").
    void consentRequired(const QString &token, const QVariantMap &details);
    void hostnameChangeHeld(const QString &path, const QString &oldHostname, const QString &newHostname);
    void statusChanged(const QString &path);
    void updated(const QString &path, const QString &summary);
    // For Backend: a fetch ended, and a linked config is due for a refresh.
    void fetchFinished(const QString &key, bool manual, const freetunnel::SubscriptionFetchResult &result);
    void refreshDue(const QString &path);

#ifdef FT_ENABLE_TEST_HOOKS
public:
    using FetcherFactory = std::function<freetunnel::SubscriptionFetcher *(QObject *parent)>;
    void setFetcherFactoryForTest(FetcherFactory factory) { m_factory = std::move(factory); }
    void setClockForTest(std::function<qint64()> clock) { m_clock = std::move(clock); }
    // `random(bound)` stands in for a random number from 0 to bound.
    void setRandomForTest(std::function<qint64(qint64)> random) { m_random = std::move(random); }
    void runSchedulerTickForTest() { tick(); }
    bool schedulingForTest() const { return m_firstCheck.isActive() || m_timer.isActive(); }
    // The checks come from runSchedulerTickForTest() alone from then on.
    void stopSchedulerForTest()
    {
        m_firstCheck.stop();
        m_timer.stop();
    }
    // Every entry storeAddress() made, so a test can see none is left behind:
    // the store itself cannot be listed.
    const QStringList &storedAddressesForTest() const { return m_storedForTest; }
#endif

private:
    struct Fetch {
        QString key;
        bool manual = false;
        freetunnel::SubscriptionFetchRequest request;
        freetunnel::SubscriptionFetcher *fetcher = nullptr; // set once it runs
    };

    void tick();
    void pump();
    void onFetched(freetunnel::SubscriptionFetcher *fetcher, const freetunnel::SubscriptionFetchResult &result);
    qint64 jitter() const;
    void markDocument(freetunnel::SubscriptionRecord &r, std::optional<qint64> hint, bool resumeAutomatic) const;
    void markFailure(freetunnel::SubscriptionRecord &r, const QString &code,
                     freetunnel::SubscriptionRefreshPolicy policy) const;
    void save();
    void dropAddressUnlessShared(const QString &sourceId);

    QString m_appVersion;
    freetunnel::SubscriptionRecords m_records;
    QHash<QString, HeldSubscriptionUpdate> m_held;
    QHash<QString, QString> m_acceptedHostnames;
    QSet<QString> m_busy;
    std::optional<SubscriptionImport> m_import;
    QList<Fetch> m_queued;
    QList<Fetch> m_running;
    QTimer m_firstCheck; // 0 to 10 minutes after start-up
    QTimer m_timer;      // then every check
    std::function<freetunnel::SubscriptionFetcher *(QObject *)> m_factory;
    std::function<qint64()> m_clock;
    std::function<qint64(qint64)> m_random;
#ifdef FT_ENABLE_TEST_HOOKS
    QStringList m_storedForTest;
#endif
};
