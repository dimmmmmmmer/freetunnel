#include "AppSettings.h"

#include <QSettings>
#include <QStandardPaths>

static QString defaultLogPath() {
#ifdef _WIN32
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/freetunnel.log";
#else
    return "/tmp/freetunnel.log";
#endif
}

static QString defaultRoutingCachePath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/ru-subnet.lst";
}

AppSettings loadAppSettings() {
    // Uses QCoreApplication's organization/application name (set to "FreeTunnel"
    // in main()), so tests can redirect the store to an isolated domain.
    QSettings s;
    AppSettings out;
    out.save_logs = s.value("logs/save", true).toBool();
    out.log_level = s.value("logs/level", "info").toString();
    out.log_path = s.value("logs/path", defaultLogPath()).toString();
    out.theme_mode = s.value("ui/theme_mode", "system").toString();
    out.language = s.value("ui/language", "en").toString();
    out.auto_connect_on_start = s.value("vpn/auto_connect_on_start", false).toBool();
    out.show_logs_panel = s.value("ui/show_logs_panel", true).toBool();
    out.show_traffic_in_status = s.value("ui/show_traffic_in_status", true).toBool();
    out.show_traffic_graph = s.value("ui/show_traffic_graph", true).toBool();
    out.notify_on_state = s.value("ui/notify_on_state", true).toBool();
    out.notify_only_errors = s.value("ui/notify_only_errors", false).toBool();
    out.killswitch_enabled = s.value("vpn/killswitch_enabled", false).toBool();
    out.strict_certificate_check = s.value("vpn/strict_certificate_check", true).toBool();
    out.first_run_checked = s.value("ui/first_run_checked", false).toBool();
    out.routing_enabled = s.value("routing/enabled", false).toBool();
    out.routing_mode = s.value("routing/mode", "tunnel_ru").toString();
    out.routing_cache_path = s.value("routing/cache_path", defaultRoutingCachePath()).toString();
    out.routing_source_url = s.value("routing/source_url",
            "https://antifilter.download/list/subnet.lst").toString();
    out.custom_dns_enabled = s.value("dns/custom_enabled", false).toBool();
    out.custom_dns_servers = s.value("dns/custom_servers", QStringList{"1.1.1.1", "8.8.8.8"}).toStringList();
    out.domain_bypass_enabled = s.value("bypass/enabled", false).toBool();
    out.vpn_mode = s.value("bypass/mode", QStringLiteral("general")).toString();
    // Split-tunnel profiles.
    const QStringList names = s.value("bypass/profile_names", QStringList{"Default"}).toStringList();
    out.profiles.clear();
    for (const QString &n : names)
        out.profiles.insert(n, s.value("bypass/profile/" + n, QStringList{}).toStringList());
    if (out.profiles.isEmpty())
        out.profiles.insert(QStringLiteral("Default"), {});
    // Migrate a pre-profiles single rule list into Default.
    const QStringList legacy = s.value("bypass/rules", QStringList{}).toStringList();
    if (!legacy.isEmpty() && out.profiles.value(QStringLiteral("Default")).isEmpty()
            && names == QStringList{QStringLiteral("Default")})
        out.profiles[QStringLiteral("Default")] = legacy;
    out.active_profile = s.value("bypass/active_profile", QStringLiteral("Default")).toString();
    if (!out.profiles.contains(out.active_profile))
        out.active_profile = out.profiles.firstKey();
    out.domain_bypass_rules = out.profiles.value(out.active_profile);
    // Preserve creation order; reconcile with the actual profile set.
    QStringList order;
    for (const QString &n : s.value("bypass/profile_order", names).toStringList())
        if (out.profiles.contains(n) && !order.contains(n))
            order << n;
    for (const QString &n : out.profiles.keys())
        if (!order.contains(n))
            order << n;
    if (!order.contains(QStringLiteral("Default")))
        order.prepend(QStringLiteral("Default"));
    out.profile_order = order;
    out.scan_adapter_conflicts = s.value("net/scan_adapter_conflicts", true).toBool();
    out.ssh_bypass_enabled = s.value("bypass/ssh_enabled", false).toBool();
    out.p2p_bypass_enabled = s.value("bypass/p2p_enabled", false).toBool();
    out.hotkey_toggle = s.value("hotkeys/toggle", "Ctrl+Shift+T").toString();
    out.hotkey_connect = s.value("hotkeys/connect", "Ctrl+Shift+E").toString();
    out.hotkey_disconnect = s.value("hotkeys/disconnect", "Ctrl+Shift+D").toString();
    if (out.log_path.isEmpty()) {
        out.log_path = defaultLogPath();
    }
    return out;
}

void saveAppSettings(const AppSettings &cfg) {
    QSettings s;
    s.setValue("logs/save", cfg.save_logs);
    s.setValue("logs/level", cfg.log_level);
    s.setValue("logs/path", cfg.log_path);
    s.setValue("ui/theme_mode", cfg.theme_mode);
    s.setValue("ui/language", cfg.language);
    s.setValue("vpn/auto_connect_on_start", cfg.auto_connect_on_start);
    s.setValue("ui/show_logs_panel", cfg.show_logs_panel);
    s.setValue("ui/show_traffic_in_status", cfg.show_traffic_in_status);
    s.setValue("ui/show_traffic_graph", cfg.show_traffic_graph);
    s.setValue("ui/notify_on_state", cfg.notify_on_state);
    s.setValue("ui/notify_only_errors", cfg.notify_only_errors);
    s.setValue("vpn/killswitch_enabled", cfg.killswitch_enabled);
    s.setValue("vpn/strict_certificate_check", cfg.strict_certificate_check);
    s.setValue("ui/first_run_checked", cfg.first_run_checked);
    s.setValue("routing/enabled", cfg.routing_enabled);
    s.setValue("routing/mode", cfg.routing_mode);
    s.setValue("routing/cache_path", cfg.routing_cache_path);
    s.setValue("routing/source_url", cfg.routing_source_url);
    s.setValue("dns/custom_enabled", cfg.custom_dns_enabled);
    s.setValue("dns/custom_servers", cfg.custom_dns_servers);
    s.setValue("bypass/enabled", cfg.domain_bypass_enabled);
    s.setValue("bypass/mode", cfg.vpn_mode);
    s.setValue("bypass/rules", cfg.domain_bypass_rules); // active mirror (core)
    s.setValue("bypass/active_profile", cfg.active_profile);
    s.setValue("bypass/profile_names", QStringList(cfg.profiles.keys()));
    s.setValue("bypass/profile_order", cfg.profile_order);
    s.remove("bypass/profile"); // drop stale per-profile entries, then rewrite
    for (auto it = cfg.profiles.constBegin(); it != cfg.profiles.constEnd(); ++it)
        s.setValue("bypass/profile/" + it.key(), it.value());
    s.setValue("net/scan_adapter_conflicts", cfg.scan_adapter_conflicts);
    s.setValue("bypass/ssh_enabled", cfg.ssh_bypass_enabled);
    s.setValue("bypass/p2p_enabled", cfg.p2p_bypass_enabled);
    s.setValue("hotkeys/toggle", cfg.hotkey_toggle);
    s.setValue("hotkeys/connect", cfg.hotkey_connect);
    s.setValue("hotkeys/disconnect", cfg.hotkey_disconnect);
}
