#pragma once

// Pure, UI-independent logic for importing a config from a deep link, so it can
// be unit-tested without MainWindow / the VPN core.

#include <QString>
#include <optional>

namespace freetunnel {

struct PreparedImport {
    QString fileName;    // safe target file name (ends with .toml)
    QString tomlContent; // TOML ready to write to disk
};

// Decode an official `tt://` deep link into a ready-to-write TOML config and a
// sanitized file name. Returns nullopt and sets *error on failure.
std::optional<PreparedImport> prepareDeepLinkImport(const QString &link, QString *error = nullptr);

} // namespace freetunnel
