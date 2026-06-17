#include "VersionCompare.h"

#include <QRegularExpression>
#include <QVersionNumber>

bool isVersionNewer(const QString &current, const QString &remote)
{
    static const QRegularExpression rxNum(QStringLiteral(R"((\d+(?:\.\d+)*))"));
    static const QRegularExpression rxSuffix(QStringLiteral(R"(\d+(?:\.\d+)*(.*)$)"));

    auto extractParts = [&](const QString &v) {
        QVersionNumber vn;
        QString suffix;
        const auto m = rxNum.match(v);
        if (m.hasMatch())
            vn = QVersionNumber::fromString(m.captured(1));
        const auto ms = rxSuffix.match(v);
        if (ms.hasMatch())
            suffix = ms.captured(1).trimmed();
        return std::pair<QVersionNumber, QString>{vn, suffix};
    };

    const auto [remoteVn, remoteSuffix] = extractParts(remote);
    const auto [currentVn, currentSuffix] = extractParts(current);

    const int cmp = QVersionNumber::compare(remoteVn, currentVn);
    if (cmp > 0)
        return true;
    if (cmp < 0)
        return false;

    // Same numeric version — compare suffix lexicographically.
    // Empty suffix (release) > any suffix (beta/rc).
    if (currentSuffix.isEmpty() && !remoteSuffix.isEmpty())
        return false;
    if (!currentSuffix.isEmpty() && remoteSuffix.isEmpty())
        return true;
    return remoteSuffix > currentSuffix;
}
