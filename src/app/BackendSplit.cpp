#include "app/Backend.h"

#include <QHostAddress>
#include <QRegularExpression>

#include "core/AppSettings.h"

void Backend::setSplitEnabled(bool v) {
    if (m_settings.domain_bypass_enabled == v) return;
    m_settings.domain_bypass_enabled = v; persistSettings(); applySplitRules(); emit splitChanged();
}
// A bypass rule is valid if it's a domain (optionally wildcard "*.x.y"), or an
// IP / CIDR subnet.
static bool isValidBypassRule(const QString &rule) {
    QString r = rule;
    if (r.startsWith(QLatin1String("*.")))
        r = r.mid(2);
    // IP or subnet?
    const int slash = r.indexOf(QLatin1Char('/'));
    const QString addr = slash >= 0 ? r.left(slash) : r;
    if (!QHostAddress(addr).isNull()) {
        if (slash < 0) return true;
        bool ok = false; const int p = r.mid(slash + 1).toInt(&ok);
        const int max = addr.contains(QLatin1Char(':')) ? 128 : 32;
        return ok && p >= 0 && p <= max;
    }
    // Otherwise a hostname: labels of [A-Za-z0-9-], at least one dot.
    static const QRegularExpression re(
        QStringLiteral("^(?=.{1,253}$)([A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?\\.)+"
                       "[A-Za-z]{2,63}$"));
    return re.match(r).hasMatch();
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
    persistSettings(); applySplitRules(); emit splitChanged();
    return true;
}
void Backend::removeDomain(int index) {
    if (index < 0 || index >= m_settings.domain_bypass_rules.size()) return;
    m_settings.domain_bypass_rules.removeAt(index);
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); emit splitChanged();
}
void Backend::clearDomains() {
    if (m_settings.domain_bypass_rules.isEmpty()) return;
    m_settings.domain_bypass_rules.clear();
    m_settings.profiles[m_settings.active_profile] = m_settings.domain_bypass_rules;
    persistSettings(); applySplitRules(); emit splitChanged();
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
    persistSettings(); applySplitRules(); emit splitChanged();
    return true;
}

void Backend::removeExcludedRoute(int index) {
    if (index < 0 || index >= m_settings.excluded_routes.size()) return;
    m_settings.excluded_routes.removeAt(index);
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::clearExcludedRoutes() {
    if (m_settings.excluded_routes.isEmpty()) return;
    m_settings.excluded_routes.clear();
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::restoreDefaultExcludedRoutes() {
    m_settings.excluded_routes = defaultExcludedRoutes();
    persistSettings(); applySplitRules(); emit splitChanged();
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
    persistSettings(); applySplitRules(); emit splitChanged();
}

// ---------- split-tunnel profiles ----------

QStringList Backend::profiles() const {
    return m_settings.profile_order; // creation order, Default first
}

void Backend::selectProfile(const QString &name) {
    if (!m_settings.profiles.contains(name) || m_settings.active_profile == name) return;
    m_settings.active_profile = name;
    m_settings.domain_bypass_rules = m_settings.profiles.value(name);
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::addProfile(const QString &name) {
    const QString n = name.trimmed();
    if (n.isEmpty() || m_settings.profiles.contains(n)) return;
    m_settings.profiles.insert(n, {});
    m_settings.profile_order << n;
    m_settings.active_profile = n;
    m_settings.domain_bypass_rules.clear();
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::removeProfile(const QString &name) {
    if (name == QLatin1String("Default") || !m_settings.profiles.contains(name)) return;
    m_settings.profiles.remove(name);
    m_settings.profile_order.removeAll(name);
    if (m_settings.active_profile == name) {
        m_settings.active_profile = QStringLiteral("Default");
        m_settings.domain_bypass_rules = m_settings.profiles.value(QStringLiteral("Default"));
    }
    persistSettings(); applySplitRules(); emit splitChanged();
}

void Backend::renameProfile(const QString &oldName, const QString &newName) {
    const QString n = newName.trimmed();
    if (oldName == QLatin1String("Default") || n.isEmpty()
            || !m_settings.profiles.contains(oldName) || m_settings.profiles.contains(n))
        return;
    m_settings.profiles.insert(n, m_settings.profiles.take(oldName));
    const int i = m_settings.profile_order.indexOf(oldName);
    if (i >= 0) m_settings.profile_order[i] = n;
    if (m_settings.active_profile == oldName)
        m_settings.active_profile = n;
    persistSettings(); emit splitChanged();
}

// Push the active profile's domain-bypass list to the core (C2). Applied on
// connect and whenever rules change; takes effect on the next (re)connect.
void Backend::applySplitRules() {
    const bool on = m_settings.domain_bypass_enabled;
    std::vector<std::string> ex;
    if (on)
        for (const QString &d : m_settings.domain_bypass_rules)
            ex.push_back(d.toStdString());
    m_client.setExtraExclusions(ex);
    // When split is off, force general mode (route everything) — otherwise a
    // leftover "selective" mode with no rules would route NOTHING through the VPN.
    m_client.setVpnMode(on && m_settings.vpn_mode == QLatin1String("selective"));
    // Excluded routes (subnets) are a separate, always-on routing rule.
    std::vector<std::string> routes;
    for (const QString &r : m_settings.excluded_routes)
        routes.push_back(r.toStdString());
    m_client.setExcludedRoutes(routes);
    reapplyIfConnected(); // make the change take effect on a live tunnel
}

// Routing/exclusion changes only bind when the tunnel is (re)built. If we're
// connected, seamlessly rebuild it so edits apply immediately rather than only
// after a manual reconnect. No-op (and no re-elevation) when disconnected.
void Backend::reapplyIfConnected() {
    if (!m_connected || m_reapplying || m_inConnect)
        return;
    m_reapplying = true; // connectVpn() below calls applySplitRules() again — don't recurse
    m_client.disconnectVpn();
    QTimer::singleShot(400, this, [this]() {
        if (!m_activePath.isEmpty())
            connectVpn();
        m_reapplying = false;
    });
}

void Backend::setVpnMode(const QString &mode) {
    if (m_settings.vpn_mode == mode) return;
    m_settings.vpn_mode = mode;
    persistSettings();
    applySplitRules();
    emit splitChanged();
}
