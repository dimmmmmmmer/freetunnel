#include "vpn/socks_vpn_client.h"

#include "vpn/qt_trusttunnel_client.h"

// QtTrustTunnelClient::State and IVpnClient::State share the same enumerators in
// the same order; map explicitly so a future divergence is caught here.
static IVpnClient::State mapState(QtTrustTunnelClient::State s) {
    using In = QtTrustTunnelClient::State;
    using Out = IVpnClient::State;
    switch (s) {
    case In::Disconnected:      return Out::Disconnected;
    case In::Connecting:        return Out::Connecting;
    case In::Connected:         return Out::Connected;
    case In::Reconnecting:      return Out::Reconnecting;
    case In::WaitingForNetwork: return Out::WaitingForNetwork;
    case In::Disconnecting:     return Out::Disconnecting;
    case In::Error:             return Out::Error;
    }
    return Out::Disconnected;
}

SocksVpnClient::SocksVpnClient(QObject *parent) : IVpnClient(parent) {
    m_inner = new QtTrustTunnelClient(this);
    connect(m_inner, &QtTrustTunnelClient::stateChanged, this,
            [this](QtTrustTunnelClient::State s) {
                m_state = mapState(s);
                emit stateChanged(m_state);
            });
    connect(m_inner, &QtTrustTunnelClient::tunnelStats, this,
            [this](quint64 up, quint64 down) { emit tunnelStats(up, down); });
    connect(m_inner, &QtTrustTunnelClient::connectionInfo, this,
            [this](const QString &m) { emit connectionInfo(m); });
    connect(m_inner, &QtTrustTunnelClient::vpnError, this,
            [this](const QString &m) { emit vpnError(m); });
}

SocksVpnClient::~SocksVpnClient() = default;

bool SocksVpnClient::loadConfigFromFile(const QString &path) {
    return m_inner->loadConfigFromFile(path);
}

void SocksVpnClient::setExtraExclusions(const std::vector<std::string> &exclusions) {
    m_inner->setExtraExclusions(exclusions);
}

void SocksVpnClient::setExcludedRoutes(const std::vector<std::string> &routes) {
    // Routes are a TUN-listener concept; harmless for SOCKS (the core ignores
    // them), but forwarded for parity so behaviour matches the helper backend.
    m_inner->setRoutingRules({}, routes);
}

void SocksVpnClient::setVpnMode(bool selective) {
    m_inner->setVpnMode(selective);
}

void SocksVpnClient::setKillSwitch(bool enabled) {
    m_inner->setKillSwitch(enabled);
}

void SocksVpnClient::connectVpn() {
    m_inner->connectVpn();
}

void SocksVpnClient::disconnectVpn() {
    m_inner->disconnectVpn();
}

void SocksVpnClient::shutdown() {
    m_inner->disconnectVpn();
}
