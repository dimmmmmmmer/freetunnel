#include "ReleaseVerify.h"

#include <QCryptographicHash>
#include <QFile>

#if __has_include(<openssl/evp.h>)
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#define FT_HAVE_OPENSSL 1
#endif

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
    constexpr qint64 kChunkSize = 1024 * 1024;
    QByteArray buf(kChunkSize, Qt::Uninitialized);
    while (true) {
        const qint64 n = f.read(buf.data(), kChunkSize);
        if (n < 0)
            return QString();
        if (n == 0)
            break;
        hash.addData(buf.constData(), static_cast<qsizetype>(n));
    }
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

bool verifyEd25519Signature(const QByteArray &data, const QByteArray &signature,
                            const QByteArray &publicKeyPem)
{
#ifdef FT_HAVE_OPENSSL
    if (publicKeyPem.isEmpty() || signature.isEmpty())
        return false;
    BIO *bio = BIO_new_mem_buf(publicKeyPem.constData(), static_cast<int>(publicKeyPem.size()));
    if (!bio)
        return false;
    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!pkey)
        return false;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx && EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey) == 1) {
        ok = EVP_DigestVerify(ctx,
                              reinterpret_cast<const unsigned char *>(signature.constData()),
                              static_cast<size_t>(signature.size()),
                              reinterpret_cast<const unsigned char *>(data.constData()),
                              static_cast<size_t>(data.size())) == 1;
    }
    if (ctx)
        EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
#else
    Q_UNUSED(data);
    Q_UNUSED(signature);
    Q_UNUSED(publicKeyPem);
    return false;
#endif
}
