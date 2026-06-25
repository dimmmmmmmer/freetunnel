#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTranslator>

#include "app/AppStartup.h"
#include "app/Backend.h"
#include "core/AppSettings.h"
#include "core/InstanceControl.h"

class TestAppStartup : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void controlArgFromArgv();
    void urlOpenFilterBuffersUntilReady();
    void quitFilterEmitsShutdown();
    void applyLanguageLoadsRussian();
    void wireInstanceServerForwardsCommand();
};

void TestAppStartup::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("AppStartupTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/ft-app-startup-test");
    QDir().mkpath(dir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
    saveAppSettings(AppSettings{});
}

void TestAppStartup::controlArgFromArgv()
{
    char arg0[] = "FreeTunnel";
    char toggle[] = "freetunnel://toggle";
    char tt[] = "tt://?abc";
    char other[] = "--foo";
    char *argvToggle[] = {arg0, toggle, nullptr};
    char *argvTt[] = {arg0, tt, nullptr};
    char *argvNone[] = {arg0, other, nullptr};

    QCOMPARE(freetunnel::controlArgFrom(2, argvToggle), QStringLiteral("freetunnel://toggle"));
    QCOMPARE(freetunnel::controlArgFrom(2, argvTt), QStringLiteral("tt://?abc"));
    QVERIFY(freetunnel::controlArgFrom(2, argvNone).isEmpty());
}

void TestAppStartup::urlOpenFilterBuffersUntilReady()
{
    UrlOpenFilter filter;
    qApp->installEventFilter(&filter);
    Backend backend;

    QFileOpenEvent ev(QUrl(QStringLiteral("freetunnel://toggle")));
    QCoreApplication::sendEvent(qApp, &ev);
    QCOMPARE(filter.pending, QStringLiteral("freetunnel://toggle"));

    filter.ready(&backend, nullptr);
    QVERIFY(filter.pending.isEmpty());
}

void TestAppStartup::quitFilterEmitsShutdown()
{
    Backend backend;
    QuitFilter filter;
    filter.backend = &backend;
    qApp->installEventFilter(&filter);
    QSignalSpy spy(&backend, &Backend::aboutToShutdown);

    QCoreApplication::postEvent(qApp, new QEvent(QEvent::Quit));
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);
}

void TestAppStartup::applyLanguageLoadsRussian()
{
    QQmlApplicationEngine engine;
    QTranslator *translator = nullptr;
    freetunnel::applyLanguage(*qGuiApp, engine, translator, QStringLiteral("ru"));
    freetunnel::applyLanguage(*qGuiApp, engine, translator, QStringLiteral("en"));
}

void TestAppStartup::wireInstanceServerForwardsCommand()
{
    Backend backend;
    QLocalServer server;
    const QString name = QStringLiteral("freetunnel-app-startup-%1").arg(QCoreApplication::applicationPid());
    QVERIFY(server.listen(name));

    freetunnel::wireInstanceServer(&server, backend, nullptr, QStringLiteral("tok"));

    QLocalSocket badClient;
    badClient.connectToServer(name);
    QVERIFY(badClient.waitForConnected(2000));
    badClient.write("wrong\n");
    badClient.waitForBytesWritten(1000);
    badClient.disconnectFromServer();

    QLocalSocket client;
    client.connectToServer(name);
    QVERIFY(client.waitForConnected(2000));
    client.write(freetunnel::formatInstanceMessage(QStringLiteral("tok"),
                                                   QStringLiteral("freetunnel://toggle")));
    client.waitForBytesWritten(1000);
    client.waitForDisconnected(2000);
}

QTEST_MAIN(TestAppStartup)
#include "test_app_startup.moc"
