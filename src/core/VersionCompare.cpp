// cppcheck-suppress-file missingIncludeSystem
#include "VersionCompare.h"

#include <QRegularExpression>
#include <QVersionNumber>
#include <utility>

namespace {

struct VersionParts {
    QVersionNumber number;
    QString suffix;
};

VersionParts extractVersionParts(const QString &version)
{
    static const QRegularExpression rxNum(QStringLiteral(R"((\d+(?:\.\d+)*))"));
    static const QRegularExpression rxSuffix(QStringLiteral(R"(\d+(?:\.\d+)*(.*)$)"));

    VersionParts parts;
    const auto m = rxNum.match(version);
    if (m.hasMatch())
        parts.number = QVersionNumber::fromString(m.captured(1));
    const auto ms = rxSuffix.match(version);
    if (ms.hasMatch())
        parts.suffix = ms.captured(1).trimmed();
    return parts;
}

// Compare two pre-release suffixes the way a person reads them: runs of digits as
// numbers, everything else as text. A plain string comparison ordered "-rc10"
// BELOW "-rc9", because '1' sorts before '9' — so anybody running rc9 was never
// offered rc10, and the more pre-releases a cycle had the more people it stranded.
int compareSuffixNaturally(const QString &a, const QString &b)
{
    int i = 0;
    int j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            int si = i;
            int sj = j;
            while (i < a.size() && a.at(i).isDigit())
                ++i;
            while (j < b.size() && b.at(j).isDigit())
                ++j;
            // Compared as numbers, so 10 beats 9 and leading zeros do not matter.
            const qulonglong na = QStringView(a).mid(si, i - si).toULongLong();
            const qulonglong nb = QStringView(b).mid(sj, j - sj).toULongLong();
            if (na != nb)
                return na < nb ? -1 : 1;
            continue;
        }
        if (a.at(i) != b.at(j))
            return a.at(i) < b.at(j) ? -1 : 1;
        ++i;
        ++j;
    }
    // One is a prefix of the other: the shorter comes first ("-rc" before "-rc1").
    if (i < a.size())
        return 1;
    if (j < b.size())
        return -1;
    return 0;
}

bool remoteSuffixIsNewer(const QString &currentSuffix, const QString &remoteSuffix)
{
    if (currentSuffix.isEmpty() && !remoteSuffix.isEmpty())
        return false;
    if (!currentSuffix.isEmpty() && remoteSuffix.isEmpty())
        return true;
    return compareSuffixNaturally(remoteSuffix, currentSuffix) > 0;
}

} // namespace

bool isVersionNewer(const QString &current, const QString &remote)
{
    const VersionParts remoteParts = extractVersionParts(remote);
    const VersionParts currentParts = extractVersionParts(current);

    const int cmp = QVersionNumber::compare(remoteParts.number, currentParts.number);
    if (cmp > 0)
        return true;
    if (cmp < 0)
        return false;
    return remoteSuffixIsNewer(currentParts.suffix, remoteParts.suffix);
}
