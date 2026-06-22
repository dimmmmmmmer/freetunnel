#pragma once

// Abstract VPN backend the Backend drives. Two implementations exist:
//   - VpnHelperClient: spawns the elevated privileged helper and runs the core
//     there (TUN, system-wide routing — needs root).
//   - SocksVpnClient:  runs the core in-process as a local SOCKS5 proxy, which
//     needs no elevation.
// The interface is deliberately core-free so Backend (and its unit tests) can
// compile without the VPN core present.

#include <QObject>
#include <QString>
#include <string>
#include <vector>

class IVpnClient : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
        Reconnecting,
        WaitingForNetwork,
        Disconnecting,
        Error,
    };
    Q_ENUM(State)

    using QObject::QObject;
    ~IVpnClient() override = default;

    virtual bool loadConfigFromFile(const QString &path) = 0;
    virtual void setExtraExclusions(const std::vector<std::string> &exclusions) = 0;
    virtual void setExcludedRoutes(const std::vector<std::string> &routes) = 0;
    virtual void setVpnMode(bool selective) = 0;
    virtual void setKillSwitch(bool enabled) = 0;
    virtual void connectVpn() = 0;
    virtual void disconnectVpn() = 0;
    virtual void shutdown() = 0;
    virtual State state() const = 0;

signals:
    void stateChanged(IVpnClient::State state);
    void tunnelStats(quint64 upload, quint64 download);
    void connectionInfo(const QString &msg);
    void vpnError(const QString &msg);
};
