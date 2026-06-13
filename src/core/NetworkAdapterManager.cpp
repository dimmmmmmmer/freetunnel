#include "NetworkAdapterManager.h"

#include <QProcess>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

NetworkAdapterManager::NetworkAdapterManager(QObject *parent)
    : QObject(parent) {}

QStringList NetworkAdapterManager::knownConflictPatterns() {
    return {
        "Radmin VPN",
        "Hamachi",
        "TAP-Windows",
        "TAP-Win32",
        "OpenVPN",
        "WireGuard",
        "ZeroTier",
        "Hyper-V Virtual",
        "VMware",
        "VirtualBox Host-Only",
        "Wintun",
        "Cloudflare WARP",
        "NordLynx",
        "ProtonVPN TUN",
        "Tailscale",
    };
}

QVector<AdapterInfo> NetworkAdapterManager::scanAdapters() const {
    QVector<AdapterInfo> result;

#ifdef _WIN32
    // Use PowerShell to enumerate all network adapters with useful fields.
    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-Command",
        "Get-NetAdapter | Format-List -Property Name,InterfaceDescription,ifIndex,Status"});
    proc.waitForFinished(15000);

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList blocks = output.split("\r\n\r\n", Qt::SkipEmptyParts);

    const QStringList ourPatterns = {
        "FreeTunnel",
        "TrustTunnel",
    };
    const QStringList conflictPatterns = knownConflictPatterns();

    for (const QString &block : blocks) {
        AdapterInfo info;
        const QStringList lines = block.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const int colonPos = line.indexOf(':');
            if (colonPos < 0) continue;
            const QString key = line.left(colonPos).trimmed();
            const QString val = line.mid(colonPos + 1).trimmed();
            if (key == "Name") {
                info.name = val;
            } else if (key == "InterfaceDescription") {
                info.description = val;
            } else if (key == "ifIndex") {
                info.interfaceIndex = val;
            } else if (key == "Status") {
                info.enabled = (val.compare("Up", Qt::CaseInsensitive) == 0);
            }
        }
        if (info.name.isEmpty()) continue;

        // Determine ownership
        const QString combined = info.name + " " + info.description;
        for (const QString &ours : ourPatterns) {
            if (combined.contains(ours, Qt::CaseInsensitive)) {
                info.isOurs = true;
                break;
            }
        }

        // Only include adapters that match known VPN/tunnel patterns
        bool isConflict = false;
        for (const QString &pat : conflictPatterns) {
            if (combined.contains(pat, Qt::CaseInsensitive)) {
                isConflict = true;
                break;
            }
        }

        if (isConflict || info.isOurs) {
            result.append(info);
        }
    }

#elif defined(__APPLE__)
    // macOS: list interfaces, flag tap/tun/vmnet
    QProcess proc;
    proc.start("ifconfig", {"-l"});
    proc.waitForFinished(5000);
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList ifaces = output.trimmed().split(' ', Qt::SkipEmptyParts);
    for (const QString &iface : ifaces) {
        const QString lower = iface.toLower();
        if (lower.startsWith("tap") || lower.startsWith("tun")
            || lower.startsWith("feth") || lower.startsWith("vmnet")
            || lower.startsWith("utun")) {
            AdapterInfo info;
            info.name = iface;
            info.description = iface;
            info.enabled = true;
            // utun created by us? heuristic: leave as not ours
            result.append(info);
        }
    }
#else
    // Linux: ip link
    QProcess proc;
    proc.start("ip", {"-o", "link", "show"});
    proc.waitForFinished(5000);
    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // format: "2: eth0: <BROADCAST,...> ..."
        const int colonPos = line.indexOf(':');
        if (colonPos < 0) continue;
        const int secondColon = line.indexOf(':', colonPos + 1);
        if (secondColon < 0) continue;
        const QString name = line.mid(colonPos + 1, secondColon - colonPos - 1).trimmed();
        const QString lower = name.toLower();
        if (lower.startsWith("tun") || lower.startsWith("tap")
            || lower.startsWith("wg") || lower.startsWith("vmnet")) {
            AdapterInfo info;
            info.name = name;
            info.description = name;
            info.enabled = line.contains("UP", Qt::CaseInsensitive);
            result.append(info);
        }
    }
#endif

    return result;
}

bool NetworkAdapterManager::disableAdapter(const QString &adapterName) const {
#ifdef _WIN32
    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-Command",
        QString("Disable-NetAdapter -Name '%1' -Confirm:$false").arg(adapterName)});
    proc.waitForFinished(15000);
    return proc.exitCode() == 0;
#else
    Q_UNUSED(adapterName);
    return false;
#endif
}

bool NetworkAdapterManager::enableAdapter(const QString &adapterName) const {
#ifdef _WIN32
    QProcess proc;
    proc.start("powershell", {"-NoProfile", "-Command",
        QString("Enable-NetAdapter -Name '%1' -Confirm:$false").arg(adapterName)});
    proc.waitForFinished(15000);
    return proc.exitCode() == 0;
#else
    Q_UNUSED(adapterName);
    return false;
#endif
}
