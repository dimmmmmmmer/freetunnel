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

    m_checksumsData.clear();
    if (!m_latest.checksumsUrl.isEmpty()) {
        fetchChecksumsThenInstaller();
    } else {
        fetchInstaller();
    }
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

    if (!m_checksumsData.isEmpty()
        && !verifyFileAgainstSums(m_downloadPath, m_checksumsData, m_latest.assetName)) {
        QFile::remove(m_downloadPath);
        emit downloadFailed(QStringLiteral("Download failed integrity check (SHA-256 mismatch)"));
        return;
    }

    emit downloadReady(m_downloadPath);
}
