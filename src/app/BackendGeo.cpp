// cppcheck-suppress-file missingIncludeSystem
#include "app/Backend.h"
#include "core/GeoListController.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

void Backend::startGeoLists()
{
    m_geoLists = new freetunnel::GeoListController(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
                + QStringLiteral("/geo"), this);
    connect(m_geoLists, &freetunnel::GeoListController::changed, this, [this] {
        applySplitRules(false);
        reapplyIfConnected();
        emit splitChanged();
    });
    syncGeoLists();
}
void Backend::syncGeoLists()
{
    if (!m_geoLists) return;
    QList<freetunnel::GeoListDefinition> definitions;
    for (const auto &profile : std::as_const(m_settings.profile_geo_lists)) definitions.append(profile);
    m_geoLists->setDefinitions(definitions, m_settings.geo_auto_update);
}
QVariantList Backend::geoPresets() const
{
    QFile file(QStringLiteral(":/geo-presets.json"));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array().toVariantList();
}
QVariantList Backend::geoLists() const
{
    QVariantList rows;
    for (const auto &definition : m_settings.profile_geo_lists.value(m_settings.active_profile)) {
        QVariantMap row{{QStringLiteral("id"), definition.id}, {QStringLiteral("name"), definition.name},
            {QStringLiteral("source"), definition.source}, {QStringLiteral("enabled"), definition.enabled},
            {QStringLiteral("bypass"), definition.direction == freetunnel::GeoDirection::Bypass}};
        if (m_geoLists) {
            const auto &state = m_geoLists->state(definition);
            row.insert(QStringLiteral("busy"), state.busy);
            row.insert(QStringLiteral("entries"), state.list.stats.entries);
            row.insert(QStringLiteral("skipped"), state.list.stats.skipped);
            row.insert(QStringLiteral("updated"), state.list.updated.toLocalTime().toString(QStringLiteral("dd.MM.yyyy HH:mm")));
            row.insert(QStringLiteral("error"), state.error);
        }
        rows.append(row);
    }
    return rows;
}
bool Backend::geoRulesFit(const QString &profile) const
{
    qint64 bytes = 0;
    if (m_geoLists)
        for (const auto &definition : m_settings.profile_geo_lists.value(profile))
            if (definition.enabled) bytes += m_geoLists->state(definition).list.routingBytes;
    // Leave room for manually entered rules and applications in the 16 MiB IPC message.
    return bytes <= 8 * 1024 * 1024;
}
bool Backend::geoLimitExceeded() const { return !geoRulesFit(m_settings.active_profile); }
void Backend::setGeoAutoUpdate(bool enabled)
{
    if (m_settings.geo_auto_update == enabled) return;
    m_settings.geo_auto_update = enabled;
    persistSettings(); syncGeoLists(); emit splitChanged();
}
bool Backend::addGeoList(const QString &name, const QString &source, bool ips, bool bypass)
{
    QString error;
    if (name.trimmed().isEmpty() || !freetunnel::parseGeoSource(source.trimmed(), &error).valid()) {
        emit errorOccurred(tr("Enter a name and an HTTPS geo list address"));
        return false;
    }
    auto &lists = m_settings.profile_geo_lists[m_settings.active_profile];
    const auto kind = ips ? freetunnel::GeoListKind::Ips : freetunnel::GeoListKind::Sites;
    for (const auto &list : std::as_const(lists))
        if (list.source == source.trimmed() && list.kind == kind) {
            emit errorOccurred(tr("This geo list is already in the profile"));
            return false;
        }
    freetunnel::GeoListDefinition definition;
    definition.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    definition.name = name.trimmed(); definition.source = source.trimmed(); definition.kind = kind;
    definition.direction = bypass ? freetunnel::GeoDirection::Bypass : freetunnel::GeoDirection::ThroughVpn;
    definition.enabled = true; // Adding a list is an explicit user action.
    lists.append(definition);
    applyProfileEdit();
    return true;
}
void Backend::setGeoListEnabled(const QString &id, bool enabled)
{
    auto &lists = m_settings.profile_geo_lists[m_settings.active_profile];
    for (auto &list : lists) if (list.id == id) {
        list.enabled = enabled; applyProfileEdit(); return;
    }
}
void Backend::setGeoListDirection(const QString &id, bool bypass)
{
    auto &lists = m_settings.profile_geo_lists[m_settings.active_profile];
    for (auto &list : lists) if (list.id == id) {
        list.direction = bypass ? freetunnel::GeoDirection::Bypass : freetunnel::GeoDirection::ThroughVpn;
        applyProfileEdit(); return;
    }
}
void Backend::removeGeoList(const QString &id)
{
    auto &lists = m_settings.profile_geo_lists[m_settings.active_profile];
    lists.removeIf([&id](const auto &list) { return list.id == id; });
    applyProfileEdit();
}
void Backend::refreshGeoList(const QString &id)
{
    if (!m_geoLists) return;
    for (const auto &list : m_settings.profile_geo_lists.value(m_settings.active_profile))
        if (list.id == id) { m_geoLists->refresh(list); return; }
}
