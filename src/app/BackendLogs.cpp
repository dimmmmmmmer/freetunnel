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
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    QVariantMap e;
    e[QStringLiteral("time")] = time;
    e[QStringLiteral("level")] = level;
    e[QStringLiteral("msg")] = msg;
    m_log.append(e);
    if (m_log.size() > 500)
        m_log.removeFirst();
    // Persist to disk so the log survives restarts and shows up in the folder.
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
    emit logChanged();
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
        if (parts.size() < 3) continue;
        const QString dt = parts.at(0); // "yyyy-MM-dd HH:mm:ss"
        QVariantMap e;
        e[QStringLiteral("time")] = dt.section(QLatin1Char(' '), 1, 1);
        e[QStringLiteral("level")] = parts.at(1);
        e[QStringLiteral("msg")] = parts.mid(2).join(QLatin1Char('\t'));
        m_log.append(e);
    }
    if (!m_log.isEmpty())
        emit logChanged();
}

void Backend::copyToClipboard(const QString &text) const {
    if (auto *cb = QGuiApplication::clipboard())
        cb->setText(text);
}

QString Backend::readTextFile(const QString &pathOrUrl) const {
    return safeReadUserTextFile(pathOrUrl);
}

QString Backend::logText() const {
    QStringList out;
    for (const QVariant &v : m_log) {
        const QVariantMap e = v.toMap();
        out << e.value(QStringLiteral("time")).toString() + QLatin1Char(' ')
               + e.value(QStringLiteral("level")).toString() + QLatin1Char(' ')
               + e.value(QStringLiteral("msg")).toString();
    }
    return out.join(QLatin1Char('\n'));
}

void Backend::clearLogs() {
    m_log.clear();
    QFile::remove(logPath()); // also clear the on-disk file
    emit logChanged();
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
