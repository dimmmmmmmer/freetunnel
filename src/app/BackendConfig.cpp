#include "app/Backend.h"

#include <QAbstractSocket>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QUrl>
#include <QVariantMap>

#include "core/ConfigImport.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"

// Read the first endpoint "host:port" out of a config TOML.
static QString firstAddress(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(QStringLiteral("addresses\\s*=\\s*\\[\\s*\"([^\"]+)\""));
    const auto m = re.match(content);
    return m.hasMatch() ? m.captured(1) : QString();
}

void Backend::pingConfigs() {
    m_pings.clear();
    for (int i = 0; i < m_paths.size(); ++i)
        m_pings << QStringLiteral("…");
    emit pingsChanged();

    for (int i = 0; i < m_paths.size(); ++i) {
        const QString addr = firstAddress(m_paths.at(i));
        const int colon = addr.lastIndexOf(':');
        if (colon < 0) {
            m_pings[i] = QStringLiteral("—");
            emit pingsChanged();
            continue;
        }
        const QString host = addr.left(colon);
        const quint16 port = addr.mid(colon + 1).toUShort();
        auto *sock = new QTcpSocket(this);
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        connect(sock, &QTcpSocket::connected, this, [this, i, sock, t0]() {
            if (i < m_pings.size())
                m_pings[i] = QString::number(QDateTime::currentMSecsSinceEpoch() - t0)
                        + tr(" ms");
            sock->abort();
            sock->deleteLater();
            emit pingsChanged();
        });
        connect(sock, &QTcpSocket::errorOccurred, this,
                [this, i, sock](QAbstractSocket::SocketError) {
                    if (i < m_pings.size() && m_pings[i].toString() == QStringLiteral("…"))
                        m_pings[i] = QStringLiteral("—");
                    sock->deleteLater();
                    emit pingsChanged();
                });
        QTimer::singleShot(3000, sock, [this, i, sock]() {
            if (sock->state() != QAbstractSocket::ConnectedState) {
                if (i < m_pings.size() && m_pings[i].toString() == QStringLiteral("…")) {
                    m_pings[i] = QStringLiteral("—");
                    emit pingsChanged();
                }
                sock->abort();
                sock->deleteLater();
            }
        });
        sock->connectToHost(host, port);
    }
}

bool Backend::importFromClipboard() {
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        emit errorOccurred(tr("Clipboard is empty"));
        return false;
    }
    // Accept tt:// links and trusttunnel.org share links (…#tt=… / …?tt=…).
    if (text.startsWith(QStringLiteral("tt://")) || text.contains(QStringLiteral("tt=")))
        return importDeepLink(text);
    emit errorOccurred(tr("No tt:// link in the clipboard"));
    return false;
}

bool Backend::importFile(const QString &path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file://")))
        p = QUrl(p).toLocalFile();
    if (!QFileInfo::exists(p)) {
        emit errorOccurred(tr("File not found: %1").arg(p));
        return false;
    }
    // Validate it's a usable config TOML before importing.
    QString content;
    {
        QFile vf(p);
        if (!vf.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emit errorOccurred(tr("Could not read the file"));
            return false;
        }
        content = QString::fromUtf8(vf.readAll());
        const freetunnel::ConfigToml c = freetunnel::parseConfigToml(content);
        if (c.addresses.trimmed().isEmpty() || c.username.trimmed().isEmpty()) {
            emit errorOccurred(tr("Not a valid TrustTunnel config (missing address or credentials)"));
            return false;
        }
    }
    // Copy the config into our owner-only config dir rather than tracking the
    // external path: the original file may live in a world-readable location
    // and we can't guarantee its permissions. The password is then migrated
    // into the OS credential store, stripping it from the on-disk copy.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    QString safe;
    for (const QChar &c : QFileInfo(p).completeBaseName())
        safe += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('_');
    if (safe.isEmpty())
        safe = QStringLiteral("imported-%1").arg(QDateTime::currentSecsSinceEpoch());
    QString target = QDir(base).filePath(safe + QStringLiteral(".toml"));
    if (QFileInfo::exists(target) && QFileInfo(target).absoluteFilePath() != QFileInfo(p).absoluteFilePath())
        target = QDir(base).filePath(QStringLiteral("%1-%2.toml").arg(safe).arg(QDateTime::currentSecsSinceEpoch()));
    {
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit errorOccurred(tr("Could not write config"));
            return false;
        }
        out.write(content.toUtf8());
        out.close();
        QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored << target;
        saveStoredConfigs(stored);
    }
    const bool hadNoActive = m_activePath.isEmpty();
    reloadConfigs();
    freetunnel::migrateConfigPassword(target);
    if (hadNoActive) { // first config in an empty list — make it the active one
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
        emit configChanged();
    }
    return true;
}

bool Backend::createConfig(const QVariantMap &f) {
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    freetunnel::ConfigToml ct;
    ct.hostname = f.value(QStringLiteral("hostname")).toString().trimmed();
    ct.addresses = f.value(QStringLiteral("addresses")).toString().trimmed();
    ct.username = f.value(QStringLiteral("username")).toString().trimmed();
    ct.password = f.value(QStringLiteral("password")).toString();
    if (ct.hostname.isEmpty() || ct.addresses.isEmpty() || ct.username.isEmpty() || ct.password.isEmpty()) {
        emit errorOccurred(tr("Fill in host, address, username and password"));
        return false;
    }
    // Each address must be host:port (e.g. 1.2.3.4:443 or [::1]:443).
    const QStringList addrs = ct.addresses.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &raw : addrs) {
        const QString a = raw.trimmed();
        const int colon = a.lastIndexOf(QLatin1Char(':'));
        bool portOk = false;
        const int port = colon >= 0 ? a.mid(colon + 1).toInt(&portOk) : 0;
        if (colon <= 0 || !portOk || port < 1 || port > 65535) {
            emit errorOccurred(tr("Address must be host:port, e.g. 1.2.3.4:443"));
            return false;
        }
    }
    ct.protocol = f.value(QStringLiteral("protocol"), QStringLiteral("http2")).toString();
    ct.allowIpv6 = f.value(QStringLiteral("allowIpv6"), true).toBool();
    ct.certificate = f.value(QStringLiteral("certificate")).toString().trimmed();
    ct.dns = f.value(QStringLiteral("dns")).toString();
    ct.customSni = f.value(QStringLiteral("customSni")).toString();
    ct.clientRandom = f.value(QStringLiteral("clientRandom")).toString();
    ct.socks5 = f.value(QStringLiteral("socks5"), false).toBool();
    ct.socksListen = f.value(QStringLiteral("socksListen")).toString().trimmed();
    ct.socksUser = f.value(QStringLiteral("socksUser")).toString().trimmed();
    ct.socksPass = f.value(QStringLiteral("socksPass")).toString();
    if (ct.socks5) {
        // SOCKS listen must be host:port; default the whole thing if left blank.
        if (ct.socksListen.isEmpty())
            ct.socksListen = QStringLiteral("127.0.0.1:1080");
        const int colon = ct.socksListen.lastIndexOf(QLatin1Char(':'));
        bool portOk = false;
        const int port = colon >= 0 ? ct.socksListen.mid(colon + 1).toInt(&portOk) : 0;
        if (colon <= 0 || !portOk || port < 1 || port > 65535) {
            emit errorOccurred(tr("SOCKS listen must be host:port, e.g. 127.0.0.1:1080"));
            return false;
        }
    }
    // DNS servers (when given): plain IP, or an encrypted-DNS URL
    // (tls://, https://, quic://, h3://, sdns://, udp://, tcp://).
    const QStringList dnsList = ct.dns.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                            Qt::SkipEmptyParts);
    static const QRegularExpression dnsScheme(
        QStringLiteral("^(tls|https|quic|h3|sdns|udp|tcp)://"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &raw : dnsList) {
        const QString d = raw.trimmed();
        if (dnsScheme.match(d).hasMatch())
            continue; // an encrypted-DNS endpoint URL — accept as-is
        // Otherwise a bare IP (optionally with :port).
        QString host = d;
        const int colon = host.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && !host.contains(QLatin1Char('[')) && host.count(QLatin1Char(':')) == 1)
            host = host.left(colon);
        if (QHostAddress(host).isNull()) {
            emit errorOccurred(tr("DNS must be an IP or DoT/DoH URL (e.g. 1.1.1.1, tls://8.8.8.8)"));
            return false;
        }
    }
    // Client random (when given) must be a hex string.
    const QString cr = ct.clientRandom.trimmed();
    if (!cr.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(cr).hasMatch()) {
        emit errorOccurred(tr("Client random must be hexadecimal"));
        return false;
    }
    const QString password = ct.password;
    ct.password.clear();
    const QString t = freetunnel::buildConfigToml(ct);
    const QString hostname = ct.hostname;

    QString safe;
    const QString src = name.isEmpty() ? hostname : name;
    for (const QChar &c : src)
        safe += (c.isLetterOrNumber() || c == '.' || c == '-' || c == '_') ? c : QChar('_');
    if (safe.isEmpty())
        safe = QStringLiteral("config-%1").arg(QDateTime::currentSecsSinceEpoch());

    const int editIndex = f.value(QStringLiteral("editIndex"), -1).toInt();
    const QString oldPath = (editIndex >= 0 && editIndex < m_paths.size())
            ? m_paths.at(editIndex) : QString();

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(safe + QStringLiteral(".toml"));
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    file.write(t.toUtf8());
    file.close();
    // Restrict the config to the owner. Passwords live in the OS credential store.
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    freetunnel::CredentialStore::storePassword(freetunnel::CredentialStore::keyForConfigPath(target),
                                               password);
    if (!oldPath.isEmpty() && oldPath != target)
        freetunnel::CredentialStore::deletePassword(freetunnel::CredentialStore::keyForConfigPath(oldPath));

    QStringList stored = loadStoredConfigs();
    const bool editing = editIndex >= 0;
    const int storedIdx = oldPath.isEmpty() ? -1 : stored.indexOf(oldPath);
    const bool wasActive = !oldPath.isEmpty() && m_activePath == oldPath;
    if (!oldPath.isEmpty() && oldPath != target) { // edit + rename
        QFile::remove(oldPath);
        if (wasActive) m_activePath = target;
    }
    if (storedIdx >= 0) {
        stored[storedIdx] = target; // keep the list position when editing
        stored.removeDuplicates();
    } else if (!stored.contains(target)) {
        stored << target; // a brand-new config is appended
    }
    saveStoredConfigs(stored);
    reloadConfigs();
    // Move the just-written password into the keychain immediately so it never
    // lingers as plaintext in the .toml (imports already do this).
    freetunnel::migrateConfigPassword(target);
    if (!editing) {
        // A newly created config becomes the active selection.
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
    } else if (wasActive) {
        m_settings.last_config_path = m_activePath;
        persistSettings();
    }

    // Per-config split profile assignment (Default is the implicit, unstored value).
    {
        QString prof = f.value(QStringLiteral("splitProfile"), QStringLiteral("Default")).toString();
        if (!m_settings.profiles.contains(prof))
            prof = QStringLiteral("Default");
        if (!oldPath.isEmpty() && oldPath != target)
            m_settings.config_profiles.remove(oldPath);
        if (prof == QLatin1String("Default"))
            m_settings.config_profiles.remove(target);
        else
            m_settings.config_profiles[target] = prof;
        persistSettings();
    }

    emit configChanged();
    // If the edited config is the live one, its profile may have changed.
    if (editing && m_activePath == target) {
        applySplitRules();
        reapplyIfConnected();
    }
    return true;
}

QVariantMap Backend::configFields(int index) const {
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
    f[QStringLiteral("socks5")] = c.socks5;
    f[QStringLiteral("socksListen")] = c.socksListen;
    f[QStringLiteral("socksUser")] = c.socksUser;
    f[QStringLiteral("socksPass")] = c.socksPass;
    // Which split-tunnel profile this config uses (Default when unassigned).
    const QString prof = m_settings.config_profiles.value(configPath);
    f[QStringLiteral("splitProfile")] =
            (prof.isEmpty() || !m_settings.profiles.contains(prof)) ? QStringLiteral("Default") : prof;
    return f;
}

// Decode every PEM CERTIFICATE block into concatenated DER (deep links carry
// raw DER; our stored TOML keeps PEM).
static QByteArray pemCertsToDer(const QString &pem) {
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

QString Backend::configDeepLink(int index) const {
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
    // Note: the SOCKS listener is a local-only setting and is not part of the
    // tt:// schema, so a SOCKS config exports as a plain (TUN) endpoint link.
}

bool Backend::exportConfigToml(int index, const QString &fileUrl) const {
    if (index < 0 || index >= m_paths.size())
        return false;
    const QString src = m_paths.at(index);
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    freetunnel::ConfigToml c = freetunnel::parseConfigToml(QString::fromUtf8(in.readAll()));
    // Inject the password so the exported file is usable on its own.
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
    // The file carries credentials — owner-only.
    QFile::setPermissions(dest, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool Backend::importDeepLink(const QString &link) {
    QString err;
    auto prepared = freetunnel::prepareDeepLinkImport(link, &err);
    if (!prepared) {
        emit errorOccurred(tr("Link error: %1").arg(err));
        return false;
    }
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString target = QDir(base).filePath(prepared->fileName);
    QFile f(target);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    f.write(prepared->tomlContent.toUtf8());
    f.close();
    // Imported config carries credentials — owner-only (see createConfig).
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    freetunnel::migrateConfigPassword(target);
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored << target;
        saveStoredConfigs(stored);
    }
    const bool hadNoActive = m_activePath.isEmpty();
    reloadConfigs();
    if (hadNoActive) { // first config in an empty list — make it the active one
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
        emit configChanged();
    }
    return true;
}
