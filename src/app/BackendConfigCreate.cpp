// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantMap>

#include <optional>

#include "BackendConfigShared.h"
#include "SubscriptionController.h"
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
    QString authKey; // the config's client random auth key, as the store holds it
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
    snap.authKey = freetunnel::CredentialStore::loadPassword(
            freetunnel::CredentialStore::keyForConfigAuthKey(snap.oldPath));
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
    QString authKey; // the form's, trimmed; not in ct, which is what goes to disk
    QString safeName;
    // What was pasted as the certificate had something else in it, which is not
    // saved (see keepOnlyTheCertificates).
    bool certificateTrimmed = false;
};

bool validateCreateOptionalFields(const freetunnel::ConfigToml &ct, QString *err)
{
    if (!freetunnel::backend_config::validateDnsList(ct.dns)) {
        if (err)
            *err = QStringLiteral("bad_dns");
        return false;
    }
    // Hex, optionally with a mask after a slash ("prefix/mask"), as links carry it
    // and the core reads it, in whole bytes: the core quietly does without a part
    // it cannot decode, so an odd digit out used to save and then go unused.
    if (!freetunnel::isValidClientRandom(ct.clientRandom.trimmed())) {
        if (err)
            *err = QStringLiteral("bad_client_random");
        return false;
    }
    // The same for the auth key: the core quietly connects without a key it cannot
    // decode, and a server that checks one then turns the client away.
    if (!freetunnel::isValidClientRandomAuthKey(ct.clientRandomAuthKey)) {
        if (err)
            *err = QStringLiteral("bad_auth_key");
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
    out->ct.clientRandomAuthKey = f.value(QStringLiteral("authKey")).toString().trimmed();
}

// Only the certificates of what was pasted into the certificate field. A file
// that combines the certificate with its private key is as easy to paste whole
// as the certificate alone, and the key would be saved, then shared with every
// link and export made from the config. A field that holds no certificate at all
// is refused rather than saved as none, which would quietly drop the pin.
bool keepOnlyTheCertificates(ParsedCreateConfig *out, QString *err)
{
    if (out->ct.certificate.isEmpty())
        return true;
    const QString kept = freetunnel::certificatesOnly(out->ct.certificate, &out->certificateTrimmed);
    if (kept.trimmed().isEmpty()) {
        if (err)
            *err = QStringLiteral("bad_certificate");
        return false;
    }
    out->ct.certificate = kept;
    return true;
}

// The form owns the fields it shows, and nothing else. The file's own
// [listener.tun], and the keys and tables the editor has no field for, are the
// file's, and an edit keeps them. Built from the form alone, every Save, even one
// that changed nothing, wrote the default routes over a provider's own and
// dropped the rest: the config still connected, and quietly routed differently.
// post_quantum_group_enabled is read into a field of its own rather than kept as
// an unknown key, and the form has no switch for it, so it is carried over here
// too; a new config has no file, and gets the default.
// The one exception is a listener other than the tunnel, such as a SOCKS proxy:
// FreeTunnel runs the tunnel, and the core refuses a config that names two
// (see carryOverUnknownTables).
void keepWhatTheFormDoesNotShow(const QString &existingToml, freetunnel::ConfigToml *ct)
{
    const freetunnel::ConfigToml existing = freetunnel::parseConfigToml(existingToml);
    ct->tunSection = existing.tunSection;
    ct->extraRootKeys = existing.extraRootKeys;
    ct->extraEndpointKeys = existing.extraEndpointKeys;
    ct->extraSections = existing.extraSections;
    ct->postQuantum = existing.postQuantum;
}

// A renamed config's password and auth key move with it: stored under the new
// path by the save, and dropped from the old one here. A rename that only changed
// the letter case gives two keys the Windows credential store takes for one, so
// deleting the old took the new with it; it is put back.
void forgetOldPassword(const QString &oldPath, const QString &target, const QString &password,
                       const QString &authKey, const QString &previousAuthKey)
{
    using freetunnel::CredentialStore;
    const QString oldKey = CredentialStore::keyForConfigPath(oldPath);
    const QString newKey = CredentialStore::keyForConfigPath(target);
    CredentialStore::deletePassword(oldKey);
    if (oldKey.compare(newKey, Qt::CaseInsensitive) == 0 && !password.isEmpty())
        CredentialStore::storePassword(newKey, password);
    // A config that had no key has no entry to drop.
    if (previousAuthKey.isEmpty())
        return;
    const QString oldAuthKey = CredentialStore::keyForConfigAuthKey(oldPath);
    const QString newAuthKey = CredentialStore::keyForConfigAuthKey(target);
    CredentialStore::deletePassword(oldAuthKey);
    if (oldAuthKey.compare(newAuthKey, Qt::CaseInsensitive) == 0 && !authKey.isEmpty())
        CredentialStore::storePassword(newAuthKey, authKey);
}

// What the credential store holds for the file a save is about to write: the
// edited config's own password when the save goes over that file, and nothing
// when it makes a new one (a new config, or a rename).
QString passwordStoredFor(const QString &target, const EditSnapshot &edit)
{
    return freetunnel::namesTheSameFile(edit.oldPath, target) ? edit.password : QString();
}

// And the same for the auth key.
QString authKeyStoredFor(const QString &target, const EditSnapshot &edit)
{
    return freetunnel::namesTheSameFile(edit.oldPath, target) ? edit.authKey : QString();
}

// Whether a config file is the app's own copy, in its config directory.
// configs.json can name a file elsewhere (a very early build listed a picked
// .toml where it was), and that file is the user's, which is why removeConfig()
// leaves it on disk.
bool inAppConfigDir(const QString &path)
{
    const QString dir = QFileInfo(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                .absoluteFilePath();
    return QFileInfo(path).absolutePath() == dir;
}

// Cannot fail: naming is the last step and sanitizeConfigBaseName() always
// produces something, falling back to the hostname and then to a generated stem.
// Returns bool only so parseCreateConfigFields() reads as one chain of steps.
// A typed name is taken whole (createConfig() refuses one that is too long); a
// hostname standing in for one is cut to the limit, as a name nobody typed.
bool finalizeParsedCreateConfig(const QVariantMap &f, ParsedCreateConfig *out)
{
    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    out->password = out->ct.password;
    out->ct.password.clear();
    out->authKey = out->ct.clientRandomAuthKey;
    out->ct.clientRandomAuthKey.clear();
    out->safeName = freetunnel::sanitizeConfigBaseName(
            name.isEmpty() ? freetunnel::clippedConfigName(out->ct.hostname) : name,
            QStringLiteral("config"));
    return true;
}

// Whether a name is over the limit for the file it would make. A config that
// already has a longer name (links and files could give any before the limit)
// still saves under it: that file exists, so the name fits.
bool nameTooLongForANewFile(const QString &safeName, const QString &oldPath)
{
    if (safeName.toUcs4().size() <= freetunnel::kMaxConfigNameLength)
        return false;
    return oldPath.isEmpty() || QFileInfo(oldPath).completeBaseName() != safeName;
}

bool parseCreateConfigFields(const QVariantMap &f, ParsedCreateConfig *out, QString *err)
{
    if (!assignRequiredCreateFields(f, out, err))
        return false;
    assignOptionalCreateFields(f, out);
    if (!validateCreateOptionalFields(out->ct, err) || !keepOnlyTheCertificates(out, err))
        return false;
    return finalizeParsedCreateConfig(f, out);
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
        emit errorOccurred(tr("Client random must be hexadecimal in whole bytes (an even number of "
                              "digits, at most 64), optionally followed by /mask"));
    else if (parseErr == QLatin1String("bad_auth_key"))
        emit errorOccurred(tr("The auth key must be hexadecimal in whole bytes (an even number of digits)"));
    else if (parseErr == QLatin1String("bad_certificate"))
        emit errorOccurred(tr("The certificate must be in PEM format, as one or more BEGIN CERTIFICATE "
                              "blocks"));
    else if (parseErr == QLatin1String("name_too_long"))
        emit errorOccurred(tr("The name is too long: %1 characters at most")
                                   .arg(freetunnel::kMaxConfigNameLength));
}

// The row a save from the editor writes over: -1 for a new config, or nothing,
// once the error is said, when the config it was opened on has gone. Split out
// of createConfig().
std::optional<int> Backend::editIndexForSave(const QVariantMap &f)
{
    int editIndex = f.value(QStringLiteral("editIndex"), -1).toInt();
    // The editor opens on a row and saves minutes later, and the list is not
    // still under it: finalizeImportedConfig() prepends an imported config and
    // shifts every position by one. Nothing re-resolved the number, so a save
    // that landed after an import wrote the form over a DIFFERENT config —
    // silently, and over one the user had not opened.
    //
    // The path is what the editor actually means. The index stays as the
    // fallback for a save that carries no path (an editor opened before this
    // existed cannot, and neither can a test that predates it).
    const QString editPath = f.value(QStringLiteral("editPath")).toString();
    if (!editPath.isEmpty()) {
        editIndex = m_paths.indexOf(editPath);
        if (editIndex < 0) {
            emit errorOccurred(tr("That configuration is no longer there — it may have been "
                                  "deleted while you were editing it."));
            return std::nullopt;
        }
    }
    return editIndex;
}

bool Backend::createConfig(const QVariantMap &f)
{
    ParsedCreateConfig parsed;
    QString parseErr;
    if (!parseCreateConfigFields(f, &parsed, &parseErr)) {
        emitCreateConfigError(parseErr);
        return false;
    }

    const std::optional<int> resolvedIndex = editIndexForSave(f);
    if (!resolvedIndex)
        return false;
    const int editIndex = *resolvedIndex;
    const EditSnapshot edit = snapshotForEdit(editIndex, m_paths, m_settings);
    const QString &oldPath = edit.oldPath;
    if (nameTooLongForANewFile(parsed.safeName, oldPath)) {
        emitCreateConfigError(QStringLiteral("name_too_long"));
        return false;
    }
    keepWhatTheFormDoesNotShow(edit.content, &parsed.ct);
    // The certificate kept is the file's, which the form's trimming said nothing about.
    if (keepSubscriptionOwnedFields(oldPath, edit.content, edit.password, edit.authKey, &parsed.ct,
                                    &parsed.password, &parsed.authKey))
        parsed.certificateTrimmed = false;
    const QString tomlBody = freetunnel::buildConfigToml(parsed.ct);

    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    const QString target = freetunnel::ownerConfigPathForSave(parsed.safeName, oldPath);
    const bool newFile = !QFileInfo::exists(target);
    QString saveErr;
    if (!freetunnel::backend_config::saveConfigWithPassword(
                target, tomlBody.toUtf8(), parsed.password, passwordStoredFor(target, edit), parsed.authKey,
                authKeyStoredFor(target, edit), &saveErr)) {
        if (saveErr == QLatin1String("password")) {
            reportPasswordNotStored();
        } else if (saveErr == QLatin1String("authkey")) {
            recheckCredentialStorage();
            emit errorOccurred(freetunnel::backend_config::authKeyNotStoredMessage());
        } else {
            emit errorOccurred(tr("Could not write config"));
        }
        return false;
    }
    // A new file brings its own key or none: not one left at its path by a config
    // that was called the same. After the save, which is the credential store's
    // all-or-nothing step and has nothing to do with it.
    if (newFile && parsed.authKey.isEmpty())
        freetunnel::backend_config::dropLeftoverAuthKey(target);
    if (!oldPath.isEmpty() && oldPath != target)
        forgetOldPassword(oldPath, target, parsed.password, parsed.authKey, edit.authKey);

    CreatedConfigFinalize ctx;
    ctx.form = f;
    ctx.oldPath = oldPath;
    ctx.target = target;
    ctx.password = parsed.password;
    ctx.authKey = parsed.authKey;
    ctx.tomlBody = tomlBody;
    ctx.editContent = edit.content;
    ctx.editPassword = edit.password;
    ctx.editAuthKey = edit.authKey;
    ctx.editProfile = edit.profile;
    ctx.editingSnapshot = edit.active;
    ctx.editIndex = editIndex;
    // Said once the config is on disk, as a file import says it. The certificate
    // field would otherwise reopen without what was left out of it, unexplained.
    if (parsed.certificateTrimmed)
        emit errorOccurred(freetunnel::certificateExtraIgnoredNote());
    return finalizeCreatedConfig(ctx);
}

void Backend::persistCreatedConfigPaths(const QString &oldPath, const QString &target,
                                        bool editing, bool wasActive)
{
    if (!oldPath.isEmpty() && oldPath != target) {
        // A rename that only changed the letter case is one file where the file
        // system folds case, and removing the old spelling removed the new one.
        // And a config listed where the user keeps it is renamed into the app's
        // directory, as any new name is: the original there is the user's file,
        // and deleting it deleted their only copy of a config the app had merely
        // been pointed at.
        if (!freetunnel::namesTheSameFile(oldPath, target) && inAppConfigDir(oldPath))
            QFile::remove(oldPath);
        if (wasActive)
            m_activePath = target;
    }
    QStringList stored = loadStoredConfigs();
    freetunnel::backend_config::updateStoredConfigList(stored, oldPath, target);
    saveStoredConfigs(stored);
    reloadConfigs();
    // The link to a subscription goes with the config, once the list names it
    // there. A link whose config the list does not name is removed at the next
    // start, its address with it, and a list that could not be saved still
    // names the old path.
    if (!oldPath.isEmpty() && oldPath != target && m_paths.contains(target))
        m_subscriptions->rename(oldPath, target);
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
    // The config-to-profile assignment just moved, so what the tunnel would do
    // moved with it. See the note in Backend::selectConfig().
    emit splitChanged();

    const bool editing = ctx.editIndex >= 0;
    const bool noChange = ctx.editingSnapshot && ctx.oldPath == ctx.target
            && ctx.tomlBody == ctx.editContent && ctx.password == ctx.editPassword
            && ctx.authKey == ctx.editAuthKey && newProfile == ctx.editProfile;
    if (m_activePath != ctx.target)
        return;
    if (editing) {
        if (noChange)
            return;
        applySplitRules();
        // Connecting counts too. A connect that is failing on a wrong password
        // retries with the config it was given, so saving the fixed one only
        // helps if the attempt starts again from it.
        if (m_connected || m_connecting)
            reconnectActiveConfig();
        return;
    }
    // Creating a config makes it the active one (persistCreatedConfigPaths). If a
    // tunnel is up it is still carrying the PREVIOUS server, so the "connected"
    // badge would sit on a config we aren't actually talking to — rebuild it, the
    // same way selectConfig() does when the user switches by hand.
    applySplitRules();
    if (m_connected || m_connecting)
        reconnectActiveConfig();
}

bool Backend::finalizeCreatedConfig(const CreatedConfigFinalize &ctx)
{
    const bool editing = ctx.editIndex >= 0;
    const bool wasActive = !ctx.oldPath.isEmpty() && m_activePath == ctx.oldPath;
    persistCreatedConfigPaths(ctx.oldPath, ctx.target, editing, wasActive);
    maybeReapplyCreatedConfig(ctx);
    return true;
}
