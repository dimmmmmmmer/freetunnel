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

// Moved here from DeepLink.cpp, which splits a link's certificates with it, so
// the link and the certificate check below read DER the one way.
int derElementEnd(const QByteArray &der, int pos, unsigned char tag, int *contentStart)
{
    if (pos + 2 > der.size() || static_cast<unsigned char>(der.at(pos)) != tag)
        return -1;
    const auto first = static_cast<unsigned char>(der.at(pos + 1));
    int header = 2;
    qint64 length = first;
    if ((first & 0x80) != 0) {
        const int count = first & 0x7F;
        // Zero is the indefinite form, which DER forbids; more than four bytes
        // is a certificate larger than this has any business accepting.
        if (count == 0 || count > 4 || pos + 2 + count > der.size())
            return -1;
        length = 0;
        for (int i = 0; i < count; ++i)
            length = (length << 8) | static_cast<unsigned char>(der.at(pos + 2 + i));
        header = 2 + count;
    }
    const qint64 end = static_cast<qint64>(pos) + header + length;
    if (end > der.size())
        return -1;
    if (contentStart)
        *contentStart = pos + header;
    return static_cast<int>(end);
}

} // namespace freetunnel
