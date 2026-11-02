// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QUrl>
#include <QVariantMap>

#include <memory>

#include "BackendConfigShared.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "core/NetBind.h"

namespace {

QString firstAddress(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(QStringLiteral("addresses\\s*=\\s*\\[\\s*\"([^\"]+)\""));
    const auto m = re.match(content);
    return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace

void Backend::markConfigPingFailed(int index)
{
    if (index < m_pings.size() && m_pings[index].toString() == QStringLiteral("…")) {
        m_pings[index] = QStringLiteral("—");
        emit pingsChanged();
    }
}

void Backend::runConfigPing(int index, const QHostAddress &ip, quint16 port)
{
    // TCP-connect latency probe (a TLS handshake would be closer, but TrustTunnel's
    // anti-DPI servers don't complete a vanilla one). Measure the direct path to the
    // server, never via the tunnel: bind the physical interface to bypass it.
    // Exception — the active server while connected already reaches direct over
    // plain routing (the core excludes its endpoint), and a physical bind there can
    // stall on Windows, so probe it with a plain socket.
    const bool isActive = index >= 0 && index < m_paths.size() && m_paths.at(index) == m_activePath;
    startPingProbe(index, ip, port, /*physical=*/!(m_connected && isActive));
}

void Backend::startPingProbe(int index, const QHostAddress &ip, quint16 port, bool physical)
{
    QTcpSocket *sock = physical ? freetunnel::makePhysicalBoundTcpSocket(this, ip.protocol())
                                : new QTcpSocket(this);
    auto elapsed = std::make_shared<QElapsedTimer>();
    elapsed->start();
    auto finished = std::make_shared<bool>(false);
    // Probes from a superseded run (pingConfigs re-triggered, configs reloaded)
    // must not touch the repopulated list — rows may map to other servers now.
    const int gen = m_pingGeneration;

    auto fail = [this, gen, index, sock, finished]() {
        if (*finished)
            return;
        *finished = true;
        sock->deleteLater();
        if (gen == m_pingGeneration)
            markConfigPingFailed(index);
    };

    connect(sock, &QTcpSocket::connected, this, [this, gen, index, sock, elapsed, finished]() {
        if (*finished)
            return;
        *finished = true;
        sock->abort();
        sock->deleteLater();
        if (gen != m_pingGeneration)
            return;
        if (index < m_pings.size())
            m_pings[index] = QString::number(elapsed->elapsed()) + tr(" ms");
        emit pingsChanged();
    });
    connect(sock, &QTcpSocket::errorOccurred, this,
            [fail](QAbstractSocket::SocketError) { fail(); });
    QTimer::singleShot(3000, sock, [sock, fail]() {
        if (sock->state() != QAbstractSocket::ConnectedState)
            fail();
    });
    sock->connectToHost(ip, port);
}

namespace {

// Split "host:port" / "[v6]:port". A bare "host" (no port) returns false.
bool splitHostPort(const QString &addr, QString *host, quint16 *port)
{
    QString portStr;
    if (addr.startsWith(QLatin1Char('['))) {
        // Bracketed IPv6: "[2001:db8::1]:443".
        const int rb = addr.indexOf(QLatin1String("]:"));
        if (rb < 0)
            return false;
        *host = addr.mid(1, rb - 1);
        portStr = addr.mid(rb + 2);
    } else {
        const int colon = addr.lastIndexOf(QLatin1Char(':'));
        if (colon < 0)
            return false;
        *host = addr.left(colon);
        portStr = addr.mid(colon + 1);
    }
    bool ok = false;
    const uint p = portStr.toUInt(&ok);
    if (!ok || p == 0 || p > 65535)
        return false;
    *port = static_cast<quint16>(p);
    return true;
}

} // namespace

void Backend::pingConfigAtIndex(int index)
{
    const QString addr = firstAddress(m_paths.at(index)).trimmed();
    QString host;
    quint16 port = 0;
    if (!splitHostPort(addr, &host, &port)) {
        markConfigPingFailed(index);
        return;
    }
    const QHostAddress literal(host);
    if (!literal.isNull()) {
        runConfigPing(index, literal, port);
        return;
    }
    const int gen = m_pingGeneration;
    QHostInfo::lookupHost(host, this, [this, gen, index, port](const QHostInfo &hi) {
        if (gen != m_pingGeneration)
            return;
        if (hi.addresses().isEmpty())
            markConfigPingFailed(index);
        else
            runConfigPing(index, hi.addresses().first(), port);
    });
}

void Backend::pingConfigs()
{
    ++m_pingGeneration; // supersede any probes still in flight
    m_pings.clear();
    for (int i = 0; i < m_paths.size(); ++i)
        m_pings << QStringLiteral("…");
    emit pingsChanged();
    for (int i = 0; i < m_paths.size(); ++i)
        pingConfigAtIndex(i);
}

QVariantMap Backend::configFields(int index)
{
    QVariantMap f;
    if (index < 0 || index >= m_paths.size())
        return f;
    QFile file(m_paths.at(index));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return f;
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(file.readAll()));
    const QString configPath = m_paths.at(index);
    // The file's own copy of a secret first, then the store's (readConfigSecrets).
    const freetunnel::ConfigSecrets secrets = freetunnel::readConfigSecrets(configPath, c);
    if (secrets.unreadable != freetunnel::SecretUnreadable::None) {
        // Nothing of the config goes with this: a form opened without the secret
        // looks complete, and saving it writes the config over without it. The
        // editor says why and closes.
        f[QStringLiteral("path")] = configPath;
        f[QStringLiteral("secretError")] = noteSecretNotRead(secrets.unreadable);
        return f;
    }
    f[QStringLiteral("name")] = nameForPath(configPath);
    f[QStringLiteral("hostname")] = c.hostname;
    f[QStringLiteral("addresses")] = c.addresses;
    f[QStringLiteral("username")] = c.username;
    f[QStringLiteral("password")] = secrets.password;
    f[QStringLiteral("protocol")] = c.protocol;
    f[QStringLiteral("dns")] = c.dns;
    f[QStringLiteral("customSni")] = c.customSni;
    f[QStringLiteral("clientRandom")] = c.clientRandom;
    f[QStringLiteral("authKey")] = secrets.authKey;
    f[QStringLiteral("allowIpv6")] = c.allowIpv6;
    f[QStringLiteral("skipVerification")] = c.skipVerification;
    f[QStringLiteral("antiDpi")] = c.antiDpi;
    f[QStringLiteral("certificate")] = c.certificate;
    // The file this row is, so the editor can say WHICH config it is saving
    // rather than which position it was at when it opened. See createConfig().
    f[QStringLiteral("path")] = configPath;
    const QString prof = m_settings.config_profiles.value(configPath);
    f[QStringLiteral("splitProfile")] =
            (prof.isEmpty() || !m_settings.profiles.contains(prof)) ? QStringLiteral("Default") : prof;
    return f;
}

QString Backend::configDeepLink(int index)
{
    // Said here, not by the window: the window cannot tell this from a secret the
    // credential store could not be asked for, which has a message of its own.
    const auto failed = [this]() {
        const QString message = tr("Couldn’t build deep-link");
        appendLog(QStringLiteral("WARN"), message);
        emit errorOccurred(message);
        return QString();
    };
    if (index < 0 || index >= m_paths.size())
        return failed();
    const QString path = m_paths.at(index);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return failed();
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(file.readAll()));
    // A link made without a secret the config has is a link that cannot connect, or
    // one that quietly drops the auth key and comes out as version 1. So there is
    // no link at all when either could not be read.
    const freetunnel::ConfigSecrets secrets = freetunnel::readConfigSecrets(path, c);
    if (secrets.unreadable != freetunnel::SecretUnreadable::None) {
        reportSecretNotRead(secrets.unreadable);
        return QString();
    }
    freetunnel::DeepLinkConfig dl;
    dl.version = freetunnel::kDeepLinkWriteVersion;
    dl.hostname = c.hostname;
    for (const QString &a : c.addresses.split(QLatin1Char(','), Qt::SkipEmptyParts))
        dl.addresses << a.trimmed();
    dl.customSni = c.customSni;
    dl.hasIpv6 = c.allowIpv6;
    dl.username = c.username;
    dl.password = secrets.password;
    // The certificates alone, as the import keeps them. A config kept outside the
    // app's folder is never cleaned at start, and can still hold a private key
    // beside its certificate; a link is made to be handed on.
    dl.certificate = freetunnel::certificatesDer(c.certificate);
    dl.upstreamProtocol = c.protocol == QLatin1String("http3")
            ? freetunnel::UpstreamProtocol::Http3 : freetunnel::UpstreamProtocol::Http2;
    dl.clientRandomPrefix = c.clientRandom;
    // The link of a server that checks an auth key has to carry it: without it the
    // link cannot connect. It then is version 2 (encodeDeepLink), which FreeTunnel
    // 1.2.3 and earlier and TrustTunnel's clients up to 1.1.11 refuse.
    dl.clientRandomAuthKey = secrets.authKey;
    dl.name = nameForPath(path);
    for (const QString &d : freetunnel::splitDnsList(c.dns))
        dl.dnsUpstreams << d.trimmed();
    const QString link = freetunnel::encodeDeepLink(dl);
    return link.isEmpty() ? failed() : link;
}

bool Backend::exportConfigToml(int index, const QString &fileUrl)
{
    const auto failed = [this]() {
        const QString message = tr("Export failed");
        appendLog(QStringLiteral("WARN"), message);
        emit errorOccurred(message);
        return false;
    };
    if (index < 0 || index >= m_paths.size())
        return failed();
    const QString src = m_paths.at(index);
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return failed();
    freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(in.readAll()));
    // No file is written for a secret that could not be read: the export would be a
    // config that cannot connect, handed on as if it were the user's.
    const freetunnel::ConfigSecrets secrets = freetunnel::readConfigSecrets(src, c);
    if (secrets.unreadable != freetunnel::SecretUnreadable::None) {
        reportSecretNotRead(secrets.unreadable);
        return false;
    }
    c.password = secrets.password;
    c.clientRandomAuthKey = secrets.authKey;
    const QString dest = fileUrl.startsWith(QLatin1String("file:"))
            ? QUrl(fileUrl).toLocalFile() : fileUrl;
    if (dest.isEmpty())
        return failed();
    // The certificates alone, as for the link above: an export is made to be
    // handed on too.
    c.certificate = freetunnel::certificatesOnly(c.certificate);
    // The export carries the password (and the auth key) in cleartext into a user-chosen directory
    // (~/Downloads and friends): writeConfigFile makes it owner-only before the
    // first byte lands, instead of chmod'ing a world-readable file afterwards.
    return freetunnel::backend_config::writeConfigFile(dest, freetunnel::buildConfigToml(c).toUtf8())
            ? true : failed();
}

void Backend::moveConfig(int from, int to)
{
    QStringList stored = loadStoredConfigs();
    if (from < 0 || to < 0 || from >= stored.size() || to >= stored.size() || from == to)
        return;
    stored.move(from, to);
    saveStoredConfigs(stored);
    reloadConfigs();
    emit configChanged();
}
