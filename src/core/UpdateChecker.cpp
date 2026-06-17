#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

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

    m_latest.tagName = tagName;
    m_latest.version = remoteVersion;
    m_latest.htmlUrl = obj.value("html_url").toString();
    m_latest.body = obj.value("body").toString();

    // Find installer asset (*.exe for Windows, *.dmg for macOS)
    const QJsonArray assets = obj.value("assets").toArray();
    for (const QJsonValue &val : assets) {
        const QJsonObject asset = val.toObject();
        const QString name = asset.value("name").toString();
#ifdef _WIN32
        if (name.endsWith(".exe", Qt::CaseInsensitive)) {
#elif defined(__APPLE__)
        if (name.endsWith(".dmg", Qt::CaseInsensitive)) {
#else
        if (name.endsWith(".AppImage", Qt::CaseInsensitive) || name.endsWith(".deb", Qt::CaseInsensitive)) {
#endif
            m_latest.installerUrl = asset.value("browser_download_url").toString();
            m_latest.assetName = name;
            break;
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
