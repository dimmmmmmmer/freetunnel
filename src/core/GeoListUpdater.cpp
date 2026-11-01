// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoListUpdater.h"
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThread>
#include <memory>
#include <utility>

namespace freetunnel {
GeoListUpdater::GeoListUpdater(QString directory, QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), m_directory(std::move(directory)),
      m_manager(manager ? manager : new QNetworkAccessManager(this))
{
    m_manager->setProxy(QNetworkProxy::NoProxy);
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        finish({}, tr("Geo list download timed out"));
    });
}
GeoListUpdater::~GeoListUpdater()
{
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
}
bool GeoListUpdater::start(const GeoListDefinition &definition)
{
    if (m_busy)
        return false;
    m_busy = true;
    m_definition = definition;
    m_redirects = 0;
    QString error;
    const auto source = parseGeoSource(definition.source, &error);
    if (!source.valid()) {
        QTimer::singleShot(0, this, [this, error] { finish({}, error); });
        return true;
    }
    m_deadline.start(60000);
    request(source.file);
    return true;
}
void GeoListUpdater::request(const QUrl &url)
{
    m_body.clear();
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    req.setRawHeader("User-Agent", "FreeTunnel-GeoLists");
    m_reply = m_manager->get(req);
    m_reply->setReadBufferSize(256 * 1024);
    connect(m_reply, &QNetworkReply::readyRead, this, &GeoListUpdater::consumeAvailable);
    connect(m_reply, &QNetworkReply::finished, this, &GeoListUpdater::downloaded);
}
void GeoListUpdater::consumeAvailable()
{
    m_body += m_reply->read(GeoListCache::maxBytes + 1 - m_body.size());
    if (m_body.size() > GeoListCache::maxBytes)
        finish({}, tr("Geo list is too large"));
}
void GeoListUpdater::downloaded()
{
    consumeAvailable();
    if (!m_reply)
        return;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl redirect = m_reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (status >= 300 && status < 400 && !redirect.isEmpty()) {
        QUrl target = m_reply->url().resolved(redirect);
        target.setFragment({});
        if (++m_redirects > 5 || target.scheme() != QStringLiteral("https")
                || target.host().isEmpty() || !target.userInfo().isEmpty()) {
            finish({}, tr("Geo list server returned an invalid redirect"));
            return;
        }
        m_reply->deleteLater();
        m_reply = nullptr;
        request(target);
        return;
    }
    if (m_reply->error() != QNetworkReply::NoError || status != 200) {
        finish({}, tr("Could not download geo list (HTTP %1)").arg(status));
        return;
    }
    m_deadline.stop();
    m_reply->deleteLater();
    m_reply = nullptr;
    struct Parsed { CachedGeoList list; QString error; };
    const auto result = std::make_shared<Parsed>();
    QThread *worker = QThread::create([directory = m_directory, definition = m_definition,
                                      body = std::move(m_body), result] {
        GeoListCache(directory).store(definition.source, definition.kind,
                definition.direction, body, &result->list, &result->error);
    });
    connect(worker, &QThread::finished, this, [this, result] {
        finish(result->list, result->error);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
}
void GeoListUpdater::finish(const CachedGeoList &list, const QString &error)
{
    m_deadline.stop();
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_body.clear();
    m_busy = false;
    emit finished(m_definition, list, error);
}
}
