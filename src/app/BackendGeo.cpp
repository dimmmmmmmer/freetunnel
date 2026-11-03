// cppcheck-suppress-file missingIncludeSystem
// Geo routing: the Split tunnelling page's choice between routing only what is
// blocked in Russia through the tunnel, everything but Russian sites, or the
// user's own rules alone; and the lists those modes route by (GeoListUpdater).
// Every platform. How it turns into what the core is told is in
// applySplitRules() (BackendSplit.cpp).
#include "app/Backend.h"

#include <QLocale>
#include <QStandardPaths>

#include "core/GeoListUpdater.h"

using freetunnel::GeoListUpdater;
using freetunnel::GeoRoutingMode;

namespace {

QString slotFor(GeoRoutingMode mode, bool sites)
{
    switch (mode) {
    case GeoRoutingMode::Blocked:
        return sites ? QStringLiteral("blocked_sites") : QStringLiteral("blocked_ips");
    case GeoRoutingMode::ExceptRussia:
        return sites ? QStringLiteral("russia_sites") : QStringLiteral("russia_ips");
    case GeoRoutingMode::Off:
        break;
    }
    return {};
}

} // namespace

bool Backend::geoRoutingAvailable() const
{
    // On every platform: the lists are downloaded and read here, the helper hands
    // their entries to the core, and the core matches them the same way
    // everywhere. Kept as a property so that a platform can be held back again.
    return true;
}

GeoRoutingMode Backend::effectiveGeoMode() const
{
    return geoRoutingAvailable() ? freetunnel::geoRoutingModeFromName(m_settings.geo_mode)
                                 : GeoRoutingMode::Off;
}

GeoRoutingMode Backend::geoModeInUse() const
{
    const GeoRoutingMode mode = effectiveGeoMode();
    if (mode == GeoRoutingMode::Off || !m_geo || !m_geo->ready() || m_geo->entriesMode() != mode)
        return GeoRoutingMode::Off;
    return mode;
}

void Backend::startGeoRouting()
{
    if (!geoRoutingAvailable())
        return;
    const QString cacheDir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/geo");
    m_geo = new GeoListUpdater(cacheDir, appVersion(), this);
    m_geo->setSources(m_settings.geo_sources);
    m_geo->setAutoUpdate(m_settings.geo_auto_update);
    // A new list, or the lists of a newly chosen mode read in: the tunnel routes
    // by them from now on. Live, like any other split-tunnelling edit.
    connect(m_geo, &GeoListUpdater::entriesChanged, this, [this]() {
        applySplitRules(/*warnOfLeak=*/false);
        emit geoChanged();
        emit splitChanged();
    });
    connect(m_geo, &GeoListUpdater::statusChanged, this, [this]() {
        const bool busy = m_geo->busy();
        if (m_geoWasBusy && !busy) {
            const QString error = m_geo->lastError();
            if (error.isEmpty())
                appendLog(QStringLiteral("INFO"), QStringLiteral("Geo lists checked: %1 sites, %2 addresses")
                                                          .arg(m_geo->siteCount())
                                                          .arg(m_geo->addressCount()));
            else
                appendLog(QStringLiteral("WARN"), QStringLiteral("Geo lists: %1").arg(error));
        }
        m_geoWasBusy = busy;
        emit geoChanged();
    });
    m_geo->setMode(effectiveGeoMode());
}

QString Backend::geoMode() const
{
    return freetunnel::geoRoutingModeName(effectiveGeoMode());
}

void Backend::setGeoMode(const QString &mode)
{
    if (!geoRoutingAvailable())
        return;
    const QString normalized = freetunnel::geoRoutingModeName(freetunnel::geoRoutingModeFromName(mode));
    if (m_settings.geo_mode == normalized)
        return;
    m_settings.geo_mode = normalized;
    persistSettings();
    // The old mode's lists stop routing at once: until the new one's are read
    // in, the tunnel routes as geoStatus says (applySplitRules), and they take
    // over when they are (entriesChanged above).
    if (m_geo)
        m_geo->setMode(effectiveGeoMode());
    applySplitRules();
    emit geoChanged();
    emit splitChanged();
}

bool Backend::geoActive() const
{
    return geoModeInUse() != GeoRoutingMode::Off;
}

bool Backend::geoBusy() const
{
    return m_geo && m_geo->busy();
}

QString Backend::geoStatus() const
{
    const GeoRoutingMode mode = effectiveGeoMode();
    if (mode == GeoRoutingMode::Off || !m_geo)
        return {};
    if (!geoActive()) {
        if (m_geo->busy())
            return tr("Downloading the lists… Until they are in, everything goes through the VPN.");
        if (!m_geo->lastError().isEmpty())
            return tr("The lists could not be downloaded, so everything goes through the VPN for now. "
                      "FreeTunnel tries again within the hour.");
        return tr("The lists are not downloaded yet, so everything goes through the VPN for now.");
    }
    const QLocale locale;
    QString line = tr("%1 sites and %2 addresses")
                           .arg(locale.toString(m_geo->siteCount()), locale.toString(m_geo->addressCount()));
    const QDateTime checked = m_geo->lastChecked();
    if (checked.isValid())
        line += QStringLiteral(" · ") + tr("checked %1").arg(locale.toString(checked.toLocalTime(), QLocale::ShortFormat));
    if (m_geo->busy())
        line += QStringLiteral(" · ") + tr("updating…");
    else if (!m_geo->lastError().isEmpty())
        line += QStringLiteral(" · ") + tr("the last update failed, see the log");
    return line;
}

void Backend::setGeoAutoUpdate(bool v)
{
    if (m_settings.geo_auto_update == v)
        return;
    m_settings.geo_auto_update = v;
    persistSettings();
    if (m_geo)
        m_geo->setAutoUpdate(v);
    emit geoChanged();
}

QString Backend::geoSitesSource() const
{
    const QString slot = slotFor(effectiveGeoMode(), true);
    return slot.isEmpty() || !m_geo ? QString() : m_geo->sourceFor(slot);
}

QString Backend::geoAddressesSource() const
{
    const QString slot = slotFor(effectiveGeoMode(), false);
    return slot.isEmpty() || !m_geo ? QString() : m_geo->sourceFor(slot);
}

bool Backend::geoSourcesCustomized() const
{
    const GeoRoutingMode mode = effectiveGeoMode();
    return m_settings.geo_sources.contains(slotFor(mode, true))
            || m_settings.geo_sources.contains(slotFor(mode, false));
}

bool Backend::setGeoSource(bool sites, const QString &source)
{
    const QString slot = slotFor(effectiveGeoMode(), sites);
    if (slot.isEmpty())
        return false;
    const QString trimmed = source.trimmed();
    const freetunnel::GeoListSlot *def = freetunnel::geoListSlot(slot);
    if (trimmed.isEmpty() || (def && trimmed == def->defaultSource)) {
        if (!m_settings.geo_sources.remove(slot))
            return true;
    } else {
        if (!freetunnel::parseGeoSource(trimmed).valid()) {
            emit errorOccurred(tr("Not a list address: %1. Use an https address of a text list, or of a "
                                  ".dat file with its category after #, as in …/geoip.dat#ru-blocked.")
                                       .arg(trimmed));
            return false;
        }
        if (m_settings.geo_sources.value(slot) == trimmed)
            return true;
        m_settings.geo_sources.insert(slot, trimmed);
    }
    persistSettings();
    if (m_geo)
        m_geo->setSources(m_settings.geo_sources);
    emit geoChanged();
    return true;
}

bool Backend::setGeoSitesSource(const QString &source)
{
    return setGeoSource(true, source);
}

bool Backend::setGeoAddressesSource(const QString &source)
{
    return setGeoSource(false, source);
}

void Backend::resetGeoSources()
{
    const GeoRoutingMode mode = effectiveGeoMode();
    const bool removed = m_settings.geo_sources.remove(slotFor(mode, true)) > 0;
    const bool removedToo = m_settings.geo_sources.remove(slotFor(mode, false)) > 0;
    if (!removed && !removedToo)
        return;
    persistSettings();
    if (m_geo)
        m_geo->setSources(m_settings.geo_sources);
    emit geoChanged();
}

void Backend::updateGeoLists()
{
    if (m_geo)
        m_geo->updateNow();
}
