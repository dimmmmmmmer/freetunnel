#include <QtTest>

#include <QGuiApplication>
#include <QStandardPaths>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>

#include "ui/MockBackend.h"
#include "ui/MockShell.h"
#include "ui/UiTheme.h"

class TestQmlUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void homePageLoads();
    void configsPageLoads();
    void splitPageLoads();
    void settingsPageLoads();
    void logsPageLoads();
    void createConfigOverlayLoads();
    void mainWindowLoads();
    void mainWindowPageNavigation();

private:
    QObject *loadPage(const char *qmlPath);

    QQmlEngine m_engine;
    MockBackend m_backend;
    MockShell m_shell;
    UiTheme m_theme;
};

void TestQmlUi::initTestCase()
{
    qputenv("QML_XHR_ALLOW_FILE_READ", "1");
    m_engine.rootContext()->setContextProperty(QStringLiteral("backend"), &m_backend);
}

QObject *TestQmlUi::loadPage(const char *qmlPath)
{
    QQmlComponent component(&m_engine, QUrl(QStringLiteral("qrc:/%1").arg(QLatin1String(qmlPath))));
    if (!QTest::qVerify(component.isReady(), "component.isReady()", component.errorString().toUtf8().constData(), __FILE__, __LINE__))
        return nullptr;
    QVariantMap props;
    props[QStringLiteral("shell")] = QVariant::fromValue(static_cast<QObject *>(&m_shell));
    props[QStringLiteral("backend")] = QVariant::fromValue(static_cast<QObject *>(&m_backend));
    props[QStringLiteral("theme")] = QVariant::fromValue(static_cast<QObject *>(&m_theme));
    QObject *root = component.createWithInitialProperties(props, m_engine.rootContext());
    if (!QTest::qVerify(root != nullptr, "root != nullptr", component.errorString().toUtf8().constData(), __FILE__, __LINE__))
        return nullptr;
    return root;
}

void TestQmlUi::homePageLoads()
{
    QObject *root = loadPage("pages/HomePage.qml");
    QVERIFY(root);
    QVERIFY(root->property("backend").isValid());
    delete root;
}

void TestQmlUi::configsPageLoads()
{
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);
    delete root;
}

void TestQmlUi::splitPageLoads()
{
    QObject *root = loadPage("pages/SplitPage.qml");
    QVERIFY(root);
    delete root;
}

void TestQmlUi::settingsPageLoads()
{
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    delete root;
}

void TestQmlUi::logsPageLoads()
{
    QObject *root = loadPage("pages/LogsPage.qml");
    QVERIFY(root);
    delete root;
}

void TestQmlUi::createConfigOverlayLoads()
{
    m_shell.setEditIndex(-1);
    QObject *root = loadPage("CreateConfigOverlay.qml");
    QVERIFY(root);
    delete root;
}

void TestQmlUi::mainWindowLoads()
{
    QQmlComponent component(&m_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY2(component.isReady(), component.errorString().toUtf8().constData());
    QObject *root = component.create();
    QVERIFY2(root, component.errorString().toUtf8().constData());
    QVERIFY(qobject_cast<QQuickWindow *>(root));
    delete root;
}

void TestQmlUi::mainWindowPageNavigation()
{
    QQmlComponent component(&m_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY2(component.isReady(), component.errorString().toUtf8().constData());
    QObject *root = component.create();
    QVERIFY(root);
    for (int page = 0; page < 5; ++page) {
        root->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        QCOMPARE(root->property("currentPage").toInt(), page);
    }
    delete root;
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Isolate from the real app's on-disk state: never read or clobber the
    // user's configs.json / settings under the production app/org names.
    QStandardPaths::setTestModeEnabled(true);
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("QmlUiTest"));
    app.setOrganizationName(QStringLiteral("FreeTunnelTest"));
    TestQmlUi tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_qml_ui.moc"
