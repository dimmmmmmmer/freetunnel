#include "ProcessManager.h"
#include <QFileInfo>
#include <QSet>
#include <algorithm>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/sysctl.h>
#include <unistd.h>
#endif

QList<ProcessInfo> ProcessManager::getRunningProcesses() {
#ifdef _WIN32
    return getProcessesWindows();
#elif __APPLE__
    return getProcessesMacOS();
#else
    return getProcessesLinux();
#endif
}

#ifdef __APPLE__
QList<ProcessInfo> ProcessManager::getProcessesMacOS() {
    QList<ProcessInfo> processes;
    QSet<QString> seenPaths;

    int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
    struct kinfo_proc *procs = nullptr;
    size_t length = 0;

    if (sysctl(mib, 3, nullptr, &length, nullptr, 0) < 0) {
        return processes;
    }

    procs = (struct kinfo_proc *)malloc(length);
    if (procs == nullptr) {
        return processes;
    }

    if (sysctl(mib, 3, procs, &length, nullptr, 0) < 0) {
        free(procs);
        return processes;
    }

    // Системные процессы для фильтрации
    static const QStringList systemProcesses = {
        "kernel_task", "loginwindow", "Finder", "Dock", "SystemUIServer",
        "Spotlight", "WindowServer", "fontd", "diskarbitrationd", "launchd",
        "syslogd", "configd", "mDNSResponder", "nsurlsessiond", "cloudphotod",
        "Bluetooth", "secd", "sharingd", "nsurlproxyd", "trustd", "logd",
        "locationd", "CalendarAgent", "NotificationCenter", "CoreServicesUIAgent"
    };

    size_t count = length / sizeof(struct kinfo_proc);
    for (size_t i = 0; i < count; i++) {
        struct kinfo_proc *proc = &procs[i];
        if (proc->kp_proc.p_pid == 0) continue;

        QString procName = QString::fromUtf8(proc->kp_proc.p_comm);

        // Пропускаем системные процессы
        if (systemProcesses.contains(procName)) continue;

        // Пропускаем скрытые процессы (начинающиеся с .)
        if (procName.startsWith('.')) continue;

        char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
        int ret = proc_pidpath(proc->kp_proc.p_pid, pathbuf, sizeof(pathbuf));
        if (ret <= 0) continue;

        QString path = QString::fromUtf8(pathbuf);

        // Дедублирование - берём первый экземпляр процесса
        if (seenPaths.contains(path)) continue;
        seenPaths.insert(path);

        ProcessInfo info;
        info.pid = QString::number(proc->kp_proc.p_pid);
        info.path = path;
        info.name = procName;

        QFileInfo fi(info.path);
        info.displayName = fi.baseName();
        if (info.displayName.isEmpty()) {
            info.displayName = info.name;
        }

        processes.append(info);
    }

    free(procs);

    // Сортируем по имени
    std::sort(processes.begin(), processes.end(), [](const ProcessInfo &a, const ProcessInfo &b) {
        return a.displayName.toLower() < b.displayName.toLower();
    });

    return processes;
}
#endif

#ifdef _WIN32
QList<ProcessInfo> ProcessManager::getProcessesWindows() {
    QList<ProcessInfo> processes;
    return processes;
}
#endif

#ifdef __linux__
QList<ProcessInfo> ProcessManager::getProcessesLinux() {
    QList<ProcessInfo> processes;
    return processes;
}
#endif
