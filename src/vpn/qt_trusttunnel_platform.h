#pragma once

#include "vpn/trusttunnel/client.h"
#include "vpn/vpn.h"

#include <QString>

ag::LogLevel qt_trusttunnel_parse_log_level(const QString &level);
void qt_trusttunnel_protect_outbound_socket(ag::SocketProtectEvent *event);
void qt_trusttunnel_verify_server_certificate(ag::VpnVerifyCertificateEvent *event);
QString qt_trusttunnel_connection_info_line(ag::VpnConnectionInfoEvent *event);

#ifdef _WIN32
bool qt_trusttunnel_is_process_elevated();
#endif
