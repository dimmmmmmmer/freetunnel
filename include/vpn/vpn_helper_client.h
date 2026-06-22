#pragma once

// GUI-side proxy for the VPN. Spawns the privileged helper (the same binary run
// with --helper, elevated once per session) and drives it over loopback TCP.
// Exposes the same signal/slot surface the Backend expects, so the GUI never
// runs the VPN core (or root) in-process.

#include <QByteArray>
#include <QString>
#include <string>
#include <vector>

#include "vpn/vpn_client.h"

class QTcpSocket;
class QProcess;
class QTimer;
class QJsonObject;

class VpnHelperClient : public IVpnClient {
    Q_OBJECT
public:
    explicit VpnHelperClient(QObject *parent = nullptr);
    ~VpnHelperClient() override;

    bool loadConfigFromFile(const QString &path) override;
    void setExtraExclusions(const std::vector<std::string> &exclusions) override;
    void setExcludedRoutes(const std::vector<std::string> &routes) override;
    void setVpnMode(bool selective) override;
    void setKillSwitch(bool enabled) override;

    void connectVpn() override;
    void disconnectVpn() override;
    void shutdown() override;
    State state() const override { return m_state; }

private:
    bool ensureHelper();
    bool spawnElevatedHelper(quint16 port, const QString &tokenPath, QString *err);
    void abortStartup();
    void clearTokenFile();
    void send(const QJsonObject &obj);
    void onReadyRead();
    void onSocketConnected();
    void handleEvent(const QJsonObject &ev);
    void setState(State s);
    void fail(const QString &msg);

    QProcess *m_proc = nullptr;
    QTcpSocket *m_sock = nullptr;
    quint16 m_tcpPort = 0;
    QString m_token;
    QString m_tokenPath;
    QString m_configPath;
    std::vector<std::string> m_exclusions;
    std::vector<std::string> m_excludedRoutes;
    bool m_selective = false;
    bool m_killSwitch = false;
    State m_state = State::Disconnected;
    bool m_helloAcked = false;
    bool m_connectPending = false;
    bool m_starting = false;
    QTimer *m_attempt = nullptr;
    int m_tries = 0;
    QByteArray m_buf;
};
