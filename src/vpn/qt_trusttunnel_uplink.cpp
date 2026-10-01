// cppcheck-suppress-file missingIncludeSystem
// Uplink follower for QtTrustTunnelClient: tells the core when the network
// adapter it must send through changes, on the one platform where nothing else
// does.
//
// The core binds every socket of its own — the connection to the server, DNS,
// whatever a rule sends around the tunnel — to one adapter, the "outbound
// interface", so that its own traffic cannot be routed into the tunnel it
// serves. On Linux and macOS its AutoNetworkMonitor watches the system and moves
// that binding when the default route moves. On Windows it does not: the
// network monitor it uses has no Windows implementation (native_libs_common
// 8.1.52, NetworkMonitorImpl::start() has only Apple and Linux branches), so the
// adapter picked when the session started was used for its whole life. Unplug
// the cable a session started on while Wi-Fi is up and the core kept trying the
// dead adapter until it ran out of recovery attempts, about a minute, and
// dropped the session. The core's API says what an embedder must do instead
// (vpn_network_manager_set_outbound_interface, network_manager.h): set the new
// adapter, THEN report the change. This is that.
#include "qt_trusttunnel_client.h"

#include "net/network_manager.h"

#ifdef Q_OS_WIN
#include "core/NetBind.h"
#endif

#include <QNetworkInterface>

#include <algorithm>

namespace {

// Windows only in the shipping app; elsewhere AutoNetworkMonitor already does
// this, and doing it twice would recover from every change twice. The test
// build can switch it on anywhere, so the state machine is tested on every
// platform the tests run on instead of only on the one with no developer
// machine behind it.
bool followsUplinkHere()
{
#ifdef FT_ENABLE_TEST_HOOKS
    if (qEnvironmentVariableIsSet("FT_TEST_FOLLOW_UPLINK"))
        return true;
#endif
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

// The adapter the core itself calls active — see captureWindowsPhysicalOutbound
// for why its choice and not ours. The test build reads the mock core's answer
// on every platform; the shipping app has it on Windows only.
uint32_t activeUplinkIndex()
{
#if defined(Q_OS_WIN) || defined(FT_ENABLE_TEST_HOOKS)
    return ag::vpn_win_detect_active_if();
#else
    return 0;
#endif
}

// Offline, whether an adapter the core could take has a default route again,
// asked without the core: its own look writes a warning to its log each time it
// finds none. Windows reads the routing table as the core does
// (windowsDefaultRouteInterface()), so the two agree. FT_TEST_DEFAULT_ROUTE (test
// hooks) scripts the answer; elsewhere, with nothing to read, the core is asked.
bool anUplinkMayBeBack()
{
#ifdef FT_ENABLE_TEST_HOOKS
    if (qEnvironmentVariableIsSet("FT_TEST_DEFAULT_ROUTE"))
        return qEnvironmentVariableIntValue("FT_TEST_DEFAULT_ROUTE") != 0;
#endif
#ifdef Q_OS_WIN
    return freetunnel::windowsDefaultRouteInterface() != 0;
#else
    return true;
#endif
}

// How many quiet looks offline before the core is asked all the same, in case
// the routing table and the core ever disagree: 30 s at the usual 2 s, so the
// core's warning comes at most twice a minute instead of with every look.
constexpr int kQuietOfflineLooks = 15;

QString describeUplink(uint32_t ifIndex)
{
    const QString name =
            QNetworkInterface::interfaceFromIndex(static_cast<int>(ifIndex)).humanReadableName();
    return name.isEmpty() ? QStringLiteral("interface %1").arg(ifIndex)
                          : QStringLiteral("%1 (interface %2)").arg(name).arg(ifIndex);
}

// How the core's set_system_dns() fails on Windows when it finds no adapter to
// read the DNS servers from (trusttunnel/src/client.cpp). Only its Windows
// branch returns this, so nowhere else is a DNS failure taken for being offline.
constexpr auto kNoActiveAdapter = "Couldn't detect active network interface";

// Online over a link the core does not count as an adapter. Its detection
// admits Ethernet, Wi-Fi and mobile broadband (interface types 6, 71, 243 and
// 244) and nothing else, so a computer that dials its own internet connection
// — PPPoE, or a modem that brings up a PPP link (type 23, Qt's Ppp) — is
// online and still has no active adapter as far as the core can tell. Qt
// cannot see routes, so this asks only for an up PPP link, a link that is
// itself the way out. An adapter of another kind the core skips, a VPN's or a
// virtual machine's, can be up with no network behind it, and taking that for
// being online would turn being offline back into a stop.
bool onlineOverPpp()
{
#ifdef FT_ENABLE_TEST_HOOKS
    if (qEnvironmentVariableIsSet("FT_TEST_PPP_LINK"))
        return true;
#endif
#ifdef Q_OS_WIN
    const QList<QNetworkInterface> all = QNetworkInterface::allInterfaces();
    return std::any_of(all.cbegin(), all.cend(), [](const QNetworkInterface &ni) {
        return ni.type() == QNetworkInterface::Ppp && ni.flags().testFlag(QNetworkInterface::IsUp)
                && ni.flags().testFlag(QNetworkInterface::IsRunning);
    });
#else
    return false;
#endif
}

} // namespace

// What a set_system_dns() that failed with this text means for the attempt.
// Decided from the failure itself rather than from a second look at the
// network: an adapter that turns up in between must not turn being offline
// into a stop.
QtTrustTunnelClient::ConnectAttempt::Outcome
QtTrustTunnelClient::dnsFailureOutcome(const QString &coreError, QString *error)
{
    if (!coreError.contains(QLatin1String(kNoActiveAdapter)))
        return ConnectAttempt::Outcome::FatalStop; // a broken setup, not the network
    // Online all the same, over a link the core cannot use. Retrying would
    // show "Reconnecting" for ever; it used to stop with the core's words,
    // which name no cause. Name it. (Constant false off Windows, which is all a
    // Linux-configured cppcheck sees.)
    // cppcheck-suppress knownConditionTrueFalse
    if (onlineOverPpp()) {
        *error = tr("Your internet connection is a PPP link (PPPoE or a modem), which the VPN "
                    "can't run over on Windows. Connect through a router, Ethernet or Wi-Fi "
                    "instead.");
        return ConnectAttempt::Outcome::FatalStop;
    }
    // Being offline. Stopping for good on it ended every session that had to
    // be rebuilt while the network was gone, and the kill switch's block with
    // it, so it is retried like a connect that cannot reach the server.
    return ConnectAttempt::Outcome::Retry;
}

void QtTrustTunnelClient::startFollowingUplink()
{
    // Constant off Windows outside the tests, which is all a Linux-configured
    // cppcheck sees.
    // cppcheck-suppress knownConditionTrueFalse
    if (followsUplinkHere())
        m_uplinkTimer.start(m_uplinkPollMs);
}

// A session starts out on whatever the core was pointed at when it was built.
void QtTrustTunnelClient::resetUplinkTracking()
{
    m_uplinkReported = ag::vpn_network_manager_get_outbound_interface();
    m_uplinkSeen = m_uplinkReported;
    m_quietOfflineLooks = 0;
}

void QtTrustTunnelClient::followUplink()
{
    if (!m_client)
        return;
    // Offline, every look still comes every 2 s, so that a network back after
    // wake or a Wi-Fi rejoin resumes the session within one. But the core is
    // asked only once the routing table has something it could take: asked
    // every time, it put a warning in the log every 2 s for as long as the
    // machine was offline. Looking every 10 s instead, as this did, kept the log
    // down and left the session down for up to 10 s after the network was back.
    // (anUplinkMayBeBack() is constant off Windows, which is all a
    // Linux-configured cppcheck sees.)
    // cppcheck-suppress knownConditionTrueFalse
    if (m_uplinkReported == 0 && !anUplinkMayBeBack()
        && ++m_quietOfflineLooks < kQuietOfflineLooks)
        return;
    m_quietOfflineLooks = 0;
    const uint32_t seen = activeUplinkIndex();
    // Leaving an adapter that works waits for a second look that agrees: moving
    // costs the session a reconnect, and an adapter caught between two states
    // (a route being replaced, a lease being renewed) must not cost it two.
    // Coming back from no network at all does not wait — an adapter with a
    // default route is not a guess, and the session is down until it is used.
    const bool settled = seen == m_uplinkSeen || m_uplinkReported == 0;
    m_uplinkSeen = seen;
    if (!settled || seen == m_uplinkReported)
        return;
    m_uplinkReported = seen;
    reportUplink(seen);
}

void QtTrustTunnelClient::reportUplink(uint32_t ifIndex)
{
    if (ifIndex == 0) {
        // The binding is left as it is, as the core's own monitor does: there
        // is nothing better to point it at, and the adapter may come back.
        emit connectionInfo(QStringLiteral("network: no adapter has a default route — waiting"));
        m_client->notify_network_change(ag::VPN_NS_NOT_CONNECTED);
        return;
    }
#ifdef Q_OS_WIN
    m_winPhysicalIfIndex.store(ifIndex); // what protectOutboundSocket re-pins
#endif
    ag::vpn_network_manager_set_outbound_interface(ifIndex);
    emit connectionInfo(QStringLiteral("network: now using %1").arg(describeUplink(ifIndex)));
    // What the core does not send through the tunnel it resolves with the
    // system DNS servers: bypass-rule domains, every unlisted domain in
    // selective mode, and all of DNS while the tunnel is down with the kill
    // switch off. It was handed those servers once, when the session was built,
    // read off the adapter active then — a home router's address, or an IPv6
    // one scoped to that adapter, which the new network cannot reach. Read
    // them again now, before the change notification restarts the core's DNS
    // with whatever it holds. This only stores them; if it fails, the old ones
    // stay, which is where things stood anyway.
    const auto dnsErr = m_client->set_system_dns();
    if (dnsErr)
        emit connectionInfo(QStringLiteral("network: could not read its DNS servers, keeping the "
                                           "previous ones: %1")
                                    .arg(QString::fromStdString(dnsErr->str())));
    m_client->notify_network_change(ag::VPN_NS_CONNECTED);
}
