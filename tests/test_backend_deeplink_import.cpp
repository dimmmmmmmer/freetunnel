#include <QtTest>

#include <QCoreApplication>
#include <QSignalSpy>

#include "app/Backend.h"
#include "core/DeepLink.h"

class TestBackendDeepLinkImport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void skipVerificationRequiresConfirmation();
    void confirmImportsUnsafeLink();
};

void TestBackendDeepLinkImport::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("FreeTunnelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DeepLinkImportTest"));
}

static QString unsafeLink()
{
    freetunnel::DeepLinkConfig c;
    c.hostname = QStringLiteral("unsafe.example.com");
    c.addresses = {QStringLiteral("203.0.113.1:443")};
    c.username = QStringLiteral("user");
    c.password = QStringLiteral("pass");
    c.skipVerification = true;
    return freetunnel::encodeDeepLink(c);
}

void TestBackendDeepLinkImport::skipVerificationRequiresConfirmation()
{
    Backend backend;
    QSignalSpy confirmSpy(&backend, &Backend::deepLinkImportConfirmationRequired);
    QSignalSpy importedSpy(&backend, &Backend::configImported);

    QVERIFY(!backend.importDeepLink(unsafeLink()));
    QCOMPARE(confirmSpy.count(), 1);
    QCOMPARE(importedSpy.count(), 0);
}

void TestBackendDeepLinkImport::confirmImportsUnsafeLink()
{
    Backend backend;
    QSignalSpy importedSpy(&backend, &Backend::configImported);

    QVERIFY(backend.confirmDeepLinkImport(unsafeLink()));
    QCOMPARE(importedSpy.count(), 1);
}

QTEST_MAIN(TestBackendDeepLinkImport)
#include "test_backend_deeplink_import.moc"
