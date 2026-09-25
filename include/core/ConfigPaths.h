// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QString>
#include <QStringList>

namespace freetunnel {

/// The config file @p fileName would collide with inside @p dir, or an empty
/// string when nothing collides.
///
/// The filesystem decides, not the platform: on a case-insensitive one (APFS,
/// NTFS) "work.toml" collides with an existing "Work.toml", and the name really
/// on disk is the one every path-keyed thing downstream must use — the keychain
/// entry, the configs.json membership test, the name shown in the confirm
/// dialog. Using the name a deep link carried instead let a link replace a
/// config while its password survived under the other casing.
QString existingConfigPath(const QString &dir, const QString &fileName);

/// Which of @p entries names the same file as @p fileName, given that the
/// filesystem has already reported @p fileName as existing. Exact match wins;
/// otherwise the single case-insensitive match. Empty when none does.
/// Split out from existingConfigPath() so the case folding is testable on a
/// case-sensitive host, where the collision itself cannot be reproduced.
QString configEntryMatching(const QStringList &entries, const QString &fileName);

/// True when @p name mixes writing systems — Latin letters next to Cyrillic ones,
/// say. Legitimate names are written in one alphabet; a name that is not is the
/// signature of a homoglyph, where "Wоrk" with a Cyrillic о reads exactly like a
/// config the user already trusts. Restricting names to ASCII is not an option
/// here: this app ships a Russian UI and Cyrillic names are ordinary.
/// Digits, punctuation and spaces belong to no script and are ignored.
bool nameMixesScripts(const QString &name);

/// A display name / hostname as a config filename stem: the name as typed, with
/// only what some file system refuses replaced by '_' (separators, the characters
/// Windows reserves, control and formatting characters, a leading dot) and a
/// Windows device name such as "CON" kept from being one.
QString sanitizeConfigBaseName(const QString &name, const QString &fallbackPrefix = QStringLiteral("imported"));

/// Whether two paths name one file: the same path, or two that differ only in
/// letter case on a file system that folds it (APFS, NTFS).
bool namesTheSameFile(const QString &a, const QString &b);

/// Resolve a unique owner-only config path under AppConfigLocation.
QString uniqueOwnerConfigPath(const QString &stem);

/// Pick a save path for create/edit: reuse @p existingPath when the stem is unchanged.
/// When only its letter case changed on a file system that folds case, the file
/// is renamed to the new spelling first and that path returned.
QString ownerConfigPathForSave(const QString &stem, const QString &existingPath = {});

} // namespace freetunnel
