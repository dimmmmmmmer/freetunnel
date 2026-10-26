// cppcheck-suppress-file missingIncludeSystem
// The config values that more than the writer has to read the same way: an
// address, a client random as the core takes it, whether the core can use one as
// it is written, and the servers in a DNS list. The editor, link import and
// subscriptions check with these, and buildConfigToml() writes through them. Split out of ConfigToml.cpp, which had
// grown past the point where one file could be read end to end.
//
// And a certificate: which DER elements and PEM blocks are certificates, so the
// link, the file import, the editor and the share link all keep the same ones.
#include "ConfigToml.h"

#include <QRegularExpression>
#include <QStringList>

namespace freetunnel {

// Moved here from the editor's validateAddressList(), unchanged.
bool isValidEndpointAddress(const QString &address)
{
    const QString a = address.trimmed();
    const int colon = a.lastIndexOf(QLatin1Char(':'));
    bool portOk = false;
    const int port = colon >= 0 ? a.mid(colon + 1).toInt(&portOk) : 0;
    return colon > 0 && portOk && port >= 1 && port <= 65535;
}

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

// Certificate ::= SEQUENCE { tbsCertificate, signatureAlgorithm, signatureValue },
// RFC 5280 section 4.1. Nothing inside the three is read: the shape alone is what
// no key has. A PKCS#8 key is SEQUENCE { INTEGER, SEQUENCE, OCTET STRING }, and a
// PKCS#1 or SEC1 key starts with an INTEGER too.
bool isCertificateDer(const QByteArray &der)
{
    static constexpr unsigned char kFields[] = {0x30, 0x30, 0x03};
    int at = 0;
    if (derElementEnd(der, 0, 0x30, &at) != der.size())
        return false;
    for (const unsigned char tag : kFields) {
        int content = 0;
        const int end = derElementEnd(der, at, tag, &content);
        // A BIT STRING holds at least the byte that counts its unused bits.
        if (end < 0 || (tag == 0x03 && end == content))
            return false;
        at = end;
    }
    return at == der.size();
}

namespace {

// Whether the bytes are one or more certificates, one after another, and nothing
// else. FreeTunnel 1.1.9 and earlier put a link's whole chain into one block, and
// OpenSSL, which the core reads it with, takes the first certificate of it as the
// pin. Read as one certificate, such a block held none, and a config stored back
// then lost its pin at the next start.
bool certificatesBackToBack(const QByteArray &der)
{
    int pos = 0;
    while (pos < der.size()) {
        const int end = derElementEnd(der, pos);
        if (end <= pos || !isCertificateDer(der.mid(pos, end - pos)))
            return false;
        pos = end;
    }
    return pos > 0;
}

// The DER of one PEM block, or nothing when the block is not a certificate. The
// body has to be strict base64 once its line breaks are out: decoded leniently,
// what is not base64 is skipped, and a damaged block could still pass.
QByteArray certificateBlockDer(const QString &label, QString body)
{
    if (label != QLatin1String("CERTIFICATE") && label != QLatin1String("X509 CERTIFICATE"))
        return QByteArray();
    static const QRegularExpression space(QStringLiteral("\\s"));
    body.remove(space);
    // One character past a whole group of four encodes no byte. Qt's decoder
    // drops it without a word; OpenSSL and the Android app refuse the block, and
    // so does this, so the two apps keep and drop the same blocks.
    if (body.size() % 4 == 1)
        return QByteArray();
    const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(
            body.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    return decoded && certificatesBackToBack(decoded.decoded) ? decoded.decoded : QByteArray();
}

struct PemCertificates {
    QStringList blocks; // as written
    QByteArray der;     // theirs, one after another
    bool dropped = false;
};

// Every block whose END names the label its BEGIN does. A body holds nothing but
// base64 and white space, so a block with another BEGIN inside it is not one:
// the inner block is read on its own, and the text around it is outside them.
PemCertificates readPemCertificates(const QString &pem)
{
    static const QRegularExpression block(
            QStringLiteral("-----BEGIN ([A-Z0-9 ]+)-----([A-Za-z0-9+/=\\s]*)-----END \\1-----"));
    PemCertificates out;
    qsizetype last = 0;
    auto it = block.globalMatch(pem);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QByteArray der = certificateBlockDer(m.captured(1), m.captured(2));
        if (der.isEmpty()) {
            out.dropped = true;
        } else {
            out.blocks << m.captured(0);
            out.der += der;
        }
        // Text between the blocks, such as the "Bag Attributes" an export of a
        // PKCS#12 file writes, is no certificate either.
        if (!QStringView(pem).mid(last, m.capturedStart() - last).trimmed().isEmpty())
            out.dropped = true;
        last = m.capturedEnd();
    }
    if (!QStringView(pem).mid(last).trimmed().isEmpty())
        out.dropped = true;
    return out;
}

} // namespace

QString certificatesOnly(const QString &pem, bool *dropped)
{
    const PemCertificates found = readPemCertificates(pem);
    if (dropped)
        *dropped = found.dropped;
    if (!found.dropped)
        return pem;
    return found.blocks.isEmpty() ? QString() : found.blocks.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QByteArray certificatesDer(const QString &pem, bool *dropped)
{
    const PemCertificates found = readPemCertificates(pem);
    if (dropped)
        *dropped = found.dropped;
    return found.der;
}

} // namespace freetunnel
