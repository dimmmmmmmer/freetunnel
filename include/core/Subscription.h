// cppcheck-suppress-file missingIncludeSystem
#pragma once

// TrustTunnel subscriptions (endpoint 1.3.0 and later): the document a
// subscription address serves, what it changes in a config, and the address
// itself. Nothing here touches the network, the credential store or a file; like
// DeepLink, it depends only on Qt Core, so every rule is unit-tested without a
// server. Fetching the document, storing the address and applying an update are
// for the code that uses these.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

#include "core/ConfigToml.h"

namespace freetunnel {

// The newest document version this client reads. A newer one may mean something
// else by a field, so it is refused as newer, never read by these rules.
inline constexpr int kSubscriptionMaxVersion = 1;

// The largest document taken. The endpoint's is a few hundred bytes, or a few KB
// with a certificate chain; its own client takes up to 1 MiB.
inline constexpr qsizetype kMaxSubscriptionBodyBytes = 65536;

// The most certificates a document may pin, counted one by one, however many PEM
// blocks they are written in.
inline constexpr int kMaxSubscriptionCertificates = 8;

// One document, every field checked. Strings are as the document has them, but
// the client random, which is in the form the core reads (clientRandomForCore).
struct SubscriptionDocument {
    QString hostname;
    QString address;  // one "host:port" or "[v6]:port"
    QString username;
    QString password;
    bool hasIpv6 = true;
    QString upstreamProtocol = QStringLiteral("http2"); // "http2" | "http3"
    bool antiDpi = false;
    // The certificates of the document's field and nothing else (certificatesOnly),
    // as written; empty when it pins none.
    QString certificatePem;
    // The field held something besides them, such as a private key, which is left
    // out and never stored.
    bool extraPemIgnored = false;
    QString customSni;
    QString clientRandomPrefix;
    // Taken when a config is created, never on a refresh; absent when the document
    // does not set them.
    std::optional<QString> name;
    std::optional<QStringList> dnsUpstreams;
};

// Why a document was not taken. Each is a different answer to the user, and
// decides whether trying again later can help.
enum class SubscriptionDocError {
    Invalid,            // damaged, or a field this client cannot take
    UnsupportedVersion, // newer than kSubscriptionMaxVersion
    SkipVerification,   // asks to skip TLS verification, which a subscription never may
    NeedsNewerCore,     // a client_random_auth_key, which this core cannot compute
    TooLarge,           // over kMaxSubscriptionBodyBytes
};

struct SubscriptionParseError {
    SubscriptionDocError kind = SubscriptionDocError::Invalid;
    // The document's name for the field at fault, empty when it is the document as
    // a whole. For logs: it never holds a value, since values include the password.
    QString field;
};

// The document in a reply body, with every field checked before any of it is
// used, or nullopt and *error. The version is read first, so a newer document is
// refused as one whatever its fields hold. Fields this version does not know are
// ignored.
std::optional<SubscriptionDocument> parseSubscriptionJson(const QByteArray &body,
                                                          SubscriptionParseError *error = nullptr);

// Refresh: an update to a config that came from the subscription before.
// Creation: a config made from the document.
enum class SubscriptionApplyMode { Refresh, Creation };

// The fields a refresh can change.
enum class SubscriptionField { Hostname, Address, Username, Password, Certificate, CustomSni, ClientRandom };

struct SubscriptionChange {
    SubscriptionField field = SubscriptionField::Hostname;
    // For the hostname, the address, the SNI and the client random only. A change
    // to the credentials or the certificate is listed without them.
    QString oldValue;
    QString newValue;
};

struct SubscriptionApplied {
    ConfigToml config;    // its password is empty: that is for the credential store
    QString password;
    QList<SubscriptionChange> changes; // what Refresh changed; empty for Creation
    // The server is another one, not the same name in other letter case: a
    // Refresh whose hostname differs once both are in ACE form, ignoring case.
    bool hostnameChanged = false;
};

// The config a document makes, as TrustTunnel's own client applies one. The
// subscription owns the hostname, the address, the credentials, the certificate,
// the SNI and the client random: Refresh overwrites them all, an address list
// becomes the document's one address, and a certificate, SNI or client random the
// document leaves out is cleared. Everything else stays the user's: the protocol,
// IPv6 and anti-DPI, which the endpoint serves fixed values for and which would
// undo the user's own choice on every refresh; the name and the DNS servers; and
// what is local by nature, such as skip_verification, post-quantum, the tunnel's
// routing and the keys this editor does not know.
//
// Creation starts from a new config instead, and also takes the protocol, IPv6,
// anti-DPI, the name and the DNS servers from the document; `current` and
// `currentPassword` are not read.
SubscriptionApplied applySubscription(const ConfigToml &current, const QString &currentPassword,
                                      const SubscriptionDocument &document, SubscriptionApplyMode mode);

struct SubscriptionUrl {
    QUrl requestUrl;          // without its userinfo and fragment
    QByteArray authorization; // "Basic ..." for the userinfo; empty when there is none
    // Which subscription this is, whatever its password: the SHA-256, in hex, of
    // the origin, the path, a newline and the user. The same subscription sent
    // again with a new password matches the config it made.
    QString identity;
};

// A subscription address, split into what a fetch needs, or nullopt when it is not
// one: not https, no host, longer than kMaxSubscriptionUrlBytes, or with a space,
// a control or a format character in it. The userinfo is the server's Basic
// credentials, percent-decoded.
std::optional<SubscriptionUrl> parseSubscriptionUrl(const QString &url);

enum class RedactKeep {
    Origin,        // https://host[:port], for logs and errors
    OriginAndPath, // and the path, for the details the user consents to
};

// An address with nothing secret in it: never the userinfo, the query or the
// fragment, and the host in ACE form. A path may hold a token on another
// provider's server, so only the consent details show it.
QString redactUrl(const QUrl &url, RedactKeep keep = RedactKeep::Origin);

// How long to wait between refreshes, in seconds: the operator's hint, clamped to
// one hour to seven days, or a day without one. Jitter and backoff are added to
// it, never taken from it.
qint64 subscriptionRefreshInterval(std::optional<qint64> hintSeconds);

// The operator's refresh hint, in seconds, from a document or the reply's
// profile-update-interval header. This is the one place it is read.
std::optional<qint64> subscriptionRefreshHint(const QJsonObject &document,
                                              const QByteArray &profileUpdateIntervalHeader);

} // namespace freetunnel
