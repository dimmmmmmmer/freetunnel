// cppcheck-suppress-file missingIncludeSystem
#include "core/SubscriptionStore.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include "core/ConfigStore.h"

namespace freetunnel {
namespace {

constexpr int kFormatVersion = 1;

QJsonObject toJson(const SubscriptionRecord &r)
{
    return QJsonObject{
            {QStringLiteral("source_id"), r.sourceId},
            {QStringLiteral("origin"), r.origin},
            {QStringLiteral("identity"), r.identity},
            {QStringLiteral("last_fetched_at"), r.lastFetchedAt},
            {QStringLiteral("last_attempt_at"), r.lastAttemptAt},
            {QStringLiteral("last_status"), r.lastStatus},
            {QStringLiteral("failures"), r.failures},
            {QStringLiteral("auto_refresh"), r.autoRefresh},
            {QStringLiteral("next_attempt_at"), r.nextAttemptAt},
            {QStringLiteral("hint_seconds"), r.hintSeconds},
            {QStringLiteral("held_hostname"), r.heldHostname},
    };
}

// A record whose source id is not one this app makes names no entry in the
// credential store it could have written, and is left out.
bool fromJson(const QJsonObject &o, SubscriptionRecord *r)
{
    r->sourceId = o.value(QStringLiteral("source_id")).toString();
    if (QUuid::fromString(r->sourceId).isNull())
        return false;
    r->origin = o.value(QStringLiteral("origin")).toString();
    r->identity = o.value(QStringLiteral("identity")).toString();
    r->lastFetchedAt = o.value(QStringLiteral("last_fetched_at")).toInteger();
    r->lastAttemptAt = o.value(QStringLiteral("last_attempt_at")).toInteger();
    r->lastStatus = o.value(QStringLiteral("last_status")).toString();
    r->failures = o.value(QStringLiteral("failures")).toInt();
    r->autoRefresh = o.value(QStringLiteral("auto_refresh")).toBool(true);
    r->nextAttemptAt = o.value(QStringLiteral("next_attempt_at")).toInteger();
    r->hintSeconds = o.value(QStringLiteral("hint_seconds")).toInteger();
    r->heldHostname = o.value(QStringLiteral("held_hostname")).toString();
    return true;
}

} // namespace

QString subscriptionsPath()
{
    return QFileInfo(storagePath()).absolutePath() + QStringLiteral("/subscriptions.json");
}

SubscriptionRecords loadSubscriptions()
{
    QFile f(subscriptionsPath());
    if (!f.exists())
        return {};
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("SubscriptionStore: subscriptions.json cannot be read; no config updates from a subscription");
        return {};
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.value(QStringLiteral("version")).toInt() != kFormatVersion) {
        qWarning("SubscriptionStore: subscriptions.json is damaged or from a newer version; it is left as it is");
        return {};
    }
    SubscriptionRecords out;
    const QJsonObject configs = root.value(QStringLiteral("configs")).toObject();
    for (auto it = configs.constBegin(); it != configs.constEnd(); ++it) {
        SubscriptionRecord record;
        if (fromJson(it.value().toObject(), &record))
            out.insert(it.key(), record);
    }
    return out;
}

bool saveSubscriptions(const SubscriptionRecords &records)
{
    QJsonObject configs;
    for (auto it = records.constBegin(); it != records.constEnd(); ++it)
        configs.insert(it.key(), toJson(it.value()));
    const QJsonObject root{{QStringLiteral("version"), kFormatVersion}, {QStringLiteral("configs"), configs}};
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    // Staged and renamed into place, owner-only before the first byte, as
    // configs.json is: it names which configs update from where.
    QSaveFile f(subscriptionsPath());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (!f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) || f.write(json) != json.size()) {
        f.cancelWriting();
        return false;
    }
    return f.commit();
}

} // namespace freetunnel
