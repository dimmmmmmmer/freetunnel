#pragma once

// Bridge object exposed to QML. Wraps the VPN client + config/settings storage
// so the QML UI never touches the C++ core directly. Phase 1 wires the home
// screen (connect/disconnect, state, live speeds, active config); other screens
// are wired incrementally.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "vpn/qt_trusttunnel_client.h"

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString sessionTime READ sessionTime NOTIFY tick)
    Q_PROPERTY(QString downSpeed READ downSpeed NOTIFY tick)
    Q_PROPERTY(QString upSpeed READ upSpeed NOTIFY tick)
    Q_PROPERTY(QString activeConfig READ activeConfig NOTIFY configChanged)
    Q_PROPERTY(QStringList configs READ configs NOTIFY configsChanged)

public:
    explicit Backend(QObject *parent = nullptr);

    bool connected() const { return m_connected; }
    QString statusText() const;
    QString sessionTime() const;
    QString downSpeed() const;
    QString upSpeed() const;
    QString activeConfig() const;
    QStringList configs() const { return m_names; }

    Q_INVOKABLE void toggle();
    Q_INVOKABLE void connectVpn();
    Q_INVOKABLE void disconnectVpn();
    Q_INVOKABLE void selectConfig(int index);
    Q_INVOKABLE bool importDeepLink(const QString &link);

signals:
    void stateChanged();
    void tick();
    void configChanged();
    void configsChanged();
    void errorOccurred(const QString &msg);

private:
    void reloadConfigs();
    bool ensureElevated(); // returns false (and relaunches) if elevation needed
    QString nameForPath(const QString &path) const;

    QtTrustTunnelClient m_client;
    QStringList m_paths;       // config file paths
    QStringList m_names;       // display names, parallel to m_paths
    QString m_activePath;
    bool m_connected = false;

    QElapsedTimer m_session;
    QTimer m_ticker;
    quint64 m_accUp = 0, m_accDown = 0; // bytes accumulated since last tick
    double m_upRate = 0, m_downRate = 0; // bytes/sec
};
