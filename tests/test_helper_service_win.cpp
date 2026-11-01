// cppcheck-suppress-file missingIncludeSystem
#include "real_helper.h"
#include "vpn/vpn_helper_launch.h"
#include <QDir>
#include <QTemporaryFile>
#include <windows.h>
#include <winsvc.h>

class TestHelperService : public QObject {
    Q_OBJECT
private slots:
    void serviceAuthenticatesAndStops();
};

void TestHelperService::serviceAuthenticatesAndStops()
{
    const SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!manager) {
        if (qEnvironmentVariableIsSet("GITHUB_ACTIONS"))
            QFAIL("CI must be able to create the test service");
        QSKIP("Service lifecycle test requires an elevated test process");
    }
    const auto closeManager = qScopeGuard([&] { CloseServiceHandle(manager); });
    QVERIFY(realhelper::ensureWintunPlaceholder());
    const std::wstring name = QString::fromLatin1(freetunnel::kHelperServiceName).toStdWString();
    const std::wstring command = (QLatin1Char('"') + QStringLiteral(FT_TEST_HELPER_BINARY)
                                 + QStringLiteral("\" --service")).toStdWString();
    // Never modify an existing installation's service: creation must succeed.
    const SC_HANDLE service = CreateServiceW(manager, name.c_str(), name.c_str(),
            SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
            command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
    QVERIFY2(service, "Cannot create test service (or a real installation already exists)");
    const auto removeService = qScopeGuard([&] {
        SERVICE_STATUS status{};
        ControlService(service, SERVICE_CONTROL_STOP, &status);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000 && QueryServiceStatus(service, &status)
               && status.dwCurrentState != SERVICE_STOPPED)
            QTest::qWait(50);
        DeleteService(service);
        CloseServiceHandle(service);
    });
    const QString token = QStringLiteral("0123456789abcdef0123456789abcdef");
    // Match production's inherited Windows ACLs. QTemporaryDir creates a
    // private ACL whose child files cannot be read by the LocalSystem service.
    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/.fthelper-service-XXXXXX"));
    QVERIFY(file.open());
    const QString path = file.fileName();
    QCOMPARE(file.write(token.toUtf8()), qint64(32));
    file.close();
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    const std::wstring portText = QString::number(port).toStdWString();
    const std::wstring tokenPath = path.toStdWString();
    const wchar_t *args[] = {L"--port", portText.c_str(), L"--token-file", tokenPath.c_str()};
    QVERIFY(StartServiceW(service, 4, args));
    QTcpSocket socket;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(100))
            break;
        socket.abort();
        QTest::qWait(50);
    }
    SERVICE_STATUS connectionStatus{};
    QVERIFY(QueryServiceStatus(service, &connectionStatus));
    const QByteArray diagnostic = QStringLiteral("Service state=%1, Windows exit=%2, helper exit=%3; socket: %4")
            .arg(connectionStatus.dwCurrentState).arg(connectionStatus.dwWin32ExitCode)
            .arg(connectionStatus.dwServiceSpecificExitCode).arg(socket.errorString()).toUtf8();
    QVERIFY2(socket.state() == QAbstractSocket::ConnectedState, diagnostic.constData());
    QVERIFY(realhelper::authenticate(socket, token));
    // A normal GUI disconnect ends the service, without an administrator stop.
    socket.disconnectFromHost();
    SERVICE_STATUS status{};
    QTRY_VERIFY_WITH_TIMEOUT(QueryServiceStatus(service, &status)
                             && status.dwCurrentState == SERVICE_STOPPED, 10000);
    QCOMPARE(status.dwWin32ExitCode, DWORD(NO_ERROR));
    // Restart it and exercise an administrator/uninstaller stop as well.
    QVERIFY(StartServiceW(service, 4, args));
    QTRY_VERIFY_WITH_TIMEOUT(QueryServiceStatus(service, &status)
                             && status.dwCurrentState == SERVICE_RUNNING, 10000);
    QVERIFY(ControlService(service, SERVICE_CONTROL_STOP, &status));
    QTRY_VERIFY_WITH_TIMEOUT(QueryServiceStatus(service, &status)
                             && status.dwCurrentState == SERVICE_STOPPED, 10000);
}

QTEST_GUILESS_MAIN(TestHelperService)
#include "test_helper_service_win.moc"
