// cppcheck-suppress-file missingIncludeSystem
#pragma once

// Bind outbound sockets to the physical (non-VPN) interface so they take the
// original network path even while a full-tunnel VPN is up. Used for the config
// pinger, which should measure real reachability rather than latency through an
// existing tunnel.
//
// Reliable bypass needs per-socket interface binding (a source-address bind is
// not enough: a full tunnel routes by destination): IP_BOUND_IF on macOS,
// IP_UNICAST_IF on Windows, and a best-effort source-bind on Linux (where
// SO_BINDTODEVICE needs root the GUI doesn't have).

#include <QAbstractSocket>
#include <QHostAddress>

class QObject;
class QTcpSocket;

namespace freetunnel {

// The physical interface to send through. index <= 0 means none was found.
struct PhysicalRoute {
    int index = 0;
    QHostAddress v4;
    QHostAddress v6;
};

PhysicalRoute physicalOutboundRoute();

// Bind an existing socket to the physical interface for the given protocol.
// No-op when no physical interface is available or binding isn't supported.
// Works on any QTcpSocket subclass (e.g. QSslSocket) via virtual dispatch.
void bindSocketToPhysicalRoute(QTcpSocket *sock,
                               QAbstractSocket::NetworkLayerProtocol proto);

// A TCP socket bound to the physical interface for the given protocol. Falls
// back to an ordinary (unbound) socket when no physical interface is available
// or binding isn't supported. Caller takes ownership (parented to `parent`).
QTcpSocket *makePhysicalBoundTcpSocket(QObject *parent,
                                       QAbstractSocket::NetworkLayerProtocol proto);

} // namespace freetunnel
