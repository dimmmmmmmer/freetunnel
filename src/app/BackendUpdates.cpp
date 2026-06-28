// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QFile>
#include <QProcess>
#include <QWindow>

#include "core/AppUiUtils.h"
#include "core/UpdateChecker.h"

// ---------- updater ----------

QString Backend::appVersion() const {
#ifdef FREETUNNEL_VERSION
    return QStringLiteral(FREETUNNEL_VERSION);
#else
    return QStringLiteral("1.1.1");
#endif
}

QString Backend::coreVersion() const {
#ifdef FREETUNNEL_CORE_REF
    return QStringLiteral(FREETUNNEL_CORE_REF);
#else
    return QStringLiteral("unknown");
#endif
}

void Backend::wireUpdaterSignals()
{
    connect(m_updater, &UpdateChecker::updateAvailable, this,
            [this](const UpdateChecker::ReleaseInfo &info) {
                m_updateState = QStringLiteral("available");
                m_latestVersion = info.version;
                m_latestUrl = info.htmlUrl;
                m_updateMessage = tr("Version %1 is available").arg(info.version);
                emit updateChanged();
            });
    connect(m_updater, &UpdateChecker::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_updateState = QStringLiteral("downloading");
                m_updateMessage = total > 0 ? tr("Downloading… %1%").arg(received * 100 / total)
                                            : tr("Downloading…");
                emit updateChanged();
            });
    connect(m_updater, &UpdateChecker::downloadReady, this,
            [this](const QString &path) {
                m_updateState = QStringLiteral("ready");
                m_updateMessage = tr("Update downloaded — opening installer");
                emit updateChanged();
#if defined(Q_OS_WIN)
                QProcess::startDetached(path, {});
#elif defined(Q_OS_MACOS)
                QProcess::startDetached(QStringLiteral("open"), {path});
#else
                if (path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)) {
                    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner);
                    QProcess::startDetached(path, {});
                } else {
                    QProcess::startDetached(QStringLiteral("xdg-open"), {path});
                }
#endif
            });
    connect(m_updater, &UpdateChecker::downloadFailed, this, [this](const QString &msg) {
        m_updateState = QStringLiteral("error");
        m_updateMessage = msg;
        emit updateChanged();
    });
    connect(m_updater, &UpdateChecker::noUpdateAvailable, this, [this](const QString &) {
        if (!m_updateCheckUserInitiated)
            return;
        m_updateState = QStringLiteral("current");
        m_updateMessage = tr("You have the latest version");
        emit updateChanged();
    });
}

void Backend::ensureUpdater()
{
    if (m_updater)
        return;
    m_updater = new UpdateChecker(QStringLiteral("dimmmmmmmer/freetunnel"), appVersion(), this);
    wireUpdaterSignals();
}

void Backend::checkForUpdates(bool userInitiated)
{
    if (m_updateState == QLatin1String("checking"))
        return;
    ensureUpdater();
    m_updateCheckUserInitiated = userInitiated;
    if (userInitiated) {
        m_updateState = QStringLiteral("checking");
        m_updateMessage = tr("Checking…");
        emit updateChanged();
    }
    m_updater->checkNow();
}

void Backend::openLatestRelease() {
    if (m_updateState == QLatin1String("available") || m_updateState == QLatin1String("error"))
        downloadUpdate();
    else {
        const QString url = m_latestUrl.isEmpty()
                ? QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/latest")
                : m_latestUrl;
        openHttpUrl(url);
    }
}

void Backend::downloadUpdate() {
    if (!m_updater || m_updateState == QLatin1String("downloading"))
        return;
    m_updateState = QStringLiteral("downloading");
    m_updateMessage = tr("Downloading…");
    emit updateChanged();
    m_updater->downloadLatest();
}

void Backend::openUrl(const QString &url) {
    openHttpUrl(url);
}

void Backend::startWindowDrag(QObject *window) {
    // The QQuickWindow content view eats mouse events, so AppKit's
    // movableByWindowBackground never fires; drive the native move directly.
    if (auto *w = qobject_cast<QWindow *>(window))
        w->startSystemMove();
}
