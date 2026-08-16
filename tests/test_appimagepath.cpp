// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/AppImagePath.h"
#include "app/PlatformAutoStart.h"

// The decision these functions make picks the binary that a pkexec/sudo prompt
// will execute as root, and the path an autostart entry will launch. Both used to
// be answered from $APPIMAGE/$APPDIR, which the attacker in the threat model
// controls; the tests below pin that the answer now comes from mountinfo, and
// that nothing an environment variable can say gets a foot in the door.
class TestAppImagePath : public QObject {
    Q_OBJECT
private slots:
    void findsTheAppImageBehindAFuseMount();
    void picksTheDeepestMountNotTheLongestSharedPrefix();
    void ignoresNonFuseMounts();
    void ignoresAMountThatDoesNotContainTheExecutable();
    void decodesOctalEscapesInPaths();
    void autoStartTargetIsUnquoted();
    void autoStartTargetHandlesAMissingExecLine();
};

namespace {

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

QTEST_MAIN(TestAppImagePath)
#include "test_appimagepath.moc"
