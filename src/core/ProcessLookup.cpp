// cppcheck-suppress-file missingIncludeSystem
#include "core/ProcessLookup.h"

#include <QDir>
#include <QSet>

#include <algorithm>
#include <QtGlobal>

#include <cerrno>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>

#if defined(Q_OS_WIN)
// <windows.h> defines min and max as function-like macros, so every later
// std::max(a, b) in this file becomes std::(a, b): an error reported at the use
// site with no mention of the cause, on the one platform that is not the
// development machine. NOMINMAX is the documented way to decline them, and it
// is set here rather than in the build files because the file is compiled by
// two of those and only one of them would be remembered.
#define NOMINMAX
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <psapi.h>
// clang-format on
#elif defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <unistd.h>
#else
// The Linux branch. It already names /proc paths, so it is not portable to any
// other Unix, and the kernel headers below do not make it less so.
// clang-format off
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/sock_diag.h>
// clang-format on
#endif

namespace freetunnel {

namespace {

// ntohs is called unqualified on purpose: on Darwin it is a macro
// (__DARWIN_OSSwapInt16), and ::ntohs does not parse there.
//
// The family is part of the key, not a detail. IPv4 and IPv6 carry separate port
// spaces on every platform here, so one number can be two different sockets held
// by two different programs at the same moment — and without the family in the
// key the second one is either invisible or answers for the first. Both failures
// end the same way: a connection routed by a rule written about another program.
constexpr std::uint32_t ownerKey(int family, int proto, std::uint16_t port)
{
    return (static_cast<std::uint32_t>(family & 0xFF) << 24)
            | (static_cast<std::uint32_t>(proto & 0xFF) << 16) | port;
}

// A socket's owner is kept unless the new one is a real owner and the kept one
// is not. See SocketOwnerTable::record.
template <typename KeyT>
void keepFirstOwner(QHash<KeyT, qint64> *owners, const KeyT &key, qint64 pid)
{
    const auto it = owners->find(key);
    if (it == owners->end())
        owners->insert(key, pid);
    else if (it.value() == kUnattributed && pid != kUnattributed)
        it.value() = pid;
}

SocketAddress ipv4Address(quint32 hostOrder)
{
    SocketAddress out{};
    out[0] = static_cast<std::uint8_t>(hostOrder >> 24);
    out[1] = static_cast<std::uint8_t>(hostOrder >> 16);
    out[2] = static_cast<std::uint8_t>(hostOrder >> 8);
    out[3] = static_cast<std::uint8_t>(hostOrder);
    return out;
}

} // namespace

// How an IPv6 socket spells an IPv4 address: ::ffff:a.b.c.d.
SocketAddress v4MappedAddress(quint32 hostOrder)
{
    SocketAddress out{};
    out[10] = 0xff;
    out[11] = 0xff;
    const SocketAddress v4 = ipv4Address(hostOrder);
    std::copy_n(v4.begin(), 4, out.begin() + 12);
    return out;
}

namespace {

// How much of this machine walking the process table may have. A quarter of
// real time, saved up to a hundred and fifty milliseconds' worth.
//
// The obvious rule — "wait four times what the last walk cost before walking
// again" — is wrong in precisely the case it exists for. Connections arrive in
// bursts: opening one page makes twenty of them in a few milliseconds, each one
// a new socket, each one needing a look that the previous connection's walk was
// too early to have taken. A gap rule refuses nineteen of those twenty. Credit
// earned while the machine was idle, which is nearly all the time, is what lets
// the burst be answered — and a program that opens connections without pause
// still cannot take more than its quarter.
constexpr qint64 kLookDutyDivisor = 4;
// Enough saved up to answer a page's worth of connections back to back, on a
// machine where a walk is slow. Anything larger buys nothing: the connections
// are set up one after another, so the wait a person would notice is the walks
// themselves, not the permission to make them.
constexpr qint64 kLookCreditCapUs = 150000;

// Test-only, and false in release builds, where the hook is compiled out. No
// machine the tests run on has a walk that fails, and refreshIfStale() has to be
// shown one that fails every time: this ends each walk the way a Windows walk
// with a table it could not read ends, with what it saw and a failure.
bool walkFailsForTests()
{
#ifdef FT_ENABLE_TEST_HOOKS
    return qEnvironmentVariableIsSet("FT_TEST_PROCESS_WALK_FAILS");
#else
    return false;
#endif
}

// Test-only as well, and 0 in release builds: microseconds added to what a walk
// cost, so that a test can have one walk spend more than the whole credit, as a
// walk on a slow machine can, without a slow machine.
qint64 extraWalkCostForTests()
{
#ifdef FT_ENABLE_TEST_HOOKS
    return qEnvironmentVariableIntValue("FT_TEST_PROCESS_WALK_COST_US");
#else
    return 0;
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// SocketOwnerTable
// ---------------------------------------------------------------------------

void SocketOwnerTable::record(int family, int proto, std::uint16_t port, const SocketAddress &address,
                              qint64 pid, bool v6only)
{
    const std::uint32_t endpoint = ownerKey(family, proto, port);
    keepFirstOwner(&m_sockets, Key{endpoint, address}, pid);
    keepFirstOwner(&m_ports, endpoint, pid);
    if (v6only && family == AF_INET6 && address == SocketAddress{})
        m_ipv6OnlyWildcards.insert(endpoint);
}

void SocketOwnerTable::clear()
{
    m_sockets.clear();
    m_ports.clear();
    m_ipv6OnlyWildcards.clear();
}

int SocketOwnerTable::distinctPids() const
{
    QSet<qint64> pids;
    for (const qint64 pid : m_sockets) {
        if (pid != kUnattributed)
            pids.insert(pid);
    }
    return static_cast<int>(pids.size());
}

SocketOwnerTable::Match SocketOwnerTable::find(const LocalFlow &flow) const
{
    const QHostAddress address(flow.ip);
    if (address.isNull()) {
        const int other = flow.family == AF_INET6 ? AF_INET : AF_INET6;
        for (const int family : {flow.family, other}) {
            const auto it = m_ports.constFind(ownerKey(family, flow.proto, flow.port));
            if (it != m_ports.constEnd())
                return {true, it.value(), false};
        }
        return {};
    }

    // The address in both spellings it can be recorded under.
    bool isV4 = false;
    const quint32 v4 = address.toIPv4Address(&isV4); // also true for ::ffff:a.b.c.d
    SocketAddress v6{};
    if (!isV4 || address.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR raw = address.toIPv6Address();
        std::copy_n(raw.c, 16, v6.begin());
    }
    const SocketAddress any{};
    QList<Key> candidates;
    if (isV4) {
        const std::uint32_t six = ownerKey(AF_INET6, flow.proto, flow.port);
        candidates = {{ownerKey(AF_INET, flow.proto, flow.port), ipv4Address(v4)},
                      {ownerKey(AF_INET, flow.proto, flow.port), any},
                      {six, v4MappedAddress(v4)},
                      // An IPv6 socket bound to the IPv4 wildcard, as .NET's dual-mode
                      // sockets are when bound to IPAddress.Any.
                      {six, v4MappedAddress(0)}};
        if (!m_ipv6OnlyWildcards.contains(six))
            candidates.append({six, any});
    } else {
        candidates = {{ownerKey(AF_INET6, flow.proto, flow.port), v6},
                      {ownerKey(AF_INET6, flow.proto, flow.port), any}};
    }
    for (const Key &candidate : candidates) {
        const auto it = m_sockets.constFind(candidate);
        if (it != m_sockets.constEnd())
            return {true, it.value(), true};
    }
    return {};
}

// ---------------------------------------------------------------------------
// /proc/net table parsing. Pure, and built on every platform for the tests.
// ---------------------------------------------------------------------------

namespace {

// The local address column of a /proc/net table. The kernel prints each 32-bit
// word of the address as it lies in memory, with %08X — so on this machine's
// byte order, which is the one that wrote the file: eight hex digits for IPv4,
// thirty-two for IPv6.
bool parseProcNetAddress(QStringView hex, int family, SocketAddress *out)
{
    const qsizetype words = family == AF_INET6 ? 4 : 1;
    if (hex.size() != words * 8)
        return false;
    *out = {};
    for (qsizetype w = 0; w < words; ++w) {
        bool ok = false;
        const quint32 word = hex.mid(w * 8, 8).toUInt(&ok, 16);
        if (!ok)
            return false;
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&word), 4, out->begin() + w * 4);
    }
    return true;
}

} // namespace

QList<SocketOwner> parseProcNetTable(const QString &contents, int proto, int family)
{
    QList<SocketOwner> out;
    const QList<QStringView> lines = QStringView(contents).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QStringView &line : lines) {
        const QList<QStringView> f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        // sl local rem st tx:rx tr:tm retrnsmt uid timeout inode
        //  0     1   2  3    4     5        6   7       8     9
        if (f.size() < 10)
            continue; // the header line, and anything truncated
        const QList<QStringView> local = f[1].split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (local.size() != 2)
            continue;
        bool ok = false;
        const uint port = local[1].toUInt(&ok, 16);
        if (!ok || port == 0 || port > 0xFFFF)
            continue;
        bool inodeOk = false;
        const qulonglong inode = f[9].toULongLong(&inodeOk);
        if (!inodeOk)
            continue;

        SocketOwner owner;
        if (!parseProcNetAddress(local[0], family, &owner.address))
            continue;
        owner.port = static_cast<std::uint16_t>(port);
        owner.proto = proto;
        owner.family = family;
        owner.inode = inode;
        out.append(owner);
    }
    return out;
}

// ---------------------------------------------------------------------------
// identityForPid
// ---------------------------------------------------------------------------

namespace {

// What the system says a process is running, or empty when it will not say.
// Split from identityForPid so that each platform's answer is one function
// rather than one branch of three inside another — every branch is compiled
// away except one, but they are all read, and all counted.
QString executablePathOf(qint64 pid)
{
#if defined(Q_OS_WIN)
    // PROCESS_QUERY_LIMITED_INFORMATION is the weakest right that answers this,
    // and the only one a few protected system processes will grant at all.
    HANDLE h = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr)
        return {};
    wchar_t buf[MAX_PATH * 2] = {};
    DWORD len = static_cast<DWORD>(std::size(buf));
    const BOOL ok = ::QueryFullProcessImageNameW(h, 0, buf, &len);
    ::CloseHandle(h);
    if (!ok || len == 0)
        return {};
    const QString path = QString::fromWCharArray(buf, static_cast<int>(len));
#elif defined(Q_OS_MACOS)
    char buf[PROC_PIDPATHINFO_MAXSIZE] = {};
    const int n = ::proc_pidpath(static_cast<int>(pid), buf, sizeof(buf));
    if (n <= 0)
        return {};
    const QString path = QString::fromUtf8(buf, n);
#else
    // /proc/<pid>/exe is the kernel's own answer, which is why it is used
    // instead of /proc/<pid>/cmdline: a process can rewrite its command line,
    // and a rule that can be spoofed by the program it is meant to constrain is
    // worse than no rule.
    //
    // readlink(2) rather than QFile::symLinkTarget(), which canonicalises what
    // it reads — a stat and a path walk per process, on top of the readlink it
    // does anyway. The kernel has already resolved this link; there is nothing
    // left to canonicalise. Measured on a machine with seven hundred and fifty
    // processes, this is the difference between six milliseconds of walk and
    // under two, and the walk now happens on connections rather than on a timer.
    char buf[PATH_MAX + 1] = {};
    char procPath[64] = {};
    std::snprintf(procPath, sizeof(procPath), "/proc/%lld/exe", static_cast<long long>(pid));
    const ssize_t n = ::readlink(procPath, buf, sizeof(buf) - 1);
    if (n <= 0)
        return {};
    QString path = QString::fromLocal8Bit(buf, static_cast<int>(n));
    // The kernel marks a binary that has been replaced or removed since the
    // process started. The suffix is not part of any path and would stop the
    // rule matching, which is the opposite of what an upgraded application
    // should do to a rule about it.
    if (path.endsWith(QLatin1String(" (deleted)")))
        path.chop(10);
#endif
    return path;
}

} // namespace

AppIdentity identityForPid(qint64 pid)
{
    if (pid <= 0)
        return {};
    const QString path = executablePathOf(pid);
    if (path.isEmpty())
        return {};
    AppIdentity id;
    id.executablePath = QDir::toNativeSeparators(path);
    const qsizetype slash = id.executablePath.lastIndexOf(QDir::separator());
    id.name = slash >= 0 ? id.executablePath.mid(slash + 1) : id.executablePath;
    return id;
}

// ---------------------------------------------------------------------------
// ProcessLookup
// ---------------------------------------------------------------------------

ProcessLookup::ProcessLookup(std::chrono::milliseconds ttl)
    : m_ttl(ttl)
{
}

void ProcessLookup::setWatchList(const QStringList &rules)
{
    // Sanitised, so that the comparison below is between two lists in one
    // spelling, and a list of nothing but rules that cannot match is the empty
    // list resolve() walks nothing for. It does not spare the walk any work:
    // appMatchesRules() takes rules in any spelling and normalises each one on
    // every call, sanitised or not.
    const QStringList wanted = sanitizedAppRules(rules);
    if (m_watch == wanted)
        return;
    m_watch = wanted;
    // The table answers "which of THESE programs owns which port". A different
    // list is a different question, and the most important case is the one where
    // a program was just added: it was skipped during every previous walk, so
    // every one of its connections would be answered "not yours" from a table
    // that never looked. Comparing the lists is the whole of the bookkeeping —
    // no version counter to forget to bump.
    invalidate();
}

// The one entry point to a walk. Each platform implements walk() and nothing
// else, so the decision of WHEN to look is in a single place instead of being
// repeated, slightly differently, three times.
void ProcessLookup::refreshIfStale()
{
    const auto now = std::chrono::steady_clock::now();
    // A walk that failed is tried again on the next connection, but only out of
    // the credit that pays for every other look (see shouldLookAgain()). Trying
    // again is worth it: on Windows one of the four socket tables refusing still
    // leaves the other three read, so a fresh walk answers every connection
    // whose socket is in those. Tried on every connection regardless, a walk that
    // fails every time cost a whole walk each, outside the credit. Once the
    // credit is spent it is kept like a table until its time is up: it answers
    // for the sockets it did see, and any other is reported as not looked at.
    const bool keep = m_everBuilt
            || (m_walkFailed && m_credit < std::max<qint64>(m_report.elapsedUs, 0));
    if (keep && now - m_builtAt < currentTtl())
        return;
    m_owners.clear();
    // Counters are per walk. They used to accumulate, which was invisible while
    // the table was rebuilt twice a minute and would be nonsense now.
    const ScanReport fresh;
    m_report = fresh;
    ++m_walks;
    walk(now);
    m_credit -= m_report.elapsedUs;
}

// How long a table that contains the flow is trusted without asking again.
//
// This bounds one specific error and no other: the port was in the table, and
// the process that owned it has since let it go and another has taken it. That
// needs the operating system to work its way through an ephemeral range of some
// thirty thousand ports, so the bound can be generous. A MISS is never
// answered from here — see resolve() — because a miss has a cause we can act
// on, and this does not.
//
// Derived from what the last walk cost rather than fixed, because the fixed
// number was wrong by two orders of magnitude on a real machine: the walk took
// about 4 ms there and was trusted for 1500.
std::chrono::milliseconds ProcessLookup::currentTtl() const
{
    if (m_report.elapsedUs <= 0)
        return m_ttl;
    const auto derived = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::microseconds(m_report.elapsedUs * 20));
    return std::clamp(derived, std::chrono::milliseconds(100), m_ttl);
}

void ProcessLookup::finishScan(std::chrono::steady_clock::time_point startedAt, bool ok)
{
    ok &= !walkFailsForTests();
    const auto done = std::chrono::steady_clock::now();
    m_report.ok = ok;
#ifdef Q_OS_WIN
    m_report.euid = -1; // no such notion here
#else
    m_report.euid = static_cast<int>(::geteuid());
#endif
    m_report.entries = m_owners.size();
    m_report.distinctPids = m_owners.distinctPids();
    m_report.elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(done - startedAt).count()
            + extraWalkCostForTests();
    // Stamped now rather than with the time taken before the walk: stamping
    // with the earlier value made the table count as one scan-duration old the
    // moment it was published.
    m_builtAt = done;
    m_everBuilt = ok;
    m_walkFailed = !ok;
}

void ProcessLookup::invalidate()
{
    m_everBuilt = false;
    m_walkFailed = false; // a new list, or a new session, is looked at at once
    m_owners.clear();
    m_report = {};
}

// walk() is defined once for each platform, in ProcessLookupWalk.cpp.

void ProcessLookup::accrueLookCredit(std::chrono::steady_clock::time_point now)
{
    if (m_creditAt == std::chrono::steady_clock::time_point{}) {
        // The first connections of a session are the ones a person is watching,
        // so the bucket starts full rather than empty.
        m_credit = kLookCreditCapUs;
        m_creditAt = now;
        return;
    }
    const qint64 elapsedUs =
            std::chrono::duration_cast<std::chrono::microseconds>(now - m_creditAt).count();
    if (elapsedUs > 0)
        m_credit = std::min(kLookCreditCapUs, m_credit + elapsedUs / kLookDutyDivisor);
    m_creditAt = now;
}

// Whether to go and look again for a flow the table does not have.
//
// A miss is not evidence of a race. The operating system had to bind this
// socket's local port before the packet that carries it could exist, and this
// question was only asked because that packet arrived — so the socket is in the
// system's tables right now, and the only thing that can be wrong is our copy of
// them. Hence the middle test: if the table was finished AFTER the question was
// asked, it did look, the socket genuinely is not attributable, and looking a
// third time would find the same nothing. Otherwise the table simply predates
// the question and is worth rebuilding.
//
// That leaves cost as the only reason to decline, which is what the credit is.
bool ProcessLookup::shouldLookAgain(std::chrono::steady_clock::time_point asked) const
{
    if (!m_everBuilt)
        return false; // the last walk did not complete; refreshIfStale() retries it
    if (m_builtAt >= asked)
        return false;
    return m_credit >= std::max<qint64>(m_report.elapsedUs, 0);
}

// Whether a row the table found is the answer, or only looks like one.
//
// Found by the flow's own address, it is the answer whoever it names, including
// nobody: the walk saw this very socket and established that no watched program
// holds it. That is final, and it is the answer to most connections.
//
// Found by port number alone — a flow that came without its address — the row is
// only some socket on that number, which a socket opened since the walk may
// share. It is trusted only from a table built after the question was asked, when
// the socket behind the flow is certain to be among the ones on that number —
// which of them the row names is then SocketOwnerTable::record's rule. Otherwise
// it is treated as the miss it may well be.
bool ProcessLookup::settles(const SocketOwnerTable::Match &match,
                            std::chrono::steady_clock::time_point asked) const
{
    return match.found && (match.byAddress || (m_everBuilt && m_builtAt >= asked));
}

AppIdentity ProcessLookup::resolve(const LocalFlow &flow, bool *lookWasSkipped)
{
    if (lookWasSkipped != nullptr)
        *lookWasSkipped = false;
    // No rules means no watched processes, and the walk would have nothing to
    // look for. This is also what makes the feature cost nothing when it is off.
    if (flow.port == 0 || m_watch.isEmpty())
        return {};

    // Stamped first, and used twice below. This is the instant from which the
    // socket is known to exist, so it is what "fresh enough" is measured
    // against — a timestamp comparison rather than an interval anyone had to
    // guess the right length for.
    const auto asked = std::chrono::steady_clock::now();
    accrueLookCredit(asked);
    refreshIfStale();

    // Which socket this is: by its own address — see SocketOwnerTable::find for
    // the order, and for why a port number alone is not enough.
    SocketOwnerTable::Match owner = m_owners.find(flow);
    if (!settles(owner, asked)) {
        if (shouldLookAgain(asked)) {
            m_everBuilt = false;
            refreshIfStale();
            owner = m_owners.find(flow);
        } else if (lookWasSkipped != nullptr && (!m_everBuilt || m_builtAt < asked)) {
            // Not "looked and found nothing" — never looked, or looked and came
            // back incomplete. Either way the socket may well be in the system's
            // tables right now: what stopped us was the budget or a failed walk,
            // not the answer.
            *lookWasSkipped = true;
        }
    }
    if (!owner.found || owner.pid == kUnattributed)
        return {};
    const qint64 pid = owner.pid;

    // Asked now, not remembered. A pid is only the number of a process, and
    // exec() keeps the number while replacing the program behind it — so a
    // remembered answer would name the wrong program for the whole life of
    // anything started through a wrapper that execs, in either direction: a
    // rule that silently never applies, or one program's traffic routed by
    // another program's rule. It is one system call.
    const AppIdentity id = identityForPid(pid);
    // And the filter, once, for everyone. Where the walk reads every process it
    // is this that decides; where the walk already skipped what no rule names,
    // this can only agree with it.
    return appMatchesRules(id, m_watch) ? id : AppIdentity{};
}

} // namespace freetunnel
