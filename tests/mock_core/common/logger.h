// cppcheck-suppress-file missingIncludeSystem
// Mock of the core's ag::Logger (native-libs-common, common/logger.h): the parts
// the Qt wrapper and the mock core touch, with the property that makes the real
// one dangerous in a long-lived process kept intact. The callback is ONE value
// for the whole process; set_callback replaces it and nothing ever puts the old
// one back. LogToFile holds a FILE* it does not own.
//
// What a test cannot see in the real thing is a write through a FILE* that has
// already been fclose()d: the bytes are touched inside the C library, where ASan
// does not look, and usually nothing visible happens. So the mock core's file
// handler records each FILE it closes, and a LogToFile asked to write through
// one counts the attempt instead of making it (writesThroughClosedFile()).
#pragma once

#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace ag {

enum LogLevel {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE,
};

using LoggerCallback = std::function<void(LogLevel level, std::string_view formatted_message)>;

class Logger {
public:
    explicit Logger(std::string_view name) : m_name(name) {}

    // The real one formats with fmt; the mock core only ever logs ready text.
    // Like the real log_impl it takes its own copy of the callback first, so a
    // concurrent set_callback cannot pull it out from under the call.
    void log(LogLevel level, std::string_view message) const
    {
        const std::shared_ptr<LoggerCallback> callback = current();
        (*callback)(level, m_name + " " + std::string(message));
    }

    // Recorded so tests can assert that the Verbose-logs toggle actually reaches
    // the core: the wrapper reads `loglevel` back out of the config TOML and
    // pushes it here, and that readback silently regressed once already.
    static LogLevel &last_level()
    {
        static LogLevel level = LOG_LEVEL_INFO;
        return level;
    }
    static void set_log_level(LogLevel l) { last_level() = l; }

    // Empty restores the default, as in the real one.
    static void set_callback(LoggerCallback callback)
    {
        auto next = std::make_shared<LoggerCallback>(callback ? std::move(callback)
                                                              : LoggerCallback(LOG_TO_STDERR));
        std::lock_guard<std::mutex> lock(state().mutex);
        state().callback = std::move(next);
        ++state().callbackSets;
    }

    class LogToFile {
    public:
        explicit LogToFile(FILE *file) : m_file(file) {}

        void operator()(LogLevel level, std::string_view message)
        {
            if (m_file == stderr)
                noteStderrLine();
            else if (countIfClosed(m_file))
                return; // writing would be the use-after-free itself
            // Not flushed, like the real one: a file the core opened is fully
            // buffered, so a reader of the file sees nothing until the buffer
            // fills or the file is closed.
            std::fprintf(m_file, "%d %.*s\n", static_cast<int>(level),
                         static_cast<int>(message.size()), message.data());
        }

    private:
        FILE *m_file;
    };

    static inline const LoggerCallback LOG_TO_STDERR = LogToFile(stderr);

    // ---- mock-only bookkeeping, for the core's file handler and the tests ----
    static void noteFileOpened(FILE *file)
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        state().closed.erase(file); // the C library may hand out a freed FILE again
    }
    static void noteFileClosed(FILE *file)
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        state().closed.insert(file);
    }
    static int writesThroughClosedFile()
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        return state().closedWrites;
    }
    static int linesToStderr()
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        return state().stderrLines;
    }
    // How often anything has replaced the callback in this process, ever. Not
    // reset with the counters: it is what tells a test whether the process still
    // logs where the core does by default.
    static int callbackSets()
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        return state().callbackSets;
    }
    // The counters only. The callback and the closed files are process state
    // that outlives every client, which is the whole point of mirroring them.
    static void resetCounters()
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        state().closedWrites = 0;
        state().stderrLines = 0;
    }

private:
    struct State {
        std::mutex mutex;
        std::shared_ptr<LoggerCallback> callback = std::make_shared<LoggerCallback>(LOG_TO_STDERR);
        std::set<const FILE *> closed;
        int closedWrites = 0;
        int stderrLines = 0;
        int callbackSets = 0;
    };
    static State &state()
    {
        static State s;
        return s;
    }
    static std::shared_ptr<LoggerCallback> current()
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        return state().callback;
    }
    static bool countIfClosed(const FILE *file)
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        if (state().closed.count(file) == 0)
            return false;
        ++state().closedWrites;
        return true;
    }
    static void noteStderrLine()
    {
        std::lock_guard<std::mutex> lock(state().mutex);
        ++state().stderrLines;
    }

    std::string m_name;
};

} // namespace ag
