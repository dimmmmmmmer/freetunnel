#pragma once

#include <QString>
#include <QStringList>
#include <QList>

struct ProcessInfo {
    QString pid;           // Process ID
    QString name;          // Process name (executable name)
    QString path;          // Full path to executable
    QString displayName;   // Display name for UI
};

class ProcessManager {
public:
    static QList<ProcessInfo> getRunningProcesses();

private:
#ifdef _WIN32
    static QList<ProcessInfo> getProcessesWindows();
#elif __APPLE__
    static QList<ProcessInfo> getProcessesMacOS();
#else
    static QList<ProcessInfo> getProcessesLinux();
#endif
};
