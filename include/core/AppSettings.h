#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

struct AppSettings {
    bool save_logs = true;
    QString log_level = "info";
    QString log_path = "";
    QString theme_mode = "system";
    QString language = "en";
    bool auto_connect_on_start = false;
    bool show_logs_panel = true;
    bool show_traffic_in_status = true;
    bool show_traffic_graph = true;
    bool notify_on_state = true;
    bool notify_only_errors = false;
    bool killswitch_enabled = false;
    bool strict_certificate_check = true;
    bool first_run_checked = false;
    bool routing_enabled = false;
    QString routing_mode = "tunnel_ru"; // tunnel_ru | bypass_ru
    QString routing_cache_path = "";
    QString routing_source_url = "https://antifilter.download/list/subnet.lst";

    // Custom DNS servers (override config dns_upstreams when non-empty)
    bool custom_dns_enabled = false;
    QStringList custom_dns_servers = {"1.1.1.1", "8.8.8.8"};

    // Domain bypass rules: domains matching these patterns skip the VPN tunnel.
    // Supports wildcards: *.example.com, exact: example.com. domain_bypass_rules
    // mirrors the active profile's list (this is what the core consumes).
    bool domain_bypass_enabled = false;
    QStringList domain_bypass_rules;

    // Split-tunnel profiles: named sets of domain-bypass rules. The active
    // profile's rules are mirrored into domain_bypass_rules above. profile_order
    // preserves creation order (QMap would sort alphabetically).
    QString active_profile = "Default";
    QMap<QString, QStringList> profiles{{"Default", {}}};
    QStringList profile_order{"Default"};

    // Adapter conflict scanning
    bool scan_adapter_conflicts = true;

    // SSH / P2P traffic bypass: when enabled, SSH (port 22) and P2P
    // (BitTorrent, etc.) traffic bypasses the VPN tunnel.
    bool ssh_bypass_enabled = false;
    bool p2p_bypass_enabled = false;

    // Global system hotkeys (portable key sequences, e.g. "Ctrl+Alt+T").
    // Empty string = unbound.
    QString hotkey_toggle = "";
    QString hotkey_connect = "";
    QString hotkey_disconnect = "";

    // Last used config
    QString last_config_path = "";

    // Notifications
    bool enable_notifications = true;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &cfg);
