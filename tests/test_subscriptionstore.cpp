// cppcheck-suppress-file missingIncludeSystem
// subscriptions.json: what FreeTunnel keeps about the configs that update from a
// TrustTunnel subscription, besides the configs themselves. QStandardPaths test
// mode keeps it out of the real folder.
#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include "core/ConfigStore.h"
#include "core/SubscriptionStore.h"

using freetunnel::SubscriptionRecord;
using freetunnel::SubscriptionRecords;

class TestSubscriptionStore : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void nothingWhenThereIsNoFile();
    void itSitsNextToTheConfigList();
    void aRecordRoundTrips();
    void theFileIsOwnerOnly();
    void aDamagedFileReadsAsEmptyAndStaysUntilTheNextSave();
    void aFileFromANewerVersionReadsAsEmpty();
    void aRecordWithoutASourceIdOfOursIsLeftOut();
    void aSaveThatCannotBeWrittenSaysSo();
};

namespace {

SubscriptionRecord sample()
{
    SubscriptionRecord r;
    r.sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.origin = QStringLiteral("https://vpn.example.com");
    r.identity = QStringLiteral("abc123");
    r.lastFetchedAt = 1'800'000'000;
    r.lastAttemptAt = 1'800'000'100;
    r.lastStatus = QStringLiteral("network");
    r.failures = 2;
    r.autoRefresh = false;
    r.nextAttemptAt = 1'800'003'700;
    r.hintSeconds = 7200;
    r.heldHostname = QStringLiteral("vpn2.example.com");
    return r;
}

void writeFile(const QByteArray &bytes)
{
    QFile f(freetunnel::subscriptionsPath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(bytes);
}

QByteArray readFile()
{
    QFile f(freetunnel::subscriptionsPath());
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

void TestSubscriptionStore::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SubscriptionStoreTest"));
    QStandardPaths::setTestModeEnabled(true);
}

void TestSubscriptionStore::init()
{
    const QString path = freetunnel::subscriptionsPath();
    if (QFileInfo(path).isDir())
        QDir(path).removeRecursively();
    QFile::remove(path);
}

void TestSubscriptionStore::nothingWhenThereIsNoFile()
{
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
}

void TestSubscriptionStore::itSitsNextToTheConfigList()
{
    QCOMPARE(QFileInfo(freetunnel::subscriptionsPath()).absolutePath(), QFileInfo(storagePath()).absolutePath());
    QCOMPARE(QFileInfo(freetunnel::subscriptionsPath()).fileName(), QStringLiteral("subscriptions.json"));
}

void TestSubscriptionStore::aRecordRoundTrips()
{
    const SubscriptionRecord r = sample();
    SubscriptionRecord fresh;
    fresh.sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString path = QStringLiteral("/configs/Work.toml");
    QVERIFY(freetunnel::saveSubscriptions({{path, r}, {QStringLiteral("/configs/New.toml"), fresh}}));

    const SubscriptionRecords loaded = freetunnel::loadSubscriptions();
    QCOMPARE(loaded.size(), 2);
    const SubscriptionRecord back = loaded.value(path);
    QCOMPARE(back.sourceId, r.sourceId);
    QCOMPARE(back.origin, r.origin);
    QCOMPARE(back.identity, r.identity);
    QCOMPARE(back.lastFetchedAt, r.lastFetchedAt);
    QCOMPARE(back.lastAttemptAt, r.lastAttemptAt);
    QCOMPARE(back.lastStatus, r.lastStatus);
    QCOMPARE(back.failures, r.failures);
    QCOMPARE(back.autoRefresh, r.autoRefresh);
    QCOMPARE(back.nextAttemptAt, r.nextAttemptAt);
    QCOMPARE(back.hintSeconds, r.hintSeconds);
    QCOMPARE(back.heldHostname, r.heldHostname);
    // The defaults hold too: a new record refreshes by itself.
    QVERIFY(loaded.value(QStringLiteral("/configs/New.toml")).autoRefresh);

    // The address is not in it, nor anything else that is secret: the record
    // has nowhere to put one, and the file shows none.
    QVERIFY(!readFile().contains("https://alice"));
}

void TestSubscriptionStore::theFileIsOwnerOnly()
{
#if defined(Q_OS_UNIX)
    QVERIFY(freetunnel::saveSubscriptions({{QStringLiteral("/configs/Work.toml"), sample()}}));
    const QFile::Permissions perms = QFile::permissions(freetunnel::subscriptionsPath());
    QVERIFY2(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ReadOther
                        | QFileDevice::WriteOther)),
             "subscriptions.json can be read by other local users");
    QVERIFY(perms & QFileDevice::ReadOwner);
    QVERIFY(perms & QFileDevice::WriteOwner);
#else
    QSKIP("no POSIX mode bits: the file inherits the app directory ACL");
#endif
}

// Damaged by a crash or by hand: nothing is linked while it cannot be read, and
// it is left for someone to look at, until the next save writes a good one.
void TestSubscriptionStore::aDamagedFileReadsAsEmptyAndStaysUntilTheNextSave()
{
    const QByteArray damaged = QByteArrayLiteral("{\"version\":1,\"configs\":{\"/a.toml\":{\"source_id\"");
    writeFile(damaged);
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    QCOMPARE(readFile(), damaged);
    QVERIFY(freetunnel::saveSubscriptions({{QStringLiteral("/configs/Work.toml"), sample()}}));
    QCOMPARE(freetunnel::loadSubscriptions().size(), 1);
}

void TestSubscriptionStore::aFileFromANewerVersionReadsAsEmpty()
{
    QJsonObject record;
    record.insert(QStringLiteral("source_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QJsonObject configs{{QStringLiteral("/configs/Work.toml"), record}};
    writeFile(QJsonDocument(QJsonObject{{QStringLiteral("version"), 2}, {QStringLiteral("configs"), configs}}).toJson());
    QVERIFY(freetunnel::loadSubscriptions().isEmpty());
    writeFile(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("configs"), configs}}).toJson());
    QCOMPARE(freetunnel::loadSubscriptions().size(), 1);
}

// The source id names an entry in the credential store; one this app never
// makes, such as an empty one shared by every such record, names none of its.
void TestSubscriptionStore::aRecordWithoutASourceIdOfOursIsLeftOut()
{
    QJsonObject good;
    good.insert(QStringLiteral("source_id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QJsonObject configs{{QStringLiteral("/configs/Good.toml"), good},
                              {QStringLiteral("/configs/Empty.toml"), QJsonObject{{QStringLiteral("source_id"), ""}}},
                              {QStringLiteral("/configs/Path.toml"),
                               QJsonObject{{QStringLiteral("source_id"), QStringLiteral("../../etc")}}}};
    writeFile(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("configs"), configs}}).toJson());
    const SubscriptionRecords loaded = freetunnel::loadSubscriptions();
    QCOMPARE(loaded.keys(), QStringList{QStringLiteral("/configs/Good.toml")});
    // A record that does not say refreshes by itself, as a new one does.
    QVERIFY(loaded.value(QStringLiteral("/configs/Good.toml")).autoRefresh);
}

// Something in the way of the file: the save says it failed, and the caller can
// leave nothing behind that the file was to record.
void TestSubscriptionStore::aSaveThatCannotBeWrittenSaysSo()
{
    QVERIFY(QDir().mkpath(freetunnel::subscriptionsPath()));
    QVERIFY(!freetunnel::saveSubscriptions({{QStringLiteral("/configs/Work.toml"), sample()}}));
    QVERIFY(QFileInfo(freetunnel::subscriptionsPath()).isDir());
}

QTEST_GUILESS_MAIN(TestSubscriptionStore)
#include "test_subscriptionstore.moc"
