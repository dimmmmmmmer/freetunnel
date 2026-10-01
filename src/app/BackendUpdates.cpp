// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"
#ifdef Q_OS_MACOS
#include "app/MacWindow.h"
#endif

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QWindow>

#include <QLocalServer>

#include "core/AppImagePath.h"
#include "core/InstanceControl.h"
#include "core/AppUiUtils.h"
#include "core/UpdateChecker.h"

// ---------- updater ----------

// Whether installing this release closes FreeTunnel, taking the tunnel down with
// it: on Windows the installer replaces it, and an AppImage is replaced and then
// restarted (applyLinuxUpdate). A function rather than a constant per platform:
// a lambda that captures a constant is a warning to clang, an error under -Werror.
static bool installingClosesTheApp(const UpdateChecker::ReleaseInfo &info)
{
#if defined(Q_OS_WIN)
    Q_UNUSED(info)
    return true;
#elif defined(Q_OS_MACOS)
    Q_UNUSED(info)
    return false;
#else
    return info.assetName.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive)
            && !freetunnel::updatableAppImage().path.isEmpty();
#endif
}

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
// Hand the verified download to whatever installs it. True when that started.
static bool startInstaller(const QString &path)
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Lets a test see an installer that will not start without making Windows
    // run a file that is not a program: on a fresh CI runner such a launch hung
    // past the test's time limit, while the same test run again took seconds.
    if (qEnvironmentVariableIsSet("FT_TEST_INSTALLER_WONT_START"))
        return false;
#endif
#if defined(Q_OS_WIN)
    return QProcess::startDetached(path, {});
#else
    return QProcess::startDetached(QStringLiteral("open"), {path});
#endif
}
#endif

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// Open the folder holding the verified download, so "we could not install this
// for you" comes with the file rather than just a path in a label. False when
// there was nothing to open it with.
static bool revealDownload(const QString &path)
{
    return QProcess::startDetached(QStringLiteral("xdg-open"), {QFileInfo(path).absolutePath()});
}

// Put the build that was running back where it was, over whatever replaced it.
static void restoreAppImage(const QString &current, const QString &backup)
{
    QFile::remove(current);
    QFile::rename(backup, current);
}

// Put the verified download where the running AppImage is. The running one is
// renamed aside to <name>.old rather than overwritten, so that whichever step
// fails — the rename, the copy, or making the copy executable (QFile::copy
// keeps the staged file's owner-only mode, so it is not executable until then)
// — the original goes back where it was. True with the new file in place and
// executable; the .old stays until the new one has been started.
static bool swapInAppImage(const QString &download, const QString &current, const QString &backup)
{
    QFile::remove(backup);
    if (!QFile::rename(current, backup))
        return false;
    if (QFile::copy(download, current)
        && QFile::setPermissions(current, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                  | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                                  | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                                  | QFileDevice::ExeOther))
        return true;
    restoreAppImage(current, backup);
    return false;
}

// Give the single-instance name up to the replacement by closing this instance's
// listener, which unlinks the name. Only unlinking it, as this used to, left the
// listener listening — and the application destroys it on the way out, and a
// listener being destroyed unlinks its name again: by then the replacement's
// socket. The new FreeTunnel ran on where nothing could reach it, and the next
// launch or tt:// link started a second one beside it. A closed listener has no
// name left to unlink. Connections it already accepted are sockets of their own
// and keep working.
static void giveUpTheInstanceName(QLocalServer *server)
{
    if (server)
        server->close();
    else
        QLocalServer::removeServer(freetunnel::instanceServerName());
}

// The replacement did not start, so this instance stays — and has to be the one
// a second launch or a tt:// link reaches again, or that starts a second
// FreeTunnel beside it. Listening again on the listener closed for the
// replacement puts it back. The token was never touched. False, with the same
// warning as when the application first listens, when the name could not be
// taken again; true when there is no listener to put back.
static bool listenAsTheRunningInstanceAgain(QLocalServer *server)
{
    if (!server || server->listen(freetunnel::instanceServerName()))
        return true;
    qWarning("Single-instance server failed to listen again on '%s': %s",
             qPrintable(freetunnel::instanceServerName()), qPrintable(server->errorString()));
    return false;
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
    const freetunnel::RunningAppImage running = freetunnel::updatableAppImage();
    const QString current = running.path;
    if (!path.endsWith(QStringLiteral(".AppImage"), Qt::CaseInsensitive) || current.isEmpty()) {
        // A .deb (or an AppImage we cannot locate on disk) is not ours to install:
        // that is the package manager's job, and doing it silently would need root.
        // Show the file and say so, instead of claiming success.
        if (!revealDownload(path)) {
            // Nothing to show it with: say where it is, and offer the release
            // page, as the row does for a release it cannot install. The row
            // is otherwise inert once an update is downloaded.
            m_updateState = QStringLiteral("error");
            m_updateErrorFromDownload = false;
            m_updateErrorOpensPage = true;
            setUpdateMessage([path] {
                return tr("Update downloaded to %1 — install it with your package manager.").arg(path);
            });
            emit updateChanged();
            return;
        }
        setUpdateMessage([] {
            return tr("Update downloaded. Finish installing it from the file manager — "
                      "packages are installed by your package manager.");
        });
        emit updateChanged();
        return;
    }

    // Replace the AppImage the user actually launches, then restart from it. The
    // path comes from the kernel (see runningAppImage), never from $APPIMAGE,
    // so a hostile environment cannot redirect this write. (Only a test build
    // lets the environment name it; see updatableAppImage.)
    const QString backup = current + QStringLiteral(".old");
    if (!swapInAppImage(path, current, backup)) {
        failAppImageUpdate([current] {
            return tr("Could not replace %1 — check that you can write to it.").arg(current);
        }, path);
        return;
    }

    // Give up the single-instance socket BEFORE the replacement starts, because
    // quitting does not do it. quitApplication() only posts an exit; the listening
    // QLocalServer is owned by the application and is destroyed after the event
    // loop returns, behind prepareQuit() and a credential-store round trip that
    // can sit on a locked keyring for as long as it likes. A replacement started
    // in that window connects, forwards "focus", and exits — which leaves nothing
    // running at all, and is the bug this ordering was written to avoid.
    //
    // Closing the listener rather than only unlinking its name: see
    // giveUpTheInstanceName. A new connectToServer() finds nothing and the
    // replacement starts normally. The token is deliberately left alone — the
    // replacement writes its own, and the quit handler now removes only a token
    // that is still ours.
    giveUpTheInstanceName(m_instanceServer);

    // This used to quit whether or not the replacement started, which left no
    // FreeTunnel running at all when it did not. Now this one stays, on the build
    // it was started from, and says so. Started the way this copy was: an AppImage
    // that had to be unpacked rather than mounted may not start any other way.
    if (!QProcess::startDetached(current, running.launchArguments())) {
        restoreAppImage(current, backup);
        if (!listenAsTheRunningInstanceAgain(m_instanceServer)) {
            appendLog(QStringLiteral("WARN"),
                      tr("FreeTunnel could not take back the name later launches look for "
                         "(%1), so opening it again may start a second FreeTunnel. Restart "
                         "FreeTunnel to fix this.")
                              .arg(m_instanceServer->errorString()));
        }
        failAppImageUpdate([] { return tr("The new version could not be started — "
                                          "FreeTunnel was left as it was."); },
                           path);
        return;
    }
    // Started: neither the old build nor the download is needed any more.
    QFile::remove(backup);
    QFile::remove(path);
    setUpdateMessage([] { return tr("Update installed — restarting"); });
    emit updateChanged();
    quitApplication();
}

// A replacement that failed is an error the row can retry, not "ready": left
// there, it showed the download arrow, and the arrow opened the release page
// instead. The download is shown, for installing by hand.
void Backend::failAppImageUpdate(std::function<QString()> words, const QString &path)
{
    m_updateState = QStringLiteral("error");
    m_updateErrorFromDownload = true;
    setUpdateMessage(std::move(words));
    emit updateChanged();
    revealDownload(path);
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
                const QString version = info.version;
                // Said before the click, not after.
                const bool closesApp = installingClosesTheApp(info);
                setUpdateMessage([version, closesApp] {
                    return closesApp ? tr("Version %1 is available — installing it closes FreeTunnel").arg(version)
                                     : tr("Version %1 is available").arg(version);
                });
                emit updateChanged();
            });
    connect(m_updater, &UpdateChecker::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                m_updateState = QStringLiteral("downloading");
                setUpdateMessage([received, total] {
                    return total > 0 ? tr("Downloading… %1%").arg(received * 100 / total)
                                     : tr("Downloading…");
                });
                emit updateChanged();
            });
    connect(m_updater, &UpdateChecker::downloadReady, this,
            [this](const QString &path) { onUpdateDownloadReady(path); });
    connect(m_updater, &UpdateChecker::downloadFailed, this,
            [this](const QString &msg, UpdateChecker::DownloadFailure kind) {
        m_updateState = QStringLiteral("error");
        // What the row offers next. A refused download is never retried with the
        // release this check found: only a new check, which may find it fixed.
        // Nothing for this platform: the release page, where the user can see why.
        m_updateErrorFromDownload = kind == UpdateChecker::DownloadFailure::Transient;
        m_updateErrorOpensPage = kind == UpdateChecker::DownloadFailure::NoInstaller;
        setUpdateMessage([msg] { return msg; });
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
        setUpdateMessage([upToDate, message] {
            return upToDate ? tr("You have the latest version")
                            : tr("Update check failed: %1").arg(message);
        });
        emit updateChanged();
    });
}

// What a verified download does next: start the installer, or on Linux install
// it here. Split out of wireUpdaterSignals(), where it was half the function.
void Backend::onUpdateDownloadReady(const QString &path)
{
    // Quitting, or saying the installer opened, when it never started
    // left the user with no FreeTunnel, or a line waiting on nothing.
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    if (!startInstaller(path)) {
        m_updateState = QStringLiteral("error");
        m_updateErrorFromDownload = true;
        setUpdateMessage([] { return tr("The downloaded installer could not be started"); });
        emit updateChanged();
        return;
    }
#endif
    m_updateState = QStringLiteral("ready");
#if defined(Q_OS_WIN)
    // Then get out of its way. The installer cannot replace files this
    // process has open, and it should not have to force us out either:
    // a forced kill would leave the tunnel up and the privileged helper
    // running with nothing left to stop them. Quitting here runs the
    // ordinary shutdown — tunnel down, helper stopped — while the
    // installer waits for us (see win/installer.nsi).
    setUpdateMessage([] { return tr("Update downloaded — closing FreeTunnel to install it"); });
    emit updateChanged();
    quitApplication();
#elif defined(Q_OS_MACOS)
    // What happens next is the user's: the line stays up all session.
    setUpdateMessage([] { return tr("Update downloaded — install it from the disk image that opened"); });
    emit updateChanged();
#else
    setUpdateMessage([] { return tr("Update downloaded — opening installer"); });
    emit updateChanged();
    applyLinuxUpdate(path);
#endif
}

void Backend::ensureUpdater()
{
    if (m_updater)
        return;
    m_updater = new UpdateChecker(QStringLiteral("dimmmmmmmer/freetunnel"), appVersion(), this);
    // Made once a run and before any download, so anything staged is an earlier
    // run's, done with.
    m_updater->discardStagedDownloads();
    wireUpdaterSignals();
}

// The update line is kept as a way to say it rather than as a sentence, so a
// change of language says it again in the new one. See retranslate().
void Backend::setUpdateMessage(std::function<QString()> words)
{
    m_updateWords = std::move(words);
    m_updateMessage = m_updateWords ? m_updateWords() : QString();
}

void Backend::checkForUpdates(bool userInitiated)
{
    // Not while a download runs either: the check's answer turned the state back
    // to "available" and offered the same download a second time, into the same
    // staging file as the first.
    if (m_updateState == QLatin1String("checking") || m_updateState == QLatin1String("downloading"))
        return;
    ensureUpdater();
    m_updateCheckUserInitiated = userInitiated;
    m_updateErrorFromDownload = false;
    m_updateErrorOpensPage = false;
    if (userInitiated) {
        m_updateState = QStringLiteral("checking");
        setUpdateMessage([] { return tr("Checking…"); });
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
    if (m_updateState == QLatin1String("error") && m_updateErrorOpensPage) {
        openHttpUrl(m_latestUrl.isEmpty()
                            ? QStringLiteral("https://github.com/dimmmmmmmer/freetunnel/releases/latest")
                            : m_latestUrl);
    } else if (m_updateState == QLatin1String("error") && !m_updateErrorFromDownload) {
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
    m_updateErrorOpensPage = false;
    setUpdateMessage([] { return tr("Downloading…"); });
    emit updateChanged();
    m_updater->downloadLatest();
}

void Backend::openUrl(const QString &url) {
    openHttpUrl(url);
}

// Handed over rather than looked up among the application's children: a lookup
// that found nothing, or another QLocalServer, would fail without a word.
void Backend::setInstanceServer(QLocalServer *server)
{
    m_instanceServer = server;
}

QLocalServer *Backend::instanceServer() const
{
    return m_instanceServer;
}

void Backend::startWindowDrag(QObject *window) {
    // The QQuickWindow content view eats mouse events, so AppKit's
    // movableByWindowBackground never fires; drive the native move directly.
    auto *w = qobject_cast<QWindow *>(window);
    if (!w)
        return;
#ifdef Q_OS_MACOS
    // Natively on macOS, which also gives the band the title-bar double-click the
    // user configured and survives presses Qt 6.8 cannot turn into a move. See
    // macHandleTitlebarPress(); Qt's own move is the fallback, not the path.
    if (macHandleTitlebarPress(w->winId()))
        return;
#endif
    w->startSystemMove();
}
