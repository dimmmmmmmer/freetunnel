// cppcheck-suppress-file missingIncludeSystem
// Core log file handling for QtTrustTunnelClient: the file the VPN core's log
// goes to, emptying it between sessions, and tailing it so the GUI can show core
// lines live. Split out of qt_trusttunnel_client.cpp, which had grown past the
// point where one file could be read end to end.
//
// The path is chosen here rather than accepted from the GUI on purpose: this
// runs elevated, so a caller-supplied log path would be a write primitive for
// root. See docs/security-threats.md.
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_events.h"
#include "qt_trusttunnel_platform.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QString>
#include <QTimer>

#include <cstdio>
#include <mutex>
#include <string_view>

#ifdef Q_OS_WIN
#include <share.h> // _SH_DENYNO
#endif

namespace {

std::FILE *openCoreLogForWriting(const QString &path)
{
#ifdef Q_OS_WIN
    // Not fopen: it reads the path in the ANSI code page, which cannot name a
    // profile folder with letters outside it. _wfsopen rather than _wfopen,
    // which /sdl refuses as deprecated; _SH_DENYNO is the sharing fopen uses.
    std::FILE *f = _wfsopen(path.toStdWString().c_str(), L"w", _SH_DENYNO);
#else
    std::FILE *f = std::fopen(QFile::encodeName(path).constData(), "w");
#endif
    // Unbuffered, so each line is in the file for the tail as soon as it is
    // logged rather than when a buffer happens to fill.
    if (f)
        std::setvbuf(f, nullptr, _IONBF, 0);
    return f;
}

// Where the core's log goes, for the life of the process.
//
// The core logs through ONE process-wide callback (ag::Logger), and its own way
// of logging to a file is unsafe in a process that outlives its clients: given a
// log path, TrustTunnelClient opens the file, points that callback at the FILE*,
// fclose()s it in its destructor — and never points the callback anywhere else.
// The helper builds a new client for every session, so every core line logged
// after a session ended went through a closed FILE*, in a root process, and with
// logging off for the next session (no path, so no new callback) so did every
// line of that whole session.
//
// So the core is never given a path. This is its callback instead, installed
// once and never replaced, and the file behind it is opened and closed only
// here, under the lock every write takes. Nothing can close it under the core.
class CoreLogSink {
public:
    static CoreLogSink &instance()
    {
        // Leaked on purpose: core threads can still be logging while statics are
        // destroyed at exit, and a destroyed mutex is no better than a closed FILE.
        static CoreLogSink *const sink = [] {
            auto *s = new CoreLogSink();
            ag::Logger::set_callback(
                    [s](ag::LogLevel level, std::string_view message) { s->write(level, message); });
            return s;
        }();
        return *sink;
    }

    void reopen(const QString &path)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        closeLocked();
        m_file = openCoreLogForWriting(path); // "w": every session starts empty
    }

    // Logging off means the lines go nowhere. Not back to the default callback:
    // that is stderr, which in the helper is a pipe or a temp file nobody reads.
    void close()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        closeLocked();
    }

private:
    void write(ag::LogLevel level, std::string_view message)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_file)
            return;
        // The core's own writer, on a FILE it does not own — so the lines look
        // exactly as they always have to the GUI, which parses them.
        ag::Logger::LogToFile toFile(m_file);
        toFile(level, message);
    }

    void closeLocked()
    {
        if (m_file)
            std::fclose(m_file);
        m_file = nullptr;
    }

    std::mutex m_mutex;
    std::FILE *m_file = nullptr;
};

} // namespace

void QtTrustTunnelClient::applyCoreLogPathToConfig()
{
    std::lock_guard<std::mutex> lk(m_configMutex);
    applyCoreLogPathToConfigLocked();
}

void QtTrustTunnelClient::applyCoreLogPathToConfigLocked()
{
    // Never the core's own log file, logging on or off: see CoreLogSink.
    if (m_config.has_value())
        m_config->log_file_path.clear();
    if (m_loggingEnabled && m_coreLogPath.isEmpty())
        m_coreLogPath = qt_trusttunnel_default_core_log_path();
}

void QtTrustTunnelClient::resetCoreLogFile()
{
    QString path;
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        if (m_loggingEnabled)
            path = m_coreLogPath;
    }
    // The sink is installed here at the latest, with logging on or off: this
    // runs before every attempt builds a core client.
    if (path.isEmpty()) {
        CoreLogSink::instance().close();
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    CoreLogSink::instance().reopen(path);
    m_coreLogOffset = 0;
}

void QtTrustTunnelClient::startCoreLogTail()
{
    // Called from ensureClientReady on the connect thread: a QTimer created
    // there would be parented across threads (Qt drops the parent) and take the
    // connect thread's affinity, where no event loop ever runs — the poll never
    // fired and no core log line reached the GUI. Marshal onto our own thread.
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { startCoreLogTail(); }, Qt::QueuedConnection);
        return;
    }
    // The hop above is queued, so a disconnect can land between the request and
    // this call — re-arming the poll then would leave it running with no session
    // behind it.
    if (m_stopRequested)
        return;
    QString path;
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        if (!m_loggingEnabled)
            return;
        applyCoreLogPathToConfigLocked();
        path = m_coreLogPath;
    }
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());

    if (!m_coreLogPoll) {
        m_coreLogPoll = new QTimer(this);
        m_coreLogPoll->setInterval(800);
        connect(m_coreLogPoll, &QTimer::timeout, this, &QtTrustTunnelClient::pollCoreLogFile);
    }
    if (!m_coreLogPoll->isActive())
        m_coreLogPoll->start();
    pollCoreLogFile();
}

void QtTrustTunnelClient::stopCoreLogTail()
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this]() { stopCoreLogTail(); }, Qt::QueuedConnection);
        return;
    }
    if (m_coreLogPoll)
        m_coreLogPoll->stop();
    m_coreLogLineBuffer.clear();
}

void QtTrustTunnelClient::pollCoreLogFile()
{
    QString path;
    {
        std::lock_guard<std::mutex> lk(m_configMutex);
        path = m_coreLogPath;
    }
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    if (!f.seek(m_coreLogOffset))
        return;
    const QByteArray chunk = f.readAll();
    m_coreLogOffset = f.pos();
    f.close();
    // Not "return when the file did not grow": the per-poll line cap leaves whole
    // lines in the buffer, and they were only ever flushed by the NEXT chunk. A
    // core that logged a burst and then went quiet — which is exactly what happens
    // once a tunnel settles — left its last lines sitting in memory, never shown.
    if (chunk.isEmpty() && m_coreLogLineBuffer.isEmpty())
        return;

    constexpr int kMaxLinesPerPoll = 24;
    drainCoreLogTailBytes(&m_coreLogLineBuffer, chunk, kMaxLinesPerPoll,
            [this](const QString &line) { emit coreLogLine(line); });
}
