// cppcheck-suppress-file missingIncludeSystem
#include "UpdateChecker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include "ReleaseSigning.h"
#include "ReleaseVerify.h"
#include "VersionCompare.h"

namespace {

bool signatureVerificationConfigured()
{
    return freetunnel::kReleaseSigningPublicKeyPem
           && freetunnel::kReleaseSigningPublicKeyPem[0] != '\0';
}

bool signatureVerificationActive()
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Test-only escape hatch: compiled out of release builds so a shipped binary
    // can never be told to skip update-signature verification via the environment.
    if (qEnvironmentVariableIsSet("FT_TEST_SKIP_UPDATE_SIG"))
        return false;
#endif
    return signatureVerificationConfigured();
}

// Release assets must be downloaded from GitHub's own hosts. The asset URLs come
// from the GitHub API JSON, so without this an API/MITM that swapped a
// browser_download_url could point the installer/manifest at an attacker host.
// Integrity is still gated by the Ed25519-signed SHA256SUMS, but pinning the
// host is cheap defense-in-depth (and matters if signing is ever unconfigured).
bool isGithubHost(const QString &host)
{
    return host == QLatin1String("github.com")
            || host.endsWith(QLatin1String(".github.com"))
            || host.endsWith(QLatin1String(".githubusercontent.com"));
}

#ifdef FT_ENABLE_TEST_HOOKS
// When the update endpoint is redirected for tests, allow that same host (the
// mock serves assets over http on loopback). The whole helper — and its call
// site below — only exist in test-hook builds, never in a shipped binary.
bool matchesTestBaseHost(const QString &host)
{
    const QByteArray base = qgetenv("FT_GITHUB_API_BASE");
    if (base.isEmpty())
        return false;
    const QUrl baseUrl(QString::fromUtf8(base));
    return baseUrl.isValid() && !host.isEmpty() && host == baseUrl.host().toLower();
}
#endif

bool isTrustedDownloadUrl(const QString &urlStr)
{
    const QUrl url(urlStr);
    if (!url.isValid())
        return false;
    const QString host = url.host().toLower();
#ifdef FT_ENABLE_TEST_HOOKS
    if (matchesTestBaseHost(host))
        return true;
#endif
    if (url.scheme() != QLatin1String("https"))
        return false;
    return isGithubHost(host);
}

QString githubApiUrl(const QString &path)
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Test-only redirect of the update endpoint. Compiled out of release builds
    // so a shipped binary always talks to GitHub and can't be pointed at a
    // server that suppresses updates (freezing the user on a vulnerable build).
    const QByteArray base = qgetenv("FT_GITHUB_API_BASE");
    if (!base.isEmpty()) {
        const QString root = QString::fromUtf8(base);
        return root.endsWith(QLatin1Char('/')) ? root.left(root.size() - 1) + path
                                               : root + path;
    }
#endif
    return QStringLiteral("https://api.github.com") + path;
}
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
    const QString url = githubApiUrl(QStringLiteral("/repos/%1/releases/latest").arg(m_githubRepo));

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
        const QString downloadUrl = asset.value("browser_download_url").toString();
        // Ignore any asset whose download URL isn't on a GitHub host.
        if (!isTrustedDownloadUrl(downloadUrl))
            continue;
        if (name == QLatin1String("SHA256SUMS.txt")) {
            m_latest.checksumsUrl = downloadUrl;
            continue;
        }
        if (name == QLatin1String("SHA256SUMS.txt.sig")) {
            m_latest.signatureUrl = downloadUrl;
            continue;
        }
#ifdef _WIN32
        if (name.endsWith(".exe", Qt::CaseInsensitive)) {
#elif defined(__APPLE__)
        if (name.endsWith(".dmg", Qt::CaseInsensitive)) {
#else
        if (name.endsWith(".AppImage", Qt::CaseInsensitive) || name.endsWith(".deb", Qt::CaseInsensitive)) {
#endif
            m_latest.installerUrl = downloadUrl;
            // Basename only — never let a crafted asset name escape the temp dir.
            m_latest.assetName = QFileInfo(name).fileName();
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
    // Owner-only: the installer is verified (Ed25519 + SHA-256) and then executed,
    // so keep other local users from swapping it in the window between the two.
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
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
            [this](qint64 received, qint64 total) {
                emit downloadProgress(received * 5 / qMax<qint64>(total, 1), 100);
            });
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

    if (signatureVerificationActive()) {
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
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                emit downloadProgress(5 + received * 5 / qMax<qint64>(total, 1), 100);
            });
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

    const QByteArray pub(freetunnel::kReleaseSigningPublicKeyPem);
    if (!verifyEd25519Signature(m_checksumsData, m_signatureData, pub)) {
        emit downloadFailed(QStringLiteral("Update signature is invalid — aborting."));
        return;
    }
    fetchInstaller();
}

void UpdateChecker::fetchInstaller()
{
    QNetworkRequest req(m_latest.installerUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_currentVersion));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                const qint64 base = signatureVerificationActive() ? 10 : 5;
                emit downloadProgress(base + received * (100 - base) / qMax<qint64>(total, 1), 100);
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
