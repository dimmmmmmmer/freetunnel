// cppcheck-suppress-file missingIncludeSystem
// What macOS and Windows are asked to run with administrator rights to start the
// helper, read back the way each of them reads it.
//
// Most users are on these two, and only the Linux argv had a test: the AppleScript
// and the ShellExecuteExW parameters were built inside the functions that launch
// them, where nothing could look. A quoting mistake here is the helper starting
// with the wrong token file — the GUI then waits a minute for a helper that cannot
// authenticate and blames the elevation — or, on macOS, a path that ends its
// quotes early and becomes a command run as root. Each half of the escaping had a
// test of its own (test_appuiutils); the two together, inside the script, did not.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include "vpn/vpn_helper_launch.h"

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

// The string inside an AppleScript literal, as AppleScript reads it: \" is a
// quote and \\ a backslash. Strict on purpose: any other escape, or a quote left
// bare, means the text was not escaped the way it has to be, and is an error
// rather than something to guess at.
bool appleScriptUnquote(const QString &literal, QString *out)
{
    out->clear();
    for (qsizetype i = 0; i < literal.size(); ++i) {
        const QChar c = literal.at(i);
        if (c == QLatin1Char('"'))
            return false;
        if (c != QLatin1Char('\\')) {
            out->append(c);
            continue;
        }
        if (++i == literal.size())
            return false;
        const QChar next = literal.at(i);
        if (next != QLatin1Char('\\') && next != QLatin1Char('"'))
            return false;
        out->append(next);
    }
    return true;
}

// The shell command out of `do shell script "…" with administrator privileges`.
QString shellCommandOf(const QString &script)
{
    const QString head = QStringLiteral("do shell script \"");
    const QString tail = QStringLiteral("\" with administrator privileges");
    if (!script.startsWith(head) || !script.endsWith(tail))
        return QString();
    QString command;
    if (!appleScriptUnquote(script.mid(head.size(), script.size() - head.size() - tail.size()), &command))
        return QString();
    return command;
}

// The arguments a program started with `parameters` sees after its own name.
QStringList argumentsOf(const QString &parameters)
{
#if defined(Q_OS_WIN)
    // The parser the helper's own QCoreApplication::arguments() goes through.
    const std::wstring line = L"FreeTunnel.exe " + parameters.toStdWString();
    int argc = 0;
    LPWSTR *argv = ::CommandLineToArgvW(line.c_str(), &argc);
    QStringList out;
    for (int i = 1; argv != nullptr && i < argc; ++i)
        out << QString::fromWCharArray(argv[i]);
    ::LocalFree(argv);
    return out;
#else
    // The same reading for anything with no backslash before a quote, which a
    // file path never has; the Windows run of this test uses the real parser.
    return QProcess::splitCommand(parameters);
#endif
}

bool writeFile(const QString &path, const QByteArray &body)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(body) == body.size();
}

QStringList expectedArguments(quint16 port, const QString &tokenPath)
{
    return {QStringLiteral("--helper"), QStringLiteral("--port"), QString::number(port),
            QStringLiteral("--token-file"), tokenPath};
}

} // namespace

class TestHelperElevation : public QObject {
    Q_OBJECT

private slots:
    void macScriptStartsTheHelperWithExactlyItsArguments();
    void windowsParametersReachTheHelperAsTheyWereMeant_data();
    void windowsParametersReachTheHelperAsTheyWereMeant();
};

// The AppleScript is unquoted as AppleScript unquotes it and the command inside
// is run by /bin/sh, as `do shell script` runs it, with a stand-in for the helper
// that writes down the arguments it got. Both paths carry everything a shell or
// an AppleScript string treats specially. Only the elevation itself is left out.
void TestHelperElevation::macScriptStartsTheHelperWithExactlyItsArguments()
{
#if defined(Q_OS_WIN)
    QSKIP("runs the script's command with /bin/sh");
#else
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = root.filePath(QStringLiteral("it's \"Free\" Tunnel $(touch pwned) `touch pwned` \\ x"));
    QVERIFY(QDir().mkpath(dir));
    const QString exe = dir + QStringLiteral("/Free Tunnel");
    const QString seen = root.filePath(QStringLiteral("argv"));
    QVERIFY(writeFile(exe, QByteArrayLiteral("#!/bin/sh\n"
                                             "printf '%s\\n' \"$@\" > \"$FT_TEST_ARGV.part\"\n"
                                             "mv \"$FT_TEST_ARGV.part\" \"$FT_TEST_ARGV\"\n")));
    QVERIFY(QFile::setPermissions(exe, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const QString tokenPath = dir + QStringLiteral("/.fthelper-'\"$HOME\"'");
    const quint16 port = 51234;

    const QString command = shellCommandOf(freetunnel::macHelperElevationScript(exe, port, tokenPath));
    QVERIFY2(!command.isEmpty(), "not a well-formed `do shell script` for AppleScript");

    QProcess sh;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("FT_TEST_ARGV"), seen);
    env.insert(QStringLiteral("TMPDIR"), root.path()); // where the script's own log goes
    sh.setProcessEnvironment(env);
    sh.setWorkingDirectory(root.path());
    sh.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command});
    QVERIFY(sh.waitForFinished(10000));
    QCOMPARE(sh.exitCode(), 0);

    // The helper is started in the background, so its record comes after sh exits.
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(seen), 10000);
    QFile record(seen);
    QVERIFY(record.open(QIODevice::ReadOnly));
    const QStringList got = QString::fromUtf8(record.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(got, expectedArguments(port, tokenPath));
    QVERIFY2(!QFile::exists(root.filePath(QStringLiteral("pwned"))), "a path was run as a command");
#endif
}

void TestHelperElevation::windowsParametersReachTheHelperAsTheyWereMeant_data()
{
    QTest::addColumn<QString>("folder");
    // A profile folder is named after the account, which may be two words, and in
    // any script; and a temporary folder may sit under such a profile.
    QTest::newRow("plain") << QStringLiteral("FreeTunnel");
    QTest::newRow("spaces") << QStringLiteral("John Smith/AppData/Local/FreeTunnel");
    QTest::newRow("cyrillic") << QStringLiteral("Иван Петров/AppData/Local/FreeTunnel");
    QTest::newRow("punctuation") << QStringLiteral("O'Brien & Co (2) [x]; 100%/FreeTunnel");
}

// What ShellExecuteExW is given must come apart, on the helper's side, into the
// very arguments the GUI meant, and the helper's own parser must then find the
// port and the token in them.
void TestHelperElevation::windowsParametersReachTheHelperAsTheyWereMeant()
{
    QFETCH(QString, folder);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dir = root.filePath(folder);
    QVERIFY(QDir().mkpath(dir));
    const QString tokenPath = dir + QStringLiteral("/.fthelper-a1B2c3");
    QVERIFY(writeFile(tokenPath, QByteArrayLiteral("0123456789abcdef0123456789abcdef")));
    const quint16 port = 61000;

    const QStringList args = argumentsOf(freetunnel::windowsHelperParameters(port, tokenPath));
    QCOMPARE(args, expectedArguments(port, tokenPath));

    const freetunnel::HelperLaunchConfig cfg =
            freetunnel::parseHelperLaunchArgs(QStringList{QStringLiteral("FreeTunnel.exe")} + args);
    QCOMPARE(cfg.port, port);
    QCOMPARE(cfg.token, QStringLiteral("0123456789abcdef0123456789abcdef"));
}

QTEST_MAIN(TestHelperElevation)
#include "test_helper_elevation.moc"
