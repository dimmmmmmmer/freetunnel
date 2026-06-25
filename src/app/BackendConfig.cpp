// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QAbstractSocket>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"
#include "core/DeepLink.h"
#include "core/NetBind.h"

#include <algorithm>
#include <iterator>

namespace {

bool validateAddressList(const QString &addresses)
{
    const QStringList addrs = addresses.split(QLatin1Char(','), Qt::SkipEmptyParts);
    return std::all_of(addrs.cbegin(), addrs.cend(), [](const QString &raw) {
        const QString a = raw.trimmed();
        const int colon = a.lastIndexOf(QLatin1Char(':'));
        bool portOk = false;
        const int port = colon >= 0 ? a.mid(colon + 1).toInt(&portOk) : 0;
        return colon > 0 && portOk && port >= 1 && port <= 65535;
    });
}

bool validateDnsList(const QString &dns)
{
    static const QRegularExpression dnsScheme(
        QStringLiteral("^(tls|https|quic|h3|sdns|udp|tcp)://"), QRegularExpression::CaseInsensitiveOption);
    const QStringList dnsList = dns.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                         Qt::SkipEmptyParts);
    return std::all_of(dnsList.cbegin(), dnsList.cend(), [&](const QString &raw) {
        const QString d = raw.trimmed();
        if (dnsScheme.match(d).hasMatch())
            return true;
        QString host = d;
        const int colon = host.lastIndexOf(QLatin1Char(':'));
        if (colon > 0 && !host.contains(QLatin1Char('[')) && host.count(QLatin1Char(':')) == 1)
            host = host.left(colon);
        return !QHostAddress(host).isNull();
    });
}

struct EditSnapshot {
    QString oldPath;
    QString content;
    QString password;
    QString profile;
    bool active = false;
};

EditSnapshot snapshotForEdit(int editIndex, const QStringList &paths, const AppSettings &settings)
{
    EditSnapshot snap;
    if (editIndex < 0 || editIndex >= paths.size())
        return snap;
    snap.oldPath = paths.at(editIndex);
    snap.active = true;
    QFile of(snap.oldPath);
    if (of.open(QIODevice::ReadOnly | QIODevice::Text))
        snap.content = QString::fromUtf8(of.readAll());
    snap.password = freetunnel::CredentialStore::loadPassword(
            freetunnel::CredentialStore::keyForConfigPath(snap.oldPath));
    snap.profile = settings.config_profiles.value(snap.oldPath);
    if (snap.profile.isEmpty() || !settings.profiles.contains(snap.profile))
        snap.profile = QStringLiteral("Default");
    return snap;
}

bool writeConfigFile(const QString &target, const QByteArray &body)
{
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(body);
    file.close();
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

bool storeConfigPassword(const QString &target, const QString &password)
{
    freetunnel::CredentialStore::storePassword(freetunnel::CredentialStore::keyForConfigPath(target),
                                               password);
    if (!password.isEmpty()
        && freetunnel::CredentialStore::loadPassword(
                   freetunnel::CredentialStore::keyForConfigPath(target))
                   .isEmpty()) {
        QFile::remove(target);
        return false;
    }
    return true;
}

void updateStoredConfigList(QStringList &stored, const QString &oldPath, const QString &target)
{
    const int storedIdx = oldPath.isEmpty() ? -1 : stored.indexOf(oldPath);
    if (storedIdx >= 0) {
        stored[storedIdx] = target;
        stored.removeDuplicates();
    } else if (!stored.contains(target)) {
        stored.prepend(target);
    }
}

QString normalizedSplitProfile(const QVariantMap &f, const AppSettings &settings)
{
    QString profile = f.value(QStringLiteral("splitProfile"), QStringLiteral("Default")).toString();
    if (!settings.profiles.contains(profile))
        profile = QStringLiteral("Default");
    return profile;
}

void assignSplitProfile(AppSettings &settings, const QString &oldPath, const QString &target,
                        const QString &profile)
{
    if (!oldPath.isEmpty() && oldPath != target)
        settings.config_profiles.remove(oldPath);
    if (profile == QLatin1String("Default"))
        settings.config_profiles.remove(target);
    else
        settings.config_profiles[target] = profile;
}

struct ParsedCreateConfig {
    freetunnel::ConfigToml ct;
    QString password;
    QString safeName;
};

bool validateCreateOptionalFields(const freetunnel::ConfigToml &ct, QString *err)
{
    if (!validateDnsList(ct.dns)) {
        if (err)
            *err = QStringLiteral("bad_dns");
        return false;
    }
    const QString cr = ct.clientRandom.trimmed();
    if (!cr.isEmpty() && !QRegularExpression(QStringLiteral("^[0-9a-fA-F]+$")).match(cr).hasMatch()) {
        if (err)
            *err = QStringLiteral("bad_client_random");
        return false;
    }
    return true;
}

bool parseCreateConfigFields(const QVariantMap &f, ParsedCreateConfig *out, QString *err)
{
    out->ct.hostname = f.value(QStringLiteral("hostname")).toString().trimmed();
    out->ct.addresses = f.value(QStringLiteral("addresses")).toString().trimmed();
    out->ct.username = f.value(QStringLiteral("username")).toString().trimmed();
    out->ct.password = f.value(QStringLiteral("password")).toString();
    if (out->ct.hostname.isEmpty() || out->ct.addresses.isEmpty() || out->ct.username.isEmpty()
            || out->ct.password.isEmpty()) {
        if (err)
            *err = QStringLiteral("missing_fields");
        return false;
    }
    if (!validateAddressList(out->ct.addresses)) {
        if (err)
            *err = QStringLiteral("bad_address");
        return false;
    }
    out->ct.protocol = f.value(QStringLiteral("protocol"), QStringLiteral("http2")).toString();
    out->ct.allowIpv6 = f.value(QStringLiteral("allowIpv6"), true).toBool();
    out->ct.certificate = f.value(QStringLiteral("certificate")).toString().trimmed();
    out->ct.dns = f.value(QStringLiteral("dns")).toString();
    out->ct.customSni = f.value(QStringLiteral("customSni")).toString();
    out->ct.clientRandom = f.value(QStringLiteral("clientRandom")).toString();
    if (!validateCreateOptionalFields(out->ct, err))
        return false;
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    out->password = out->ct.password;
    out->ct.password.clear();
    out->safeName = freetunnel::sanitizeConfigBaseName(
            name.isEmpty() ? out->ct.hostname : name, QStringLiteral("config"));
    return true;
}

bool readValidatedImportContent(const QString &path, QString *contentOut, QString *errOut)
{
    QFile vf(path);
    if (!vf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errOut)
            *errOut = QStringLiteral("read");
        return false;
    }
    const QString content = QString::fromUtf8(vf.readAll());
    const freetunnel::ConfigToml c = freetunnel::parseConfigToml(content);
    if (c.addresses.trimmed().isEmpty() || c.username.trimmed().isEmpty()) {
        if (errOut)
            *errOut = QStringLiteral("invalid");
        return false;
    }
    if (contentOut)
        *contentOut = content;
    return true;
}

bool copyImportIntoAppConfigDir(const QString &content, const QString &sourcePath, QString *targetOut)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(base);
    const QString stem = freetunnel::sanitizeConfigBaseName(QFileInfo(sourcePath).completeBaseName());
    QString target = freetunnel::uniqueOwnerConfigPath(stem);
    if (QFileInfo(target).absoluteFilePath() == QFileInfo(sourcePath).absoluteFilePath())
        target = freetunnel::uniqueOwnerConfigPath(stem + QStringLiteral("-copy"));
    if (!writeConfigFile(target, content.toUtf8()))
        return false;
    if (targetOut)
        *targetOut = target;
    return true;
}

} // namespace

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

void Backend::markConfigPingFailed(int index)
{
    if (index < m_pings.size() && m_pings[index].toString() == QStringLiteral("…")) {
        m_pings[index] = QStringLiteral("—");
        emit pingsChanged();
    }
}

void Backend::runConfigPing(int index, const QHostAddress &ip, quint16 port)
{
    QTcpSocket *sock = freetunnel::makePhysicalBoundTcpSocket(this, ip.protocol());
    auto *elapsed = new QElapsedTimer();
    elapsed->start();
    connect(sock, &QTcpSocket::connected, this, [this, index, sock, elapsed]() {
        if (index < m_pings.size())
            m_pings[index] = QString::number(elapsed->elapsed()) + tr(" ms");
        delete elapsed;
        sock->abort();
        sock->deleteLater();
        emit pingsChanged();
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this, index, sock](QAbstractSocket::SocketError) {
        markConfigPingFailed(index);
        sock->deleteLater();
    });
    QTimer::singleShot(3000, sock, [this, index, sock]() {
        if (sock->state() != QAbstractSocket::ConnectedState) {
            markConfigPingFailed(index);
            sock->abort();
            sock->deleteLater();
        }
    });
    sock->connectToHost(ip, port);
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
    const QHostAddress literal(host);
    if (!literal.isNull()) {
        runConfigPing(index, literal, port);
        return;
    }
    QHostInfo::lookupHost(host, this, [this, index, port](const QHostInfo &hi) {
        if (hi.addresses().isEmpty())
            markConfigPingFailed(index);
        else
            runConfigPing(index, hi.addresses().first(), port);
    });
}

void Backend::pingConfigs() {
    m_pings.clear();
    for (int i = 0; i < m_paths.size(); ++i)
        m_pings << QStringLiteral("…");
    emit pingsChanged();
    for (int i = 0; i < m_paths.size(); ++i)
        pingConfigAtIndex(i);
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
    QString content;
    QString importErr;
    if (!readValidatedImportContent(p, &content, &importErr)) {
        if (importErr == QLatin1String("read"))
            emit errorOccurred(tr("Could not read the file"));
        else
            emit errorOccurred(tr("Not a valid TrustTunnel config (missing address or credentials)"));
        return false;
    }
    QString target;
    if (!copyImportIntoAppConfigDir(content, p, &target)) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored.prepend(target);
        saveStoredConfigs(stored);
    }
    const bool hadNoActive = m_activePath.isEmpty();
    reloadConfigs();
    if (!freetunnel::migrateConfigPassword(target)) {
        QFile::remove(target);
        stored.removeAll(target);
        saveStoredConfigs(stored);
        emit errorOccurred(tr("Could not store the VPN password securely. Install "
                             "gnome-keyring or KWallet, then try again."));
        return false;
    }
    if (hadNoActive) {
        m_activePath = target;
        m_settings.last_config_path = target;
        persistSettings();
        emit configChanged();
    }
    emit configImported(nameForPath(target));
    return true;
}

bool Backend::createConfig(const QVariantMap &f) {
    ParsedCreateConfig parsed;
    QString parseErr;
    if (!parseCreateConfigFields(f, &parsed, &parseErr)) {
        if (parseErr == QLatin1String("missing_fields"))
            emit errorOccurred(tr("Fill in host, address, username and password"));
        else if (parseErr == QLatin1String("bad_address"))
            emit errorOccurred(tr("Address must be host:port, e.g. 1.2.3.4:443"));
        else if (parseErr == QLatin1String("bad_dns"))
            emit errorOccurred(tr("DNS must be an IP or DoT/DoH URL (e.g. 1.1.1.1, tls://8.8.8.8)"));
        else if (parseErr == QLatin1String("bad_client_random"))
            emit errorOccurred(tr("Client random must be hexadecimal"));
        return false;
    }

    const QString tomlBody = freetunnel::buildConfigToml(parsed.ct);
    const int editIndex = f.value(QStringLiteral("editIndex"), -1).toInt();
    const EditSnapshot edit = snapshotForEdit(editIndex, m_paths, m_settings);
    const QString &oldPath = edit.oldPath;

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    const QString target = freetunnel::uniqueOwnerConfigPath(parsed.safeName);
    if (!writeConfigFile(target, tomlBody.toUtf8())) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    if (!storeConfigPassword(target, parsed.password)) {
        emit errorOccurred(tr("Could not store the VPN password securely. Install "
                             "gnome-keyring or KWallet, then try again."));
        return false;
    }
    if (!oldPath.isEmpty() && oldPath != target)
        freetunnel::CredentialStore::deletePassword(freetunnel::CredentialStore::keyForConfigPath(oldPath));

    return finalizeCreatedConfig(f, oldPath, target, parsed.password, tomlBody, edit.content, edit.password,
                                 edit.profile, edit.active, editIndex);
}

void Backend::persistCreatedConfigPaths(const QString &oldPath, const QString &target,
                                        bool editing, bool wasActive)
{
    if (!oldPath.isEmpty() && oldPath != target) {
        QFile::remove(oldPath);
        if (wasActive)
            m_activePath = target;
    }
    QStringList stored = loadStoredConfigs();
    updateStoredConfigList(stored, oldPath, target);
    saveStoredConfigs(stored);
    reloadConfigs();
    freetunnel::migrateConfigPassword(target);

    if (!editing) {
        m_activePath = target;
        m_settings.last_config_path = target;
    } else if (wasActive) {
        m_settings.last_config_path = m_activePath;
    }
    persistSettings();
}

void Backend::maybeReapplyCreatedConfig(const QVariantMap &f, const QString &oldPath,
                                        const QString &target, const QString &password,
                                        const QString &tomlBody, const QString &editContent,
                                        const QString &editPassword, const QString &editProfile,
                                        bool editingSnapshot, int editIndex)
{
    const QString newProfile = normalizedSplitProfile(f, m_settings);
    assignSplitProfile(m_settings, oldPath, target, newProfile);
    persistSettings();
    emit configChanged();

    const bool editing = editIndex >= 0;
    const bool noChange = editingSnapshot && oldPath == target && tomlBody == editContent
            && password == editPassword && newProfile == editProfile;
    if (editing && m_activePath == target && !noChange) {
        applySplitRules();
        reapplyIfConnected();
    }
}

bool Backend::finalizeCreatedConfig(const QVariantMap &f, const QString &oldPath, const QString &target,
                                    const QString &password, const QString &tomlBody,
                                    const QString &editContent, const QString &editPassword,
                                    const QString &editProfile, bool editingSnapshot, int editIndex)
{
    const bool editing = editIndex >= 0;
    const bool wasActive = !oldPath.isEmpty() && m_activePath == oldPath;
    persistCreatedConfigPaths(oldPath, target, editing, wasActive);
    maybeReapplyCreatedConfig(f, oldPath, target, password, tomlBody, editContent, editPassword,
                              editProfile, editingSnapshot, editIndex);
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
    if (!freetunnel::migrateConfigPassword(target)) {
        QFile::remove(target);
        emit errorOccurred(tr("Could not store the VPN password securely. Install "
                             "gnome-keyring or KWallet, then try again."));
        return false;
    }
    QStringList stored = loadStoredConfigs();
    if (!stored.contains(target)) {
        stored.prepend(target); // a newly added config goes to the top of the list
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
    emit configImported(nameForPath(target));
    return true;
}

void Backend::moveConfig(int from, int to) {
    QStringList stored = loadStoredConfigs();
    if (from < 0 || to < 0 || from >= stored.size() || to >= stored.size() || from == to)
        return;
    stored.move(from, to);
    saveStoredConfigs(stored);
    reloadConfigs();      // m_paths mirrors the stored order; rebuild names/pings
    emit configChanged(); // activeIndex follows the active path to its new row
}
