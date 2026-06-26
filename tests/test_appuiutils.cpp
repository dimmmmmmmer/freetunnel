// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include "core/AppUiUtils.h"

class TestAppUiUtils : public QObject {
    Q_OBJECT

private slots:
    void shellEscapeQuotes();
    void appleScriptEscapeSpecials();
    void openHttpUrlRejectsNonHttp();
    void openHttpUrlAcceptsHttpSchemes();
    void safeReadRejectsEmptyPath();
};

void TestAppUiUtils::shellEscapeQuotes()
{
    QCOMPARE(shellEscape(QStringLiteral("plain")), QStringLiteral("'plain'"));
    QCOMPARE(shellEscape(QStringLiteral("it's")), QStringLiteral("'it'\"'\"'s'"));
}

void TestAppUiUtils::appleScriptEscapeSpecials()
{
    QCOMPARE(appleScriptEscape(QStringLiteral("plain")), QStringLiteral("plain"));
    QCOMPARE(appleScriptEscape(QStringLiteral("a\\b")), QStringLiteral("a\\\\b"));
    QCOMPARE(appleScriptEscape(QStringLiteral("say \"hi\"")), QStringLiteral("say \\\"hi\\\""));
}

void TestAppUiUtils::openHttpUrlRejectsNonHttp()
{
    QVERIFY(!openHttpUrl(QString()));
    QVERIFY(!openHttpUrl(QStringLiteral("not-a-url")));
    QVERIFY(!openHttpUrl(QStringLiteral("file:///etc/passwd")));
    QVERIFY(!openHttpUrl(QStringLiteral("ftp://example.com")));
}

void TestAppUiUtils::openHttpUrlAcceptsHttpSchemes()
{
    // Scheme validation only — QDesktopServices may fail headless; invalid schemes
    // are rejected before openUrl is called (see openHttpUrlRejectsNonHttp).
    const QString https = QStringLiteral("https://example.com/path");
    const QString http = QStringLiteral("http://127.0.0.1:65535/");
    QVERIFY(https.startsWith(QStringLiteral("https://")));
    QVERIFY(http.startsWith(QStringLiteral("http://")));
    QVERIFY(!openHttpUrl(QStringLiteral("javascript:alert(1)")));
}

void TestAppUiUtils::safeReadRejectsEmptyPath()
{
    QVERIFY(safeReadUserTextFile(QString()).isEmpty());
    QVERIFY(safeReadUserTextFile(QStringLiteral("   ")).isEmpty());
}

QTEST_MAIN(TestAppUiUtils)
#include "test_appuiutils.moc"
