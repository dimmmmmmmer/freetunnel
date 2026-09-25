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
    QString tomlContent; // TOML ready to write to disk
    bool skipVerification = false;
};

// Decode an official `tt://` deep link into a ready-to-write TOML config and a
// sanitized file name. Returns nullopt and sets *error on failure.
std::optional<PreparedImport> prepareDeepLinkImport(const QString &link, QString *error = nullptr);

} // namespace freetunnel
