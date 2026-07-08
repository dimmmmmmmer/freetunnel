// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QCryptographicHash>
#include <QFile> // full type: std::unique_ptr<QFile> member needs it for ~UpdateChecker
#include <QObject>
#include <QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * Checks for application updates via GitHub Releases API.
 *
 * Usage:
 *   auto *checker = new UpdateChecker("dimmmmmmmer/freetunnel", "1.0.0", this);
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
        QString installerUrl; ///< direct download URL for the platform asset
        QString assetName;    ///< filename of the installer asset
        QString checksumsUrl; ///< SHA256SUMS.txt download URL (when published)
        QString signatureUrl; ///< SHA256SUMS.txt.sig (Ed25519) URL (when published)
    };

    /**
     * @param githubRepo  "owner/repo" string, e.g. "dimmmmmmmer/freetunnel"
     * @param currentVersion  current app version string, e.g. "0.6b"
     * @param parent  QObject parent
     */
    explicit UpdateChecker(const QString &githubRepo,
                           const QString &currentVersion,
                           QObject *parent = nullptr);

    /// Trigger an update check immediately.
    void checkNow();

    /// Download the latest release installer after updateAvailable, verifying
    /// against SHA256SUMS.txt when available.
    void downloadLatest();

    /// Latest release info (valid after updateAvailable signal).
    const ReleaseInfo &latestRelease() const { return m_latest; }

signals:
    /// Emitted when a newer version is found on GitHub.
    void updateAvailable(const ReleaseInfo &info);

    /// Emitted when the check completes with no update (or on error).
    void noUpdateAvailable(const QString &message);

    void downloadProgress(qint64 received, qint64 total);
    void downloadReady(const QString &localPath);
    void downloadFailed(const QString &message);

private slots:
    void onCheckFinished(QNetworkReply *reply);

private:
    void fetchChecksumsThenInstaller();
    void fetchSignature();
    void fetchInstaller();
    void onChecksumsFetched(QNetworkReply *reply);
    void onSignatureFetched(QNetworkReply *reply);
    void onInstallerFetched(QNetworkReply *reply);

    QString m_githubRepo;
    QString m_currentVersion;
    QNetworkAccessManager *m_nam = nullptr;
    ReleaseInfo m_latest;
    QByteArray m_checksumsData;
    QByteArray m_signatureData;
    QString m_downloadPath;
    // Installer download is streamed to disk and hashed incrementally so a
    // 100+ MB asset never sits in RAM in one piece.
    std::unique_ptr<QFile> m_installerOut;
    QCryptographicHash m_installerHash{QCryptographicHash::Sha256};
};
