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

// How much of this machine walking the process table may have. A quarter of
// real time, saved up to fifty milliseconds' worth.
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
    // Normalised once here rather than per rule per process per walk, which is
    // what appMatchesRules() would otherwise do — it takes rules in any
    // spelling, and the walk asks it about every process on the machine.
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
    if (m_everBuilt && now - m_builtAt < currentTtl())
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
            std::chrono::duration_cast<std::chrono::microseconds>(done - startedAt).count();
    // Stamped now rather than with the time taken before the walk: stamping
    // with the earlier value made the table count as one scan-duration old the
    // moment it was published.
    m_builtAt = done;
    m_everBuilt = ok;
}

void ProcessLookup::invalidate()
{
    m_everBuilt = false;
    m_owners.clear();
    m_report = {};
}

#if defined(Q_OS_WIN)

namespace {

// The local address of a row. The IPv4 rows hold it as a DWORD in network byte
// order, the IPv6 ones as sixteen bytes; the byte order is the same either way,
// so it is copied as it lies.
SocketAddress localAddressOf(const MIB_TCPROW_OWNER_PID &row)
{
    SocketAddress out{};
    std::copy_n(reinterpret_cast<const std::uint8_t *>(&row.dwLocalAddr), 4, out.begin());
    return out;
}
SocketAddress localAddressOf(const MIB_UDPROW_OWNER_PID &row)
{
    SocketAddress out{};
    std::copy_n(reinterpret_cast<const std::uint8_t *>(&row.dwLocalAddr), 4, out.begin());
    return out;
}
SocketAddress localAddressOf(const MIB_TCP6ROW_OWNER_PID &row)
{
    SocketAddress out{};
    std::copy_n(row.ucLocalAddr, 16, out.begin());
    return out;
}
SocketAddress localAddressOf(const MIB_UDP6ROW_OWNER_PID &row)
{
    SocketAddress out{};
    std::copy_n(row.ucLocalAddr, 16, out.begin());
    return out;
}

// Every row of one Windows socket table. The four tables hold different row
// types, and all four of them spell the port and the pid the same way, which is
// what lets one function read all of them — it was four copies of this loop,
// and the copies drifted apart the moment one of them was corrected.
template <typename TableT>
void recordWindowsRows(const TableT *table, SocketOwnerTable *owners, int family, int proto)
{
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto &row = table->table[i];
        // A row the system attributes to nobody — a connection in TIME_WAIT is
        // reported that way — can never name a program, and recording it would
        // displace a row that can.
        if (row.dwOwningPid == 0)
            continue;
        // First one wins, as on the other two platforms, among rows for the same
        // socket: a listener and the connections accepted on it share one.
        owners->record(family, proto, ntohs(static_cast<u_short>(row.dwLocalPort)),
                       localAddressOf(row), static_cast<qint64>(row.dwOwningPid));
    }
}

// One pass over a Windows socket table. GetExtended*Table returns the whole
// table in one buffer, which is exactly the shape we want: one syscall per
// refresh rather than one per connection.
//
// Asked twice per attempt — once for the size, once for the contents — and the
// table is a live thing that gains rows in between. When it does, the second
// call answers ERROR_INSUFFICIENT_BUFFER with the new size rather than filling
// the old buffer, and a single attempt then returns having recorded nothing.
// That is not a row lost, it is every port of this family and protocol lost for
// the whole walk, while the scan still reports success: a machine that looks
// like it has no IPv4 TCP sockets at all. Microsoft's own pattern for these APIs
// is the retry, and the answer is reported so a walk that gave up does not read
// as a walk that found nothing.
template <typename TableT>
bool collectWindowsTable(SocketOwnerTable *owners, ULONG af, int proto, bool tcp)
{
    for (int attempt = 0; attempt < 4; ++attempt) {
        ULONG size = 0;
        DWORD rc = tcp ? ::GetExtendedTcpTable(nullptr, &size, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0)
                       : ::GetExtendedUdpTable(nullptr, &size, FALSE, af, UDP_TABLE_OWNER_PID, 0);
        if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0)
            return rc == NO_ERROR; // nothing to read is not a failure
        // Room for a few more rows than were there a moment ago, so the ordinary
        // case of the table growing slightly does not cost another round trip.
        QByteArray buf(static_cast<int>(size) + 4096, Qt::Uninitialized);
        ULONG given = static_cast<ULONG>(buf.size());
        rc = tcp ? ::GetExtendedTcpTable(buf.data(), &given, FALSE, af, TCP_TABLE_OWNER_PID_ALL, 0)
                 : ::GetExtendedUdpTable(buf.data(), &given, FALSE, af, UDP_TABLE_OWNER_PID, 0);
        if (rc == NO_ERROR) {
            recordWindowsRows(reinterpret_cast<const TableT *>(buf.constData()), owners,
                              static_cast<int>(af), proto);
            return true;
        }
        if (rc != ERROR_INSUFFICIENT_BUFFER)
            return false;
    }
    return false;
}

} // namespace

void ProcessLookup::walk(std::chrono::steady_clock::time_point now)
{
    // Windows never enumerates processes: the IP helper API hands over a
    // finished table naming the owning pid of every port on the machine. So
    // every port is attributed here, and which of them a rule names is decided
    // in resolve(), for the one port being asked about rather than for the
    // hundreds that were not.
    bool ok = collectWindowsTable<MIB_TCPTABLE_OWNER_PID>(&m_owners, AF_INET, IPPROTO_TCP, true);
    ok &= collectWindowsTable<MIB_TCP6TABLE_OWNER_PID>(&m_owners, AF_INET6, IPPROTO_TCP, true);
    ok &= collectWindowsTable<MIB_UDPTABLE_OWNER_PID>(&m_owners, AF_INET, IPPROTO_UDP, false);
    ok &= collectWindowsTable<MIB_UDP6TABLE_OWNER_PID>(&m_owners, AF_INET6, IPPROTO_UDP, false);
    // A walk that lost a whole table is not a walk that completed. Reporting it
    // as one leaves resolve() treating a port it never saw as a port nobody
    // owns, and the scan line printing a plausible entry count for a machine it
    // saw three quarters of.
    finishScan(now, ok);
}

#elif defined(Q_OS_MACOS)

namespace {

// Every process on the machine. Named and shaped like the Linux one below, and
// empty when the system refused, with errno left as the failed call set it.
QList<qint64> listProcessIds()
{
    QList<qint64> pids;
    int count = ::proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (count <= 0)
        return pids;
    QByteArray buffer(count, Qt::Uninitialized);
    count = ::proc_listpids(PROC_ALL_PIDS, 0, buffer.data(), buffer.size());
    if (count <= 0)
        return pids;
    const int found = count / static_cast<int>(sizeof(pid_t));
    const auto *raw = reinterpret_cast<const pid_t *>(buffer.constData());
    pids.reserve(found);
    for (int i = 0; i < found; ++i) {
        if (raw[i] > 0)
            pids.append(static_cast<qint64>(raw[i]));
    }
    return pids;
}

// The protocol, local port and local address one socket descriptor is bound
// to, or false when it is not an internet socket with a port.
bool socketEndpointOf(const socket_fdinfo &info, int *family, int *proto, std::uint16_t *port,
                      SocketAddress *address, bool *v6only)
{
    *family = info.psi.soi_family;
    if (*family != AF_INET && *family != AF_INET6)
        return false;
    const in_sockinfo *ini = nullptr;
    if (info.psi.soi_kind == SOCKINFO_TCP) {
        *proto = IPPROTO_TCP;
        ini = &info.psi.soi_proto.pri_tcp.tcpsi_ini;
    } else if (info.psi.soi_kind == SOCKINFO_IN) {
        *proto = IPPROTO_UDP;
        ini = &info.psi.soi_proto.pri_in;
    } else {
        return false;
    }
    *port = ntohs(static_cast<std::uint16_t>(ini->insi_lport));
    // An IPv6 socket carrying IPv4 keeps the address in the IPv4 half of the
    // union and says so in insi_vflag; it is recorded the way Linux and Windows
    // report the same socket, as ::ffff:a.b.c.d.
    const in_addr &v4 = ini->insi_laddr.ina_46.i46a_addr4;
    // INI_IPV4 is set on an IPv6 socket exactly when it can carry IPv4: cleared
    // by IPV6_V6ONLY, set for a dual-stack one.
    *v6only = *family == AF_INET6 && (ini->insi_vflag & INI_IPV4) == 0;
    *address = {};
    if (*family == AF_INET) {
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&v4), 4, address->begin());
    } else if ((ini->insi_vflag & INI_IPV4) != 0 && (ini->insi_vflag & INI_IPV6) == 0) {
        *address = v4MappedAddress(ntohl(v4.s_addr));
    } else {
        std::copy_n(reinterpret_cast<const std::uint8_t *>(&ini->insi_laddr.ina_6), 16, address->begin());
    }
    return *port != 0;
}

// The ports one process holds open. The counterpart of collectSocketInodes() on
// Linux, and asked of every process for the reason given in walk().
void collectSocketsOfProcess(pid_t pid, SocketOwnerTable *owners, ProcessLookup::ScanReport *report)
{
    errno = 0;
    int bufSize = ::proc_pidinfo(pid, PROC_PIDLISTFDS, 0, nullptr, 0);
    if (bufSize <= 0) {
        // A process that has gone is not a process that refused. As root the
        // refusals should be none, so a count here is itself the answer to why
        // nothing matches.
        if (errno == ESRCH)
            ++report->pidsWithoutProgram;
        else
            ++report->pidsSkipped;
        report->lastErrno = errno;
        return;
    }
    QByteArray fdBuf(bufSize, Qt::Uninitialized);
    bufSize = ::proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fdBuf.data(), fdBuf.size());
    if (bufSize <= 0)
        return;
    const int nFds = bufSize / static_cast<int>(sizeof(proc_fdinfo));
    const auto *fds = reinterpret_cast<const proc_fdinfo *>(fdBuf.constData());
    for (int f = 0; f < nFds; ++f) {
        if (fds[f].proc_fdtype != PROX_FDTYPE_SOCKET)
            continue;
        ++report->socketsSeen;
        // Over-allocated as insurance, and nothing more than that. It was
        // committed as "the macOS cause" on the theory that a kernel newer than
        // the build SDK would refuse an exact-sized buffer with ENOMEM and skip
        // every socket on the machine. The refusal mechanism is real, but the
        // premise is not: sizeof(struct socket_fdinfo) has been 792 bytes at
        // every XNU release from macOS 10.14 to now — the union is sized by
        // un_sockinfo, which has not moved — and the structs sit outside any
        // PRIVATE/KERNEL guard, so the SDK header and the kernel header cannot
        // disagree. The exact-sized version was correct. This is kept because it
        // costs nothing and removes the question, not because it fixed anything.
        alignas(socket_fdinfo) char raw[sizeof(socket_fdinfo) + 1024] = {};
        const int got = ::proc_pidfdinfo(pid, fds[f].proc_fd, PROC_PIDFDSOCKETINFO, raw,
                                         static_cast<int>(sizeof(raw)));
        if (got <= 0)
            continue;
        int family = 0;
        int proto = 0;
        std::uint16_t port = 0;
        SocketAddress address{};
        bool v6only = false;
        if (!socketEndpointOf(*reinterpret_cast<const socket_fdinfo *>(raw), &family, &proto, &port,
                              &address, &v6only))
            continue;
        // Does not overwrite: two processes can legitimately hold the same
        // socket address — a listener and an accepted connection, or a socket
        // one of them is about to close — and letting whichever pid the scan
        // happened to reach last win makes the answer depend on process
        // enumeration order.
        owners->record(family, proto, port, address, static_cast<qint64>(pid), v6only);
    }
}

} // namespace

void ProcessLookup::walk(std::chrono::steady_clock::time_point now)
{
    // macOS has no socket table to read; the ports have to be gathered from each
    // process's own file descriptors, and every process is read rather than only
    // the ones a rule names.
    //
    // That is deliberate, and it is the cheaper of the two here. Reading only
    // the named processes would mean asking every process for its executable
    // first, which on this platform costs about what reading its descriptors
    // costs — and it would leave the walk knowing nothing about the ports it
    // skipped. Those ports are the answer to most connections: a table that can
    // say "this port is open and belongs to nobody you named" settles them
    // outright, while a table that omits them makes each one look like a table
    // too old to trust and buy another walk.
    const QList<qint64> pids = listProcessIds();
    if (pids.isEmpty()) {
        // Not cached: m_everBuilt stays false so the next connection tries
        // again, instead of every connection for the next TTL inheriting one
        // failed call's emptiness.
        m_report.lastErrno = errno;
        finishScan(now, false);
        return;
    }

    for (const qint64 pid : pids) {
        ++m_report.pidsScanned;
        collectSocketsOfProcess(static_cast<pid_t>(pid), &m_owners, &m_report);
    }

    finishScan(now, true);
}

#else

namespace {

// Every process on the machine, cheaply. readdir(3) rather than
// QDir::entryList(QDir::Dirs), which would stat each of the thousand-odd entries
// in /proc to establish what the name alone already says: an entry that is not a
// number is not a process.
QList<qint64> listProcessIds()
{
    QList<qint64> pids;
    DIR *proc = ::opendir("/proc");
    if (proc == nullptr)
        return pids;
    while (const dirent *entry = ::readdir(proc)) {
        char *end = nullptr;
        const long long parsed = std::strtoll(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || parsed <= 0)
            continue;
        pids.append(static_cast<qint64>(parsed));
    }
    ::closedir(proc);
    return pids;
}



// The socket inodes held by the processes a rule names, and nobody else's.
//
// Reading the executable is one readlink; reading a process's descriptors is a
// directory listing and a readlink each. Asking the cheap question first is what
// keeps the walk affordable enough to happen on a connection.
QHash<std::uint64_t, qint64> watchedSocketInodes(const QList<qint64> &pids,
                                                 const QStringList &watch,
                                                 ProcessLookup::ScanReport *report);

// The socket inodes one process holds open, by reading its /proc/<pid>/fd
// links. This is what `ss -p` does, and it is the expensive half of a Linux
// lookup — a directory listing plus one readlink per descriptor — which is why
// it is asked only of the processes a rule names.
void collectSocketInodes(qint64 pid, QHash<std::uint64_t, qint64> *out,
                         ProcessLookup::ScanReport *report)
{
    QDir fdDir(QStringLiteral("/proc/%1/fd").arg(pid));
    // Not QDir::Files: these are symlinks pointing at sockets, and QDir
    // classifies a symlink by what it resolves to, so a socket link is not a
    // "file" and the filter would return nothing at all.
    const QStringList fds = fdDir.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &fd : fds) {
        // readlink(2) rather than QFile::symLinkTarget(): a socket link reads
        // "socket:[12345]", which is not a path, and Qt helpfully resolves it
        // against the directory into "/proc/<pid>/fd/socket:[12345]". Everything
        // below then fails to match, silently, and every rule stops working —
        // which is exactly what happened the first time this was written.
        const QByteArray linkPath = fdDir.filePath(fd).toLocal8Bit();
        char buf[64] = {};
        const ssize_t n = ::readlink(linkPath.constData(), buf, sizeof(buf) - 1);
        if (n <= 0)
            continue;
        const QByteArray target(buf, static_cast<int>(n));
        if (!target.startsWith("socket:[") || !target.endsWith(']'))
            continue;
        ++report->socketsSeen;
        bool ok = false;
        const qulonglong inode = target.mid(8, target.size() - 9).toULongLong(&ok);
        if (ok && inode != 0)
            out->insert(inode, pid);
    }
}

QHash<std::uint64_t, qint64> watchedSocketInodes(const QList<qint64> &pids,
                                                 const QStringList &watch,
                                                 ProcessLookup::ScanReport *report)
{
    QHash<std::uint64_t, qint64> byInode;
    for (const qint64 pid : pids) {
        ++report->pidsScanned;
        errno = 0;
        const AppIdentity id = identityForPid(pid);
        if (id.executablePath.isEmpty()) {
            // Two different things, and telling them apart is the whole value of
            // the count. A kernel thread has no executable and never will —
            // there are hundreds of them on an ordinary desktop — while a
            // refusal means this walk cannot see the machine it is on.
            if (errno == ENOENT || errno == ESRCH)
                ++report->pidsWithoutProgram;
            else
                ++report->pidsSkipped;
            continue;
        }
        if (!appMatchesRules(id, watch))
            continue;
        ++report->pidsWatched;
        collectSocketInodes(pid, &byInode, report);
    }
    return byInode;
}

} // namespace

void ProcessLookup::walk(std::chrono::steady_clock::time_point now)
{
    const QList<qint64> pids = listProcessIds();
    if (pids.isEmpty()) {
        // /proc is always readable on a running Linux system, so this is the
        // walk having failed rather than the machine having no processes. Said
        // so explicitly: with the tables still recording every port it saw, a
        // silent empty listing would mark the whole machine unattributed and
        // report a healthy scan while no rule could ever match.
        m_report.lastErrno = errno;
        finishScan(now, false);
        return;
    }

    const QHash<std::uint64_t, qint64> byInode = watchedSocketInodes(pids, m_watch, &m_report);

    // Every socket on the machine, whether or not anything watched holds it. The
    // ones nothing holds are the point: they are what lets a connection from
    // some other program be answered outright instead of buying a walk of
    // its own.
    //
    // Gathered in full every time, which is a deliberate choice rather than an
    // oversight. Remembering which port an inode had would be sound, since
    // neither changes for the life of a socket; the trap is the inodes that are
    // NOT mentioned. A socket that has been created and not yet bound appears
    // nowhere — measured against both sources — and is indistinguishable from a
    // unix or netlink socket, which will never appear. Remembering "this one has
    // no port" would therefore catch every connection whose socket was created a
    // few microseconds before a walk happened to look, and misroute it for as
    // long as it lasted — the intermittent failure this whole change exists to
    // remove, reintroduced by the optimisation meant to pay for it.
    bool viaNetlink = false;
    const QList<SocketOwner> sockets = allInetSockets(&viaNetlink, &m_report.lastErrno);
    m_report.netlink = viaNetlink;

    for (const SocketOwner &sock : sockets) {
        // A socket no process holds — one in TIME_WAIT, or closed and still
        // finishing, or half-open on a listener — is reported with inode 0. It
        // can never be the socket behind a new connection, and a row for it
        // would answer "nobody you named" for whichever new socket takes its
        // address and port next, which the kernel allows. Windows leaves these
        // out the same way; macOS never sees them, having no descriptor to find.
        if (sock.inode == 0)
            continue;
        // Seen, and held by a watched program or by nobody watched. One address
        // can carry several sockets — a listener and the connections accepted
        // on it — and the program that was asked about wins over the ones that
        // were not; among watched owners the first one does, as on macOS.
        const auto owner = byInode.constFind(sock.inode);
        m_owners.record(sock.family, sock.proto, sock.port, sock.address,
                        owner == byInode.constEnd() ? kUnattributed : owner.value(), sock.v6only);
    }

    finishScan(now, true);
}

#endif

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
        return false; // the walk we just tried did not complete; it will be retried
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
