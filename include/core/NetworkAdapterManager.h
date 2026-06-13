#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

/// Describes a network adapter discovered on the system.
struct AdapterInfo {
    QString name;            ///< Human-readable adapter name (e.g. "Radmin VPN Network Adapter")
    QString description;     ///< Driver description
    QString interfaceIndex;  ///< Interface index (Windows)
    bool enabled = true;     ///< Whether the adapter is currently enabled
    bool isOurs = false;     ///< true if adapter belongs to FreeTunnel / WinTUN we own
};

/// Manages discovery and deactivation of third-party virtual network adapters
/// that may conflict with the FreeTunnel WinTUN adapter (Windows-only logic,
/// stubs on other platforms).
class NetworkAdapterManager : public QObject {
    Q_OBJECT
public:
    explicit NetworkAdapterManager(QObject *parent = nullptr);

    /// Well-known adapter name substrings that are known to conflict.
    static QStringList knownConflictPatterns();

    /// Scan the system for WinTUN / TAP / third-party VPN adapters.
    /// Returns the full list; caller can filter by `isOurs`.
    QVector<AdapterInfo> scanAdapters() const;

    /// Disable (deactivate) a network adapter by its name.
    /// Returns true on success.  On non-Windows platforms returns false.
    bool disableAdapter(const QString &adapterName) const;

    /// Enable (reactivate) a network adapter by its name.
    bool enableAdapter(const QString &adapterName) const;

signals:
    void scanFinished(const QVector<AdapterInfo> &adapters);
    void adapterStateChanged(const QString &name, bool enabled);
};
