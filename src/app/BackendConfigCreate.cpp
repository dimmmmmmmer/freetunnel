// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVariantMap>

#include "BackendConfigShared.h"
#include "core/AppSettings.h"
#include "core/ConfigImport.h"
#include "core/ConfigPaths.h"
#include "core/ConfigStore.h"
#include "core/ConfigToml.h"
#include "core/CredentialStore.h"

namespace {

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
    if (!freetunnel::backend_config::validateDnsList(ct.dns)) {
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

bool assignRequiredCreateFields(const QVariantMap &f, ParsedCreateConfig *out, QString *err)
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
    if (!freetunnel::backend_config::validateAddressList(out->ct.addresses)) {
        if (err)
            *err = QStringLiteral("bad_address");
        return false;
    }
    return true;
}

void assignOptionalCreateFields(const QVariantMap &f, ParsedCreateConfig *out)
{
    out->ct.protocol = f.value(QStringLiteral("protocol"), QStringLiteral("http2")).toString();
    out->ct.allowIpv6 = f.value(QStringLiteral("allowIpv6"), true).toBool();
    out->ct.skipVerification = f.value(QStringLiteral("skipVerification"), false).toBool();
    out->ct.antiDpi = f.value(QStringLiteral("antiDpi"), false).toBool();
    out->ct.certificate = f.value(QStringLiteral("certificate")).toString().trimmed();
    out->ct.dns = f.value(QStringLiteral("dns")).toString();
    out->ct.customSni = f.value(QStringLiteral("customSni")).toString();
    out->ct.clientRandom = f.value(QStringLiteral("clientRandom")).toString();
}

bool finalizeParsedCreateConfig(const QVariantMap &f, ParsedCreateConfig *out, QString *err)
{
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    out->password = out->ct.password;
    out->ct.password.clear();
    out->safeName = freetunnel::sanitizeConfigBaseName(
            name.isEmpty() ? out->ct.hostname : name, QStringLiteral("config"));
    return true;
}

bool parseCreateConfigFields(const QVariantMap &f, ParsedCreateConfig *out, QString *err)
{
    if (!assignRequiredCreateFields(f, out, err))
        return false;
    assignOptionalCreateFields(f, out);
    if (!validateCreateOptionalFields(out->ct, err))
        return false;
    return finalizeParsedCreateConfig(f, out, err);
}

} // namespace

void Backend::emitCreateConfigError(const QString &parseErr)
{
    if (parseErr == QLatin1String("missing_fields"))
        emit errorOccurred(tr("Fill in host, address, username and password"));
    else if (parseErr == QLatin1String("bad_address"))
        emit errorOccurred(tr("Address must be host:port, e.g. 1.2.3.4:443"));
    else if (parseErr == QLatin1String("bad_dns"))
        emit errorOccurred(tr("DNS must be an IP or DoT/DoH URL (e.g. 1.1.1.1, tls://8.8.8.8)"));
    else if (parseErr == QLatin1String("bad_client_random"))
        emit errorOccurred(tr("Client random must be hexadecimal"));
}

bool Backend::createConfig(const QVariantMap &f)
{
    ParsedCreateConfig parsed;
    QString parseErr;
    if (!parseCreateConfigFields(f, &parsed, &parseErr)) {
        emitCreateConfigError(parseErr);
        return false;
    }

    const QString tomlBody = freetunnel::buildConfigToml(parsed.ct);
    const int editIndex = f.value(QStringLiteral("editIndex"), -1).toInt();
    const EditSnapshot edit = snapshotForEdit(editIndex, m_paths, m_settings);
    const QString &oldPath = edit.oldPath;

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    const QString target = freetunnel::ownerConfigPathForSave(parsed.safeName, oldPath);
    if (!freetunnel::backend_config::writeConfigFile(target, tomlBody.toUtf8())) {
        emit errorOccurred(tr("Could not write config"));
        return false;
    }
    if (!freetunnel::backend_config::storeConfigPassword(target, parsed.password)) {
        emit errorOccurred(tr("Could not store the VPN password securely. Install "
                             "gnome-keyring or KWallet, then try again."));
        return false;
    }
    if (!oldPath.isEmpty() && oldPath != target)
        freetunnel::CredentialStore::deletePassword(freetunnel::CredentialStore::keyForConfigPath(oldPath));

    CreatedConfigFinalize ctx;
    ctx.form = f;
    ctx.oldPath = oldPath;
    ctx.target = target;
    ctx.password = parsed.password;
    ctx.tomlBody = tomlBody;
    ctx.editContent = edit.content;
    ctx.editPassword = edit.password;
    ctx.editProfile = edit.profile;
    ctx.editingSnapshot = edit.active;
    ctx.editIndex = editIndex;
    return finalizeCreatedConfig(ctx);
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
    freetunnel::backend_config::updateStoredConfigList(stored, oldPath, target);
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

void Backend::maybeReapplyCreatedConfig(const CreatedConfigFinalize &ctx)
{
    const QString newProfile = normalizedSplitProfile(ctx.form, m_settings);
    assignSplitProfile(m_settings, ctx.oldPath, ctx.target, newProfile);
    persistSettings();
    emit configChanged();

    const bool editing = ctx.editIndex >= 0;
    const bool noChange = ctx.editingSnapshot && ctx.oldPath == ctx.target
            && ctx.tomlBody == ctx.editContent && ctx.password == ctx.editPassword
            && newProfile == ctx.editProfile;
    if (editing && m_activePath == ctx.target && !noChange) {
        applySplitRules();
        reapplyIfConnected();
    }
}

bool Backend::finalizeCreatedConfig(const CreatedConfigFinalize &ctx)
{
    const bool editing = ctx.editIndex >= 0;
    const bool wasActive = !ctx.oldPath.isEmpty() && m_activePath == ctx.oldPath;
    persistCreatedConfigPaths(ctx.oldPath, ctx.target, editing, wasActive);
    maybeReapplyCreatedConfig(ctx);
    return true;
}
