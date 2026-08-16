// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDesktopServices>
#include <QScopeGuard>
#include <QStringList>
#include <QUrl>

#include "core/AppUiUtils.h"

// Stands in for the desktop's URL opener. QDesktopServices::openUrl() gives a
// registered scheme handler priority over the platform integration, and that is
// the only way to observe which URLs openHttpUrl() actually forwards: with no
// handler installed the real opener fails headless for every scheme alike, so a
// build that happily handed javascript: to the browser would still return false
// and look correct.
class UrlHandlerSpy : public QObject {
    Q_OBJECT
public:
    QStringList opened;

public slots:
    void handle(const QUrl &url) { opened << url.toString(); }
};

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

// openHttpUrl() is the single door between untrusted strings and the user's
// browser: Backend::openUrl() is invokable from QML, and the release-page URL it
// is usually handed comes out of the GitHub API response. So it has to be
// verified from both sides — http/https really do get through, and nothing else
// does. Asserting only that the refused schemes return false is not enough
// (headless the opener fails anyway), hence the spy, which records what would
// actually have been handed to the desktop.
void TestAppUiUtils::openHttpUrlAcceptsHttpSchemes()
{
    UrlHandlerSpy spy;
    const QStringList schemes = {QStringLiteral("http"), QStringLiteral("https"),
                                 QStringLiteral("file"), QStringLiteral("ftp"),
                                 QStringLiteral("javascript")};
    for (const QString &scheme : schemes)
        QDesktopServices::setUrlHandler(scheme, &spy, "handle");
    // Leave no handler behind for the rest of this binary even if an assertion
    // below aborts the function early.
    const auto cleanup = qScopeGuard([&schemes]() {
        for (const QString &scheme : schemes)
            QDesktopServices::unsetUrlHandler(scheme);
    });

    // The accepted half: a plain https link and an http one with a port, both
    // forwarded unchanged. If the scheme check were ever tightened into
    // rejecting everything, the "open release notes" and "download update in a
    // browser" actions would silently do nothing, and only this half notices.
    QVERIFY(openHttpUrl(QStringLiteral("https://example.com/path")));
    QVERIFY(openHttpUrl(QStringLiteral("http://127.0.0.1:65535/")));
    QCOMPARE(spy.opened, QStringList({QStringLiteral("https://example.com/path"),
                                      QStringLiteral("http://127.0.0.1:65535/")}));

    // The refused half. javascript: is the one that matters most: handed to a
    // browser it runs in whatever page happens to be open, and these URLs come
    // straight out of a remote JSON document.
    spy.opened.clear();
    QVERIFY(!openHttpUrl(QStringLiteral("javascript:alert(1)")));
    QVERIFY(!openHttpUrl(QStringLiteral("file:///etc/passwd")));
    QVERIFY(!openHttpUrl(QStringLiteral("ftp://example.com/x")));
    QVERIFY2(spy.opened.isEmpty(), qPrintable(spy.opened.join(QLatin1Char(' '))));
}

void TestAppUiUtils::safeReadRejectsEmptyPath()
{
    QVERIFY(safeReadUserTextFile(QString()).isEmpty());
    QVERIFY(safeReadUserTextFile(QStringLiteral("   ")).isEmpty());
}

QTEST_MAIN(TestAppUiUtils)
#include "test_appuiutils.moc"
