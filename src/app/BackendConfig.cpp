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
#include <QSslSocket>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QUrl>
#include <QVariantMap>

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

void Backend::runConfigPing(int index, const QString &sniHost, const QHostAddress &ip, quint16 port)
{
    const bool activeWhileConnected = m_connected && index >= 0 && index < m_paths.size()
            && m_paths.at(index) == m_activePath;
    // TLS-handshake probe: a host that accepts a bare TCP connection but can't
    // complete the secure handshake (wrong/closed listener, not actually up) no
    // longer reports a misleading latency. Verification is off — we time the
    // handshake, we don't validate the certificate.
    auto *sock = new QSslSocket(this);
    sock->setPeerVerifyMode(QSslSocket::VerifyNone);
    // While connected, the active server is reached through the tunnel exclusion
    // path; binding to the physical IF often fails or times out on Windows.
    if (!activeWhileConnected)
        freetunnel::bindSocketToPhysicalRoute(sock, ip.protocol());
    QElapsedTimer elapsed;
    elapsed.start();
    connect(sock, &QSslSocket::encrypted, this, [this, index, sock, elapsed]() {
        if (index < m_pings.size())
            m_pings[index] = QString::number(elapsed.elapsed()) + tr(" ms");
        sock->abort();
        sock->deleteLater();
        emit pingsChanged();
    });
    connect(sock, &QSslSocket::errorOccurred, this, [this, index, sock](QAbstractSocket::SocketError) {
        markConfigPingFailed(index);
        sock->deleteLater();
    });
    QTimer::singleShot(5000, sock, [this, index, sock]() {
        if (!sock->isEncrypted()) {
            markConfigPingFailed(index);
            sock->abort();
            sock->deleteLater();
        }
    });
    sock->connectToHostEncrypted(ip.toString(), port, sniHost);
}

void Backend::pingConfigAtIndex(int index)
{
    const QString addr = firstAddress(m_paths.at(index));
    const int colon = addr.lastIndexOf(':');
    if (colon < 0) {
        markConfigPingFailed(index);
        return;
    }
    const QString host = addr.left(colon);
    const quint16 port = addr.mid(colon + 1).toUShort();
    // SNI for the handshake: prefer the config's custom SNI, then its hostname,
    // then the address host. Many servers only complete the handshake for the
    // expected name (domain fronting / anti-DPI), so a bare IP would mislead.
    const QVariantMap f = configFields(index);
    QString sni = f.value(QStringLiteral("customSni")).toString().trimmed();
    if (sni.isEmpty())
        sni = f.value(QStringLiteral("hostname")).toString().trimmed();
    if (sni.isEmpty())
        sni = host;
    const QHostAddress literal(host);
    if (!literal.isNull()) {
        runConfigPing(index, sni, literal, port);
        return;
    }
    QHostInfo::lookupHost(host, this, [this, index, sni, port](const QHostInfo &hi) {
        if (hi.addresses().isEmpty())
            markConfigPingFailed(index);
        else
            runConfigPing(index, sni, hi.addresses().first(), port);
    });
}

void Backend::pingConfigs()
{
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
