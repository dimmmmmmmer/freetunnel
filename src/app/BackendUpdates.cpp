// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QWindow>

#include "core/AppImagePath.h"
#include "core/AppUiUtils.h"
#include "core/UpdateChecker.h"

// ---------- updater ----------

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// Open the folder holding the verified download, so "we could not install this
// for you" comes with the file rather than just a path in a label.
static void revealDownload(const QString &path)
{
    QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
}

// Install a verified Linux download, or say honestly that we cannot.
//
// The old code ran the downloaded .AppImage straight out of the cache. That
// never updated anything: the new process reaches runGuiApplication, calls
// forwardToRunningInstance(), finds this instance, sends it "focus" and exits —
// so the window merely came forward while the UI announced "Update downloaded".
// Even with no instance running, executing a copy in the cache replaces neither
// the installed .deb nor the .AppImage the user launches.
void Backend::applyLinuxUpdate(const QString &path)
{
    const QString current = freetunnel::runningAppImagePath();
    if (!path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive) || current.isEmpty()) {
        // A .deb (or an AppImage we cannot locate on disk) is not ours to install:
        // that is the package manager's job, and doing it silently would need root.
        // Show the file and say so, instead of claiming success.
        m_updateMessage = tr("Update downloaded. Finish installing it from the file manager — "
                             "packages are installed by your package manager.");
        emit updateChanged();
        revealDownload(path);
        return;
    }

    // Replace the AppImage the user actually launches, then restart from it. The
    // path comes from the kernel (see runningAppImagePath), never from $APPIMAGE,
    // so a hostile environment cannot redirect this write.
    const QString backup = current + QStringLiteral(".old");
    QFile::remove(backup);
    if (!QFile::rename(current, backup)) {
        m_updateMessage = tr("Could not replace %1 — check that you can write to it.").arg(current);
        emit updateChanged();
        revealDownload(path);
        return;
    }
    if (!QFile::copy(path, current)) {
        QFile::rename(backup, current); // put the working build back
        m_updateMessage = tr("Could not replace %1 — check that you can write to it.").arg(current);
        emit updateChanged();
        revealDownload(path);
        return;
    }
    QFile::setPermissions(current, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                           | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                           | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                           | QFileDevice::ExeOther);
    QFile::remove(backup);

    // Quit first: the replacement cannot start while this instance still owns the
    // single-instance socket — it would forward "focus" and exit, which is the bug
    // being fixed. startDetached survives our exit.
    m_updateMessage = tr("Update installed — restarting");
    emit updateChanged();
    QProcess::startDetached(current, {});
    quitApplication();
}
#endif

QString Backend::appVersion() const {
#ifdef FREETUNNEL_VERSION
    return QStringLiteral(FREETUNNEL_VERSION);
#else
    return QStringLiteral("1.1.6");
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
                // Then get out of its way. The installer cannot replace files this
                // process has open, and it should not have to force us out either:
                // a forced kill would leave the tunnel up and the privileged helper
                // running with nothing left to stop them. Quitting here runs the
                // ordinary shutdown — tunnel down, helper stopped — while the
                // installer waits for us (see win/installer.nsi).
                m_updateMessage = tr("Update downloaded — closing FreeTunnel to install it");
                emit updateChanged();
                quitApplication();
#elif defined(Q_OS_MACOS)
                QProcess::startDetached(QStringLiteral("open"), {path});
#else
                applyLinuxUpdate(path);
#endif
            });
    connect(m_updater, &UpdateChecker::downloadFailed, this, [this](const QString &msg) {
        m_updateState = QStringLiteral("error");
        m_updateErrorFromDownload = true;
        m_updateMessage = msg;
        emit updateChanged();
    });
    connect(m_updater, &UpdateChecker::noUpdateAvailable, this, [this](const QString &message) {
        if (!m_updateCheckUserInitiated)
            return;
        // noUpdateAvailable doubles as the checker's failure path — it also carries
        // "Network error: …" / "Invalid response from GitHub API". Dropping the
        // message told an offline user their check had succeeded and they were up
        // to date; only the checker's own up-to-date line means we actually reached
        // GitHub and compared versions.
        const bool upToDate = message.contains(QLatin1String("latest version"),
                                               Qt::CaseInsensitive);
        m_updateState = upToDate ? QStringLiteral("current") : QStringLiteral("error");
        m_updateMessage = upToDate ? tr("You have the latest version")
                                   : tr("Update check failed: %1").arg(message);
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
    m_updateErrorFromDownload = false;
    if (userInitiated) {
        m_updateState = QStringLiteral("checking");
        m_updateMessage = tr("Checking…");
        emit updateChanged();
    }
    m_updater->checkNow();
}

void Backend::openLatestRelease() {
    // "error" covers a failed download (retry it — we know what to fetch) and a
    // failed update *check*, where there is nothing resolved to download yet.
    // Track which one it was explicitly: m_latestVersion was a bad proxy, because
    // it is set on the first successful check and never cleared, so once ANY
    // check had found a release every later CHECK failure was treated as a
    // download failure and silently started downloading instead of retrying.
    if (m_updateState == QLatin1String("error") && !m_updateErrorFromDownload) {
        checkForUpdates(true);
    } else if (m_updateState == QLatin1String("available")
               || m_updateState == QLatin1String("error")) {
        downloadUpdate();
    } else {
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
