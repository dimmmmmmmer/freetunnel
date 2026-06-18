#pragma once

#include <QByteArray>
#include <QString>

// Parse SHA256SUMS-style manifest lines and return the expected hex digest for
// @p assetName (basename match), or an empty string when not found.
QString expectedSha256FromSums(const QByteArray &sumsContent, const QString &assetName);

// Return the lowercase SHA-256 hex digest of @p filePath, or empty on failure.
QString sha256HexOfFile(const QString &filePath);

bool verifyFileAgainstSums(const QString &filePath, const QByteArray &sumsContent,
                           const QString &assetName);

// Verify an Ed25519 signature over @p data using a SubjectPublicKeyInfo PEM public
// key. Returns false when OpenSSL is unavailable or the signature is invalid.
bool verifyEd25519Signature(const QByteArray &data, const QByteArray &signature,
                            const QByteArray &publicKeyPem);
