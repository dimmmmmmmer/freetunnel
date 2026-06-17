#include "UpdateChecker.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

#include "ReleaseVerify.h"
#include "VersionCompare.h"

#if __has_include(<openssl/evp.h>)
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#define FT_HAVE_OPENSSL 1
#endif

namespace {
// Ed25519 public key (SubjectPublicKeyInfo PEM) used to verify SHA256SUMS.txt.sig.
// Empty => signature verification is OFF and updates rely on the SHA-256 match
// against SHA256SUMS.txt fetched over HTTPS. To enable supply-chain verification:
//   1. openssl genpkey -algorithm ed25519 -out ed25519.pem
//   2. openssl pkey -in ed25519.pem -pubout      → paste the PEM below
//   3. add ed25519.pem as the ED25519_SIGNING_KEY repo secret (CI signs releases)
const char *kUpdateSigningPublicKeyPem = "";

bool signatureVerificationConfigured() {
    return kUpdateSigningPublicKeyPem && kUpdateSigningPublicKeyPem[0] != '\0';
}

#ifdef FT_HAVE_OPENSSL
bool verifyEd25519(const QByteArray &data, const QByteArray &sig, const QByteArray &pubPem) {
    if (pubPem.isEmpty() || sig.isEmpty())
        return false;
    BIO *bio = BIO_new_mem_buf(pubPem.constData(), static_cast<int>(pubPem.size()));
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
                              reinterpret_cast<const unsigned char *>(sig.constData()),
                              static_cast<size_t>(sig.size()),
                              reinterpret_cast<const unsigned char *>(data.constData()),
                              static_cast<size_t>(data.size())) == 1;
    }
    if (ctx)
        EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(pkey);
    return ok;
}
#endif
} // namespace

UpdateChecker::UpdateChecker(const QString &githubRepo,
                             const QString &currentVersion,
                             QObject *parent)
    : QObject(parent)
    , m_githubRepo(githubRepo)
    , m_currentVersion(currentVersion)
    , m_nam(new QNetworkAccessManager(this))
{
}

void UpdateChecker::checkNow()
{
    // https://docs.github.com/en/rest/releases/releases#get-the-latest-release
    const QString url = QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                            .arg(m_githubRepo);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_currentVersion));
    req.setRawHeader("Accept", "application/vnd.github+json");

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onCheckFinished(reply);
    });
}

void UpdateChecker::onCheckFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit noUpdateAvailable(QStringLiteral("Network error: %1").arg(reply->errorString()));
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit noUpdateAvailable(QStringLiteral("Invalid response from GitHub API"));
        return;
    }

    const QJsonObject obj = doc.object();
    const QString tagName = obj.value("tag_name").toString();
    if (tagName.isEmpty()) {
        emit noUpdateAvailable(QStringLiteral("No releases found"));
        return;
    }

    // Strip leading 'v' from tag
    QString remoteVersion = tagName;
    if (remoteVersion.startsWith('v') || remoteVersion.startsWith('V')) {
        remoteVersion = remoteVersion.mid(1);
    }

    m_latest = ReleaseInfo{};
    m_latest.tagName = tagName;
    m_latest.version = remoteVersion;
    m_latest.htmlUrl = obj.value("html_url").toString();
    m_latest.body = obj.value("body").toString();

    const QJsonArray assets = obj.value("assets").toArray();
    for (const QJsonValue &val : assets) {
        const QJsonObject asset = val.toObject();
        const QString name = asset.value("name").toString();
        if (name == QLatin1String("SHA256SUMS.txt")) {
            m_latest.checksumsUrl = asset.value("browser_download_url").toString();
            continue;
        }
        if (name == QLatin1String("SHA256SUMS.txt.sig")) {
            m_latest.signatureUrl = asset.value("browser_download_url").toString();
            continue;
        }
#ifdef _WIN32
        if (name.endsWith(".exe", Qt::CaseInsensitive)) {
#elif defined(__APPLE__)
        if (name.endsWith(".dmg", Qt::CaseInsensitive)) {
#else
        if (name.endsWith(".AppImage", Qt::CaseInsensitive) || name.endsWith(".deb", Qt::CaseInsensitive)) {
#endif
            m_latest.installerUrl = asset.value("browser_download_url").toString();
            m_latest.assetName = name;
        }
    }

    if (isVersionNewer(m_currentVersion, remoteVersion)) {
        emit updateAvailable(m_latest);
    } else {
        emit noUpdateAvailable(QStringLiteral("You are running the latest version (%1)").arg(m_currentVersion));
    }
}

bool UpdateChecker::isNewerVersion(const QString &remote) const
{
    return isVersionNewer(m_currentVersion, remote);
}

void UpdateChecker::downloadLatest()
{
    if (m_latest.installerUrl.isEmpty()) {
        emit downloadFailed(QStringLiteral("No installer asset found for this platform"));
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/freetunnel-update");
    QDir().mkpath(dir);
    m_downloadPath = QDir(dir).filePath(m_latest.assetName);

    // Never install an asset we can't integrity-check. A release without a
    // SHA256SUMS.txt manifest is treated as untrusted.
    if (m_latest.checksumsUrl.isEmpty()) {
        emit downloadFailed(QStringLiteral("This release has no SHA256SUMS.txt — refusing to "
                                           "download an unverifiable update."));
        return;
    }
    m_checksumsData.clear();
    fetchChecksumsThenInstaller();
}

void UpdateChecker::fetchChecksumsThenInstaller()
{
    QNetworkRequest req(m_latest.checksumsUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_currentVersion));
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit downloadProgress(received / 20, total / 20); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onChecksumsFetched(reply); });
}

void UpdateChecker::onChecksumsFetched(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(QStringLiteral("Could not download SHA256SUMS.txt: %1").arg(reply->errorString()));
        return;
    }
    m_checksumsData = reply->readAll();

    if (signatureVerificationConfigured()) {
        if (m_latest.signatureUrl.isEmpty()) {
            emit downloadFailed(QStringLiteral("This release is not signed — refusing to update."));
            return;
        }
        fetchSignature();
        return;
    }
    fetchInstaller();
}

void UpdateChecker::fetchSignature()
{
    QNetworkRequest req(m_latest.signatureUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_currentVersion));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onSignatureFetched(reply); });
}

void UpdateChecker::onSignatureFetched(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(QStringLiteral("Could not download the signature: %1").arg(reply->errorString()));
        return;
    }
    m_signatureData = reply->readAll();

#ifdef FT_HAVE_OPENSSL
    const QByteArray pub(kUpdateSigningPublicKeyPem);
    if (!verifyEd25519(m_checksumsData, m_signatureData, pub)) {
        emit downloadFailed(QStringLiteral("Update signature is invalid — aborting."));
        return;
    }
    fetchInstaller();
#else
    // A signing key is configured but this build can't verify it — fail closed.
    emit downloadFailed(QStringLiteral("This build cannot verify update signatures."));
#endif
}

void UpdateChecker::fetchInstaller()
{
    QNetworkRequest req(m_latest.installerUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_currentVersion));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                const qint64 base = m_checksumsData.isEmpty() ? 0 : 5;
                emit downloadProgress(base + received * 95 / qMax<qint64>(total, 1), 100);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onInstallerFetched(reply); });
}

void UpdateChecker::onInstallerFetched(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        emit downloadFailed(QStringLiteral("Download failed: %1").arg(reply->errorString()));
        return;
    }

    QFile out(m_downloadPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFailed(QStringLiteral("Could not write the downloaded file"));
        return;
    }
    out.write(reply->readAll());
    out.close();

    // Mandatory integrity check: the manifest must be present and the asset's
    // SHA-256 must match before we hand the file off to be executed.
    if (m_checksumsData.isEmpty()
        || !verifyFileAgainstSums(m_downloadPath, m_checksumsData, m_latest.assetName)) {
        QFile::remove(m_downloadPath);
        emit downloadFailed(QStringLiteral("Download failed integrity check (SHA-256 mismatch)"));
        return;
    }

    emit downloadReady(m_downloadPath);
}
