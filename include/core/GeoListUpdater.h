// cppcheck-suppress-file missingIncludeSystem
#pragma once
#include "core/GeoListCache.h"
#include <QObject>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

namespace freetunnel {
// One download at a time. HTTPS redirects are allowed (GitHub release assets
// use another host), but credentials and HTTPS downgrades are never followed.
// Failed downloads retain the last valid cache. Parsing runs off the UI thread.
class GeoListUpdater : public QObject {
    Q_OBJECT
public:
    explicit GeoListUpdater(QString cacheDirectory, QObject *parent = nullptr,
                            QNetworkAccessManager *manager = nullptr);
    ~GeoListUpdater() override;
    bool busy() const { return m_busy; }
    bool start(const GeoListDefinition &definition);
signals:
    void finished(const freetunnel::GeoListDefinition &definition,
                  const freetunnel::CachedGeoList &list, const QString &error);
private:
    void request(const QUrl &url);
    void read();
    void downloaded();
    void finish(const CachedGeoList &list, const QString &error);
    QString m_directory;
    GeoListDefinition m_definition;
    QNetworkAccessManager *m_manager;
    QNetworkReply *m_reply = nullptr;
    QTimer m_deadline;
    QByteArray m_body;
    int m_redirects = 0;
    bool m_busy = false;
};
}
