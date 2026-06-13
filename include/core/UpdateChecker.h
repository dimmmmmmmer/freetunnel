#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Checks for application updates via GitHub Releases API.
 *
 * Usage:
 *   auto *checker = new UpdateChecker("pnsrc/TrustTunnelClient", "0.7", this);
 *   connect(checker, &UpdateChecker::updateAvailable, ...);
 *   checker->checkNow();
 */
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    struct ReleaseInfo {
        QString tagName;      ///< e.g. "v0.6b"
        QString version;      ///< tag without leading 'v', e.g. "0.6b"
        QString htmlUrl;      ///< browser URL to the release page
        QString body;         ///< release notes / changelog (markdown)
        QString installerUrl; ///< direct download URL for the .exe asset (Windows)
        QString assetName;    ///< filename of the installer asset
    };

    /**
     * @param githubRepo  "owner/repo" string, e.g. "pnsrc/TrustTunnelClient"
     * @param currentVersion  current app version string, e.g. "0.6b"
     * @param parent  QObject parent
     */
    explicit UpdateChecker(const QString &githubRepo,
                           const QString &currentVersion,
                           QObject *parent = nullptr);

    /// Trigger an update check immediately.
    void checkNow();

    /// Latest release info (valid after updateAvailable signal).
    const ReleaseInfo &latestRelease() const { return m_latest; }

signals:
    /// Emitted when a newer version is found on GitHub.
    void updateAvailable(const ReleaseInfo &info);

    /// Emitted when the check completes with no update (or on error).
    void noUpdateAvailable(const QString &message);

private slots:
    void onCheckFinished(QNetworkReply *reply);

private:
    bool isNewerVersion(const QString &remote) const;

    QString m_githubRepo;
    QString m_currentVersion;
    QNetworkAccessManager *m_nam = nullptr;
    ReleaseInfo m_latest;
};
