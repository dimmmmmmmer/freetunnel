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

bool remoteSuffixIsNewer(const QString &currentSuffix, const QString &remoteSuffix)
{
    if (currentSuffix.isEmpty() && !remoteSuffix.isEmpty())
        return false;
    if (!currentSuffix.isEmpty() && remoteSuffix.isEmpty())
        return true;
    return remoteSuffix > currentSuffix;
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
