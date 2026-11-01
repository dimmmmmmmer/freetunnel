// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoListController.h"
#include <QThread>
#include <memory>
#include <utility>
namespace freetunnel {
QString GeoListController::key(const GeoListDefinition &definition)
{
    return definition.source + QChar(0) + QString::number(int(definition.kind))
            + QLatin1Char(':') + QString::number(int(definition.direction));
}
GeoListController::GeoListController(QString directory, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)), m_updater(m_directory)
{
    m_timer.setInterval(60 * 60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &GeoListController::schedule);
    m_timer.start();
    connect(&m_updater, &GeoListUpdater::finished, this,
            [this](const GeoListDefinition &definition, const CachedGeoList &list, const QString &error) {
        auto &state = m_states[key(definition)];
        state.busy = false;
        state.error = error;
        if (error.isEmpty()) state.list = list;
        emit changed();
        pump();
    });
}
const GeoListState &GeoListController::state(const GeoListDefinition &definition) const
{
    static const GeoListState empty;
    const auto found = m_states.constFind(key(definition));
    return found == m_states.cend() ? empty : found.value();
}
void GeoListController::setDefinitions(const QList<GeoListDefinition> &definitions, bool automatic)
{
    m_definitions.clear();
    for (const auto &definition : definitions) {
        const QString identity = key(definition);
        const bool enabledElsewhere = m_definitions.value(identity).enabled;
        m_definitions[identity] = definition;
        m_definitions[identity].enabled |= enabledElsewhere;
    }
    m_automatic = automatic;
    schedule();
}
void GeoListController::refresh(const GeoListDefinition &definition)
{
    if (m_definitions.contains(key(definition))) enqueue(key(definition), true);
}
void GeoListController::schedule()
{
    for (auto it = m_definitions.cbegin(); it != m_definitions.cend(); ++it)
        if (it->enabled) enqueue(it.key(), false);
}
void GeoListController::enqueue(const QString &identity, bool force)
{
    if (force) m_forced.insert(identity);
    if (!m_queue.contains(identity)) m_queue.append(identity);
    pump();
}
void GeoListController::pump()
{
    if (m_loading || m_updater.busy()) return;
    while (!m_queue.isEmpty()) {
        const QString identity = m_queue.takeFirst();
        if (!m_definitions.contains(identity)) { m_forced.remove(identity); continue; }
        const auto definition = m_definitions.value(identity);
        auto &state = m_states[identity];
        if (!state.loaded) {
            m_loading = true;
            state.busy = true;
            auto result = std::make_shared<CachedGeoList>();
            QThread *worker = QThread::create([directory = m_directory, definition, result] {
                QString ignored;
                GeoListCache(directory).load(definition.source, definition.kind,
                                             definition.direction, result.get(), &ignored);
            });
            connect(worker, &QThread::finished, this, [this, identity, result] {
                auto &loaded = m_states[identity];
                loaded.list = std::move(*result);
                loaded.loaded = true;
                loaded.busy = false;
                m_loading = false;
                m_queue.prepend(identity);
                emit changed();
                pump();
            });
            connect(worker, &QThread::finished, worker, &QObject::deleteLater);
            worker->start();
            emit changed();
            return;
        }
        const bool forced = m_forced.remove(identity);
        const auto now = QDateTime::currentDateTimeUtc();
        const bool due = !state.list.updated.isValid() || state.list.updated.secsTo(now) >= 86400;
        const bool retry = !state.attempted.isValid() || state.attempted.secsTo(now) >= 3600;
        if (!forced && (!definition.enabled || !due || !retry
                        || (!m_automatic && !state.list.entries.empty()))) continue;
        state.busy = true;
        state.attempted = now;
        m_updater.start(definition);
        emit changed();
        return;
    }
}
}
