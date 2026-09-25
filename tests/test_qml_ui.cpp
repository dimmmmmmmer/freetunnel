// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDirIterator>
#include <QScopeGuard>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QWindow>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatformsystemtrayicon.h>
#include <qpa/qplatformtheme.h>
#include <qpa/qwindowsysteminterface.h>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>

#include "ui/MockBackend.h"
#include "ui/MockDesktop.h"
#include "app/DesktopChrome.h"
#include "ui/MockShell.h"
#include "ui/UiTheme.h"

class TestQmlUi : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void homePageLoads();
    void configsPageLoads();
    void splitPageLoads();
    void splitPageNamesApplicationsTheWayThePickerDoes();
    void typingAProgramNameOffersTheProgram();
    void switchingProfileSwitchesTheApplicationList();
    void everyFileDialogActuallyOpens();
    void everyFileDialogActuallyOpens_data();
    void settingsPageLoads();
    void logsPageLoads();
    void createConfigOverlayLoads();
    void mainWindowLoads();
    void mainWindowPageNavigation();
    void closingTheWindowQuitsWhenThereIsNoTray();
    void everythingTheWindowsAgentLooksForCanBeFound();
    void theWindowButtonsFollowTheDesktopLayout();
    void theTitleBarStepsAsideForOverlays();
    void titleBarClicksDoWhatTheDesktopSays();
    void aTitleBarPressIsNotYetAMove();
    void theDesktopDecidesDarkOnlyWhenQtCannot();
    void showFreeTunnelInTheTrayMenuBringsTheWindowForward();
    void theTrayIconBringsTheWindowForward_data();
    void theTrayIconBringsTheWindowForward();
    void theTrayMenuShowsConfigNamesAsTyped();
    void doubleClickingTheLogoTogglesOnce();
    void aToastWaitsForAWindowThatIsAway();
    void theTickedConfigInTheTrayTurnsTheConnectionOnAndOff();
    void aMinimisedWindowIsBroughtBack();
    void everyComponentLoadsOnItsOwn();
    void everyComponentLoadsOnItsOwn_data();
    void confirmDialogShowsTheThirdButtonOnlyWhenItHasOne();
    void confirmDialogAnswersReturnAndEscape();
    void aSecondConfirmQueuesInsteadOfReplacingTheLiveOne();
    void aHotkeyNeedsAModifierAndBackspaceUnbinds();
    void aConfirmDialogTakesTheKeyboardFromAHotkeyField();

private:
    QObject *loadPage(const char *qmlPath);

    QQmlEngine m_engine;
    MockBackend m_backend;
    MockDesktop m_desktop;
    MockShell m_shell;
    UiTheme m_theme;
};

namespace {

// A QML binding that names something which does not exist is not an error to the
// engine — it warns, leaves the property undefined, and carries on. On screen
// that is a blank where a value should be, or a control that does nothing, and it
// survives every test that only asks whether the page loaded. So the warnings are
// collected and a test that produced one fails.
//
// Only the engine's own diagnostics. "does not have a property called shell" is a
// different thing entirely — it comes from handing every component the same three
// initial properties on purpose, and is expected on the ones that take fewer.
QStringList g_qmlErrors;
QtMessageHandler g_previousHandler = nullptr;

void collectQmlErrors(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    static const char *const kEngineErrors[] = {"ReferenceError", "TypeError",
                                                "Unable to assign", "Cannot assign"};
    for (const char *needle : kEngineErrors) {
        if (msg.contains(QLatin1String(needle))) {
            g_qmlErrors << msg;
            break;
        }
    }
    if (g_previousHandler != nullptr)
        g_previousHandler(type, ctx, msg);
}

} // namespace

void TestQmlUi::initTestCase()
{
    // Icons load through backend.readBundledText — no QML XHR file access needed.
    m_engine.rootContext()->setContextProperty(QStringLiteral("backend"), &m_backend);
    m_engine.rootContext()->setContextProperty(QStringLiteral("desktop"), &m_desktop);
    // The drawn dialog rather than the platform's own: it is the one that has to
    // work where the desktop offers nothing, and it does not put a modal native
    // window in front of a CI runner.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    // Chained, not replaced: the one already installed is Qt Test's, and it is
    // what turns a qWarning into the QWARN lines in the report.
    g_previousHandler = qInstallMessageHandler(collectQmlErrors);
}

void TestQmlUi::cleanup()
{
    const QStringList errors = g_qmlErrors;
    g_qmlErrors.clear();
    QVERIFY2(errors.isEmpty(), qPrintable(QStringLiteral("QML engine errors:\n  ")
                                          + errors.join(QStringLiteral("\n  "))));
}

// Every `text` in the tree. Chips are built by a Repeater inside a Flow, so
// there is no single object to ask; what a person would read off the screen is
// the union of all of them.
//
// Visual children as well as QObject ones: a Repeater's delegates are parented
// into the layout as items, and a walk that only followed QObject::children()
// found the page's fixed labels and not one chip — which is exactly the half
// that matters here.
static QStringList everyText(QObject *node)
{
    QStringList out;
    const QVariant text = node->property("text");
    if (text.isValid() && !text.toString().isEmpty())
        out << text.toString();
    QSet<QObject *> visited;
    const QObjectList children = node->children();
    for (QObject *child : children) {
        visited.insert(child);
        out << everyText(child);
    }
    if (auto *item = qobject_cast<QQuickItem *>(node)) {
        const QList<QQuickItem *> items = item->childItems();
        for (QQuickItem *child : items) {
            if (!visited.contains(child))
                out << everyText(child);
        }
    }
    return out;
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

// A rule is stored as a path, and the file at the end of that path is not what
// the person recognises: they picked "Firefox Web Browser" from a list and the
// file is called firefox. The mock's labels are deliberately not derivable from
// its rules, so a page that ignored them could not pass this by accident.
void TestQmlUi::splitPageNamesApplicationsTheWayThePickerDoes()
{
    QObject *root = loadPage("pages/SplitPage.qml");
    QVERIFY(root);
    const QStringList texts = everyText(root);
    QVERIFY2(texts.contains(QStringLiteral("Firefox Web Browser")),
             "the chip must say what the picker said");
    QVERIFY2(texts.contains(QStringLiteral("Some App")), "and so must the second one");
    QVERIFY2(!texts.contains(QStringLiteral("firefox")),
             "not the file name the rule happens to end with");
    delete root;
}

// Applications belong to the profile, the same as the addresses above them. The
// page has to show that: switch profile and the chips have to change, or the
// list is lying about what the tunnel will do.
void TestQmlUi::switchingProfileSwitchesTheApplicationList()
{
    QObject *root = loadPage("pages/SplitPage.qml");
    QVERIFY(root);
    QVERIFY2(everyText(root).contains(QStringLiteral("Firefox Web Browser")),
             "the Default profile's applications");

    m_backend.addProfile(QStringLiteral("Work"));
    m_backend.selectProfile(QStringLiteral("Work"));
    QCoreApplication::processEvents();
    QVERIFY2(!everyText(root).contains(QStringLiteral("Firefox Web Browser")),
             "a new profile starts with no applications of its own");

    m_backend.selectProfile(QStringLiteral("Default"));
    QCoreApplication::processEvents();
    QVERIFY2(everyText(root).contains(QStringLiteral("Firefox Web Browser")),
             "and switching back brings them back");
    delete root;
}

// Typing a name has to find the program. A name on its own is not a rule anyone
// should be asked to spell — it is only a way of pointing at one of the
// installed applications — so what the field offers comes from the same list the
// picker shows.
void TestQmlUi::typingAProgramNameOffersTheProgram()
{
    QObject *root = loadPage("pages/SplitPage.qml");
    QVERIFY(root);

    QObject *input = root->findChild<QObject *>(QStringLiteral("appPathInput"));
    QVERIFY2(input, "the path field");
    QObject *suggestions = root->findChild<QObject *>(QStringLiteral("appSuggestions"));
    QVERIFY2(suggestions, "the suggestion list");
    QVERIFY2(suggestions->property("rows").toList().isEmpty(), "nothing offered before anything is typed");

    input->setProperty("text", QStringLiteral("fire"));
    const QVariantList rows = suggestions->property("rows").toList();
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Firefox"));

    // A name that matches nothing offers nothing, rather than everything.
    input->setProperty("text", QStringLiteral("nothing-is-called-this"));
    QVERIFY(suggestions->property("rows").toList().isEmpty());
    delete root;
}

void TestQmlUi::everyFileDialogActuallyOpens_data()
{
    QTest::addColumn<QString>("page");
    QTest::addColumn<QString>("dialog");

    QTest::newRow("choose an application") << QStringLiteral("AppPickerOverlay.qml")
                                           << QStringLiteral("appFileDialog");
    QTest::newRow("import a config") << QStringLiteral("pages/ConfigsPage.qml")
                                     << QStringLiteral("configImportDialog");
    QTest::newRow("export a config") << QStringLiteral("pages/ConfigsPage.qml")
                                     << QStringLiteral("configExportDialog");
    QTest::newRow("pick a certificate") << QStringLiteral("CreateConfigOverlay.qml")
                                        << QStringLiteral("certificateDialog");
}

// Every one of these opened nothing at all on a desktop with no native file
// dialog, and said so only on stderr: Qt.labs.platform falls back to Qt Widgets,
// which this application does not link — it is a QGuiApplication. Reported as
// "no file manager opens" for the application picker; choosing a config file or
// a certificate had been broken the same way for as long as they existed.
//
// AA_DontUseNativeDialogs is set for the whole test binary, so this exercises
// the drawn dialog — the one that has to exist when the desktop offers nothing —
// rather than opening a native modal window on a CI runner.
void TestQmlUi::everyFileDialogActuallyOpens()
{
    QFETCH(QString, page);
    QFETCH(QString, dialog);

    QQuickWindow window;
    window.resize(520, 640);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    m_shell.setEditIndex(-1);
    QQmlComponent component(&m_engine, QUrl(QStringLiteral("qrc:/%1").arg(page)));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));

    // The window has to be in place BEFORE creation finishes: a dialog looks for
    // it once, on component-complete. A Loader parents its item first, so this is
    // what the running application does; reparenting afterwards is not.
    QObject *root = component.beginCreate(m_engine.rootContext());
    QVERIFY2(root, qPrintable(component.errorString()));
    QVariantMap props;
    props[QStringLiteral("shell")] = QVariant::fromValue(static_cast<QObject *>(&m_shell));
    props[QStringLiteral("backend")] = QVariant::fromValue(static_cast<QObject *>(&m_backend));
    props[QStringLiteral("theme")] = QVariant::fromValue(static_cast<QObject *>(&m_theme));
    component.setInitialProperties(root, props);
    auto *item = qobject_cast<QQuickItem *>(root);
    QVERIFY(item);
    item->setParentItem(window.contentItem());
    item->setWidth(window.width());
    item->setHeight(window.height());
    component.completeCreate();

    QObject *fileDialog = root->findChild<QObject *>(dialog);
    QVERIFY2(fileDialog, qPrintable(QStringLiteral("no dialog called %1").arg(dialog)));
    QVERIFY2(!fileDialog->property("visible").toBool(), "not open before it is opened");

    QVERIFY(QMetaObject::invokeMethod(fileDialog, "open"));
    QTRY_VERIFY_WITH_TIMEOUT(fileDialog->property("visible").toBool(), 5000);
    QVERIFY(QMetaObject::invokeMethod(fileDialog, "close"));
    QTRY_VERIFY(!fileDialog->property("visible").toBool());
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
    // Writing an int and reading it back cannot fail. What navigation is FOR is
    // the Loader the property drives — onCurrentPageChanged calls setSource with
    // pagePaths[currentPage] and pageProps(), and neither of those was observed
    // by anything: the per-page tests build each page directly with their own
    // property map and never go through this path at all. A page that failed to
    // load left this green.
    QObject *loader = root->findChild<QObject *>(QStringLiteral("pageLoader"));
    QVERIFY2(loader, "the page Loader");
    for (int page = 0; page < 5; ++page) {
        root->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        QCOMPARE(root->property("currentPage").toInt(), page);
        QCOMPARE(loader->property("status").toInt(), 1); // Loader.Ready
        QVERIFY2(loader->property("item").value<QObject *>() != nullptr,
                 "navigation has to produce a page, not just set a number");
    }
    delete root;
}

// The pages instantiate most of these, but not all on every path — a component
// behind a Loader or a conditional can be broken for weeks and only show up as
// an empty panel when a user finally opens it. Loading each one on its own turns
// that into a build failure.
void TestQmlUi::everyComponentLoadsOnItsOwn_data()
{
    QTest::addColumn<QString>("path");
    // Read out of the resource rather than listed here. A hand-written list
    // covers the components that existed when it was written, and the next one
    // added is exactly the one nobody thinks to add to it — so the check would
    // be weakest against the newest code, which is where it is needed most.
    QDirIterator it(QStringLiteral(":/components"), {QStringLiteral("*.qml")}, QDir::Files);
    int found = 0;
    while (it.hasNext()) {
        const QString path = it.next().mid(2); // ":/components/Foo.qml" -> "components/Foo.qml"
        QTest::newRow(path.toUtf8().constData()) << path;
        ++found;
    }
    // An empty iteration would report a pass for every component at once, which
    // is the one result this test must never be able to give.
    QVERIFY(found > 0);
    // These two predate components/ and still sit at the top of the tree, where
    // everything else is a page or a window that needs a whole context.
    for (const char *p : {"Field.qml", "Toggle.qml"})
        QTest::newRow(p) << QString::fromLatin1(p);
}

// ✕ minimizes rather than quits, because the tray icon is how you come back and
// how you quit. Qt.labs.platform shows that icon through a StatusNotifier host
// or not at all — it has no other implementation available to an application
// that does not link Qt Widgets — so on GNOME without an AppIndicator extension,
// or on a plain window manager, there is no icon and no tray menu. Minimizing
// there can put the window somewhere with nothing to bring it back from.
//
// Which way this goes depends on the machine, so the test asks the tray what it
// is and requires the matching behaviour, rather than assuming either.
void TestQmlUi::closingTheWindowQuitsWhenThereIsNoTray()
{
#ifdef Q_OS_MACOS
    // There is no ✕ of ours there. macOS keeps its native traffic lights, the
    // window controls this is about are drawn only where the window is
    // frameless, and closing goes through installMacWindowCloseToTray instead —
    // to a Dock icon, which is always a way back whatever the tray is doing.
    QSKIP("the custom window controls exist only on Linux and Windows");
#else
    QQmlComponent component(&m_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QObject *root = component.create();
    QVERIFY2(root, qPrintable(component.errorString()));

    QObject *tray = root->findChild<QObject *>(QStringLiteral("systemTray"));
    QVERIFY2(tray, "the tray icon");
    const bool trayAvailable = tray->property("available").toBool();

    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    auto *close = root->findChild<QQuickItem *>(QStringLiteral("windowCloseButton"));
    QVERIFY2(close, "the window's own close button");
    // A click on something invisible lands nowhere and proves nothing, which is
    // how this first passed everywhere and then failed on the platform that does
    // not draw it.
    QVERIFY2(close->isVisible(), "and it has to be on screen for a click to mean anything");
    QVERIFY(!m_backend.applicationClosingDown());

    // A real click rather than the signal: what is being tested is the handler
    // the button actually has, reached the way a person reaches it.
    const QPointF centre =
            close->mapToScene(QPointF(close->width() / 2.0, close->height() / 2.0));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    QCoreApplication::processEvents();

    if (trayAvailable) {
        QVERIFY2(!m_backend.applicationClosingDown(),
                 "with a tray to minimize alongside, the window must not quit");
    } else {
        QVERIFY2(m_backend.applicationClosingDown(),
                 "with no tray there is no way back and no Quit — so it has to quit");
    }
    delete root;
#endif
}

void TestQmlUi::everyComponentLoadsOnItsOwn()
{
    QFETCH(QString, path);
    QObject *root = loadPage(path.toLatin1().constData());
    QVERIFY(root);
    delete root;
}

// The deep-link dialog grew a third button so a link that collides with an
// existing config can offer Replace next to Add copy. Every other caller —
// delete a config, remove a profile — must still get two buttons: an extra
// destructive-looking action appearing in those would be its own bug.
// Return confirms and Escape cancels, but Return only after the dialog has been
// on screen for a moment. A deep link can raise this dialog with no warning while
// the user is typing, and a keystroke already in flight must not answer a
// question nobody has read yet.
void TestQmlUi::confirmDialogAnswersReturnAndEscape()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);
    auto *item = qobject_cast<QQuickItem *>(root);
    QVERIFY(item);
    QQuickWindow window;
    item->setParentItem(window.contentItem());
    item->setWidth(400);
    item->setHeight(400);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QSignalSpy confirmed(root, SIGNAL(confirmed()));
    QVERIFY(confirmed.isValid());

    QMetaObject::invokeMethod(root, "open");
    QVERIFY(root->property("visible").toBool());

    // Immediately after opening the dialog is deliberately deaf to Return.
    QVERIFY(!root->property("armed").toBool());
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(confirmed.count(), 0);
    QVERIFY2(root->property("visible").toBool(),
             "Return closed the dialog before it had been on screen long enough to read");

    // Once armed, Return is the confirm button.
    QTRY_VERIFY(root->property("armed").toBool());
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(confirmed.count(), 1);
    QVERIFY(!root->property("visible").toBool());

    // Escape cancels: the dialog closes and confirmed() must NOT fire.
    QMetaObject::invokeMethod(root, "open");
    QTRY_VERIFY(root->property("armed").toBool());
    QTest::keyClick(&window, Qt::Key_Escape);
    QVERIFY(!root->property("visible").toBool());
    QCOMPARE(confirmed.count(), 1);

    // The three-button form is the deep-link name collision: its primary action
    // replaces an existing config with one a link chose, so there is no answer
    // safe enough to be the keyboard default and Return must stay inert.
    root->setProperty("altText", QStringLiteral("Add copy"));
    QMetaObject::invokeMethod(root, "open");
    QTRY_VERIFY(root->property("armed").toBool());
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(confirmed.count(), 1);
    QVERIFY(root->property("visible").toBool());
}

void TestQmlUi::confirmDialogShowsTheThirdButtonOnlyWhenItHasOne()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);

    // A Quick item reports EFFECTIVE visibility, which stays false while it is
    // not in a scene — so put it in a window before judging any of it.
    auto *item = qobject_cast<QQuickItem *>(root);
    QVERIFY(item);
    QQuickWindow window;
    item->setParentItem(window.contentItem());
    window.show();

    QObject *alternate = root->findChild<QObject *>(QStringLiteral("alternateButton"));
    QVERIFY2(alternate, "the dialog has no alternate button at all");
    QVERIFY(root->findChild<QObject *>(QStringLiteral("cancelButton")));
    QVERIFY(root->findChild<QObject *>(QStringLiteral("confirmButton")));

    // The dialog starts hidden and open()/close() drive it. A child item reports
    // EFFECTIVE visibility, so the buttons can only be judged while it is open.
    QVERIFY(!root->property("visible").toBool());
    QMetaObject::invokeMethod(root, "open");
    QVERIFY(root->property("visible").toBool());

    // Default: a plain two-button confirmation, as every delete/remove uses.
    QVERIFY2(!alternate->property("visible").toBool(),
             "a third button showed up in an ordinary confirmation");

    root->setProperty("altText", QStringLiteral("Add copy"));
    QVERIFY2(alternate->property("visible").toBool(),
             "the collision dialog offers no way to add a copy");

    QMetaObject::invokeMethod(root, "close");
    QVERIFY(!root->property("visible").toBool());
}

// Main.qml carries a comment describing a bug that already happened: a second
// confirm request used to overwrite the live dialog in place, so the user
// answered a question they never read, with the buttons of the previous one, and
// the callback that ran belonged to the new one. Deep links arrive
// asynchronously and can legitimately land back to back, so the fix was to queue.
//
// Nothing tested the queue. Both halves could be deleted — the queueing itself
// and the Qt.callLater that defers the next dialog — with the suite still green.
// Checked first that a QML edit reaches this binary at all, by breaking Main.qml
// on purpose and watching qml_ui fail: a mutation that never got into the
// resource would have "survived" for the wrong reason entirely.
void TestQmlUi::aSecondConfirmQueuesInsteadOfReplacingTheLiveOne()
{
    QQmlComponent component(&m_engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    QVERIFY2(component.isReady(), component.errorString().toUtf8().constData());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(!root.isNull(), component.errorString().toUtf8().constData());

    // The dialog is an unnamed child of the window; find it by the property set it
    // exposes rather than by an id C++ cannot see.
    QObject *dialog = nullptr;
    const QList<QObject *> children = root->findChildren<QObject *>();
    for (QObject *child : children) {
        if (child->property("confirmText").isValid() && child->property("armed").isValid()) {
            dialog = child;
            break;
        }
    }
    QVERIFY2(dialog, "could not find the confirm dialog inside Main.qml");

    auto showConfirm = [&](const QString &message) {
        return QMetaObject::invokeMethod(root.data(), "showConfirm",
                                         Q_ARG(QVariant, QVariant(message)),
                                         Q_ARG(QVariant, QVariant(QStringLiteral("Yes"))),
                                         Q_ARG(QVariant, QVariant()));
    };

    QVERIFY(showConfirm(QStringLiteral("first question")));
    QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(dialog->property("text").toString(), QStringLiteral("first question"));

    // The second request must wait its turn, and the dialog on screen must still
    // be asking the first question — that is the whole point.
    QVERIFY(showConfirm(QStringLiteral("second question")));
    QCOMPARE(root->property("confirmQueue").toList().size(), 1);
    QVERIFY2(dialog->property("text").toString() == QStringLiteral("first question"),
             "the second request replaced the question already on screen");

    // Answering the first hands over to the second, deferred so the current answer
    // runs before confirmCb is reassigned.
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_COMPARE(dialog->property("text").toString(), QStringLiteral("second question"));
    QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(root->property("confirmQueue").toList().size(), 0);

    // And nothing queues behind the last one.
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
    QTRY_VERIFY(!dialog->property("visible").toBool());

    // The other half of the fix is that showNextConfirm is DEFERRED. The dialog
    // clears `visible` before it emits confirmed(), so handing over inline would
    // reassign win.confirmCb before the answer to the question on screen had run —
    // the user confirms one thing and a different callback fires. Queueing alone
    // does not prevent that, and the check above cannot see it, because close()
    // never emits confirmed() at all.
    //
    // Driven exactly as the confirm button does it: visible = false, then
    // confirmed(). The callbacks record into the mock backend because a QML
    // closure has nowhere else to write that C++ can read back.
    m_backend.setProperty("confirmLog", QString());
    QQmlExpression setup(qmlContext(root.data()), root.data(),
                         QStringLiteral(
                                 "showConfirm('first', 'Yes', function() { "
                                 "    backend.confirmLog = backend.confirmLog + 'A' });"
                                 "showConfirm('second', 'Yes', function() { "
                                 "    backend.confirmLog = backend.confirmLog + 'B' });"));
    setup.evaluate();
    QVERIFY2(!setup.hasError(), qPrintable(setup.error().toString()));
    QCOMPARE(dialog->property("text").toString(), QStringLiteral("first"));

    dialog->setProperty("visible", false);
    QVERIFY(QMetaObject::invokeMethod(dialog, "confirmed"));
    QCOMPARE(m_backend.property("confirmLog").toString(), QStringLiteral("A"));

    // Only now does the second question appear, with its own callback intact.
    QTRY_COMPARE(dialog->property("text").toString(), QStringLiteral("second"));
    dialog->setProperty("visible", false);
    QVERIFY(QMetaObject::invokeMethod(dialog, "confirmed"));
    QCOMPARE(m_backend.property("confirmLog").toString(), QStringLiteral("AB"));
}

namespace {

QObject *createMainWindow(QQmlEngine &engine)
{
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/Main.qml")));
    if (!component.isReady())
        qWarning("%s", qPrintable(component.errorString()));
    QObject *root = component.create();
    // Positions are only meaningful once the window has been laid out: until the
    // platform has sized it, the content item is 0 wide and everything anchored to
    // its right edge sits at x = 0.
    if (auto *window = qobject_cast<QQuickWindow *>(root)) {
        if (!QTest::qWaitFor([window] { return window->contentItem()->width() > 0; }, 3000))
            qWarning("the main window was never laid out");
    }
    return root;
}

} // namespace

// The Windows window agent finds the title bar, the three buttons and the nav row
// by objectName with findChild(). Anything it cannot find it silently leaves
// unregistered — and an unregistered maximise button is Snap Layouts never
// appearing, on a platform nobody here can look at. This is the contract, checked
// on the platform that can run it. It is not hypothetical: the buttons were first
// written with a Repeater, whose delegates findChild() cannot reach.
void TestQmlUi::everythingTheWindowsAgentLooksForCanBeFound()
{
    m_desktop.setLayout({}, {QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")});
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    for (const char *name : {"titleBar", "windowMinButton", "windowMaxButton", "windowCloseRect", "navRow"})
        QVERIFY2(root->findChild<QQuickItem *>(QLatin1String(name)), name);
    delete root;
}

// Which buttons exist, and on which side, is the desktop's choice. Pop!_OS has no
// maximise button; a layout set in Tweaks can put everything on the left.
void TestQmlUi::theWindowButtonsFollowTheDesktopLayout()
{
#ifdef Q_OS_MACOS
    QSKIP("macOS keeps AppKit's own window buttons; these are drawn only on Linux and Windows");
#endif
    m_desktop.setLayout({}, {QStringLiteral("minimize"), QStringLiteral("close")});
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    // No maximise where the desktop has none — and nothing left answering to its
    // name either, or the Windows agent would register a button nobody can see.
    QVERIFY2(!root->findChild<QQuickItem *>(QStringLiteral("windowMaxButton")),
             "no maximise button where the desktop has none");
    auto *close = root->findChild<QQuickItem *>(QStringLiteral("windowCloseRect"));
    QVERIFY(close && close->isVisible());
    const qreal width = root->property("width").toReal();
    QVERIFY2(close->mapToScene(QPointF(0, 0)).x() > width / 2, "close on the right");
    delete root;

    m_desktop.setLayout({QStringLiteral("close"), QStringLiteral("minimize")}, {});
    root = createMainWindow(m_engine);
    QVERIFY(root);
    close = root->findChild<QQuickItem *>(QStringLiteral("windowCloseRect"));
    auto *min = root->findChild<QQuickItem *>(QStringLiteral("windowMinButton"));
    QVERIFY(close && min && close->isVisible() && min->isVisible());
    QVERIFY2(close->mapToScene(QPointF(0, 0)).x() < width / 2, "close moved to the left");
    QVERIFY2(close->mapToScene(QPointF(0, 0)).x() < min->mapToScene(QPointF(0, 0)).x(),
             "and in the order the layout gives");
    delete root;

    m_desktop.setLayout({}, {QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")});
}

// The agent decides what is title bar by geometry alone, knowing nothing of what
// is drawn over it, so the item it uses has to be switched off while an overlay
// or a popup covers the band — or the click meant to close the overlay would
// drag the window instead.
void TestQmlUi::theTitleBarStepsAsideForOverlays()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *bar = root->findChild<QQuickItem *>(QStringLiteral("titleBar"));
    QVERIFY(bar);
    QVERIFY(bar->isEnabled());
    root->setProperty("overlay", QStringLiteral("create"));
    QCoreApplication::processEvents();
    QVERIFY2(!bar->isEnabled(), "not a drag handle under an open overlay");
    root->setProperty("overlay", QString());
    QCoreApplication::processEvents();
    QVERIFY(bar->isEnabled());
    delete root;
}

// What a title bar does when clicked is the desktop's setting, dispatched by name.
void TestQmlUi::titleBarClicksDoWhatTheDesktopSays()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    const int before = m_desktop.menuRequests;
    QMetaObject::invokeMethod(root, "titlebarAction", Q_ARG(QVariant, QStringLiteral("menu")));
    QCOMPARE(m_desktop.menuRequests, before + 1);
    // Actions this window cannot perform do nothing at all, rather than something
    // else — a shade request must not, say, maximise.
    QMetaObject::invokeMethod(root, "titlebarAction", Q_ARG(QVariant, QStringLiteral("toggle-shade")));
    QCOMPARE(m_desktop.menuRequests, before + 1);
    delete root;
}

// Driven with real mouse events rather than by calling titlebarAction, because
// what matters is which gesture reaches which action. A move hands the pointer to
// the window manager; started on the press, as it once was, it would swallow the
// second click of every double-click.
void TestQmlUi::aTitleBarPressIsNotYetAMove()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    // Mid-band: below the resize grip, above the nav, clear of the buttons.
    const QPoint at(window->width() / 2, 20);
    const int drags = m_backend.windowDrags;
    const int menus = m_desktop.menuRequests;

#ifdef Q_OS_MACOS
    // macOS is the exception, on purpose: the press goes straight to AppKit
    // (macHandleTitlebarPress), which tells a drag from a double-click itself and
    // does what Desktop & Dock says for the latter. So the move is handed over at
    // once, and nothing here acts on a double-click on top of AppKit.
    QTest::mousePress(window, Qt::LeftButton, {}, at);
    QCOMPARE(m_backend.windowDrags, drags + 1);
    QTest::mouseRelease(window, Qt::LeftButton, {}, at);
    m_desktop.setProperty("doubleClickAction", QStringLiteral("menu"));
    QTest::mouseDClick(window, Qt::LeftButton, {}, at);
    QCOMPARE(m_desktop.menuRequests, menus);
    m_desktop.setProperty("doubleClickAction", QStringLiteral("toggle-maximize"));
    delete root;
    return;
#endif

    QTest::mousePress(window, Qt::LeftButton, {}, at);
    QCOMPARE(m_backend.windowDrags, drags);
    QTest::mouseMove(window, at + QPoint(2, 0));
    QCOMPARE(m_backend.windowDrags, drags); // a tremor is not a drag
    QTest::mouseMove(window, at + QPoint(40, 0));
    QCOMPARE(m_backend.windowDrags, drags + 1);
    QTest::mouseMove(window, at + QPoint(80, 0));
    QCOMPARE(m_backend.windowDrags, drags + 1); // one move per press
    QTest::mouseRelease(window, Qt::LeftButton, {}, at + QPoint(80, 0));

    // A double-click does what the desktop says, and moves nothing. "menu" here
    // only because it is the action the mock can count.
    m_desktop.setProperty("doubleClickAction", QStringLiteral("menu"));
    QTest::mouseDClick(window, Qt::LeftButton, {}, at);
    QCOMPARE(m_desktop.menuRequests, menus + 1);
    QCOMPARE(m_backend.windowDrags, drags + 1);
    m_desktop.setProperty("doubleClickAction", QStringLiteral("toggle-maximize"));

    // A right-click is the window menu on the press, as on a real title bar.
    QTest::mousePress(window, Qt::RightButton, {}, at);
    QCOMPARE(m_desktop.menuRequests, menus + 2);
    QTest::mouseRelease(window, Qt::RightButton, {}, at);
    delete root;
}

namespace {

// Says what Qt tells the QML about light and dark, for as long as it lives. The
// offscreen platform's own theme cannot be asked to change its answer — it
// ignores QStyleHints::setColorScheme — so it is stood in for, and put back.
class QtSays : public QPlatformTheme {
public:
    explicit QtSays(Qt::ColorScheme scheme)
        : m_scheme(scheme), m_previous(QGuiApplicationPrivate::platform_theme)
    {
        QGuiApplicationPrivate::platform_theme = this;
        announce();
    }
    ~QtSays() override
    {
        QGuiApplicationPrivate::platform_theme = m_previous;
        announce();
    }
    Qt::ColorScheme colorScheme() const override { return m_scheme; }
    void set(Qt::ColorScheme scheme)
    {
        m_scheme = scheme;
        announce();
    }

private:
    static void announce()
    {
        QWindowSystemInterface::handleThemeChange<QWindowSystemInterface::SynchronousDelivery>();
    }
    Qt::ColorScheme m_scheme;
    QPlatformTheme *m_previous;
};

} // namespace

// On GNOME and Pop!_OS outside Flatpak Qt 6.8 reports Unknown, and a dark desktop
// got a light window. The desktop's own setting fills in then, and only then.
void TestQmlUi::theDesktopDecidesDarkOnlyWhenQtCannot()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);

    // Every pairing of what Qt says with what the desktop says.
    const int unknown = int(Qt::ColorScheme::Unknown);
    const int light = int(Qt::ColorScheme::Light);
    const int dark = int(Qt::ColorScheme::Dark);
    const struct {
        int qt;
        int desktop;
        bool isDark;
    } cases[] = {
        {unknown, unknown, false}, {unknown, light, false}, {unknown, dark, true},
        // Where Qt knows, it is right — including against the desktop.
        {light, unknown, false}, {light, light, false}, {light, dark, false},
        {dark, unknown, true}, {dark, light, true}, {dark, dark, true},
    };
    for (const auto &c : cases) {
        QVariant isDark;
        QVERIFY(QMetaObject::invokeMethod(root, "systemDarkFrom", Q_RETURN_ARG(QVariant, isDark),
                                          Q_ARG(QVariant, c.qt), Q_ARG(QVariant, c.desktop)));
        QVERIFY2(isDark.toBool() == c.isDark,
                 qPrintable(QStringLiteral("Qt %1, desktop %2").arg(c.qt).arg(c.desktop)));
    }

    // And through to the palette, on a platform where Qt does not know — the
    // offscreen one the tests run on reports Unknown, as Qt's GNOME theme does.
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Unknown);
    auto *theme = root->property("theme").value<QObject *>();
    QVERIFY(theme);
    m_backend.setThemeMode(QStringLiteral("system"));
    m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Dark));
    QVERIFY(theme->property("dark").toBool());
    m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Light));
    QVERIFY(!theme->property("dark").toBool());
    m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Unknown));
    QVERIFY(!theme->property("dark").toBool());
    // Where Qt does know, its answer decides, through the window's own binding —
    // and on Windows and macOS it is the only thing that ever does.
    {
        QtSays qt(Qt::ColorScheme::Dark);
        QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Dark);
        m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Light));
        QVERIFY(theme->property("dark").toBool());
        qt.set(Qt::ColorScheme::Light);
        m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Dark));
        QVERIFY(!theme->property("dark").toBool());
    }
    QCOMPARE(QGuiApplication::styleHints()->colorScheme(), Qt::ColorScheme::Unknown);
    QVERIFY(theme->property("dark").toBool()); // Qt knows nothing again: the desktop's dark

    // A choice made in the app is not overruled by the desktop.
    m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Dark));
    m_backend.setThemeMode(QStringLiteral("light"));
    QVERIFY(!theme->property("dark").toBool());

    m_backend.setThemeMode(QStringLiteral("dark"));
    m_desktop.setProperty("colorScheme", QVariant::fromValue(Qt::ColorScheme::Unknown));
    delete root;
}

// The tray menu's «Show FreeTunnel» is the user asking for the window from outside
// it, and goes through the path that handles that — see bringWindowForward().
void TestQmlUi::showFreeTunnelInTheTrayMenuBringsTheWindowForward()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QObject *show = nullptr;
    const auto candidates = root->findChildren<QObject *>();
    for (QObject *o : candidates) {
        if (o->property("text").toString() == QStringLiteral("Show FreeTunnel")
            && o->metaObject()->indexOfSignal("triggered()") >= 0)
            show = o;
    }
    QVERIFY2(show, "the tray menu has a «Show FreeTunnel» item");
    const int before = m_desktop.bringRequests;
    QVERIFY(QMetaObject::invokeMethod(show, "triggered"));
    QCOMPARE(m_desktop.bringRequests, before + 1);
    delete root;
}

void TestQmlUi::theTrayIconBringsTheWindowForward_data()
{
    QTest::addColumn<int>("reason");
    QTest::addColumn<bool>("brings");
    // Trigger is what every Linux host sends — a left click on KDE, a
    // double-click on GNOME — and a left click on Windows.
    QTest::newRow("click") << int(QPlatformSystemTrayIcon::Trigger) << true;
    QTest::newRow("double-click") << int(QPlatformSystemTrayIcon::DoubleClick) << true;
    QTest::newRow("menu") << int(QPlatformSystemTrayIcon::Context) << false;
    QTest::newRow("middle click") << int(QPlatformSystemTrayIcon::MiddleClick) << false;
}

// The tray icon's own gesture for the same request. Not on macOS, where the icon
// only opens its menu — see the comment on the tray's onActivated. On Linux the
// handler used to wait for a DoubleClick that no StatusNotifierItem host sends.
void TestQmlUi::theTrayIconBringsTheWindowForward()
{
    QFETCH(int, reason);
    QFETCH(bool, brings);
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QObject *tray = root->findChild<QObject *>(QStringLiteral("systemTray"));
    QVERIFY(tray);
    const int before = m_desktop.bringRequests;
    QVERIFY(QMetaObject::invokeMethod(
            tray, "activated",
            Q_ARG(QPlatformSystemTrayIcon::ActivationReason,
                  static_cast<QPlatformSystemTrayIcon::ActivationReason>(reason))));
#ifdef Q_OS_MACOS
    brings = false;
#endif
    QCOMPARE(m_desktop.bringRequests, before + (brings ? 1 : 0));
    delete root;
}

// Menus take '&' for the mnemonic marker everywhere, and on Linux the dbusmenu
// protocol takes '_' as one too, which Qt does not escape: the GNOME host dropped
// the first underscore of every config name.
void TestQmlUi::theTrayMenuShowsConfigNamesAsTyped()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] { m_backend.setConfigs(saved); });
    m_backend.setConfigs({QStringLiteral("my_vpn"), QStringLiteral("Tom & Jerry")});
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QStringList labels;
    const auto all = root->findChildren<QObject *>();
    for (QObject *o : all) {
        if (o->property("checkable").toBool() && o->metaObject()->indexOfSignal("triggered()") >= 0)
            labels << o->property("text").toString();
    }
#ifdef Q_OS_LINUX
    QStringList expected = {QStringLiteral("my__vpn"), QStringLiteral("Tom && Jerry")};
#else
    QStringList expected = {QStringLiteral("my_vpn"), QStringLiteral("Tom && Jerry")};
#endif
    labels.sort();
    expected.sort();
    QCOMPARE(labels, expected);
    delete root;
}

// People double-click whatever looks like an icon. Each click of the pair was a
// toggle, so a double-click from Off connected and at once cancelled.
void TestQmlUi::doubleClickingTheLogoTogglesOnce()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *logo = root->findChild<QQuickItem *>(QStringLiteral("connectionLogo"));
    QVERIFY2(logo && logo->isVisible(), "the logo on the Connection page");
    const QPoint at = logo->mapToScene(QPointF(logo->width() / 2.0, logo->height() / 2.0)).toPoint();

    const int before = m_backend.toggleCount();
    QTest::mouseDClick(window, Qt::LeftButton, {}, at);
    QCOMPARE(m_backend.toggleCount(), before + 1);
    QVERIFY(m_backend.connecting());

    m_backend.setConnecting(false);
    delete root;
}

// An action started from the tray while the window is hidden or minimised
// reports its failure in a toast nobody can see, and the toast used to be gone
// three seconds later. It now waits for the window to be back.
void TestQmlUi::aToastWaitsForAWindowThatIsAway()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QObject *toast = root->findChild<QObject *>(QStringLiteral("toast"));
    QObject *timer = root->findChild<QObject *>(QStringLiteral("toastTimer"));
    QVERIFY(toast && timer);

    window->hide();
    emit m_backend.errorOccurred(QStringLiteral("The server did not answer"));
    QCOMPARE(toast->property("message").toString(), QStringLiteral("The server did not answer"));
    QVERIFY2(!timer->property("running").toBool(), "nobody can read it yet");

    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY2(timer->property("running").toBool(), "its time starts when the window is back");
    delete root;
}

// The configs in the tray menu, driven the way the menu drives them: it flips a
// checkable item's tick itself, then reports the click. Choosing the ticked one
// used to clear its tick and do nothing else.
void TestQmlUi::theTickedConfigInTheTrayTurnsTheConnectionOnAndOff()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    const auto item = [root](const QString &text) -> QObject * {
        const auto all = root->findChildren<QObject *>();
        for (QObject *o : all) {
            if (o->property("text").toString() == text && o->property("checkable").toBool())
                return o;
        }
        return nullptr;
    };
    const auto click = [](QObject *menuItem) {
        menuItem->setProperty("checked", !menuItem->property("checked").toBool());
        return QMetaObject::invokeMethod(menuItem, "triggered");
    };
    QObject *active = item(QStringLiteral("Test Config"));
    QObject *other = item(QStringLiteral("Backup"));
    QVERIFY(active && other);
    QCOMPARE(m_backend.property("activeIndex").toInt(), 0);
    QVERIFY(active->property("checked").toBool());

    m_backend.setConnected(true);
    const int toggles = m_backend.toggleCount();
    QVERIFY(click(active));
    QCOMPARE(m_backend.toggleCount(), toggles + 1); // off
    QVERIFY(!m_backend.property("connected").toBool());
    QVERIFY2(active->property("checked").toBool(), "the active config keeps its tick");

    QVERIFY(click(active));
    QCOMPARE(m_backend.toggleCount(), toggles + 2); // and on again

    // Another config is switched to, not toggled, and the tick follows it.
    QVERIFY(click(other));
    QCOMPARE(m_backend.toggleCount(), toggles + 2);
    QCOMPARE(m_backend.property("activeIndex").toInt(), 1);
    QVERIFY(other->property("checked").toBool());
    QVERIFY(!active->property("checked").toBool());

    m_backend.selectConfig(0);
    m_backend.setConnected(false);
    delete root;
}

// Our own close button minimises. Brought back, the window has to be neither
// minimised nor robbed of being maximised, which show() — showNormal() in Qt 6.8 —
// would do. (Whether the window manager then lets it come forward is X11's part,
// and needs a real desktop to see.)
void TestQmlUi::aMinimisedWindowIsBroughtBack()
{
    QWindow window;
    window.resize(200, 200);
    window.show();
    window.setWindowStates(Qt::WindowMinimized);
    QVERIFY(window.windowStates() & Qt::WindowMinimized);
    freetunnel::bringWindowForward(&window);
    QVERIFY(!(window.windowStates() & Qt::WindowMinimized));
    QVERIFY(window.isVisible());

    // And a window minimised from maximised comes back maximised.
    window.setWindowStates(Qt::WindowMaximized | Qt::WindowMinimized);
    freetunnel::bringWindowForward(&window);
    QCOMPARE(window.windowStates(), Qt::WindowStates(Qt::WindowMaximized));
}

namespace {

// Starts recording the way a click on the field does.
void startCapture(QObject *field)
{
    field->setProperty("capturing", true);
    QMetaObject::invokeMethod(field, "forceActiveFocus");
}

} // namespace

// A global hotkey takes its combo from every other application. The field used
// to take any key: Enter pressed to confirm, Tab to move on or a plain letter
// was saved as a system-wide grab, and there was no way to unbind one.
void TestQmlUi::aHotkeyNeedsAModifierAndBackspaceUnbinds()
{
    QObject *root = loadPage("components/HotkeyField.qml");
    QVERIFY(root);
    auto *field = qobject_cast<QQuickItem *>(root);
    QVERIFY(field);
    QQuickWindow window;
    field->setParentItem(window.contentItem());
    field->setWidth(400);
    field->setHeight(42);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QSignalSpy captured(root, SIGNAL(captured(QString)));
    QVERIFY(captured.isValid());

    startCapture(field);
    QCOMPARE(m_backend.hotkeySuspensions, 1); // its own combos cannot fire meanwhile
    QTest::keyClick(&window, Qt::Key_Return);
    QTest::keyClick(&window, Qt::Key_Tab);
    QTest::keyClick(&window, Qt::Key_A);
    QTest::keyClick(&window, Qt::Key_A, Qt::ShiftModifier);
    QCOMPARE(captured.count(), 0);
    QVERIFY2(root->property("capturing").toBool(), "a refused key keeps the field recording");
    QVERIFY2(root->property("needsModifier").toBool(), "and says what it is waiting for");

    QTest::keyClick(&window, Qt::Key_F5);
    QCOMPARE(captured.count(), 1);
    QCOMPARE(captured.constLast().at(0).toString(), QStringLiteral("F5"));
    QVERIFY(!root->property("capturing").toBool());
    QCOMPARE(m_backend.hotkeySuspensions, 0);

    startCapture(field);
    QTest::keyClick(&window, Qt::Key_T, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(captured.constLast().at(0).toString(), QStringLiteral("Ctrl+Alt+T"));

    startCapture(field);
    QTest::keyClick(&window, Qt::Key_Backspace);
    QCOMPARE(captured.count(), 3);
    QVERIFY2(captured.constLast().at(0).toString().isEmpty(), "Backspace unbinds");
    QCOMPARE(m_backend.hotkeySuspensions, 0);
    delete root;
}

// A dialog opened over a field still recording a combo, by a click elsewhere or
// a link arriving, left the keyboard with the field: Return meant for the dialog
// was recorded as the hotkey, and the dialog stayed open.
void TestQmlUi::aConfirmDialogTakesTheKeyboardFromAHotkeyField()
{
    QObject *fieldRoot = loadPage("components/HotkeyField.qml");
    QObject *dialogRoot = loadPage("components/ConfirmDialog.qml");
    QVERIFY(fieldRoot && dialogRoot);
    auto *field = qobject_cast<QQuickItem *>(fieldRoot);
    auto *dialog = qobject_cast<QQuickItem *>(dialogRoot);
    QQuickWindow window;
    window.resize(400, 400);
    field->setParentItem(window.contentItem());
    field->setWidth(400);
    field->setHeight(42);
    dialog->setParentItem(window.contentItem());
    dialog->setWidth(400);
    dialog->setHeight(400);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QSignalSpy captured(fieldRoot, SIGNAL(captured(QString)));
    QSignalSpy confirmed(dialogRoot, SIGNAL(confirmed()));

    startCapture(field);
    QVERIFY(field->hasActiveFocus());
    QMetaObject::invokeMethod(dialogRoot, "open");
    QVERIFY2(!fieldRoot->property("capturing").toBool(), "the dialog took the keyboard");
    QCOMPARE(m_backend.hotkeySuspensions, 0);

    QTRY_VERIFY(dialogRoot->property("armed").toBool());
    QTest::keyClick(&window, Qt::Key_Return);
    QCOMPARE(confirmed.count(), 1);
    QCOMPARE(captured.count(), 0);
    QVERIFY2(field->hasActiveFocus(), "and gave it back when it closed");
    delete dialogRoot;
    delete fieldRoot;
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    // Nor the machine's platform theme, which Qt loads even under offscreen: the
    // palette tests need Qt to know nothing of light and dark unless a test says so.
    qunsetenv("QT_QPA_PLATFORMTHEME");
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
