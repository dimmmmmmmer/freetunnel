// cppcheck-suppress-file missingIncludeSystem
// Core callbacks for QtTrustTunnelClient: what the VPN core calls on its own
// threads, and the hop from there back onto this object's thread, taken under
// the liveness guard. That includes the per-application split-tunnelling
// decision the core asks for on every new connection. Split out of
// qt_trusttunnel_client.cpp, which had grown past the 500-line limit again.
#include "qt_trusttunnel_client.h"
#include "qt_trusttunnel_platform.h"
#include "qt_trusttunnel_events.h"
#include "core/AppRules.h"
#include "core/ProcessLookup.h"

#include <QMetaObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <mutex>

void QtTrustTunnelClient::postCoreStateChanged(quint64 session, int coreState, int errCode,
                                               const QString &errText)
{
    QMetaObject::invokeMethod(
            this,
            [this, session, coreState, errCode, errText]() {
                if (session != m_sessionGen)
                    return;
                handleCoreStateChanged(static_cast<ag::VpnSessionState>(coreState), errCode,
                                       errText);
            },
            Qt::QueuedConnection);
}

void QtTrustTunnelClient::postTunnelStats(quint64 session, quint64 up, quint64 down)
{
    QMetaObject::invokeMethod(
            this,
            [this, session, up, down]() {
                if (session == m_sessionGen)
                    emit tunnelStats(up, down);
            },
            Qt::QueuedConnection);
}

void QtTrustTunnelClient::postConnectionInfo(quint64 session, const QString &line)
{
    QMetaObject::invokeMethod(
            this,
            [this, session, line]() {
                if (session == m_sessionGen)
                    emit connectionInfo(line);
            },
            Qt::QueuedConnection);
}

namespace {

// One line, in the order someone diagnosing reads it: did it run, as whom, how
// much did it see, and how long did it take.
QString describeScan(const freetunnel::ProcessLookup &lookup)
{
    const freetunnel::ProcessLookup::ScanReport &r = lookup.lastScan();
#ifdef Q_OS_LINUX
    // Only Linux has two ways of reading the socket tables, and which one
    // answered is the difference between a walk of about one millisecond and
    // one of nearly three — worth seeing in a report before anyone concludes
    // the machine is slow.
    const QString source = r.netlink ? QStringLiteral(", netlink") : QStringLiteral(", /proc/net");
#else
    const QString source;
#endif
    return QStringLiteral("app rules: walk %1 %2 — euid %3, pids %4 (%5 watched, %6 without a "
                          "program, %7 refused), sockets %8, entries %9, distinct %10, "
                          "errno %11, %12 ms%13")
            .arg(lookup.walksTaken())
            .arg(r.ok ? QStringLiteral("ok") : QStringLiteral("FAILED"))
            .arg(r.euid)
            .arg(r.pidsScanned)
            .arg(r.pidsWatched)
            .arg(r.pidsWithoutProgram)
            .arg(r.pidsSkipped)
            .arg(r.socketsSeen)
            .arg(r.entries)
            .arg(r.distinctPids)
            .arg(r.lastErrno)
            .arg(r.elapsedUs / 1000.0, 0, 'f', 2)
            .arg(source);
}

// The core's own vocabulary for what the rules decided.
ag::VpnConnectAction coreAction(freetunnel::AppAction action)
{
    switch (action) {
    case freetunnel::AppAction::ForceBypass:
        return ag::VPN_CA_FORCE_BYPASS;
    case freetunnel::AppAction::ForceTunnel:
        return ag::VPN_CA_FORCE_REDIRECT;
    case freetunnel::AppAction::Default:
        break;
    }
    return ag::VPN_CA_DEFAULT;
}

// Said once per session, in the app's own log, because the helper's stderr goes
// where nobody reporting a problem will ever look: a root-owned temp file on
// macOS, the null device on Linux, no console at all on Windows.
//
// Without this the feature is unobservable: a rule that never matched and a rule
// that matched and was overruled look identical from outside, and the first
// question anyone asks — "did it even see my program?" — has no answer.
QString scanReportLine(const freetunnel::ProcessLookup &lookup, const QStringList &rules)
{
    QString line = describeScan(lookup);
    // When the walk saw the machine and none of it matched, the rules themselves
    // are the next thing anyone would ask for, and asking costs a round trip
    // through whoever is reporting the problem. This is what a real one turned
    // on: a rule naming the file a menu entry points at, which is a launcher
    // that execs something else and so is never a running program.
    //
    // Only where the walk decides which processes to open, which is Linux: the
    // other two read every process and leave pidsWatched at zero whatever the
    // rules say, so the same test there would print this on every session
    // including the ones that work.
#ifdef Q_OS_LINUX
    const freetunnel::ProcessLookup::ScanReport &r = lookup.lastScan();
    if (r.ok && r.pidsWatched == 0 && r.pidsScanned > 0) {
        line += QStringLiteral("\n  no running program matches: %1")
                        .arg(rules.join(QStringLiteral(", ")));
    }
#else
    Q_UNUSED(rules)
#endif
    return line;
}

// Where a flow came from, for the one line a person reads to find out what
// happened to it.
QString sourceEndpoint(const ag::VpnConnectRequestSnapshot &req)
{
    const QString src = QString::fromStdString(req.src_ip);
    if (src.isEmpty())
        return QStringLiteral("port %1").arg(req.src_port);
    if (src.contains(QLatin1Char(':'))) // an IPv6 address needs brackets to be read
        return QStringLiteral("[%1]:%2").arg(src).arg(req.src_port);
    return QStringLiteral("%1:%2").arg(src).arg(req.src_port);
}

// What was decided about one connection.
//
// An unnamed flow means one of two things, and the source endpoint is what tells
// them apart in a report: either no rule names the program — the walk
// deliberately never opened it, which is the ordinary case and the reason this
// line only appears in verbose mode — or a rule does name it and the walk could
// not see it, which the scan line reports as refusals.
QString decisionLine(const ag::VpnConnectRequestSnapshot &req, const freetunnel::AppIdentity &app,
                     ag::VpnConnectAction action)
{
    const QString who = app.name.isEmpty()
            ? QStringLiteral("unknown (%1)").arg(sourceEndpoint(req))
            : app.name;
    const QString what = action == ag::VPN_CA_FORCE_BYPASS ? QStringLiteral("bypass")
            : action == ag::VPN_CA_FORCE_REDIRECT          ? QStringLiteral("tunnel")
                                                           : QStringLiteral("no rule");
    return QStringLiteral("app %1 → %2").arg(who, what);
}

} // namespace

// Split out of makeCallbacks, which had grown to 145 lines around it. The seam
// is the one the code already had: this is a self-contained lambda with its own
// captures and no reference to anything else being built there.
std::function<void(const ag::VpnConnectRequestSnapshot &, ag::VpnConnectDecision *)>
QtTrustTunnelClient::makeConnectRequestHandler(const GuardPtr &guard, quint64 session) {
    // Per-application split tunnelling. This runs on the wrapper's own loop, one
    // connection at a time, which is what lets it read the system's socket
    // tables without stalling traffic. The rules come from a shared snapshot so
    // the lookup needs no lock on this object; `this` is touched only at the
    // end, to log, and only under the liveness guard.
    auto lookup = std::make_shared<freetunnel::ProcessLookup>();
    auto scanWarned = std::make_shared<bool>(false);
    auto skipWarned = std::make_shared<bool>(false);
    auto appRules = m_appRules;
    return [this, guard, session, appRules, lookup, scanWarned,
            skipWarned](const ag::VpnConnectRequestSnapshot &req,
                        ag::VpnConnectDecision *decision) {
        if (decision == nullptr)
            return;
        QStringList rules;
        bool selective = false;
        bool verbose = false;
        {
            std::lock_guard<std::mutex> lk(appRules->mutex);
            rules = appRules->rules;
            selective = appRules->selective;
            verbose = appRules->verbose;
        }
        // No rules means the feature is off, and off must cost nothing: no
        // table walk, and the same VPN_CA_DEFAULT the wrapper answered before
        // any of this existed.
        if (rules.isEmpty())
            return;

        // Which programs the walk needs to look at. Pushed on every connection
        // rather than wired to a change notification: comparing the list is
        // cheaper than the notification would be to get right, and a rule the
        // user has just added takes effect on their next connection instead of
        // on their next session.
        lookup->setWatchList(rules);

        const freetunnel::LocalFlow flow{req.family, req.proto, req.src_port,
                                         QString::fromStdString(req.src_ip)};
        bool lookWasSkipped = false;
        const freetunnel::AppIdentity app = lookup->resolve(flow, &lookWasSkipped);
        freetunnel::AppAction act = freetunnel::appActionFor(app, rules, selective);
        // A connection we could not afford to look at is not a connection we
        // established nothing about — it is one we know nothing about, and the
        // two must not be answered the same way.
        //
        // In "Through VPN" the core's default is to leave the tunnel, so
        // answering "no rule" for an unexamined connection puts it on the open
        // network; if it did belong to a listed program, that is the exact leak
        // this feature exists to prevent. Keeping it in the tunnel is the wrong
        // answer only for a program nobody listed, and being wrong in that
        // direction costs bandwidth rather than privacy. It is also what the
        // rest of this client already does when it cannot tell: an empty rule
        // set falls back to the full tunnel for the same reason.
        //
        // Only in selective mode. In bypass mode the default already keeps the
        // connection inside the tunnel, so there is nothing to correct.
        if (lookWasSkipped && selective && act == freetunnel::AppAction::Default)
            act = freetunnel::AppAction::ForceTunnel;
        decision->action = coreAction(act);
        // decision->app_name is deliberately NOT set. It looks like a harmless way
        // to get the program into the core's own log, and it is not: the core
        // passes it to the upstream, which puts it in the CONNECT request sent
        // to the VPN endpoint (upstream open_connection -> send_connect_request
        // -> make_http_connect_request). That would tell the operator which
        // application opened every connection — a thing this app exists to avoid
        // telling anyone. The line below puts it in the local log instead, which
        // is where the user was going to look anyway.

        // Once per session, and only when it happens. Without this the budget
        // running out is invisible: the rules keep being listed, the walk keeps
        // reporting success, and the only symptom is that the feature works for
        // the first few dozen connections of a burst and then appears not to.
        if (lookWasSkipped && !*skipWarned) {
            *skipWarned = true;
            const QString line =
                    QStringLiteral("app rules: no budget left to look again — connections are "
                                   "being answered without one%1")
                            .arg(selective ? QStringLiteral(", and kept in the tunnel")
                                           : QString());
            std::lock_guard<std::mutex> lk(guard->mutex);
            if (guard->alive)
                postConnectionInfo(session, line);
        }

        if (!*scanWarned) {
            *scanWarned = true;
            const QString line = scanReportLine(*lookup, rules);
            std::lock_guard<std::mutex> lk(guard->mutex);
            if (guard->alive)
                postConnectionInfo(session, line);
        }

        const bool routed = decision->action == ag::VPN_CA_FORCE_BYPASS
                || decision->action == ag::VPN_CA_FORCE_REDIRECT;
        // A connection nobody wrote a rule for is the overwhelming majority, and
        // logging those buries the handful that matter under every program on
        // the machine. Verbose is where that question gets answered.
        if (!routed && !verbose)
            return;
        const QString line = decisionLine(req, app, decision->action);
        // The lookup above may be slow; the guard is taken only now, and only
        // to reach back into an object that may have been destroyed meanwhile.
        std::lock_guard<std::mutex> lk(guard->mutex);
        if (guard->alive)
            postConnectionInfo(session, line);
    };
}

ag::VpnCallbacks QtTrustTunnelClient::makeCallbacks(const GuardPtr &guard) {
    // Core callbacks are queued to our event loop, so events from a client that
    // has since been torn down (config switch: disconnect + connect a new one)
    // can arrive after the next session already started. A stale DISCONNECTED
    // would then trigger a bogus reconnect of the NEW session. Tag every event
    // with the session generation it belongs to and drop mismatches on arrival.
    //
    // An ABANDONED client outlives this object entirely, so the hop back to it
    // is taken under the guard: while the mutex is held `alive` cannot flip, and
    // once the destructor has cleared it no callback dereferences `this` again.
    // Note the payload is extracted BEFORE the lock — that touches the event,
    // not this object, and must not happen while holding it.
    const quint64 session = ++m_sessionGen;
    ag::VpnCallbacks callbacks;
    callbacks.verify_handler = qt_trusttunnel_verify_server_certificate;
    callbacks.protect_handler = [this, guard](ag::SocketProtectEvent *event) {
        std::lock_guard<std::mutex> lk(guard->mutex);
        if (guard->alive)
            protectOutboundSocket(event);
    };
    callbacks.state_changed_handler = [this, guard, session](ag::VpnStateChangedEvent *event) {
        const StateChangedPayload payload = extractStateChangedPayload(event);
        std::lock_guard<std::mutex> lk(guard->mutex);
        if (guard->alive)
            postCoreStateChanged(session, static_cast<int>(payload.state), payload.errCode,
                                 payload.errText);
    };
    callbacks.tunnel_stats_handler = [this, guard,
                                      session](ag::VpnTunnelConnectionStatsEvent *event) {
        if (!event)
            return;
        const quint64 up = event->upload;
        const quint64 down = event->download;
        std::lock_guard<std::mutex> lk(guard->mutex);
        if (guard->alive)
            postTunnelStats(session, up, down);
    };
    callbacks.connect_request_handler = makeConnectRequestHandler(guard, session);
    callbacks.connection_info_handler = [this, guard, session](ag::VpnConnectionInfoEvent *event) {
        const QString line = qt_trusttunnel_connection_info_line(event);
        std::lock_guard<std::mutex> lk(guard->mutex);
        if (guard->alive)
            postConnectionInfo(session, line);
    };
    return callbacks;
}


void QtTrustTunnelClient::protectOutboundSocket(ag::SocketProtectEvent *event)
{
#ifdef Q_OS_WIN
    // Pin until the index read is still the index: the uplink follower can move
    // it between our read and our write, and writing the old one after it
    // would put the core back on the adapter it just left. The follower stores
    // before it sets, so whichever of us writes last writes the newest index.
    uint32_t ifIndex = 0;
    do {
        ifIndex = m_winPhysicalIfIndex.load();
        pinWindowsPhysicalOutbound(ifIndex);
    } while (ifIndex != m_winPhysicalIfIndex.load());
#endif
    qt_trusttunnel_protect_outbound_socket(event);
}
