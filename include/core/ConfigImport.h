// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Pure, UI-independent logic for importing a config from a deep link, so it can
// be unit-tested without the UI / the VPN core.

#include <QString>
#include <optional>

namespace freetunnel {

struct PreparedImport {
    QString fileName;    // safe target file name (ends with .toml)
    // The name 1.2.0 gave the same link, which turned every space and punctuation
    // mark into '_'. A config imported then is still under it, and a link sent
    // again has to find it there to offer to replace it.
    QString legacyFileName;
    // The whole name, when fileName had to cut it to kMaxConfigNameLength. A
    // config imported before names were cut is under it, and is found there the
    // same way.
    QString unclippedFileName;
    QString tomlContent; // TOML ready to write to disk
    bool skipVerification = false;
    // The link also had a subscription address, which was left out: the config is
    // the link's own server, and does not update itself (subscriptionNotKeptNote).
    bool subscriptionNotKept = false;
};

// Why prepareDeepLinkImport() gave nothing back. A damaged link's message is the
// parser's, a fragment the caller puts after "Link error:". The others are about
// a link that is fine but asks for what this version does not have yet, and their
// message is a whole sentence, said as it is.
enum class LinkRefusal { Damaged, SubscriptionOnly, NeedsNewerCore };

// Decode an official `tt://` deep link into a ready-to-write TOML config and a
// sanitized file name. Returns nullopt and sets *error (and *refusal) on failure.
std::optional<PreparedImport> prepareDeepLinkImport(const QString &link, QString *error = nullptr,
                                                    LinkRefusal *refusal = nullptr);

// What an import says, in the user's language, when a link or a file asks for
// what this version does not have yet. One wording for both, so a server is
// described the same way whichever way it arrived.
//
// A subscription link with no server of its own to fall back on.
QString subscriptionOnlyRefusal();
// A server that wants a client random auth key, which this version's TrustTunnel
// core cannot compute.
QString newerCoreRefusal();
// A config imported without the subscription address it came with.
QString subscriptionNotKeptNote();

} // namespace freetunnel
