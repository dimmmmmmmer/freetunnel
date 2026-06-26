// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include "core/AppUiUtils.h"

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>
#include <QVariantMap>

// Keep the on-disk log from growing without bound: if it exceeds the cap,
// drop the oldest entries and keep the most recent tail.
void Backend::trimLogFile() {
    const QString p = logPath();
    QFileInfo fi(p);
    if (!fi.exists() || fi.size() <= 5 * 1024 * 1024) // 5 MB cap
        return;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
        return;
    f.seek(fi.size() - 2 * 1024 * 1024); // keep the last ~2 MB
    f.readLine();                        // discard the partial first line
    const QByteArray tail = f.readAll();
    f.close();
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write("... (older log entries trimmed) ...\n");
        f.write(tail);
        f.close();
    }
}

void Backend::appendLog(const QString &level, const QString &msg) {
    if (!m_settings.logging_enabled)
        return;
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    // Incremental insert: the virtualized ListView only re-lays the new row, so a
    // connect-time burst no longer stalls the UI (and no coalescing is needed).
    m_logModel.append(time, level, msg);
    // CORE lines are written by the VPN core into the shared log file; keep UI only.
    if (level == QLatin1String("CORE"))
        return;
    const QString lp = logPath();
    QDir().mkpath(QFileInfo(lp).absolutePath());
    // The log can contain connection/domain info — keep it owner-only. Set perms
    // only when first creating the file to avoid a syscall on every line.
    const bool logExisted = QFileInfo::exists(lp);
    QFile f(lp);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        const QString line = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd"))
                + QLatin1Char(' ') + time + QLatin1Char('\t') + level + QLatin1Char('\t') + msg
                + QLatin1Char('\n');
        f.write(line.toUtf8());
        f.close();
        if (!logExisted)
            QFile::setPermissions(lp, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
}

// Load the most recent on-disk log lines into the in-memory view at startup so
// history isn't lost across restarts.
void Backend::loadLogTail() {
    QFile f(logPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    f.close();
    const int start = qMax(0, lines.size() - 200);
    for (int i = start; i < lines.size(); ++i) {
        const QStringList parts = lines.at(i).split(QLatin1Char('\t'));
        if (parts.size() >= 3)
            m_logModel.append(parts.at(0).section(QLatin1Char(' '), 1, 1), parts.at(1),
                              parts.mid(2).join(QLatin1Char('\t')));
        else
            m_logModel.append(QString(), QStringLiteral("CORE"), lines.at(i));
    }
}

void Backend::copyToClipboard(const QString &text) const {
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

QString Backend::readTextFile(const QString &pathOrUrl) const {
    return safeReadUserTextFile(pathOrUrl);
}

QString Backend::logText() const {
    return m_logModel.toPlainText();
}

void Backend::clearLogs() {
    m_logModel.clear();
    QFile::remove(logPath()); // also clear the on-disk file
}

void Backend::openLogFolder() {
    QString path = m_settings.log_path;
    if (path.isEmpty()) {
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                + QStringLiteral("/freetunnel.log");
    }
    const QString dir = QFileInfo(path).absolutePath();
#if defined(Q_OS_MACOS)
    if (QFileInfo::exists(path))
        QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), path});
    else
        QProcess::startDetached(QStringLiteral("open"), {dir});
#elif defined(Q_OS_WIN)
    if (QFileInfo::exists(path))
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
    else
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(dir)});
#else
    QProcess::startDetached(QStringLiteral("xdg-open"), {dir});
#endif
}
