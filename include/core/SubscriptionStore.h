// cppcheck-suppress-file missingIncludeSystem
#pragma once

// What FreeTunnel keeps about each config that updates from a TrustTunnel
// subscription, besides the config itself, in subscriptions.json next to
// configs.json. Nothing in it is secret: the subscription's address holds the VPN
// password, and is kept in the credential store under
// CredentialStore::keyForSubscription(sourceId).

#include <QMap>
#include <QString>

namespace freetunnel {

struct SubscriptionRecord {
    QString sourceId;         // names the address's entry in the credential store
    QString origin;           // redactUrl() of the address, for logs and display
    QString identity;         // SubscriptionUrl::identity: the subscription, whatever its password
    qint64 lastFetchedAt = 0; // seconds since the epoch of the last fetch that gave a document; 0: none
    qint64 lastAttemptAt = 0; // of the last fetch, whatever it gave
    QString lastStatus;       // what the last attempt gave (subscriptionFetchCode)
    int failures = 0;         // attempts in a row that gave nothing to use
    bool autoRefresh = true;  // off: only a refresh the user asks for fetches
    qint64 nextAttemptAt = 0; // when an automatic refresh is due
    qint64 hintSeconds = 0;   // the operator's refresh hint at the last fetch; 0: none
    QString heldHostname;     // another server the last fetch named, until the user answers
};

// By the config's path, as configs.json lists it.
using SubscriptionRecords = QMap<QString, SubscriptionRecord>;

// subscriptions.json, in the folder configs.json is in.
QString subscriptionsPath();

// What subscriptions.json holds. One that cannot be read, or that a newer
// version wrote, reads as holding nothing, and is left as it is on disk until
// the next save.
SubscriptionRecords loadSubscriptions();

// Written whole, owner-only and atomically, as configs.json is. False when it
// could not be, and the file on disk is then as it was.
bool saveSubscriptions(const SubscriptionRecords &records);

} // namespace freetunnel
