// cppcheck-suppress-file missingIncludeSystem
#pragma once

// A stand-in for the subscription fetcher, for the Backend tests. It connects
// to nothing: it keeps each request it is handed, and answers when the test
// says, from outside start() as the real fetcher does. Installed through the
// controller's test hook, so every fetch Backend makes is one of these.

#include <QByteArray>
#include <QList>
#include <QPointer>
#include <QString>

#include <algorithm>

#include "app/SubscriptionController.h"
#include "core/Subscription.h"
#include "core/SubscriptionFetcher.h"

class FakeSubscriptionNetwork {
public:
    class Fetcher : public freetunnel::SubscriptionFetcher {
    public:
        Fetcher(FakeSubscriptionNetwork *network, QObject *parent)
            : SubscriptionFetcher(QStringLiteral("test"), parent)
            , m_network(network)
        {
        }
        void start(const freetunnel::SubscriptionFetchRequest &request) override { m_network->started(this, request); }
        void abort() override { m_network->aborted(this); }

    private:
        FakeSubscriptionNetwork *m_network;
    };

    void install(SubscriptionController *controller)
    {
        controller->setFetcherFactoryForTest([this](QObject *parent) { return new Fetcher(this, parent); });
    }

    // Every fetch started, the ones answered and aborted included.
    const QList<freetunnel::SubscriptionFetchRequest> &requests() const { return m_requests; }
    int fetches() const { return static_cast<int>(m_requests.size()); }
    int waiting() const { return static_cast<int>(m_waiting.size()); }
    int mostAtOnce() const { return m_mostAtOnce; }
    int aborts() const { return m_aborts; }

    // Answers the fetch waiting longest, or the one for `url` (with its
    // userinfo, as Backend asked for it). False when none is waiting.
    bool answer(const freetunnel::SubscriptionFetchResult &result, const QString &url = QString())
    {
        const auto it = std::find_if(m_waiting.begin(), m_waiting.end(), [&url](const Waiting &w) {
            return url.isEmpty() || w.request.url == url;
        });
        if (it == m_waiting.end() || !it->fetcher)
            return false;
        const QPointer<Fetcher> fetcher = it->fetcher;
        m_waiting.erase(it);
        emit fetcher->finished(result);
        return true;
    }

    static freetunnel::SubscriptionFetchResult document(const QByteArray &json)
    {
        freetunnel::SubscriptionFetchResult result;
        freetunnel::SubscriptionParseError error;
        result.document = freetunnel::parseSubscriptionJson(json, &error);
        if (!result.document) {
            result.error = freetunnel::SubscriptionFetchError::Document;
            result.documentError = error;
        }
        result.httpStatus = 200;
        return result;
    }

    static freetunnel::SubscriptionFetchResult failure(freetunnel::SubscriptionFetchError error, int status = 0)
    {
        freetunnel::SubscriptionFetchResult result;
        result.error = error;
        result.httpStatus = status;
        return result;
    }

private:
    struct Waiting {
        QPointer<Fetcher> fetcher;
        freetunnel::SubscriptionFetchRequest request;
    };

    void started(Fetcher *fetcher, const freetunnel::SubscriptionFetchRequest &request)
    {
        m_requests.append(request);
        m_waiting.append({fetcher, request});
        m_mostAtOnce = std::max(m_mostAtOnce, static_cast<int>(m_waiting.size()));
    }
    void aborted(Fetcher *fetcher)
    {
        ++m_aborts;
        m_waiting.erase(std::remove_if(m_waiting.begin(), m_waiting.end(),
                                       [fetcher](const Waiting &w) { return w.fetcher == fetcher; }),
                        m_waiting.end());
    }

    QList<freetunnel::SubscriptionFetchRequest> m_requests;
    QList<Waiting> m_waiting;
    int m_mostAtOnce = 0;
    int m_aborts = 0;
};

// A document as the endpoint serves one, with what a test changes.
inline QByteArray subscriptionDocument(const QString &hostname, const QString &address,
                                       const QString &password = QStringLiteral("s3cret"),
                                       const QString &extra = QString())
{
    return QStringLiteral(R"({"version":1,"hostname":"%1","address":"%2","username":"alice","password":"%3")")
                   .arg(hostname, address, password)
            .append(extra)
            .append(QLatin1Char('}'))
            .toUtf8();
}
