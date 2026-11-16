// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include "vpn/installed_helper.h"
#include "core/ReleaseVerify.h"

using namespace freetunnel::installed_helper;

class TestInstalledHelper : public QObject {
    Q_OBJECT
private slots:
    void restrictsPrivilegeGrant() {
        quint32 uid = 0;
        QVERIFY(validUserId(QStringLiteral("501"), &uid));
        QCOMPARE(uid, quint32(501));
        for (const QString &bad : {QStringLiteral("0"), QStringLiteral("-1"), QStringLiteral("+501"),
                                  QStringLiteral("501 ALL"), QStringLiteral("4294967295"), QStringLiteral("1\n")})
            QVERIFY(!validUserId(bad, &uid));
        QVERIFY(sudoRule(0).isEmpty());
        const QString rule = sudoRule(501);
        QVERIFY(rule.startsWith(QLatin1String("#501 ALL=(root) NOPASSWD:NOSETENV:")));
        QVERIFY(rule.endsWith(QLatin1String(" --installed-helper *\n")));
        QVERIFY(!rule.contains(QLatin1String("--install-helper")));
    }
    void rejectsUnexpectedLaunchArguments() {
        const QStringList good{QStringLiteral("--port"), QStringLiteral("49152"),
                               QStringLiteral("--token-file"), QStringLiteral("/tmp/.fthelper-test")};
        QVERIFY(validStartArguments(good));
        QVERIFY(!validStartArguments(good + QStringList{QStringLiteral("--remove-helper")}));
        auto bad = good; bad[1] = QStringLiteral("22"); QVERIFY(!validStartArguments(bad));
        bad = good; bad[3] = QStringLiteral("/etc/shadow"); QVERIFY(!validStartArguments(bad));
        bad = good; bad[3] = QStringLiteral(".fthelper-relative"); QVERIFY(!validStartArguments(bad));
    }
    void acceptsOnlySignedMatchingNewReleases() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString payload = dir.filePath(QStringLiteral("payload"));
        QFile file(payload); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("runtime"); file.close();
        const auto manifest = [&](const QString &version) {
            return (QStringLiteral("#version=") + version + QLatin1Char('\n') + sha256HexOfFile(payload)
                    + QStringLiteral("  ") + assetName() + QLatin1Char('\n')).toUtf8();
        };
        const auto sign = [&](const QByteArray &bytes) {
            const QString input = dir.filePath(QStringLiteral("sums"));
            const QString output = dir.filePath(QStringLiteral("sig"));
            QFile sums(input); if (!sums.open(QIODevice::WriteOnly)) return QByteArray();
            sums.write(bytes); sums.close();
            const QString opensslRoot = qEnvironmentVariable("OPENSSL_ROOT_DIR");
            const QString openssl = opensslRoot.isEmpty() ? QStringLiteral("openssl")
                    : QDir(opensslRoot).filePath(QStringLiteral("bin/openssl"));
            if (QProcess::execute(openssl, {QStringLiteral("pkeyutl"), QStringLiteral("-sign"),
                    QStringLiteral("-rawin"), QStringLiteral("-inkey"),
                    QStringLiteral(FT_HELPER_TEST_RESOURCES "/test-key.pem"), QStringLiteral("-in"), input,
                    QStringLiteral("-out"), output}) != 0) return QByteArray();
            QFile signature(output); if (!signature.open(QIODevice::ReadOnly)) return QByteArray();
            return signature.readAll();
        };
        QString error;
        const QByteArray newer = manifest(QStringLiteral("1.2.6"));
        const QByteArray signature = sign(newer);
        QCOMPARE(signature.size(), 64);
        QVERIFY(verifyArchive(payload, newer, signature, QStringLiteral("1.2.5"), false, &error));
        QVERIFY(!verifyArchive(payload, newer + ' ', signature, QStringLiteral("1.2.5"), false, &error));
        const QByteArray old = manifest(QStringLiteral("1.2.4"));
        QVERIFY(!verifyArchive(payload, old, sign(old), QStringLiteral("1.2.5"), true, &error));
        const QByteArray same = manifest(QStringLiteral("1.2.5"));
        QVERIFY(verifyArchive(payload, same, sign(same), QStringLiteral("1.2.5"), true, &error));
        QVERIFY(!verifyArchive(payload, same, sign(same), QStringLiteral("1.2.5"), false, &error));
        QVERIFY(file.open(QIODevice::Append)); file.write("tampered"); file.close();
        QVERIFY(!verifyArchive(payload, newer, signature, QStringLiteral("1.2.5"), false, &error));
    }
};
QTEST_GUILESS_MAIN(TestInstalledHelper)
#include "test_installed_helper.moc"
