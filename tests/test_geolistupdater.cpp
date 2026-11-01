// cppcheck-suppress-file missingIncludeSystem
#include "core/GeoListUpdater.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QtTest>
#include <cstring>
using namespace freetunnel;
class Reply final : public QNetworkReply {
public:
    Reply(const QNetworkRequest &request, QByteArray body, int status, QUrl redirect, QObject *parent)
        : QNetworkReply(parent), m_body(std::move(body)) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!redirect.isEmpty()) setAttribute(QNetworkRequest::RedirectionTargetAttribute, redirect);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { emit readyRead(); setFinished(true); emit finished(); });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return m_body.size() - m_pos + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 max) override {
        const auto n = qMin(max, qint64(m_body.size() - m_pos));
        if (!n) return -1;
        memcpy(data, m_body.constData() + m_pos, size_t(n)); m_pos += n; return n;
    }
private:
    QByteArray m_body; qint64 m_pos = 0;
};
class Manager final : public QNetworkAccessManager {
public:
    QByteArray body = "domain:example.com\n";
    int status = 200;
    QUrl redirect;
    QList<QUrl> requests;
    bool redirectOnce = false;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &req, QIODevice *) override {
        requests.append(req.url());
        const bool redirected = redirectOnce && requests.size() > 1;
        return new Reply(req, body, redirected ? 200 : status, redirected ? QUrl{} : redirect, this);
    }
};
class TestGeoListUpdater : public QObject {
    Q_OBJECT
private slots:
    void downloadAndFailedRefresh() {
        QTemporaryDir dir; Manager manager;
        GeoListUpdater updater(dir.path(), nullptr, &manager);
        QSignalSpy done(&updater, &GeoListUpdater::finished);
        GeoListDefinition definition;
        definition.source = QStringLiteral("https://example.com/sites.txt");
        QVERIFY(updater.start(definition));
        QVERIFY(!updater.start(definition));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.at(0).at(2).toString().isEmpty());
        CachedGeoList previous; QString error;
        QVERIFY(GeoListCache(dir.path()).load(definition.source, definition.kind,
                                              definition.direction, &previous, &error));
        manager.body = "<html>unavailable</html>";
        QVERIFY(updater.start(definition));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!done.at(1).at(2).toString().isEmpty());
        CachedGeoList retained;
        QVERIFY(GeoListCache(dir.path()).load(definition.source, definition.kind,
                                              definition.direction, &retained, &error));
        QCOMPARE(retained.entries, previous.entries);
        QVERIFY(!updater.busy());
    }
    void redirectCannotDowngradeOrSendCredentials_data() {
        QTest::addColumn<QUrl>("target");
        QTest::newRow("http") << QUrl("http://example.com/file");
        QTest::newRow("credentials") << QUrl("https://user:password@example.com/file");
    }
    void redirectCannotDowngradeOrSendCredentials() {
        QFETCH(QUrl, target);
        QTemporaryDir dir; Manager manager;
        manager.status = 302; manager.redirect = target;
        GeoListUpdater updater(dir.path(), nullptr, &manager);
        QSignalSpy done(&updater, &GeoListUpdater::finished);
        GeoListDefinition definition; definition.source = QStringLiteral("https://example.com/file");
        QVERIFY(updater.start(definition));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done.first().at(2).toString().isEmpty());
        QCOMPARE(manager.requests.size(), 1);
    }
    void githubStyleRedirectAndLoop() {
        QTemporaryDir dir; Manager manager;
        manager.status = 302; manager.redirect = QUrl("https://assets.example.org/file");
        manager.redirectOnce = true;
        GeoListUpdater updater(dir.path(), nullptr, &manager);
        QSignalSpy done(&updater, &GeoListUpdater::finished);
        GeoListDefinition definition; definition.source = QStringLiteral("https://example.com/sites.txt#category");
        QVERIFY(updater.start(definition));
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.first().at(2).toString().isEmpty());
        QCOMPARE(manager.requests.size(), 2);
        QVERIFY(manager.requests.first().fragment().isEmpty());
        QCOMPARE(manager.requests.last().host(), QStringLiteral("assets.example.org"));
        manager.redirectOnce = false; manager.requests.clear();
        QVERIFY(updater.start(definition));
        QTRY_COMPARE(done.size(), 2);
        QVERIFY(!done.last().at(2).toString().isEmpty());
        QCOMPARE(manager.requests.size(), 6);
    }
    void rejectsBadSourceWithoutNetwork() {
        QTemporaryDir dir; Manager manager;
        GeoListUpdater updater(dir.path(), nullptr, &manager);
        QSignalSpy done(&updater, &GeoListUpdater::finished);
        GeoListDefinition definition; definition.source = QStringLiteral("http://example.com/file");
        QVERIFY(updater.start(definition));
        QCOMPARE(done.size(), 0);
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done.first().at(2).toString().isEmpty());
        QVERIFY(manager.requests.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestGeoListUpdater)
#include "test_geolistupdater.moc"
