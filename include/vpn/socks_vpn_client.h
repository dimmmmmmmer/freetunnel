#pragma once

// In-process VPN backend that runs the core as a local SOCKS5 proxy. Unlike the
// TUN backend it creates no network interface and changes no system routing, so
// it needs no elevation — the core just binds the configured loopback port.
//
// This header stays core-free (the core wrapper is only forward-declared), so it
// can be included from Backend without pulling the VPN core into that TU. The
// implementation (socks_vpn_client.cpp) is the only place that touches the core
// and is compiled into the app target only (where FT_HAVE_INPROCESS_VPN is set).

#include "vpn/vpn_client.h"

class QtTrustTunnelClient;

class SocksVpnClient : public IVpnClient {
    Q_OBJECT
public:
    explicit SocksVpnClient(QObject *parent = nullptr);
    ~SocksVpnClient() override;

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
    QtTrustTunnelClient *m_inner = nullptr;
    State m_state = State::Disconnected;
};
