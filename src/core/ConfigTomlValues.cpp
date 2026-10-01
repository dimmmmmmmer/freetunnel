// cppcheck-suppress-file missingIncludeSystem
// The config values that more than the writer has to read the same way: a client
// random as the core takes it, whether the core can use one as it is written, and
// the servers in a DNS list. The editor and link import check with these, and
// buildConfigToml() writes through them. Split out of ConfigToml.cpp, which had
// grown past the point where one file could be read end to end.
#include "ConfigToml.h"

#include <QRegularExpression>
#include <QStringList>

namespace freetunnel {

QString clientRandomForCore(const QString &value) {
    QString v = value.trimmed();
    // Every trailing slash, not only the last: "aa//" lost one and became "aa/",
    // which is exactly the empty mask the core refuses.
    while (v.endsWith(QLatin1Char('/')))
        v.chop(1);
    // Nothing before the slash leaves nothing for the mask to apply to; the core
    // sends a random of its own either way.
    return v.startsWith(QLatin1Char('/')) ? QString() : v;
}

bool isValidClientRandom(const QString &value) {
    static const QRegularExpression hex(
            QStringLiteral("^(?:[0-9a-fA-F]{2}){1,32}(?:/(?:[0-9a-fA-F]{2}){1,32})?$"));
    return value.isEmpty() || hex.match(value).hasMatch();
}

QStringList splitDnsList(const QString &dns) {
    static const QRegularExpression separators(QStringLiteral("[\\s,;]+"));
    return dns.split(separators, Qt::SkipEmptyParts);
}

} // namespace freetunnel
