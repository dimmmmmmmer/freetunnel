// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"

#include <QHostAddress>
#include <QRegularExpression>

#include "core/AppSettings.h"
#include "core/BypassRules.h"

#include <algorithm>
#include <iterator>

void Backend::setSplitEnabled(bool v) {
    if (m_settings.domain_bypass_enabled == v) return;
    m_settings.domain_bypass_enabled = v;
    persistSettings(); applySplitRules(); reapplyIfConnected(); emit splitChanged();
}

bool Backend::addDomain(const QString &domain) {
    // Accept a whole list pasted at once: split on commas / whitespace / newlines.
    const QStringList tokens = domain.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                            Qt::SkipEmptyParts);
    QStringList added, invalid;
    for (const QString &raw : tokens) {
        const QString d = raw.trimmed();
        if (d.isEmpty() || m_settings.domain_bypass_rules.contains(d) || added.contains(d))
            continue;
        if (!isValidBypassRule(d)) { invalid << d; continue; }
        added << d;
    }
    if (!invalid.isEmpty())
        emit errorOccurred(tr("Not a valid domain or subnet: %1").arg(invalid.join(QStringLiteral(", "))));
    if (added.isEmpty())
        return false;
    m_settings.domain_bypass_rules << added;
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); reapplyIfEditingActiveProfile(); emit splitChanged();
    return true;
}
void Backend::removeDomain(int index) {
    if (index < 0 || index >= m_settings.domain_bypass_rules.size()) return;
    m_settings.domain_bypass_rules.removeAt(index);
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); reapplyIfEditingActiveProfile(); emit splitChanged();
}
void Backend::clearDomains() {
    if (m_settings.domain_bypass_rules.isEmpty()) return;
    m_settings.domain_bypass_rules.clear();
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); reapplyIfEditingActiveProfile(); emit splitChanged();
}

// ---------- excluded routes (subnets that bypass the tunnel) ----------

// Valid if it's an IP or CIDR subnet (IPv4/IPv6); the optional /prefix must be sane.
static bool isValidSubnet(const QString &r) {
    const int slash = r.indexOf(QLatin1Char('/'));
    const QString addr = slash >= 0 ? r.left(slash) : r;
    if (QHostAddress(addr).isNull())
        return false;
    if (slash < 0)
        return true;
    bool ok = false; const int p = r.mid(slash + 1).toInt(&ok);
    const int max = addr.contains(QLatin1Char(':')) ? 128 : 32;
    return ok && p >= 0 && p <= max;
}

bool Backend::addExcludedRoute(const QString &route) {
    // Accept a whole list pasted at once.
    const QStringList tokens = route.split(QRegularExpression(QStringLiteral("[\\s,;]+")),
                                           Qt::SkipEmptyParts);
    QStringList added, invalid;
    for (const QString &raw : tokens) {
        const QString r = raw.trimmed();
        if (r.isEmpty() || m_settings.excluded_routes.contains(r) || added.contains(r))
            continue;
        if (!isValidSubnet(r)) { invalid << r; continue; }
        added << r;
    }
    if (!invalid.isEmpty())
        emit errorOccurred(tr("Enter a valid IP or subnet, e.g. 10.0.0.0/8"));
    if (added.isEmpty())
        return false;
    m_settings.excluded_routes << added;
    persistSettings(); applySplitRules(); reapplyIfConnected(); emit splitChanged();
    return true;
}

void Backend::removeExcludedRoute(int index) {
    if (index < 0 || index >= m_settings.excluded_routes.size()) return;
    m_settings.excluded_routes.removeAt(index);
    persistSettings(); applySplitRules(); reapplyIfConnected(); emit splitChanged();
}

void Backend::clearExcludedRoutes() {
    if (m_settings.excluded_routes.isEmpty()) return;
    m_settings.excluded_routes.clear();
    persistSettings(); applySplitRules(); reapplyIfConnected(); emit splitChanged();
}

void Backend::restoreDefaultExcludedRoutes() {
    m_settings.excluded_routes = defaultExcludedRoutes();
    persistSettings(); applySplitRules(); reapplyIfConnected(); emit splitChanged();
}

void Backend::addRecommendedRussia() {
    QStringList rules = m_settings.domain_bypass_rules;
    int added = 0;
    for (const QString &d : recommendedRussiaDomains()) {
        if (!rules.contains(d)) { rules << d; ++added; }
    }
    if (added == 0)
        return;
    m_settings.domain_bypass_rules = rules;
    m_settings.profiles[m_settings.active_profile] = rules;
    persistSettings(); applySplitRules(); reapplyIfEditingActiveProfile(); emit splitChanged();
}

// Live-apply a profile edit only when the edited profile is the one the active
// config actually uses — editing some other profile shouldn't reconnect.
void Backend::reapplyIfEditingActiveProfile() {
    if (m_settings.active_profile == activeConfigProfile())
        reapplyIfConnected();
}

// ---------- split-tunnel profiles ----------

const QStringList &Backend::profiles() const {
    return m_settings.profile_order; // creation order, Default first
}

// Selecting a profile only changes which one the Split page edits — it does not
// change what any config uses, so no re-apply.
void Backend::selectProfile(const QString &name) {
    if (!m_settings.profiles.contains(name) || m_settings.active_profile == name) return;
    m_settings.active_profile = name;
    m_settings.domain_bypass_rules = m_settings.profiles.value(name);
    persistSettings(); emit splitChanged();
}

void Backend::addProfile(const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty() || m_settings.profiles.contains(n)) return;
    m_settings.profiles.insert(n, {});
    m_settings.profile_order << n;
    m_settings.active_profile = n; // edit the newly created profile
    m_settings.domain_bypass_rules.clear();
    persistSettings(); emit splitChanged();
}

void Backend::removeProfile(const QString &name) {
    if (name == QLatin1String("Default") || !m_settings.profiles.contains(name)) return;
    const bool affectedActiveConfig = (activeConfigProfile() == name);
    m_settings.profiles.remove(name);
    m_settings.profile_order.removeAll(name);
    // Any config that used this profile falls back to Default.
    for (auto it = m_settings.config_profiles.begin(); it != m_settings.config_profiles.end(); ++it)
        if (it.value() == name)
            it.value() = QStringLiteral("Default");
    if (m_settings.active_profile == name) {
        m_settings.active_profile = QStringLiteral("Default");
        m_settings.domain_bypass_rules = m_settings.profiles.value(QStringLiteral("Default"));
    }
    persistSettings();
    if (affectedActiveConfig) { applySplitRules(); reapplyIfConnected(); }
    emit splitChanged();
    emit configChanged(); // a config's effective profile may have changed
}

namespace {

void retargetProfileName(AppSettings &settings, const QString &oldName, const QString &newName)
{
    const int i = settings.profile_order.indexOf(oldName);
    if (i >= 0)
        settings.profile_order[i] = newName;
    if (settings.active_profile == oldName)
        settings.active_profile = newName;
    for (auto it = settings.config_profiles.begin(); it != settings.config_profiles.end(); ++it) {
        if (it.value() == oldName)
            it.value() = newName;
    }
}

} // namespace

void Backend::renameProfile(const QString &oldName, const QString &newName) {
    const QString n = newName.trimmed();
    if (oldName == QLatin1String("Default") || n.isEmpty()
            || !m_settings.profiles.contains(oldName) || m_settings.profiles.contains(n))
        return;
    m_settings.profiles.insert(n, m_settings.profiles.take(oldName));
    retargetProfileName(m_settings, oldName, n);
    persistSettings(); emit splitChanged();
}

// The split profile assigned to the active config (falls back to "Default" when
// unassigned or pointing at a deleted profile).
QString Backend::activeConfigProfile() const {
    const QString p = m_settings.config_profiles.value(m_activePath);
    if (p.isEmpty() || !m_settings.profiles.contains(p))
        return QStringLiteral("Default");
    return p;
}

// Push the active CONFIG's profile domain-bypass list to the core (C2). Applied
// on connect and whenever the relevant rules change. Does NOT reconnect on its
// own — callers decide whether the change warrants a live rebuild.
void Backend::applySplitRules() {
    const bool on = m_settings.domain_bypass_enabled;
    const QStringList profileDomains = m_settings.profiles.value(activeConfigProfile());
    const QStringList coreDomains = coreBypassRules(profileDomains);
    std::vector<std::string> ex;
    if (on) {
        ex.reserve(static_cast<size_t>(coreDomains.size()));
        std::transform(coreDomains.cbegin(), coreDomains.cend(), std::back_inserter(ex),
                       [](const QString &d) { return d.toStdString(); });
    }
    m_client.setExtraExclusions(ex);
    m_client.setVpnMode(on && m_settings.vpn_mode == QLatin1String("selective"));
    std::vector<std::string> routes;
    routes.reserve(static_cast<size_t>(m_settings.excluded_routes.size()));
    std::transform(m_settings.excluded_routes.cbegin(), m_settings.excluded_routes.cend(),
                    std::back_inserter(routes), [](const QString &r) { return r.toStdString(); });
    m_client.setExcludedRoutes(routes);
}

// Routing/exclusion changes only bind when the tunnel is (re)built. If we're
// connected, seamlessly rebuild it so edits apply immediately rather than only
// after a manual reconnect. No-op (and no re-elevation) when disconnected.
void Backend::reconnectActiveConfig() {
    if (m_reapplying || m_activePath.isEmpty())
        return;
    if (!m_connected && !m_connecting)
        return;
    m_reapplying = true;
    m_connecting = false;
    m_pendingReconnect = true;
    m_client.disconnectVpn();
    // Reconnect once the old session has actually reached Disconnected (driven
    // from onVpnClientStateChanged), not after a fixed delay that could fire
    // mid-teardown and leave the previous tunnel up. The timer is only a safety
    // net for a Disconnected event that never arrives.
    QTimer::singleShot(5000, this, [this]() { firePendingReconnect(); });
}

void Backend::firePendingReconnect() {
    if (!m_pendingReconnect)
        return;
    m_pendingReconnect = false;
    if (!m_activePath.isEmpty())
        connectVpn();
}

void Backend::reapplyIfConnected() {
    if (!m_connected || m_reapplying || m_inConnect)
        return;
    reconnectActiveConfig();
}

void Backend::setVpnMode(const QString &mode) {
    if (m_settings.vpn_mode == mode) return;
    m_settings.vpn_mode = mode;
    persistSettings();
    applySplitRules();
    emit splitChanged();
}
