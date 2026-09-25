// cppcheck-suppress-file missingIncludeSystem
#include "AppSettings.h"

#include <QSettings>
#include <QStandardPaths>

#include "core/BypassRules.h"

static QString defaultLogPath() {
    // Keep the log in the per-user app data dir (owner-only). A predictable,
    // world-writable path like /tmp/freetunnel.log lets another local user
    // pre-create a symlink that we'd then append to (and chmod 0600).
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/freetunnel.log";
}

// The pre-1.0.5 default on macOS/Linux. Migrated to defaultLogPath() on load.
static QString legacyTmpLogPath() {
    return QStringLiteral("/tmp/freetunnel.log");
}

QStringList defaultExcludedRoutes() {
    // Private / special-use IPv4 ranges that should never be tunnelled
    // (RFC1918 / CGNAT / link-local / etc.).
    return {
        QStringLiteral("10.0.0.0/8"),       QStringLiteral("100.64.0.0/10"),
        QStringLiteral("169.254.0.0/16"),   QStringLiteral("172.16.0.0/12"),
        QStringLiteral("192.0.0.0/24"),     QStringLiteral("192.168.0.0/16"),
        QStringLiteral("255.255.255.255/32")};
}

QStringList recommendedRussiaDomains() {
    // RU services that TrustTunnel's DOMAIN_FILTER accepts (no TLD-only *.ru / .ru).
    return sanitizedBypassRules({
        QStringLiteral("yandex.ru"), QStringLiteral("*.yandex.ru"), QStringLiteral("ya.ru"),
        QStringLiteral("*.ya.ru"), QStringLiteral("yastatic.net"), QStringLiteral("*.yastatic.net"),
        QStringLiteral("yandex.net"), QStringLiteral("*.yandex.net"),
        QStringLiteral("vk.com"), QStringLiteral("*.vk.com"), QStringLiteral("vk.ru"),
        QStringLiteral("*.vk.ru"), QStringLiteral("vkontakte.ru"), QStringLiteral("*.vkontakte.ru"),
        QStringLiteral("mail.ru"), QStringLiteral("*.mail.ru"), QStringLiteral("list.ru"),
        QStringLiteral("*.list.ru"), QStringLiteral("bk.ru"), QStringLiteral("*.bk.ru"),
        QStringLiteral("inbox.ru"), QStringLiteral("*.inbox.ru"),
        QStringLiteral("ok.ru"), QStringLiteral("*.ok.ru"),
        QStringLiteral("rambler.ru"), QStringLiteral("*.rambler.ru"), QStringLiteral("lenta.ru"),
        QStringLiteral("*.lenta.ru"), QStringLiteral("ria.ru"), QStringLiteral("*.ria.ru"),
        QStringLiteral("tass.ru"), QStringLiteral("*.tass.ru"),
        QStringLiteral("gosuslugi.ru"), QStringLiteral("*.gosuslugi.ru"), QStringLiteral("mos.ru"),
        QStringLiteral("*.mos.ru"), QStringLiteral("api.mos.ru"), QStringLiteral("parking.mos.ru"),
        QStringLiteral("avito.ru"), QStringLiteral("*.avito.ru"), QStringLiteral("cian.ru"),
        QStringLiteral("*.cian.ru"),
        QStringLiteral("ozon.ru"), QStringLiteral("*.ozon.ru"), QStringLiteral("wildberries.ru"),
        QStringLiteral("*.wildberries.ru"),
        QStringLiteral("nalog.gov.ru"), QStringLiteral("*.nalog.gov.ru"), QStringLiteral("*.gov.ru"),
        QStringLiteral("pfr.gov.ru"), QStringLiteral("*.pfr.gov.ru"), QStringLiteral("fss.ru"),
        QStringLiteral("*.fss.ru"), QStringLiteral("rosreestr.gov.ru"), QStringLiteral("*.rosreestr.gov.ru"),
        QStringLiteral("gibdd.ru"), QStringLiteral("*.gibdd.ru"), QStringLiteral("мвд.рф"),
        QStringLiteral("*.мвд.рф"),
        QStringLiteral("cbr.ru"), QStringLiteral("*.cbr.ru"), QStringLiteral("minfin.gov.ru"),
        QStringLiteral("*.minfin.gov.ru"),
        QStringLiteral("government.ru"), QStringLiteral("*.government.ru"), QStringLiteral("kremlin.ru"),
        QStringLiteral("*.kremlin.ru"),
        QStringLiteral("rospotrebnadzor.ru"), QStringLiteral("*.rospotrebnadzor.ru"),
        QStringLiteral("tbank.ru"), QStringLiteral("*.tbank.ru"), QStringLiteral("tinkoff.ru"),
        QStringLiteral("*.tinkoff.ru"), QStringLiteral("tinkoff.com"), QStringLiteral("*.tinkoff.com"),
        QStringLiteral("sberbank.ru"), QStringLiteral("*.sberbank.ru"), QStringLiteral("sber.ru"),
        QStringLiteral("*.sber.ru"),
        QStringLiteral("vtb.ru"), QStringLiteral("*.vtb.ru"),
        QStringLiteral("alfabank.ru"), QStringLiteral("*.alfabank.ru"), QStringLiteral("alfabank.com"),
        QStringLiteral("*.alfabank.com"),
        QStringLiteral("raiffeisen.ru"), QStringLiteral("*.raiffeisen.ru"),
        QStringLiteral("gazprombank.ru"), QStringLiteral("*.gazprombank.ru"),
        QStringLiteral("open.ru"), QStringLiteral("*.open.ru"),
        QStringLiteral("psbank.ru"), QStringLiteral("*.psbank.ru"),
        QStringLiteral("rosselkhozbank.ru"), QStringLiteral("*.rosselkhozbank.ru"),
        QStringLiteral("sovcombank.ru"), QStringLiteral("*.sovcombank.ru")});
}

static QStringList domainsForProfileName(const QSettings &s, const QString &n,
                                         const QStringList &names, const QStringList &legacy)
{
    const QString key = QStringLiteral("bypass/profile/") + n;
    if (s.contains(key))
        return s.value(key).toStringList();
    if (n == QStringLiteral("Default")) {
        if (!legacy.isEmpty() && names == QStringList{QStringLiteral("Default")})
            return legacy;
        return recommendedRussiaDomains();
    }
    return {};
}

static void loadProfileMap(const QSettings &s, AppSettings &out)
{
    const QStringList names = s.value("bypass/profile_names", QStringList{"Default"}).toStringList();
    const QStringList legacy = s.value("bypass/rules", QStringList{}).toStringList();
    out.profiles.clear();
    for (const QString &n : names)
        out.profiles.insert(n, sanitizedBypassRules(domainsForProfileName(s, n, names, legacy)));
    if (out.profiles.isEmpty())
        out.profiles.insert(QStringLiteral("Default"), recommendedRussiaDomains());
    if (!legacy.isEmpty() && out.profiles.value(QStringLiteral("Default")).isEmpty()
            && names == QStringList{QStringLiteral("Default")})
        out.profiles[QStringLiteral("Default")] = sanitizedBypassRules(legacy);
}

static QStringList mergedProfileOrder(const QSettings &s, const QStringList &names,
                                      const AppSettings &out)
{
    QStringList order;
    for (const QString &n : s.value("bypass/profile_order", names).toStringList()) {
        if (out.profiles.contains(n) && !order.contains(n))
            order << n;
    }
    for (const QString &n : names) {
        if (!order.contains(n))
            order << n;
    }
    if (!order.contains(QStringLiteral("Default")))
        order.prepend(QStringLiteral("Default"));
    return order;
}

static void loadConfigProfileAssignments(const QSettings &s, AppSettings &out)
{
    const QVariantMap cp = s.value("bypass/config_profiles").toMap();
    for (auto it = cp.constBegin(); it != cp.constEnd(); ++it)
        out.config_profiles.insert(it.key(), it.value().toString());
}

static void loadProfileAppRules(const QSettings &s, AppSettings &out)
{
    // Before 1.2.0 there was one application list for the whole program, under
    // routing/app_rules, and it applied whichever profile you were on. Moving it
    // into the profiles must not read as "my rules are gone", so until the first
    // save in the new shape that one list becomes the starting list of every
    // profile — which is exactly the behaviour it used to have.
    //
    // The marker is what makes the seeding one-time. Without it a profile whose
    // application list the user had deliberately emptied would be refilled from
    // the old key on every launch.
    const bool seeded = s.value("bypass/profile_apps_seeded", false).toBool();
    const QStringList legacy = s.value("routing/app_rules").toStringList();
    out.profile_app_rules.clear();
    for (auto it = out.profiles.constBegin(); it != out.profiles.constEnd(); ++it) {
        const QStringList rules = seeded
                ? s.value(QStringLiteral("bypass/profile_apps/") + it.key()).toStringList()
                : legacy;
        // Stored as written, the way the single list was: validation happens
        // where a rule is added, and re-deriving it here would drag the whole
        // AppRules unit into every target that only wants to read settings.
        out.profile_app_rules.insert(it.key(), rules);
    }
}

static void loadProfileOrderAndAssignments(const QSettings &s, AppSettings &out)
{
    out.active_profile = s.value("bypass/active_profile", QStringLiteral("Default")).toString();
    if (!out.profiles.contains(out.active_profile))
        out.active_profile = out.profiles.firstKey();
    out.domain_bypass_rules = out.profiles.value(out.active_profile);
    out.app_rules = out.profile_app_rules.value(out.active_profile);

    const QStringList names = out.profiles.keys();
    out.profile_order = mergedProfileOrder(s, names, out);
    loadConfigProfileAssignments(s, out);
}

static void loadBypassProfiles(const QSettings &s, AppSettings &out)
{
    loadProfileMap(s, out);
    loadProfileAppRules(s, out);
    loadProfileOrderAndAssignments(s, out);
}

static void loadHotkeys(const QSettings &s, AppSettings &out)
{
    const AppSettings shipped;
    out.hotkey_toggle = s.value("hotkeys/toggle", shipped.hotkey_toggle).toString();
    out.hotkey_connect = s.value("hotkeys/connect", shipped.hotkey_connect).toString();
    out.hotkey_disconnect = s.value("hotkeys/disconnect", shipped.hotkey_disconnect).toString();
    // Until 1.2.1 global hotkeys were on out of the box, and Ctrl+Shift+T, E and
    // D stopped reaching every browser and terminal. They are opt-in now. A store
    // saved before that keeps its "on" only where someone chose it, which shows
    // as a combo changed from the shipped ones; all three untouched means nobody
    // did. The marker makes this one-time: an "on" saved since is a choice.
    const bool chosenUnderOptIn = s.value("hotkeys/opt_in", false).toBool();
    const bool untouched = out.hotkey_toggle == shipped.hotkey_toggle
            && out.hotkey_connect == shipped.hotkey_connect
            && out.hotkey_disconnect == shipped.hotkey_disconnect;
    out.hotkeys_enabled = s.value("hotkeys/enabled", shipped.hotkeys_enabled).toBool()
            && (chosenUnderOptIn || !untouched);
}

AppSettings loadAppSettings() {
    // Uses QCoreApplication's organization/application name (set to "FreeTunnel"
    // in main()), so tests can redirect the store to an isolated domain.
    QSettings s;
    AppSettings out;
    out.log_path = s.value("logs/path", defaultLogPath()).toString();
    out.logging_enabled = s.value("logs/enabled", true).toBool();
    out.verbose_logs = s.value("logs/verbose", false).toBool();
    // Migrate the old insecure /tmp default to the per-user app data dir.
    if (out.log_path == legacyTmpLogPath())
        out.log_path = defaultLogPath();
    out.theme_mode = s.value("ui/theme_mode", "system").toString();
    out.language = s.value("ui/language", "en").toString();
    out.auto_connect_on_start = s.value("vpn/auto_connect_on_start", false).toBool();
    out.killswitch_enabled = s.value("vpn/killswitch_enabled", true).toBool();
    out.domain_bypass_enabled = s.value("bypass/enabled", true).toBool();
    out.vpn_mode = s.value("bypass/mode", QStringLiteral("general")).toString();
    out.excluded_routes = s.value("routing/excluded_routes", defaultExcludedRoutes()).toStringList();
    loadBypassProfiles(s, out); // and, inside it, the per-profile application lists
    loadHotkeys(s, out);
    out.last_config_path = s.value("vpn/last_config_path", "").toString();
    if (out.log_path.isEmpty()) {
        out.log_path = defaultLogPath();
    }
    return out;
}

void saveAppSettings(const AppSettings &cfg) {
    QSettings s;
    s.setValue("logs/path", cfg.log_path);
    s.setValue("logs/enabled", cfg.logging_enabled);
    s.setValue("logs/verbose", cfg.verbose_logs);
    s.setValue("ui/theme_mode", cfg.theme_mode);
    s.setValue("ui/language", cfg.language);
    s.setValue("vpn/auto_connect_on_start", cfg.auto_connect_on_start);
    s.setValue("vpn/killswitch_enabled", cfg.killswitch_enabled);
    s.setValue("bypass/enabled", cfg.domain_bypass_enabled);
    s.setValue("bypass/mode", cfg.vpn_mode);
    s.setValue("routing/excluded_routes", cfg.excluded_routes);
    s.setValue("bypass/rules", cfg.domain_bypass_rules); // active mirror (core)
    s.setValue("bypass/active_profile", cfg.active_profile);
    s.setValue("bypass/profile_names", QStringList(cfg.profiles.keys()));
    s.setValue("bypass/profile_order", cfg.profile_order);
    s.remove("bypass/profile"); // drop stale per-profile entries, then rewrite
    for (auto it = cfg.profiles.constBegin(); it != cfg.profiles.constEnd(); ++it)
        s.setValue("bypass/profile/" + it.key(), it.value());
    s.remove("bypass/profile_apps");
    for (auto it = cfg.profile_app_rules.constBegin(); it != cfg.profile_app_rules.constEnd(); ++it)
        s.setValue("bypass/profile_apps/" + it.key(), it.value());
    // Set after the lists are written, so a save interrupted half way leaves the
    // pre-1.2.0 key still authoritative rather than an empty list looking final.
    s.setValue("bypass/profile_apps_seeded", true);
    s.remove("routing/app_rules"); // the one global list, now split across profiles
    QVariantMap cp;
    for (auto it = cfg.config_profiles.constBegin(); it != cfg.config_profiles.constEnd(); ++it)
        cp.insert(it.key(), it.value());
    s.setValue("bypass/config_profiles", cp);
    s.setValue("hotkeys/enabled", cfg.hotkeys_enabled);
    s.setValue("hotkeys/opt_in", true); // see loadHotkeys()
    s.setValue("hotkeys/toggle", cfg.hotkey_toggle);
    s.setValue("hotkeys/connect", cfg.hotkey_connect);
    s.setValue("hotkeys/disconnect", cfg.hotkey_disconnect);
    s.setValue("vpn/last_config_path", cfg.last_config_path);
}
