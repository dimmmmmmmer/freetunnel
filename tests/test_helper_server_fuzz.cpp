// cppcheck-suppress-file missingIncludeSystem
// Random input against the REAL helper (tests/real_helper.h): the code that runs
// as root and parses everything the GUI sends.
//
// test_helper_ipc_fuzz throws the same kind of input at MockHelperServer, a
// hand-written double with its own parser and its own pre-auth policy, so it
// says nothing about vpn_helper_server.cpp; the real one had two hand-written
// hostile inputs and no random ones, and none at all after authentication, where
// every command reaches the VPN client inside the helper. A crash here is a
// denial of service against a root process, and under the sanitizer job a
// memory error in it (ASan) ends the child with a non-zero status, which these
// check. Undefined behaviour (UBSan) is only reported on the child's stderr
// there, and does not fail them.
//
// The seeds are fixed so a failure repeats; FT_FUZZ_SEED picks others, and the
// one in use is printed either way.
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTemporaryDir>

#include <iterator>
#include <utility>

#include "real_helper.h"

namespace {

quint32 fuzzSeed(const char *test, quint32 fixed)
{
    bool fromEnv = false;
    const quint32 env = qEnvironmentVariable("FT_FUZZ_SEED").toUInt(&fromEnv);
    const quint32 seed = fromEnv ? env : fixed;
    qInfo("%s: seed %u (FT_FUZZ_SEED=%u repeats it)", test, seed, seed);
    return seed;
}

QByteArray randomBytes(QRandomGenerator &rng, int maxLen)
{
    QByteArray out(rng.bounded(1, maxLen + 1), Qt::Uninitialized);
    for (char &c : out)
        c = static_cast<char>(rng.bounded(256));
    return out;
}

QString randomString(QRandomGenerator &rng, int maxLen)
{
    // Quotes, backslashes, newlines, NULs and non-ASCII, not only letters: the
    // values end up in a TOML parser, a JSON writer and file names.
    static const char16_t kAlphabet[] = u"aZ09 ._-/\\\"'=[]{}#\n\r\t\0\u00E9\u044F\u2192\U0001F600";
    QString out;
    const int len = rng.bounded(0, maxLen + 1);
    for (int i = 0; i < len; ++i)
        out += QChar(kAlphabet[rng.bounded(int(std::size(kAlphabet)) - 1)]);
    return out;
}

QJsonValue randomValue(QRandomGenerator &rng, int depth = 0)
{
    switch (rng.bounded(depth < 2 ? 7 : 5)) {
    case 0: return QJsonValue();
    case 1: return rng.bounded(2) == 1;
    case 2: return rng.generateDouble() * 1e12 - 5e11;
    case 3: return randomString(rng, 64);
    case 4: return QString::fromLatin1(randomBytes(rng, 32));
    case 5: {
        QJsonArray a;
        for (int i = rng.bounded(0, 6); i > 0; --i)
            a.append(randomValue(rng, depth + 1));
        return a;
    }
    default: {
        QJsonObject o;
        for (int i = rng.bounded(0, 4); i > 0; --i)
            o.insert(randomString(rng, 8), randomValue(rng, depth + 1));
        return o;
    }
    }
}

// Something the helper might be sent: one of the commands it knows, or not, with
// any of the fields it reads — of any type. "quit" is left out after
// authentication on purpose: it is the one command that ends the session.
QJsonObject randomCommand(QRandomGenerator &rng, bool authenticated)
{
    static const QStringList kCommands{
            QStringLiteral("hello"),           QStringLiteral("auth"),
            QStringLiteral("setExclusions"),   QStringLiteral("setRoutes"),
            QStringLiteral("setAppRules"),     QStringLiteral("setMode"),
            QStringLiteral("setSplitRouting"), QStringLiteral("setKillSwitch"),
            QStringLiteral("setKillSwitchPortsFromConfig"),
            QStringLiteral("setLogLevel"),     QStringLiteral("connect"),
            QStringLiteral("disconnect"),      QStringLiteral("quit")};
    static const QStringList kFields{
            QStringLiteral("nonce"),      QStringLiteral("proof"),          QStringLiteral("domains"),
            QStringLiteral("excluded"),   QStringLiteral("rules"),          QStringLiteral("selective"),
            QStringLiteral("enabled"),    QStringLiteral("level"),          QStringLiteral("configToml"),
            QStringLiteral("configPath"), QStringLiteral("loggingEnabled"), QStringLiteral("logPath")};
    QJsonObject c;
    const int known = authenticated ? int(kCommands.size()) - 1 : int(kCommands.size());
    if (rng.bounded(8) == 0)
        c.insert(QStringLiteral("cmd"), randomValue(rng));
    else
        c.insert(QStringLiteral("cmd"), kCommands.at(rng.bounded(known)));
    for (const QString &field : kFields) {
        if (rng.bounded(2) == 1)
            c.insert(field, randomValue(rng));
    }
    // Now and then a config that gets past the parser, so the connect path
    // inside the helper runs and not only its refusals.
    if (c.value(QStringLiteral("cmd")).toString() == QLatin1String("connect") && rng.bounded(3) == 0) {
        c.insert(QStringLiteral("configToml"),
                 realhelper::minimalConfigToml() + randomString(rng, 40));
    }
    return c;
}

// A burst of lines: raw bytes, or commands.
QByteArray randomLine(QRandomGenerator &rng, bool authenticated)
{
    if (rng.bounded(3) == 0)
        return randomBytes(rng, 2048) + '\n';
    return realhelper::line(randomCommand(rng, authenticated));
}

} // namespace

class TestHelperServerFuzz : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void preAuthInputNeverTakesTheHelperDown();
    void postAuthInputNeverTakesTheHelperDown();

private:
    QTemporaryDir m_dir;
};

void TestHelperServerFuzz::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY2(QFile::exists(QStringLiteral(FT_TEST_HELPER_BINARY)),
             "helper binary was not built next to this test");
    QVERIFY2(realhelper::ensureWintunPlaceholder(), "could not place the wintun.dll placeholder");
}

// Anyone on the machine can reach the port before authenticating. Whatever they
// send, one connection after another, the helper must still be there — and
// still let the genuine client in — afterwards.
void TestHelperServerFuzz::preAuthInputNeverTakesTheHelperDown()
{
    QRandomGenerator rng(fuzzSeed("preAuthInputNeverTakesTheHelperDown", 20251001u));
    const QString token = QStringLiteral("token-for-pre-auth-fuzz");
    realhelper::Process helper(m_dir.path());
    QVERIFY(helper.start(token));

    for (int i = 0; i < 60; ++i) {
        QTcpSocket peer;
        QVERIFY(helper.connectTo(peer));
        for (int n = rng.bounded(1, 4); n > 0; --n)
            peer.write(randomLine(rng, false));
        peer.flush();
        // Most of it is dropped at once; whatever is not is left to the deadline.
        peer.waitForDisconnected(200);
        peer.abort();
        QVERIFY2(helper.running(), qPrintable(QStringLiteral("the helper died on input %1").arg(i)));
    }

    QTcpSocket gui;
    QVERIFY(helper.connectTo(gui));
    QVERIFY2(realhelper::authenticate(gui, token), "the genuine client was locked out afterwards");
}

// After authentication every command reaches the VPN client inside the helper,
// and the session is the GUI's only channel to it. Garbage must neither end the
// helper nor the session, and the helper must still leave cleanly — exit code 0,
// not a crash in its teardown — when the GUI goes.
void TestHelperServerFuzz::postAuthInputNeverTakesTheHelperDown()
{
    QRandomGenerator rng(fuzzSeed("postAuthInputNeverTakesTheHelperDown", 20251002u));
    for (int round = 0; round < 3; ++round) {
        const QString token = QStringLiteral("token-for-post-auth-fuzz-%1").arg(round);
        realhelper::Process helper(m_dir.path());
        QVERIFY(helper.start(token));
        {
            QTcpSocket gui;
            QVERIFY(helper.connectTo(gui));
            QVERIFY(realhelper::authenticate(gui, token));
            int owed = 0; // refusals due for the lines since the last ping
            for (int i = 1; i <= 150; ++i) {
                const QByteArray sent = randomLine(rng, true);
                gui.write(sent);
                owed += realhelper::refusalsOwedFor(sent);
                if (i % 50 != 0)
                    continue;
                gui.flush();
                QVERIFY2(helper.running(),
                         qPrintable(QStringLiteral("the helper died in round %1, by line %2").arg(round).arg(i)));
                QVERIFY2(realhelper::answersOnSession(gui, std::exchange(owed, 0)),
                         qPrintable(QStringLiteral("the session stopped answering in round %1, by line %2")
                                            .arg(round)
                                            .arg(i)));
            }
            gui.abort();
        }
        QVERIFY2(helper.process()->waitForFinished(15000), "the helper outlived its GUI");
        QCOMPARE(helper.process()->exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.process()->exitCode(), 0);
    }
}

QTEST_MAIN(TestHelperServerFuzz)
#include "test_helper_server_fuzz.moc"
