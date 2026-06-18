#pragma once

#include <QString>
#include <QStringList>
#include <QMap>

struct AppSettings {
    QString log_path = "";
    QString theme_mode = "system";
    QString language = "en";
    bool auto_connect_on_start = false;
    bool killswitch_enabled = true;

    // Domain bypass rules: domains matching these patterns skip the VPN tunnel.
    // Supports wildcards: *.example.com, exact: example.com. domain_bypass_rules
    // mirrors the active profile's list (this is what the core consumes).
    bool domain_bypass_enabled = true;
    QStringList domain_bypass_rules;
    // "general" = route everything except the rules (bypass); "selective" =
    // route only the rules through the VPN. Maps to the core's vpn_mode.
    QString vpn_mode = "general";

    // Excluded routes: IP/CIDR subnets that bypass the tunnel at the routing
    // level (the core's excluded_routes), independent of the domain rules above.
    QStringList excluded_routes;

    // Split-tunnel profiles: named sets of domain-bypass rules. The active
    // profile's rules are mirrored into domain_bypass_rules above. profile_order
    // preserves creation order (QMap would sort alphabetically).
    QString active_profile = "Default";
    QMap<QString, QStringList> profiles{{"Default", {}}};
    QStringList profile_order{"Default"};

    // Global system hotkeys (portable key sequences, e.g. "Ctrl+Alt+T").
    // Empty string = unbound. hotkeys_enabled is the master switch.
    bool hotkeys_enabled = true;
    QString hotkey_toggle = "Ctrl+Shift+T";
    QString hotkey_connect = "Ctrl+Shift+E";
    QString hotkey_disconnect = "Ctrl+Shift+D";

    // Last used config
    QString last_config_path = "";
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &cfg);

// Private/special-use IPv4 ranges that should bypass the tunnel by default.
QStringList defaultExcludedRoutes();
// Built-in "Recommended for Russia" domain bypass set.
QStringList recommendedRussiaDomains();
