// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "vpn/vpn_helper_launch.h"

class TestVpnHelperLaunch : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;

    // A token file named the way the GUI names one (VpnHelperClient::
    // configureProductionHelper): .fthelper-XXXXXX in a directory of its own.
    QString writeTempTokenFile(const QByteArray &body)
    {
        QTemporaryFile tf(m_dir.filePath(QStringLiteral(".fthelper-XXXXXX")));
        tf.setAutoRemove(false);
        if (!tf.open())
            return QString();
        tf.write(body);
        tf.close();
        return tf.fileName();
    }

    QString writeFile(const QString &name, const QByteArray &body)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return QString();
        f.write(body);
        f.close();
        return path;
    }

    static QByteArray contents(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void parseArgsReadsTokenFile();
    void parseArgsRejectsMissingToken();
    void readTokenFileLeavesTheFileToTheGui();
    void aFileNotNamedLikeATokenIsNeitherReadNorRemoved();
    void aLinkNamedLikeATokenIsNotFollowed();
    void aPipeNamedLikeATokenIsNotWaitedOn();
    void noMoreIsReadThanATokenTakes();
};

void TestVpnHelperLaunch::parseArgsReadsTokenFile()
{
    const QString path = writeTempTokenFile("secret-token");
    QVERIFY(!path.isEmpty());

    const QStringList args = {
        QStringLiteral("FreeTunnel"),
        QStringLiteral("--helper"),
        QStringLiteral("--port"),
        QStringLiteral("12345"),
        QStringLiteral("--token-file"),
        path,
    };
    const freetunnel::HelperLaunchConfig cfg = freetunnel::parseHelperLaunchArgs(args);
    QCOMPARE(cfg.port, static_cast<quint16>(12345));
    QCOMPARE(cfg.token, QStringLiteral("secret-token"));
    QVERIFY(cfg.ok());
}

void TestVpnHelperLaunch::parseArgsRejectsMissingToken()
{
    const QStringList args = {
        QStringLiteral("FreeTunnel"),
        QStringLiteral("--helper"),
        QStringLiteral("--port"),
        QStringLiteral("12345"),
    };
    const freetunnel::HelperLaunchConfig cfg = freetunnel::parseHelperLaunchArgs(args);
    QVERIFY(!cfg.ok());
}

// The helper runs elevated and used to delete the file it was given once it had
// read it. The GUI removes its own token file, as the user, once the helper has
// answered (VpnHelperClient::handleReadyEvent) or the attempt is given up — so
// the elevated side has no reason to delete anything, and must not.
void TestVpnHelperLaunch::readTokenFileLeavesTheFileToTheGui()
{
    const QString path = writeTempTokenFile("tok\n");
    QVERIFY(!path.isEmpty());
    QCOMPARE(freetunnel::readHelperTokenFile(path), QStringLiteral("tok"));
    QVERIFY2(QFile::exists(path), "the elevated helper deleted a file");
}

// Anything running as the user can start the genuine helper binary itself, with
// a --token-file of its choosing, behind an elevation prompt that is the real
// one. What that path names must then be left exactly as it was — the helper
// used to read it whole and delete it, as root or Administrator.
void TestVpnHelperLaunch::aFileNotNamedLikeATokenIsNeitherReadNorRemoved()
{
    const QString victim = writeFile(QStringLiteral("victim"), "not-a-token");
    QVERIFY(!victim.isEmpty());

    QCOMPARE(freetunnel::readHelperTokenFile(victim), QString());
    const freetunnel::HelperLaunchConfig cfg = freetunnel::parseHelperLaunchArgs(
            {QStringLiteral("FreeTunnel"), QStringLiteral("--helper"), QStringLiteral("--port"),
             QStringLiteral("12345"), QStringLiteral("--token-file"), victim});
    QVERIFY2(!cfg.ok(), "the helper would have started on a file the GUI never wrote");
    QCOMPARE(contents(victim), QByteArray("not-a-token"));

    // A name that merely contains the prefix is not one either.
    const QString lookalike = writeFile(QStringLiteral("x.fthelper-abc"), "not-a-token");
    QCOMPARE(freetunnel::readHelperTokenFile(lookalike), QString());
    QVERIFY(QFile::exists(lookalike));
}

// A token file's name is easy to give to something else. A symlink named like
// one leads to whatever it points at; a hard link IS whatever it is a second
// name for — with hard-link protection off, as on macOS, that can be a file only
// root may read. Either way the target stays untouched and unread.
void TestVpnHelperLaunch::aLinkNamedLikeATokenIsNotFollowed()
{
#if !defined(Q_OS_UNIX)
    QSKIP("symlinks and hard links need privileges or developer mode on Windows");
#else
    const QString victim = writeFile(QStringLiteral("victim"), "not-a-token");
    QVERIFY(!victim.isEmpty());

    const QString symlink = m_dir.filePath(QStringLiteral(".fthelper-symlink"));
    QVERIFY(::symlink(QFile::encodeName(victim).constData(),
                      QFile::encodeName(symlink).constData())
            == 0);
    QCOMPARE(freetunnel::readHelperTokenFile(symlink), QString());

    const QString hardlink = m_dir.filePath(QStringLiteral(".fthelper-hardlink"));
    QVERIFY(::link(QFile::encodeName(victim).constData(), QFile::encodeName(hardlink).constData())
            == 0);
    QCOMPARE(freetunnel::readHelperTokenFile(hardlink), QString());

    QCOMPARE(contents(victim), QByteArray("not-a-token"));
    QVERIFY(QFileInfo(symlink).isSymLink());
#endif
}

// A pipe named like a token file: with nothing writing to it, opening it to read
// would wait for a writer for ever, and the helper would sit elevated doing
// nothing; with a writer, it would read whatever that writer chose. Neither is a
// file the GUI wrote. The read runs on a thread of its own so that a helper that
// hangs fails the test instead of hanging it.
void TestVpnHelperLaunch::aPipeNamedLikeATokenIsNotWaitedOn()
{
#if !defined(Q_OS_UNIX)
    QSKIP("POSIX pipes");
#else
    const QString fifo = m_dir.filePath(QStringLiteral(".fthelper-fifo"));
    QVERIFY(::mkfifo(QFile::encodeName(fifo).constData(), 0600) == 0);

    auto done = std::make_shared<std::promise<QString>>();
    std::future<QString> token = done->get_future();
    std::thread([done, fifo]() { done->set_value(freetunnel::readHelperTokenFile(fifo)); })
            .detach();
    QVERIFY2(token.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
             "reading a pipe that nothing writes to never returned");
    QCOMPARE(token.get(), QString());

    // And with someone at the other end, ready with a token of their own. A
    // writer can only open a pipe that has a reader, so the test holds one too
    // (and never reads from it).
    const QByteArray name = QFile::encodeName(fifo);
    const int reader = ::open(name.constData(), O_RDONLY | O_NONBLOCK);
    QVERIFY(reader >= 0);
    const int writer = ::open(name.constData(), O_WRONLY | O_NONBLOCK);
    const auto closeBoth = qScopeGuard([&] {
        if (writer >= 0)
            ::close(writer);
        ::close(reader);
    });
    QVERIFY(writer >= 0);
    QCOMPARE(::write(writer, "fifo-token", 10), ssize_t(10));
    QCOMPARE(freetunnel::readHelperTokenFile(fifo), QString());
#endif
}

// The token is 32 hex digits. A bigger file is not one the GUI wrote — and
// reading it to the end, as the helper did, never finishes on one that has none.
void TestVpnHelperLaunch::noMoreIsReadThanATokenTakes()
{
    const QString big = writeTempTokenFile(QByteArray(4096, 'a'));
    QVERIFY(!big.isEmpty());
    QCOMPARE(freetunnel::readHelperTokenFile(big), QString());

    const QString token = QStringLiteral("0123456789abcdef0123456789abcdef");
    const QString real = writeTempTokenFile(token.toLatin1());
    QVERIFY(!real.isEmpty());
    QCOMPARE(freetunnel::readHelperTokenFile(real), token);
}

QTEST_MAIN(TestVpnHelperLaunch)
#include "test_vpn_helper_launch.moc"
