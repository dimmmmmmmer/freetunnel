#include "ReleaseVerify.h"

#include <QCryptographicHash>
#include <QFile>

QString expectedSha256FromSums(const QByteArray &sumsContent, const QString &assetName)
{
    for (const QByteArray &line : sumsContent.split('\n')) {
        const QList<QByteArray> parts = line.trimmed().split(' ');
        if (parts.size() < 2)
            continue;
        const QString name = QString::fromUtf8(parts.at(parts.size() - 1)).trimmed();
        if (name == assetName)
            return QString::fromLatin1(parts.first()).trimmed().toLower();
    }
    return QString();
}

QString sha256HexOfFile(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!f.atEnd())
        hash.addData(f.read(1024 * 1024));
    return QString::fromLatin1(hash.result().toHex());
}

bool verifyFileAgainstSums(const QString &filePath, const QByteArray &sumsContent,
                           const QString &assetName)
{
    const QString expected = expectedSha256FromSums(sumsContent, assetName);
    if (expected.isEmpty())
        return false;
    return sha256HexOfFile(filePath).toLower() == expected;
}
