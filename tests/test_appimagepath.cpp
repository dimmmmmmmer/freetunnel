// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include "core/AppImagePath.h"
#include "app/PlatformAutoStart.h"

// The decision these functions make picks the binary that a pkexec/sudo prompt
// will execute as root, and the path an autostart entry will launch. Both used to
// be answered from $APPIMAGE/$APPDIR, which the attacker in the threat model
// controls; the tests below pin that the answer now comes from the kernel —
// mountinfo, or the process that unpacked the AppImage — and that nothing an
// environment variable can say gets a foot in the door.
class TestAppImagePath : public QObject {
    Q_OBJECT
private slots:
    void findsTheAppImageBehindAFuseMount();
    void picksTheDeepestMountNotTheLongestSharedPrefix();
    void ignoresNonFuseMounts();
    void ignoresAMountThatDoesNotContainTheExecutable();
    void decodesOctalEscapesInPaths();
    void anUnpackedCopyNamesItsAppImageByContent();
    void anUnpackedCopyNeedsTheRuntimesDirectoryName();
    void runningAppImageFindsTheFileThatUnpackedThisProcess();
    void autoStartTargetIsUnquoted();
    void autoStartTargetHandlesAMissingExecLine();
    void autoStartProgramIsReadBackOutOfThePlist();
    void autoStartProgramIsReadBackOutOfTheRunValue();
    void windowsAutoStartIsOffWhenItsProgramIsGone();
};

namespace {

// Makes a copy of this binary report what runningAppImage() says about it.
const char kReportRunningAppImage[] = "--report-running-appimage";

QString md5Hex(const QString &path)
{
    QFile f(path);
    QCryptographicHash md5(QCryptographicHash::Md5);
    if (!f.open(QIODevice::ReadOnly) || !md5.addData(&f))
        return QString();
    return QString::fromLatin1(md5.result().toHex());
}

#ifdef Q_OS_LINUX
// Run a copy of this test binary from <dir>/<extractDirName>/usr/bin/FreeTunnel,
// as a child of this process, and return the lines it reports: the AppImage, the
// options to start it with, and the Exec= line of the autostart entry it writes
// (into a configuration directory of its own under <dir>).
QStringList reportFromUnpackedCopy(const QString &dir, const QString &extractDirName)
{
    const QString self = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    const QString root = dir + QLatin1Char('/') + extractDirName;
    const QString bin = root + QStringLiteral("/usr/bin/FreeTunnel");
    if (!QDir().mkpath(QFileInfo(bin).absolutePath()) || !QFile::copy(self, bin))
        return {QStringLiteral("could not copy %1").arg(self)};
    QProcess child;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), root + QStringLiteral("-config"));
    child.setProcessEnvironment(env);
    child.start(bin, {QString::fromLatin1(kReportRunningAppImage)});
    if (!child.waitForFinished(10000))
        return {QStringLiteral("the copy did not finish")};
    return QString::fromUtf8(child.readAllStandardOutput()).split(QLatin1Char('\n'));
}
#endif

// A realistic mountinfo: root filesystem, then the AppImage runtime's squashfuse
// mount. Field layout is
//   id parent major:minor root mountPoint options [optional...] - fstype source superOpts
const char *kAppImageMounts =
        "23 28 0:21 / /proc rw,nosuid,relatime shared:12 - proc proc rw\n"
        "28 1 259:3 / / rw,relatime shared:1 - ext4 /dev/nvme0n1p3 rw\n"
        "412 28 0:52 / /tmp/.mount_FreeTuA1b2c3 ro,nosuid,nodev,relatime shared:9 "
        "- fuse.squashfuse /home/u/Downloads/FreeTunnel-1.1.7-x86_64.AppImage ro,user_id=1000\n";

} // namespace

void TestAppImagePath::findsTheAppImageBehindAFuseMount()
{
    const QString source = freetunnel::fuseMountSourceForPath(
            QString::fromLatin1(kAppImageMounts),
            QStringLiteral("/tmp/.mount_FreeTuA1b2c3/usr/bin/FreeTunnel"));
    QCOMPARE(source, QStringLiteral("/home/u/Downloads/FreeTunnel-1.1.7-x86_64.AppImage"));
}

void TestAppImagePath::picksTheDeepestMountNotTheLongestSharedPrefix()
{
    // Two nested FUSE mounts: the executable lives in the inner one, and mountinfo
    // lists the outer one first. Matching on "first hit" would name the wrong file.
    const QString mounts =
            QString::fromLatin1(kAppImageMounts)
            + QStringLiteral("500 412 0:60 / /tmp/.mount_FreeTuA1b2c3/inner ro,relatime "
                             "- fuse.squashfuse /home/u/other.AppImage ro,user_id=1000\n");
    QCOMPARE(freetunnel::fuseMountSourceForPath(
                     mounts, QStringLiteral("/tmp/.mount_FreeTuA1b2c3/inner/usr/bin/FreeTunnel")),
             QStringLiteral("/home/u/other.AppImage"));
}

void TestAppImagePath::ignoresNonFuseMounts()
{
    // The whole point: a normal /usr/bin install must produce no AppImage at all,
    // so the elevation path falls back to the running executable. This is the case
    // the old $APPDIR=/usr forgery turned into "root runs an attacker's file".
    QVERIFY(freetunnel::fuseMountSourceForPath(QString::fromLatin1(kAppImageMounts),
                                               QStringLiteral("/usr/bin/FreeTunnel"))
                    .isEmpty());
}

void TestAppImagePath::ignoresAMountThatDoesNotContainTheExecutable()
{
    // "/tmp/.mount_FreeTuA1b2c3" must not be treated as containing
    // "/tmp/.mount_FreeTuA1b2c3extra/..." — prefix matching has to respect
    // path component boundaries.
    QVERIFY(freetunnel::fuseMountSourceForPath(
                    QString::fromLatin1(kAppImageMounts),
                    QStringLiteral("/tmp/.mount_FreeTuA1b2c3extra/usr/bin/FreeTunnel"))
                    .isEmpty());
}

void TestAppImagePath::decodesOctalEscapesInPaths()
{
    const QString mounts =
            QStringLiteral("412 28 0:52 / /tmp/.mount_a\\040b ro,relatime "
                           "- fuse.squashfuse /home/u/My\\040Apps/FreeTunnel.AppImage ro\n");
    QCOMPARE(freetunnel::fuseMountSourceForPath(
                     mounts, QStringLiteral("/tmp/.mount_a b/usr/bin/FreeTunnel")),
             QStringLiteral("/home/u/My Apps/FreeTunnel.AppImage"));
}

// Run without FUSE (--appimage-extract-and-run), the runtime unpacks into
// $TMPDIR/appimage_extracted_<MD5 of the AppImage> and there is no mount to ask.
// It waits as this process's parent, so the kernel still names a file; what makes
// it this AppImage is that its content has the MD5 the directory is named after.
void TestAppImagePath::anUnpackedCopyNamesItsAppImageByContent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString appImage = dir.filePath(QStringLiteral("FreeTunnel.AppImage"));
    {
        QFile f(appImage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not really an AppImage, but it has an MD5");
    }
    const QString digest = md5Hex(appImage);
    QCOMPARE(digest.size(), 32);
    const QString exe = QStringLiteral("/tmp/appimage_extracted_%1/usr/bin/FreeTunnel").arg(digest);

    QCOMPARE(freetunnel::extractedAppImageSource(exe, appImage),
             QFileInfo(appImage).canonicalFilePath());

    // Any other file in its place is not it, whatever the directory says.
    const QString other = dir.filePath(QStringLiteral("other.AppImage"));
    {
        QFile f(other);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("a different file");
    }
    QVERIFY(freetunnel::extractedAppImageSource(exe, other).isEmpty());
    QVERIFY(freetunnel::extractedAppImageSource(exe, dir.path()).isEmpty());
    QVERIFY(freetunnel::extractedAppImageSource(exe, QString()).isEmpty());
}

void TestAppImagePath::anUnpackedCopyNeedsTheRuntimesDirectoryName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString appImage = dir.filePath(QStringLiteral("FreeTunnel.AppImage"));
    {
        QFile f(appImage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("payload");
    }
    const QString digest = md5Hex(appImage);

    // An ordinary install is not an AppImage, whichever process started it: this
    // is the case where answering "yes" would have root run the parent.
    QVERIFY(freetunnel::extractedAppImageSource(QStringLiteral("/usr/bin/FreeTunnel"), appImage)
                    .isEmpty());
    // The runtime writes the digest in lowercase, whole, as the entire name.
    const QString prefix = QStringLiteral("appimage_extracted_");
    for (const QString &name : {prefix + digest.toUpper(), prefix + digest.left(31),
                                QStringLiteral("x_") + prefix + digest,
                                prefix + digest + QStringLiteral("x")}) {
        QVERIFY2(freetunnel::extractedAppImageSource(
                         QStringLiteral("/tmp/%1/usr/bin/FreeTunnel").arg(name), appImage)
                         .isEmpty(),
                 qPrintable(name));
    }
    // The nearest such directory is the one the executable was unpacked into.
    const QString nested = QStringLiteral("/tmp/%1%2/x/%1%3/usr/bin/FreeTunnel")
                                   .arg(prefix, QString(32, QLatin1Char('0')), digest);
    QCOMPARE(freetunnel::extractedAppImageSource(nested, appImage),
             QFileInfo(appImage).canonicalFilePath());
}

// The same, end to end through /proc: this test process stands in for the
// runtime, and a copy of this binary for the payload it unpacked and started.
void TestAppImagePath::runningAppImageFindsTheFileThatUnpackedThisProcess()
{
#ifndef Q_OS_LINUX
    QSKIP("the AppImage runtime is Linux-only");
#else
    const QString self = QFileInfo(QStringLiteral("/proc/self/exe")).canonicalFilePath();
    const QString digest = md5Hex(self);
    QCOMPARE(digest.size(), 32);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QStringList unpacked =
            reportFromUnpackedCopy(dir.path(), QStringLiteral("appimage_extracted_") + digest);
    QCOMPARE(unpacked.value(0), self);
    // Started again the same way — after an update, and by the autostart entry,
    // which used to name the unpacked copy: gone as soon as FreeTunnel quit.
    QCOMPARE(unpacked.value(1), QStringLiteral("--appimage-extract-and-run"));
    QCOMPARE(unpacked.value(2),
             QStringLiteral("Exec=\"%1\" --appimage-extract-and-run").arg(self));

    // A parent whose content does not match the name is not the AppImage, and
    // the entry names the executable itself, as for any other install.
    const QString forgedName =
            QStringLiteral("appimage_extracted_") + QString(32, QLatin1Char('0'));
    const QStringList forged = reportFromUnpackedCopy(dir.path(), forgedName);
    QCOMPARE(forged.value(0), QString());
    QCOMPARE(forged.value(1), QString());
    QCOMPARE(forged.value(2),
             QStringLiteral("Exec=\"%1/usr/bin/FreeTunnel\"")
                     .arg(QFileInfo(dir.filePath(forgedName)).canonicalFilePath()));
#endif
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
void TestAppImagePath::autoStartTargetIsUnquoted()
{
    const QString entry = QStringLiteral(
            "[Desktop Entry]\nType=Application\nName=FreeTunnel\n"
            "Exec=\"/home/u/My \\\"Apps\\\"/FreeTunnel.AppImage\"\nTerminal=false\n");
    QCOMPARE(freetunnel::autoStartExecTarget(entry),
             QStringLiteral("/home/u/My \"Apps\"/FreeTunnel.AppImage"));
    QCOMPARE(freetunnel::autoStartExecTarget(QStringLiteral("Exec=/usr/bin/FreeTunnel\n")),
             QStringLiteral("/usr/bin/FreeTunnel"));
    // An AppImage that was unpacked rather than mounted is started that way again;
    // the option after the program is not part of what has to exist.
    QCOMPARE(freetunnel::autoStartExecTarget(QStringLiteral(
                     "Exec=\"/home/u/FreeTunnel.AppImage\" --appimage-extract-and-run\n")),
             QStringLiteral("/home/u/FreeTunnel.AppImage"));
}

void TestAppImagePath::autoStartTargetHandlesAMissingExecLine()
{
    QVERIFY(freetunnel::autoStartExecTarget(QStringLiteral("[Desktop Entry]\nType=Application\n"))
                    .isEmpty());
}
#else
void TestAppImagePath::autoStartTargetIsUnquoted() { QSKIP("Exec= autostart entries are Unix-only"); }
void TestAppImagePath::autoStartTargetHandlesAMissingExecLine()
{
    QSKIP("Exec= autostart entries are Unix-only");
}
#endif

// The macOS half of the same question, checked here because this is where the
// tests run. A plist naming a bundle that has moved must not read as "on": that
// is what left the Linux toggle lying until it was fixed, and the macOS branch
// answered with nothing but "the file exists" until now.
void TestAppImagePath::autoStartProgramIsReadBackOutOfThePlist()
{
    const QString plist = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<plist version=\"1.0\"><dict>\n"
            "  <key>Label</key><string>com.freetunnel.app</string>\n"
            "  <key>ProgramArguments</key><array><string>%1</string></array>\n"
            "  <key>RunAtLoad</key><true/>\n"
            "</dict></plist>\n");

    // Not the Label, which is the <string> that comes first in the file: the one
    // inside the array is the program.
    QCOMPARE(freetunnel::autoStartProgramFromPlist(
                     plist.arg(QStringLiteral("/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel"))),
             QStringLiteral("/Applications/FreeTunnel.app/Contents/MacOS/FreeTunnel"));

    // And the escaping the writer applies is undone, including a path whose own
    // text contains the escape sequence.
    QCOMPARE(freetunnel::autoStartProgramFromPlist(
                     plist.arg(QStringLiteral("/Users/u/Rock &amp; Roll/&lt;app&gt;/FreeTunnel"))),
             QStringLiteral("/Users/u/Rock & Roll/<app>/FreeTunnel"));
    QCOMPARE(freetunnel::autoStartProgramFromPlist(plist.arg(QStringLiteral("/a/&amp;lt;b/FreeTunnel"))),
             QStringLiteral("/a/&lt;b/FreeTunnel"));

    QVERIFY(freetunnel::autoStartProgramFromPlist(QStringLiteral("<plist><dict></dict></plist>"))
                    .isEmpty());
    QVERIFY(freetunnel::autoStartProgramFromPlist(QString()).isEmpty());
}

// And the Windows half. The Run value used to count as "on" for being there at
// all, so the program it names has to be read out of it before anyone can ask
// whether that program still exists.
void TestAppImagePath::autoStartProgramIsReadBackOutOfTheRunValue()
{
    // As setPlatformAutoStart() writes it: quoted, because Program Files has a
    // space in it.
    QCOMPARE(freetunnel::autoStartProgramFromRunValue(
                     QStringLiteral("\"C:\\Program Files\\FreeTunnel\\FreeTunnel.exe\"")),
             QStringLiteral("C:\\Program Files\\FreeTunnel\\FreeTunnel.exe"));
    // Arguments after the quotes are not part of the program.
    QCOMPARE(freetunnel::autoStartProgramFromRunValue(
                     QStringLiteral(" \"D:\\Apps\\FreeTunnel.exe\" --minimized ")),
             QStringLiteral("D:\\Apps\\FreeTunnel.exe"));
    // Unquoted, which this app never writes, the first word is the program.
    QCOMPARE(freetunnel::autoStartProgramFromRunValue(
                     QStringLiteral("C:\\Tools\\FreeTunnel.exe --minimized")),
             QStringLiteral("C:\\Tools\\FreeTunnel.exe"));

    QVERIFY(freetunnel::autoStartProgramFromRunValue(QString()).isEmpty());
    QVERIFY(freetunnel::autoStartProgramFromRunValue(QStringLiteral("\"\"")).isEmpty());
}

// The same question asked of the real thing, on the platform it is for, through
// the registry. Not the real Run key: it holds the developer's own autostart
// setting, which test_backend_settings will not touch either, and a test that
// died between writing and restoring it would leave it naming a file in a
// temporary folder. FT_TEST_RUN_KEY points the code at a key of the test's own,
// and that key goes when the test does.
void TestAppImagePath::windowsAutoStartIsOffWhenItsProgramIsGone()
{
#if defined(Q_OS_WIN)
    const QString testRoot = QStringLiteral("HKEY_CURRENT_USER\\Software\\FreeTunnelTest");
    const QString testGroup = QStringLiteral("Run-%1").arg(QCoreApplication::applicationPid());
    const QString realKey =
            QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    const QString name = QStringLiteral("FreeTunnel");
    const QVariant realBefore = QSettings(realKey, QSettings::NativeFormat).value(name);
    qputenv("FT_TEST_RUN_KEY", (testRoot + QLatin1Char('\\') + testGroup).toUtf8());
    const auto restore = qScopeGuard([&testRoot, &testGroup] {
        qunsetenv("FT_TEST_RUN_KEY");
        QSettings root(testRoot, QSettings::NativeFormat);
        root.remove(testGroup);
        root.sync();
    });
    QSettings run(testRoot + QLatin1Char('\\') + testGroup, QSettings::NativeFormat);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // A space in the path, as in Program Files, so the quotes have to come off
    // before the file can be looked for.
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("Free Tunnel")));
    const QString exe =
            QDir::toNativeSeparators(dir.filePath(QStringLiteral("Free Tunnel/FreeTunnel.exe")));
    run.setValue(name, QLatin1Char('"') + exe + QLatin1Char('"'));
    run.sync();
    QVERIFY2(!freetunnel::platformAutoStartEnabled(),
             "a Run value naming a program that is not there read as on");

    QFile file(exe);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    QVERIFY2(freetunnel::platformAutoStartEnabled(),
             "a Run value naming a program that is there read as off");

    // The switch itself, which registers the program that is running.
    freetunnel::setPlatformAutoStart(false);
    QVERIFY(!freetunnel::platformAutoStartEnabled());
    freetunnel::setPlatformAutoStart(true);
    QVERIFY(freetunnel::platformAutoStartEnabled());
    run.sync();
    QCOMPARE(freetunnel::autoStartProgramFromRunValue(run.value(name).toString()),
             QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));

    // And none of it reached the real Run key.
    QCOMPARE(QSettings(realKey, QSettings::NativeFormat).value(name), realBefore);
#else
    QSKIP("the Run key is Windows-only");
#endif
}

// Not QTEST_MAIN: runningAppImageFindsTheFileThatUnpackedThisProcess() runs a
// copy of this binary that only reports what runningAppImage() makes of it.
int main(int argc, char *argv[])
{
    if (argc == 2 && qstrcmp(argv[1], kReportRunningAppImage) == 0) {
        QCoreApplication app(argc, argv);
        const freetunnel::RunningAppImage running = freetunnel::runningAppImage();
        QTextStream out(stdout);
        out << running.path << '\n' << running.launchArguments().join(QLatin1Char(' ')) << '\n';
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
        freetunnel::setPlatformAutoStart(true);
        QFile entry(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                    + QStringLiteral("/autostart/freetunnel.desktop"));
        if (entry.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &line : QString::fromUtf8(entry.readAll()).split(QLatin1Char('\n')))
                if (line.startsWith(QLatin1String("Exec=")))
                    out << line << '\n';
        }
#endif
        return 0;
    }
    QCoreApplication app(argc, argv);
    TestAppImagePath tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "test_appimagepath.moc"
