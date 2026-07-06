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

QByteArray pemCertsToDer(const QString &pem)
{
    QByteArray der;
    static const QRegularExpression block(
            QStringLiteral("-----BEGIN CERTIFICATE-----(.*?)-----END CERTIFICATE-----"),
            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression ws(QStringLiteral("\\s"));
    auto it = block.globalMatch(pem);
    while (it.hasNext()) {
        QString b64 = it.next().captured(1);
        b64.remove(ws);
        der += QByteArray::fromBase64(b64.toLatin1());
    }
    return der;
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

QVariantMap Backend::configFields(int index) const
{
    QVariantMap f;
    if (index < 0 || index >= m_paths.size())
        return f;
    QFile file(m_paths.at(index));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return f;
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(file.readAll()));
    const QString configPath = m_paths.at(index);
    QString password = freetunnel::CredentialStore::loadPassword(
            freetunnel::CredentialStore::keyForConfigPath(configPath));
    if (password.isEmpty())
        password = c.password;
    f[QStringLiteral("name")] = nameForPath(configPath);
    f[QStringLiteral("hostname")] = c.hostname;
    f[QStringLiteral("addresses")] = c.addresses;
    f[QStringLiteral("username")] = c.username;
    f[QStringLiteral("password")] = password;
    f[QStringLiteral("protocol")] = c.protocol;
    f[QStringLiteral("dns")] = c.dns;
    f[QStringLiteral("customSni")] = c.customSni;
    f[QStringLiteral("clientRandom")] = c.clientRandom;
    f[QStringLiteral("allowIpv6")] = c.allowIpv6;
    f[QStringLiteral("skipVerification")] = c.skipVerification;
    f[QStringLiteral("antiDpi")] = c.antiDpi;
    f[QStringLiteral("certificate")] = c.certificate;
    const QString prof = m_settings.config_profiles.value(configPath);
    f[QStringLiteral("splitProfile")] =
            (prof.isEmpty() || !m_settings.profiles.contains(prof)) ? QStringLiteral("Default") : prof;
    return f;
}

QString Backend::configDeepLink(int index) const
{
    if (index < 0 || index >= m_paths.size())
        return QString();
    const QString path = m_paths.at(index);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(file.readAll()));
    freetunnel::DeepLinkConfig dl;
    dl.version = freetunnel::kDeepLinkMaxVersion;
    dl.hostname = c.hostname;
    for (const QString &a : c.addresses.split(QLatin1Char(','), Qt::SkipEmptyParts))
        dl.addresses << a.trimmed();
    dl.customSni = c.customSni;
    dl.hasIpv6 = c.allowIpv6;
    dl.username = c.username;
    dl.password = freetunnel::CredentialStore::loadPassword(
            freetunnel::CredentialStore::keyForConfigPath(path));
    if (dl.password.isEmpty())
        dl.password = c.password;
    dl.certificate = pemCertsToDer(c.certificate);
    dl.upstreamProtocol = c.protocol == QLatin1String("http3")
            ? freetunnel::UpstreamProtocol::Http3 : freetunnel::UpstreamProtocol::Http2;
    dl.clientRandomPrefix = c.clientRandom;
    dl.name = nameForPath(path);
    for (const QString &d : c.dns.split(QLatin1Char(','), Qt::SkipEmptyParts))
        dl.dnsUpstreams << d.trimmed();
    return freetunnel::encodeDeepLink(dl);
}

bool Backend::exportConfigToml(int index, const QString &fileUrl) const
{
    if (index < 0 || index >= m_paths.size())
        return false;
    const QString src = m_paths.at(index);
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(in.readAll()));
    if (c.password.isEmpty())
        c.password = freetunnel::CredentialStore::loadPassword(
                freetunnel::CredentialStore::keyForConfigPath(src));
    const QString dest = fileUrl.startsWith(QLatin1String("file:"))
            ? QUrl(fileUrl).toLocalFile() : fileUrl;
    if (dest.isEmpty())
        return false;
    QFile out(dest);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    out.write(freetunnel::buildConfigToml(c).toUtf8());
    out.close();
    QFile::setPermissions(dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
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
