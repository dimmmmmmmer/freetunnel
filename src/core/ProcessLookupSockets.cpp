// cppcheck-suppress-file missingIncludeSystem
// Reading the machine's open sockets, which on Linux is a question with two
// answers: the kernel will describe them in binary through NETLINK_SOCK_DIAG,
// and where that is unavailable the same rows can be read back as text from
// /proc/net. Both live here, behind one call, apart from the walk that decides
// which process owns what — a different question asked of a different interface,
// and the two together had outgrown a single file.
#include "core/ProcessLookup.h"

#ifdef Q_OS_LINUX

#include <QByteArray>
#include <QFile>
#include <QString>

#include <algorithm>
#include <cerrno>
#include <utility>

// clang-format off
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <linux/inet_diag.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/sock_diag.h>
// clang-format on

namespace freetunnel {

namespace {

// Which tables hold the sockets we can be asked about, and what protocol each
// one is. Both ways of reading them below work through this list.
struct SocketTable {
    const char *path; // /proc/net/<name>
    int family;       // AF_INET / AF_INET6
    int proto;        // IPPROTO_TCP / IPPROTO_UDP
};
constexpr SocketTable kSocketTables[] = {
        {"/proc/net/tcp", AF_INET, IPPROTO_TCP},
        {"/proc/net/tcp6", AF_INET6, IPPROTO_TCP},
        {"/proc/net/udp", AF_INET, IPPROTO_UDP},
        {"/proc/net/udp6", AF_INET6, IPPROTO_UDP},
};

// What one message in a dump turns out to be.
enum class DiagStep {
    Recorded, // an ordinary socket, appended to out
    Ignored,  // not ours: a leftover from a dump abandoned earlier
    Finished, // the kernel says that is all of them
    Failed,   // the kernel says it will not answer
};

// Whether an IPv6 socket takes no IPv4 (IPV6_V6ONLY). The kernel says so in an
// attribute after the message, for every IPv6 socket it describes. Walked by
// hand for the reason walkDatagram() gives, with both bounds checked.
bool ipv6OnlyOf(const nlmsghdr *header)
{
    const auto *base = reinterpret_cast<const char *>(header);
    const auto total = static_cast<qsizetype>(header->nlmsg_len);
    auto offset = static_cast<qsizetype>(NLMSG_ALIGN(NLMSG_LENGTH(sizeof(inet_diag_msg))));
    while (offset + static_cast<qsizetype>(sizeof(rtattr)) <= total) {
        const auto *attribute = reinterpret_cast<const rtattr *>(base + offset);
        const auto length = static_cast<qsizetype>(attribute->rta_len);
        if (length < static_cast<qsizetype>(sizeof(rtattr)) || offset + length > total)
            break;
        if (attribute->rta_type == INET_DIAG_SKV6ONLY && length >= static_cast<qsizetype>(RTA_LENGTH(1)))
            return base[offset + static_cast<qsizetype>(RTA_LENGTH(0))] != 0;
        offset += static_cast<qsizetype>(RTA_ALIGN(attribute->rta_len));
    }
    return false;
}

// Read one message of a dump.
DiagStep readDiagMessage(const nlmsghdr *header, std::uint32_t seq, int proto,
                         QList<SocketOwner> *out, int *error)
{
    // A dump abandoned earlier would leave its remaining messages in the
    // socket; they are recognised by the sequence number and dropped.
    if (header->nlmsg_seq != seq)
        return DiagStep::Ignored;
    if (header->nlmsg_type == NLMSG_DONE)
        return DiagStep::Finished;
    if (header->nlmsg_type == NLMSG_ERROR) {
        const auto *failure = static_cast<const nlmsgerr *>(NLMSG_DATA(header));
        // The kernel reports errors as negative errno. A kernel built without
        // the matching diag module answers here rather than failing the send.
        *error = failure->error < 0 ? -failure->error : EIO;
        return DiagStep::Failed;
    }
    const auto *entry = static_cast<const inet_diag_msg *>(NLMSG_DATA(header));
    SocketOwner owner;
    owner.port = ntohs(entry->id.idiag_sport);
    owner.proto = proto;
    // From the kernel's own answer rather than from which table was asked: the
    // dump is per family, so the two agree, and reading it here means the field
    // cannot drift if that ever stops being true.
    owner.family = entry->idiag_family;
    owner.inode = entry->idiag_inode;
    // idiag_src is four network-order words: all of them for IPv6, the first for
    // IPv4.
    std::copy_n(reinterpret_cast<const std::uint8_t *>(entry->id.idiag_src), owner.family == AF_INET6 ? 16 : 4,
                owner.address.begin());
    owner.v6only = owner.family == AF_INET6 && ipv6OnlyOf(header);
    if (owner.port != 0)
        out->append(owner);
    return DiagStep::Recorded;
}

// Read every message in one datagram of a dump, and say what the last one was.
//
// Walked by hand rather than with NLMSG_OK/NLMSG_NEXT. Those macros compare the
// kernel's unsigned length against the caller's, which the client build rejects
// outright (-Wsign-compare -Werror), and the obvious way round it — an unsigned
// counter — is worse than a warning: NLMSG_ALIGN can round a message up past
// what is left, and the subtraction would then wrap to an enormous value and
// walk off the end of the buffer. Both bounds are checked here instead.
DiagStep walkDatagram(char *data, ssize_t length, std::uint32_t seq, int proto,
                      QList<SocketOwner> *out, int *error)
{
    char *cursor = data;
    ssize_t remaining = length;
    while (remaining >= static_cast<ssize_t>(sizeof(nlmsghdr))) {
        auto *header = reinterpret_cast<nlmsghdr *>(cursor);
        const ssize_t declared = static_cast<ssize_t>(header->nlmsg_len);
        if (declared < static_cast<ssize_t>(sizeof(nlmsghdr)) || declared > remaining)
            break;
        const ssize_t step = static_cast<ssize_t>(NLMSG_ALIGN(header->nlmsg_len));
        cursor += step;
        remaining = step > remaining ? 0 : remaining - step;

        const DiagStep outcome = readDiagMessage(header, seq, proto, out, error);
        if (outcome == DiagStep::Finished || outcome == DiagStep::Failed)
            return outcome;
    }
    return DiagStep::Recorded;
}

// One sock_diag dump: every socket of one family and protocol, appended to out.
// Returns 0, or the error the kernel replied with.
int dumpOneFamily(int fd, const SocketTable &table, std::uint32_t seq, QByteArray *buffer,
                  QList<SocketOwner> *out)
{
    struct {
        nlmsghdr header;
        inet_diag_req_v2 request;
    } message = {};
    message.header.nlmsg_len = sizeof(message);
    message.header.nlmsg_type = SOCK_DIAG_BY_FAMILY;
    message.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    message.header.nlmsg_seq = seq;
    message.request.sdiag_family = static_cast<std::uint8_t>(table.family);
    message.request.sdiag_protocol = static_cast<std::uint8_t>(table.proto);
    // Every state, including the ones a connection passes through on its way in
    // and out. A socket in SYN_SENT is exactly the case this all exists for: the
    // program has called connect(), which is why we are being asked at all.
    message.request.idiag_states = ~0u;

    sockaddr_nl kernel = {};
    kernel.nl_family = AF_NETLINK;
    if (::sendto(fd, &message, sizeof(message), 0, reinterpret_cast<sockaddr *>(&kernel),
                 sizeof(kernel))
        < 0) {
        return errno;
    }

    for (;;) {
        errno = 0;
        // MSG_TRUNC makes the kernel report how large the datagram really was,
        // not how much of it fitted. Without it a buffer that was too small
        // would lose sockets silently, and a lost socket is a rule that does not
        // apply to one connection — the failure this whole path exists to stop.
        // The buffer is far larger than a netlink dump chunk, so this is a
        // guard, not an expectation.
        const ssize_t got = ::recv(fd, buffer->data(), buffer->size(), MSG_TRUNC);
        if (got <= 0)
            return errno != 0 ? errno : EIO;
        if (got > buffer->size())
            return EMSGSIZE;

        int failure = 0;
        const DiagStep outcome = walkDatagram(buffer->data(), got, seq, table.proto, out, &failure);
        if (outcome == DiagStep::Finished)
            return 0;
        if (outcome == DiagStep::Failed)
            return failure;
    }
}

// Every inet socket on the machine, asked of the kernel in binary.
//
// This is the interface `ss` uses, and it exists because the text tables in
// /proc/net make the kernel format every socket on the machine one line at a
// time. Measured here with 325 sockets open: 0.95 ms against 2.6, and the two
// agree row for row — the same inodes, protocols and ports, none missing on
// either side.
//
// They are not quite identical, and the difference runs the safe way: this
// reports a socket from the moment it is bound, while the text tables show it
// only once it is listening or connected. Nothing depends on that — a connect
// request reaches us after connect(), by which point both sources have it — but
// it is why a row-by-row comparison can differ on a machine where something
// binds a port and holds it.
//
// Returns false when the kernel will not answer, which is how one built without
// the inet_diag modules replies. The caller reads the text tables instead. It is
// all four dumps or none: a machine with tcp_diag and no udp_diag would
// otherwise answer half the question, and half an answer here means a rule that
// works for some of a program's connections.
bool socketsViaNetlink(QList<SocketOwner> *out, QByteArray *buffer, int *lastErrno)
{
#ifdef FT_ENABLE_TEST_HOOKS
    // Test-only, compiled out of release builds. Every kernel this is built and
    // tested on has the inet_diag modules, so without a way to refuse them the
    // fallback below would never run outside the machine of whoever is missing
    // them — and a path that only runs where nobody is looking is a path that
    // has already stopped working by the time it is wanted.
    if (qEnvironmentVariableIsSet("FT_TEST_NO_SOCKET_NETLINK")) {
        *lastErrno = ENOSYS;
        return false;
    }
#endif
    // Opened per walk rather than kept: measured at no cost either way, and a
    // descriptor held open for the life of the client is one the descriptor
    // watchdog would have to be told about. A failure here is also the answer
    // for a kernel built without sock_diag at all, and for a sandbox that
    // refuses AF_NETLINK.
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
    if (fd < 0) {
        *lastErrno = errno;
        return false;
    }
    QList<SocketOwner> gathered;
    std::uint32_t seq = 1;
    for (const SocketTable &table : kSocketTables) {
        const int failed = dumpOneFamily(fd, table, seq++, buffer, &gathered);
        if (failed != 0) {
            *lastErrno = failed;
            ::close(fd);
            return false;
        }
    }
    ::close(fd);
    *out = std::move(gathered);
    return true;
}

} // namespace

QList<SocketOwner> allInetSockets(bool *viaNetlink, int *lastErrno)
{
    QByteArray buffer(256 * 1024, Qt::Uninitialized);
    QList<SocketOwner> sockets;
    *viaNetlink = socketsViaNetlink(&sockets, &buffer, lastErrno);
    if (*viaNetlink)
        return sockets;

    // QFile, though a raw read of these files measured a millisecond quicker
    // across the four of them. That mattered while this was how the sockets were
    // read; it does not now that it is the path taken only on a kernel without
    // the diag modules, and a hand-rolled read loop over a growing buffer is a
    // thing a reader — and a security scanner — has to take on trust.
    for (const SocketTable &table : kSocketTables) {
        QFile file(QString::fromLatin1(table.path));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        sockets.append(parseProcNetTable(QString::fromLatin1(file.readAll()), table.proto,
                                         table.family));
    }
    return sockets;
}

} // namespace freetunnel

#endif // Q_OS_LINUX
