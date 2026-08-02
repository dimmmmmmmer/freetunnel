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
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

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

bool isGithubHost(const QString &host)
{
    return host == QLatin1String("github.com") || host == QLatin1String("www.github.com");
}

// Tags end up in a URL path we compare against and in the version we show, so
// keep them to what a real release tag can contain (v1.2.3, v1.2.3-rc1, …).
bool isPlausibleTag(const QString &tag)
{
    static const QRegularExpression re(QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._+-]{0,63}\\z"));
    return re.match(tag).hasMatch();
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

// Split a GitHub URL into its path segments when it is an https URL on GitHub's
// own host. Empty otherwise. The asset/page URLs all come out of the same
// untrusted API JSON, so nothing below trusts them beyond what this returns.
QStringList githubUrlSegments(const QString &urlStr)
{
    const QUrl url(urlStr);
    if (!url.isValid() || url.scheme() != QLatin1String("https"))
        return {};
    if (!isGithubHost(url.host().toLower()))
        return {};
    return url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// Release assets must come from *this* repo's release for *the advertised tag*:
// https://github.com/<owner>/<repo>/releases/download/<tag>/<asset>.
//
// Host pinning alone is not enough. The signature only covers SHA256SUMS.txt and
// our asset names carry no version (freetunnel-linux-x86_64.deb, …), so anyone
// able to serve the API response could advertise tag_name v9.9.9 while pointing
// browser_download_url at a genuine, still-signed *older* release — every check
// would pass and the user would be silently rolled back onto a known-vulnerable
// build. GitHub itself puts the release tag in the asset path, so requiring the
// path to match the advertised tag binds the manifest and the installer to that
// tag; a forged tag has no real release to be served from. The owner/repo
// segments are checked too, or the same trick would work from an attacker's own
// repo with a tag named to match.
bool isTrustedAssetUrl(const QString &urlStr, const QString &repo, const QString &tag)
{
#ifdef FT_ENABLE_TEST_HOOKS
    if (matchesTestBaseHost(QUrl(urlStr).host().toLower()))
        return true;
#endif
    const QStringList seg = githubUrlSegments(urlStr);
    if (seg.size() != 6)
        return false;
    if (seg.at(2) != QLatin1String("releases") || seg.at(3) != QLatin1String("download"))
        return false;
    if (QStringLiteral("%1/%2").arg(seg.at(0), seg.at(1)).compare(repo, Qt::CaseInsensitive) != 0)
        return false;
    return seg.at(4) == tag;
}

// html_url is handed straight to the user's browser, and it arrives in the same
// untrusted JSON as everything else — a hostile API response would otherwise get
// an arbitrary page opened for the user. Take it only when it really is this
// repo's release page; otherwise fall back to the canonical releases URL, which
// is built from the compiled-in repo and can't be influenced.
QString sanitizedReleasePageUrl(const QString &urlStr, const QString &repo, const QString &tag)
{
#ifdef FT_ENABLE_TEST_HOOKS
    if (matchesTestBaseHost(QUrl(urlStr).host().toLower()))
        return urlStr;
#endif
    const QStringList seg = githubUrlSegments(urlStr);
    const bool isReleasePage =
            seg.size() == 5 && seg.at(2) == QLatin1String("releases")
            && seg.at(3) == QLatin1String("tag") && seg.at(4) == tag
            && QStringLiteral("%1/%2").arg(seg.at(0), seg.at(1)).compare(repo, Qt::CaseInsensitive) == 0;
    if (isReleasePage)
        return urlStr;
    return QStringLiteral("https://github.com/%1/releases/latest").arg(repo);
}

// Where the verified installer is staged before it is executed. Not
// TempLocation: on Linux that is the shared /tmp, where another local user can
// pre-create (and therefore own) a fixed subdirectory — our chmod then silently
// fails, and they can plant a symlink under the asset name or overwrite the
// .AppImage/.deb after we verify it and before it runs. The per-user cache dir
// lives inside the user's own home on every platform and cannot be squatted.
QString updateStagingDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/updates");
}

// Create the staging dir and prove we got what we asked for: a real directory
// (not a symlink pointed elsewhere), owned by us, with nothing for group/other.
// Refuse to download at all rather than stage an executable somewhere another
// local user can reach.
bool prepareStagingDir(const QString &dir, QString *error)
{
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("Could not create the update download directory");
        return false;
    }
    if (!QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                            | QFileDevice::ExeOwner)) {
        *error = QStringLiteral("Could not make the update download directory owner-only");
        return false;
    }
#if defined(Q_OS_UNIX)
    struct stat st = {};
    if (::lstat(QFile::encodeName(dir).constData(), &st) != 0 || !S_ISDIR(st.st_mode)
        || st.st_uid != ::geteuid() || (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        *error = QStringLiteral("The update download directory is not private to this user");
        return false;
    }
#endif
    return true;
}

// Create the installer file itself: owner-only from the moment it exists, and
// never through a pre-planted symlink (O_NOFOLLOW) or an existing file
// (O_EXCL) — the caller removes any leftover from a previous download first.
bool createInstallerFile(QFile &f, const QString &path)
{
#if defined(Q_OS_UNIX)
    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    if (!f.open(fd, QIODevice::WriteOnly, QFile::AutoCloseHandle)) {
        ::close(fd);
        return false;
    }
    return true;
#else
    // Windows: the file inherits the (owner-only) directory ACL; NewOnly gives
    // the same "must not exist already" guarantee as O_EXCL.
    return f.open(QIODevice::WriteOnly | QIODevice::NewOnly);
#endif
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
    if (!isPlausibleTag(tagName)) {
        emit noUpdateAvailable(QStringLiteral("Invalid response from GitHub API"));
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
    m_latest.htmlUrl = sanitizedReleasePageUrl(obj.value("html_url").toString(),
                                               m_githubRepo, tagName);

    const QJsonArray assets = obj.value("assets").toArray();
    for (const QJsonValue &val : assets) {
        const QJsonObject asset = val.toObject();
        const QString name = asset.value("name").toString();
        const QString downloadUrl = asset.value("browser_download_url").toString();
        // Ignore any asset that isn't published under this repo's release for
        // the tag the response is advertising.
        if (!isTrustedAssetUrl(downloadUrl, m_githubRepo, tagName))
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

void UpdateChecker::downloadLatest()
{
    if (m_latest.installerUrl.isEmpty()) {
        emit downloadFailed(QStringLiteral("No installer asset found for this platform"));
        return;
    }

    // Owner-only: the installer is verified (Ed25519 + SHA-256) and then executed,
    // so keep other local users from swapping it in the window between the two.
    const QString dir = updateStagingDir();
    QString dirError;
    if (!prepareStagingDir(dir, &dirError)) {
        emit downloadFailed(dirError);
        return;
    }
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
                // total <= 0 = size unknown (no Content-Length): report an
                // indeterminate (0, 0) instead of a garbage percentage.
                if (total <= 0)
                    emit downloadProgress(0, 0);
                else
                    emit downloadProgress(received * 5 / total, 100);
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
                if (total <= 0)
                    emit downloadProgress(0, 0);
                else
                    emit downloadProgress(5 + received * 5 / total, 100);
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
    QFile::remove(m_downloadPath); // leftover from an earlier run; removes a symlink as itself
    m_installerOut = std::make_unique<QFile>(m_downloadPath);
    if (!createInstallerFile(*m_installerOut, m_downloadPath)) {
        m_installerOut.reset();
        emit downloadFailed(QStringLiteral("Could not write the downloaded file"));
        return;
    }

    QNetworkRequest req(m_latest.installerUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FreeTunnel/%1").arg(m_currentVersion));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_nam->get(req);
    // Stream each chunk straight to disk — an installer is 100+ MB and must
    // never be buffered in RAM as one piece. The hash is taken from the file
    // afterwards, not from the stream (see onInstallerFetched).
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (!m_installerOut)
            return;
        const QByteArray chunk = reply->readAll();
        if (m_installerOut->write(chunk) != chunk.size()) {
            m_installerOut->close();
            m_installerOut.reset(); // disk full / IO error — fail in finished()
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total <= 0) {
                    emit downloadProgress(0, 0);
                    return;
                }
                const qint64 base = signatureVerificationActive() ? 10 : 5;
                emit downloadProgress(base + received * (100 - base) / total, 100);
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onInstallerFetched(reply); });
}

void UpdateChecker::onInstallerFetched(QNetworkReply *reply)
{
    reply->deleteLater();

    if (m_installerOut) { // drain whatever arrived after the last readyRead
        const QByteArray rest = reply->readAll();
        if (!rest.isEmpty()) {
            if (m_installerOut->write(rest) != rest.size())
                m_installerOut.reset();
        }
    }
    if (m_installerOut) {
        m_installerOut->close();
        m_installerOut.reset();
    } else {
        // Write error mid-stream (or open failure surfaced earlier).
        QFile::remove(m_downloadPath);
        if (reply->error() == QNetworkReply::NoError) {
            emit downloadFailed(QStringLiteral("Could not write the downloaded file"));
            return;
        }
    }
    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(m_downloadPath);
        emit downloadFailed(QStringLiteral("Download failed: %1").arg(reply->errorString()));
        return;
    }

    // Mandatory integrity check: the manifest must be present and the asset's
    // SHA-256 must match before we hand the file off to be executed. Hash the
    // file we just closed, not the bytes that came off the socket — the file on
    // disk is what gets executed, and only re-reading it covers a truncated
    // write or anything that touched the file while it was being written.
    if (m_checksumsData.isEmpty()
        || !verifyFileAgainstSums(m_downloadPath, m_checksumsData, m_latest.assetName)) {
        QFile::remove(m_downloadPath);
        emit downloadFailed(QStringLiteral("Download failed integrity check (SHA-256 mismatch)"));
        return;
    }

    emit downloadReady(m_downloadPath);
}
