// cppcheck-suppress-file missingIncludeSystem
#include "core/ConfigPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace freetunnel {

namespace {

// The config directory holds the .toml files (hostnames, usernames, certificates)
// and the credentials subdirectory. mkpath() creates it 0755, so tighten it to
// owner-only — every file inside is already 0600, but the directory listing
// itself shouldn't be readable by other local users either.
QString ensureOwnerConfigDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QFile::setPermissions(base, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                        | QFileDevice::ExeOwner);
    return base;
}

} // namespace

bool nameMixesScripts(const QString &name)
{
    QChar::Script seen = QChar::Script_Unknown;
    for (const QChar &c : name) {
        if (!c.isLetter())
            continue; // digits, spaces and punctuation are shared by every script
        const QChar::Script s = c.script();
        if (s == QChar::Script_Common || s == QChar::Script_Inherited
            || s == QChar::Script_Unknown)
            continue;
        if (seen == QChar::Script_Unknown) {
            seen = s;
        } else if (s != seen) {
            return true;
        }
    }
    return false;
}

namespace {

// What no file name may hold on at least one of the three systems: separators,
// the characters Windows reserves, and control and formatting characters. The
// last have no place in a name anyway, and some make one display as something
// else: U+202E turns the text after it around.
bool unusableInFileName(QChar c)
{
    static const QString reserved = QStringLiteral("/\\:*?\"<>|");
    if (reserved.contains(c))
        return true;
    switch (c.category()) {
    case QChar::Other_Control:
    case QChar::Other_Format:
    case QChar::Separator_Line:
    case QChar::Separator_Paragraph:
        return true;
    default:
        return false;
    }
}

// Device names Windows reserves with any extension: "con.toml" cannot be made.
bool reservedOnWindows(const QString &head)
{
    static const QStringList devices{QStringLiteral("CON"), QStringLiteral("PRN"),
                                     QStringLiteral("AUX"), QStringLiteral("NUL")};
    const QString upper = head.trimmed().toUpper();
    if (devices.contains(upper))
        return true;
    return upper.size() == 4 && (upper.startsWith(QLatin1String("COM")) || upper.startsWith(QLatin1String("LPT")))
            && upper.at(3) >= QLatin1Char('1') && upper.at(3) <= QLatin1Char('9');
}

} // namespace

QString sanitizeConfigBaseName(const QString &name, const QString &fallbackPrefix)
{
    // The file name is what the list, the Connection page and the tray show, so
    // it keeps the name as typed, spaces and punctuation included. Replacing
    // everything but letters, digits and ".-_" listed the editor's own example,
    // "Germany · Frankfurt", as "Germany___Frankfurt".
    QString safe;
    for (const QChar &c : name)
        safe += unusableInFileName(c) ? QChar('_') : c;
    safe = safe.trimmed();
    // A leading dot hides the file on macOS and Linux, and ".connect-*" is swept
    // at startup as a leftover.
    for (int i = 0; i < safe.size() && safe.at(i) == QLatin1Char('.'); ++i)
        safe[i] = QLatin1Char('_');
    const int firstDot = safe.indexOf(QLatin1Char('.'));
    if (reservedOnWindows(firstDot < 0 ? safe : safe.left(firstDot)))
        safe.insert(firstDot < 0 ? safe.size() : firstDot, QLatin1Char('_'));
    if (safe.isEmpty())
        safe = QStringLiteral("%1-%2").arg(fallbackPrefix).arg(QDateTime::currentSecsSinceEpoch());
    return safe;
}

QString uniqueOwnerConfigPath(const QString &stem)
{
    const QString base = ensureOwnerConfigDir();
    const QString first = QDir(base).filePath(stem + QStringLiteral(".toml"));
    if (!QFileInfo::exists(first))
        return first;
    // Count up rather than stamping the time: a one-second-resolution timestamp
    // is not unique, so two imports inside the same second resolved to the SAME
    // path and the second silently overwrote the first — precisely the clobbering
    // this function exists to prevent. "Work-2.toml" also reads better than an
    // epoch suffix in the config list.
    for (int i = 2; i < 1000; ++i) {
        const QString candidate =
                QDir(base).filePath(QStringLiteral("%1-%2.toml").arg(stem).arg(i));
        if (!QFileInfo::exists(candidate))
            return candidate;
    }
    // Absurd number of same-named configs: fall back to something collision-proof
    // rather than returning a path we know is taken.
    return QDir(base).filePath(QStringLiteral("%1-%2-%3.toml")
                                       .arg(stem)
                                       .arg(QDateTime::currentSecsSinceEpoch())
                                       .arg(QRandomGenerator::system()->generate(), 8, 16,
                                            QLatin1Char('0')));
}


QString configEntryMatching(const QStringList &entries, const QString &fileName)
{
    // An exact match is the answer whenever there is one: on a case-insensitive
    // filesystem "Work.toml" and "work.toml" cannot both exist, and on a
    // case-sensitive one the exact name is the file the caller meant.
    if (entries.contains(fileName))
        return fileName;
    for (const QString &entry : entries) {
        if (entry.compare(fileName, Qt::CaseInsensitive) == 0)
            return entry;
    }
    return QString();
}

QString existingConfigPath(const QString &dir, const QString &fileName)
{
    // Ask the filesystem first. It is the only thing that knows whether it folds
    // case, and its answer is right on both kinds: on a case-sensitive filesystem
    // "work.toml" simply does not exist next to "Work.toml", so those are two
    // different configs and there is no collision to resolve.
    if (!QFileInfo::exists(QDir(dir).filePath(fileName)))
        return QString();
    const QString actual = configEntryMatching(QDir(dir).entryList(QDir::Files), fileName);
    // A directory listing that does not contain the name the filesystem just
    // confirmed means something changed underneath us; fall back to the literal
    // path rather than reporting no collision, which would overwrite blind.
    return QDir(dir).filePath(actual.isEmpty() ? fileName : actual);
}

bool namesTheSameFile(const QString &a, const QString &b)
{
    if (a == b)
        return true;
    if (a.compare(b, Qt::CaseInsensitive) != 0)
        return false;
    const QFileInfo fa(a);
    const QFileInfo fb(b);
    if (!fa.exists() || !fb.exists())
        return false;
    // Both answer to exists(). Two files, or one that folds case? The listing
    // says: a case-sensitive file system lists both names.
    const QStringList entries = QDir(fa.absolutePath()).entryList(QDir::Files | QDir::Hidden);
    return !(entries.contains(fa.fileName()) && entries.contains(fb.fileName()));
}

QString ownerConfigPathForSave(const QString &stem, const QString &existingPath)
{
    if (!existingPath.isEmpty()) {
        const QFileInfo existing(existingPath);
        if (existing.completeBaseName() == stem)
            return existingPath;
        // Only the letter case changed ("work" to "Work"). Where the file system
        // folds case the new name is this very file, which the unique path below
        // took for another config and answered with "Work-2". Rename it in place,
        // through a temporary name because a case-only rename is a no-op on some
        // systems, and save over it.
        const QString wanted = existing.dir().filePath(stem + QStringLiteral(".toml"));
        if (existing.completeBaseName().compare(stem, Qt::CaseInsensitive) == 0
            && namesTheSameFile(existingPath, wanted)) {
            const QString step = existing.dir().filePath(
                    QStringLiteral(".rename-%1.toml")
                            .arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0')));
            if (QFile::rename(existingPath, step)) {
                if (QFile::rename(step, wanted))
                    return wanted;
                QFile::rename(step, existingPath); // put it back rather than lose it
            }
            return existingPath;
        }
    }
    return uniqueOwnerConfigPath(stem);
}

} // namespace freetunnel
