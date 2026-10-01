// cppcheck-suppress-file missingIncludeSystem
// The walk itself, once for each platform: which process holds each socket
// the machine has open, read from the finished tables Windows hands over,
// from every process's descriptors on macOS, and on Linux from the
// descriptors of the processes a rule names. Split out of ProcessLookup.cpp,
// which keeps the decision of when to walk and what a finished table
// answers, and had grown past the point where one file could be read end to
// end.
#include "core/ProcessLookup.h"

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cerrno>

#if defined(Q_OS_WIN)
// For the reason ProcessLookup.cpp gives: without it <windows.h> turns min
// and max into function-like macros for the rest of the file.
#define NOMINMAX
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#elif defined(Q_OS_MACOS)
#include <libproc.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <cstdlib>
#include <dirent.h>
#include <unistd.h>
#endif

namespace freetunnel {

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
        // Reported as a failure, not as an empty machine: m_everBuilt stays
        // false, so until the walk is tried again the connections are answered
        // as not looked at, instead of inheriting this emptiness as "nobody's".
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

} // namespace freetunnel
