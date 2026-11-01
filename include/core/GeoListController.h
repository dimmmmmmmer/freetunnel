// cppcheck-suppress-file missingIncludeSystem
#pragma once
#include "core/GeoListUpdater.h"
#include <QMap>
#include <QSet>

namespace freetunnel {
struct GeoListState {
    CachedGeoList list;
    QString error;
    bool loaded = false;
    bool busy = false;
    QDateTime attempted;
};
// Shares a downloaded list across profiles, while every profile keeps its own
// enabled state and direction. Downloaded rules are addressed by full source,
// kind and direction, never by a mutable UI name or profile index.
class GeoListController : public QObject {
    Q_OBJECT
public:
    explicit GeoListController(QString directory, QObject *parent = nullptr);
    void setDefinitions(const QList<GeoListDefinition> &definitions, bool automatic);
    void refresh(const GeoListDefinition &definition);
    const GeoListState &state(const GeoListDefinition &definition) const;
    static QString key(const GeoListDefinition &definition);
signals:
    void changed();
private:
    void schedule();
    void enqueue(const QString &key, bool force);
    void pump();
    QString m_directory;
    GeoListUpdater m_updater;
    QTimer m_timer;
    QMap<QString, GeoListDefinition> m_definitions;
    QMap<QString, GeoListState> m_states;
    QStringList m_queue;
    QSet<QString> m_forced;
    bool m_automatic = true;
    bool m_loading = false;
};
}
