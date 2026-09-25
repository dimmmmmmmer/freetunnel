// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <functional>

#include <QDirIterator>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineF>
#include <QRegularExpression>
#include <QPointer>
#include <QScopeGuard>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTranslator>
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
    void theTrayMenuShowsConfigNamesAsTyped_data();
    void theTrayMenuShowsConfigNamesAsTyped();
    void doubleClickingTheLogoTogglesOnce();
    void aToastWaitsForAWindowThatIsAway();
    void onlyATrayActionsFailureIsNotified();
    void theTickedConfigInTheTrayTurnsTheConnectionOnAndOff();
    void aMinimisedWindowIsBroughtBack();
    void minimisingKeepsAWindowMaximised();
    void hidingAFullScreenWindowLeavesFullScreenFirst();
    void theWindowsOwnButtonsMinimiseThroughTheDesktop();
    void theMacKeysForCloseAndMinimise();
    void aTrayHostThatAppearsLateGetsAnIcon();
    void theEmptyConfigsPageOffersToAdd();
    void configActionsFollowTheConfigNotTheRow();
    void theExportDialogOffersTheWholeName();
    void aLongHostnameWrapsInsideTheConfirm();
    void aClickInsideTheConfirmCardDoesNotCancelIt();
    void threeButtonsStayInsideTheConfirmCard();
    void aShortQuestionGetsANarrowCard();
    void aTwoLineToastIsAsWideAsItsLongestLine();
    void aWrappedQuestionHasNoEmptySides();
    void aWrappedToastHasNoEmptySides();
    void aLongToastIsShownWholeAndForLonger();
    void aToastOnHomeKeepsOffTheSelectorAndTheTiles();
    void theConfigPickerIsAsWideAsItsNames();
    void theActiveConfigNameUsesTheRoomItHas();
    void theConnectedConfigKeepsRoomForItsName();
    void theImportMenuIsAsWideAsItsItems();
    void theSelectPopupElidesWhatTheWindowCannotHold();
    void popupsCloseWhenTheWindowIsResized();
    void theEditorsButtonsFitTheirLabels();
    void linksActOnlyOverTheirWords();
    void chipsAreCutOnlyWhereTheRowEnds();
    void theWindowConfirmOwnsTheKeysOverTheEditorsPrompt();
    void escapeStandsDownForTheEditorsFileDialog();
    void tabMovesThroughTheEditorsFields();
    void aToastStaysOffTheEditorsButtons();
    void clearEmptiesTheLogEvenWithASelection();
    void aHeldLogCatchesUpWhenTheSelectionGoes();
    void theLogsPageSaysWhenLoggingIsOff();
    void theUpdateLineDoesWhatItOffers();
    void restoringDefaultRoutesAsksFirst();
    void theThroughVpnNoticeNamesTheConfigAndItsProfile();
    void theBuiltInProfileIsShownInTheUsersLanguage();
    void textOnTheAccentIsReadableInTheDarkTheme();
    void headingLinksStayOnANarrowPage_data();
    void headingLinksStayOnANarrowPage();
    void russianFitsAtTheDefaultWidth_data();
    void russianFitsAtTheDefaultWidth();
    void everyComponentLoadsOnItsOwn();
    void everyComponentLoadsOnItsOwn_data();
    void confirmDialogShowsTheThirdButtonOnlyWhenItHasOne();
    void confirmDialogAnswersReturnAndEscape();
    void aSecondConfirmQueuesInsteadOfReplacingTheLiveOne();
    void aHotkeyNeedsAModifierAndBackspaceUnbinds();
    void aConfirmDialogTakesTheKeyboardFromAHotkeyField();
    void theUpdateArrowBendsUnderThePointerAndBack();
    void theUpdateArrowTurnsWhileItWorks();
    void theUpdateArrowBendsFromDownIntoAClockwiseCircle();
    void theConnectingLogoPulses();
    void onlyTheLogoConnects();
    void homeLeadsStraightToTheAddMenu();
    void whatAPopupCoversDoesNotLightUp();
    void theTrayConnectItemSaysWhatItDoes();
    void anUpdateIsAnnouncedOnce();
    void theConfigListSaysWhenItIsConnecting();
    void theWindowThemeWorksInBothModes();
    void noPropertyIsNamedLikeASignalHandler();
    void theHotkeyFieldFillStaysOpaque();
    void footerLinksUnderlineLikeTheOthers();
    void theEditorsBackArrowTakesANearMiss();
    void thePickerWaitsForTheScanWithoutFreezing();
    void theSelectPopupFadesOutAndLetsGo();
    void aDoubleClickThatOpensTheAddMenuLeavesItOpen();

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

#ifdef Q_OS_LINUX
namespace {

// What Qt sends over D-Bus for a menu label: the first '&' that is not the last
// character becomes dbusmenu's '_' (QDBusMenuItem::convertMnemonic).
QString dbusLabel(const QString &label)
{
    const qsizetype at = label.indexOf(QLatin1Char('&'));
    if (at < 0 || at == label.size() - 1)
        return label;
    QString wire = label;
    wire[at] = QLatin1Char('_');
    return wire;
}

// What GNOME Shell's AppIndicator extension shows for it (dbusMenu.js):
// label.replace(/_([^_])/, '$1'), the first match only.
QString gnomeShows(const QString &wire)
{
    for (qsizetype i = 0; i + 1 < wire.size(); ++i) {
        if (wire.at(i) == QLatin1Char('_') && wire.at(i + 1) != QLatin1Char('_'))
            return wire.left(i) + wire.mid(i + 1);
    }
    return wire;
}

// What KDE shows for it: dbusmenu-qt's swapMnemonicChar('_' to '&'), and then a
// Qt menu, which hides the mnemonic marker and folds '&&'.
QString kdeShows(const QString &wire)
{
    QString menu;
    bool mnemonic = false;
    for (qsizetype i = 0; i < wire.size(); ++i) {
        const QChar c = wire.at(i);
        if (c == QLatin1Char('_')) {
            if (i + 1 < wire.size() && wire.at(i + 1) == QLatin1Char('_')) {
                menu += c;
                ++i;
            } else if (i + 1 < wire.size() && !mnemonic) {
                mnemonic = true;
                menu += QLatin1Char('&');
            }
        } else if (c == QLatin1Char('&')) {
            menu += QStringLiteral("&&");
        } else {
            menu += c;
        }
    }
    QString shown;
    for (qsizetype i = 0; i < menu.size(); ++i) {
        if (menu.at(i) == QLatin1Char('&') && i + 1 < menu.size())
            ++i;
        shown += menu.at(i);
    }
    return shown;
}

} // namespace
#endif

void TestQmlUi::theTrayMenuShowsConfigNamesAsTyped_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("linuxShows");
    QTest::newRow("one underscore") << QStringLiteral("my_vpn") << QStringLiteral("my vpn");
    // What 1.2.0 made of "My Home VPN". GNOME dropped the first underscore, and
    // doubling them all showed three where there had been two.
    QTest::newRow("from 1.2.0") << QStringLiteral("My_Home_VPN") << QStringLiteral("My Home VPN");
    QTest::newRow("an ampersand") << QStringLiteral("Tom & Jerry") << QStringLiteral("Tom & Jerry");
    QTest::newRow("two of them") << QStringLiteral("R&D & Ops") << QStringLiteral("R&D & Ops");
    QTest::newRow("punctuation") << QStringLiteral("Germany · Frankfurt") << QStringLiteral("Germany · Frankfurt");
}

// Menus read '&' as the mnemonic marker, and on Linux dbusmenu reads '_' too,
// with the hosts disagreeing on how: GNOME's extension dropped the first
// underscore of a name, and the fix that doubled every underscore showed extra
// ones there. The labels are checked through what each host actually displays.
void TestQmlUi::theTrayMenuShowsConfigNamesAsTyped()
{
    QFETCH(QString, name);
    QFETCH(QString, linuxShows);
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] { m_backend.setConfigs(saved); });
    m_backend.setConfigs({name});
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QString label;
    const auto all = root->findChildren<QObject *>();
    for (QObject *o : all) {
        if (o->property("checkable").toBool() && o->metaObject()->indexOfSignal("triggered()") >= 0)
            label = o->property("text").toString();
    }
    QVERIFY(!label.isEmpty());
#ifdef Q_OS_LINUX
    const QString wire = dbusLabel(label);
    QCOMPARE(gnomeShows(wire), linuxShows);
    QCOMPARE(kdeShows(wire), linuxShows);
#else
    Q_UNUSED(linuxShows)
    QString expected = name;
    QCOMPARE(label, expected.replace(QLatin1Char('&'), QStringLiteral("&&")));
#endif
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

// QWindow::showMinimized() replaces every state with Minimized, and on X11 Qt then
// has the window manager take maximised off first: the window shrank to normal
// size on its way down, and came back at it.
void TestQmlUi::minimisingKeepsAWindowMaximised()
{
    QWindow window;
    window.resize(200, 200);
    window.show();
    window.setWindowStates(Qt::WindowMaximized);
    freetunnel::minimizeWindow(&window);
    QCOMPARE(window.windowStates(), Qt::WindowMaximized | Qt::WindowMinimized);
    freetunnel::bringWindowForward(&window);
    QCOMPARE(window.windowStates(), Qt::WindowStates(Qt::WindowMaximized));
}

// Hiding to the tray hides, full screen or not. Waiting for macOS to finish
// leaving full screen first, the reason this is not a plain hide(), is AppKit's
// part and can only be seen on a Mac; under offscreen this takes the plain path.
void TestQmlUi::hidingAFullScreenWindowLeavesFullScreenFirst()
{
    QWindow window;
    window.resize(200, 200);
    window.setVisible(true); // not show(), which is showNormal() and would undo the next line
    window.setWindowStates(Qt::WindowFullScreen);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    freetunnel::hideWindowToTray(&window);
    QTRY_VERIFY(!window.isVisible());

    window.setWindowStates(Qt::WindowNoState);
    window.setVisible(true);
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    freetunnel::hideWindowToTray(&window);
    QVERIFY(!window.isVisible());
}

// Every minimise the window starts itself goes through the desktop, which keeps
// the window maximised: its own button, and a title band whose click the
// desktop has set to minimise.
void TestQmlUi::theWindowsOwnButtonsMinimiseThroughTheDesktop()
{
#ifdef Q_OS_MACOS
    QSKIP("macOS draws its own traffic lights");
#else
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *minimise = root->findChild<QQuickItem *>(QStringLiteral("windowMinButton"));
    QVERIFY2(minimise && minimise->isVisible(), "the window's own minimise button");
    const int before = m_desktop.minimizeRequests;
    const QPoint at =
            minimise->mapToScene(QPointF(minimise->width() / 2.0, minimise->height() / 2.0)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(m_desktop.minimizeRequests, before + 1);
    QMetaObject::invokeMethod(root, "titlebarAction", Q_ARG(QVariant, QStringLiteral("minimize")));
    QCOMPARE(m_desktop.minimizeRequests, before + 2);
    delete root;
#endif
}

// ⌘W and ⌘M reached nothing on macOS: a QGuiApplication has no Window menu to
// carry them. Elsewhere Ctrl+W and Ctrl+M are not the window's.
void TestQmlUi::theMacKeysForCloseAndMinimise()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    const int hides = m_desktop.hideRequests;
    const int minimises = m_desktop.minimizeRequests;
    QTest::keyClick(window, Qt::Key_W, Qt::ControlModifier); // ⌘W on macOS
    QTest::keyClick(window, Qt::Key_M, Qt::ControlModifier); // ⌘M on macOS
#ifdef Q_OS_MACOS
    QCOMPARE(m_desktop.hideRequests, hides + 1);
    QCOMPARE(m_desktop.minimizeRequests, minimises + 1);
#else
    QCOMPARE(m_desktop.hideRequests, hides);
    QCOMPARE(m_desktop.minimizeRequests, minimises);
#endif
    delete root;
}

// A connection retrying in the background reports every failed try. Sent as
// notifications, that was one about every half minute through an outage; only
// the failure of something started from the tray, with the window away, is.
void TestQmlUi::onlyATrayActionsFailureIsNotified()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->hide();
    const auto restore = qScopeGuard([this] { m_backend.setConnecting(false); });

    QVERIFY(!root->property("errorsGoToTray").toBool());
    emit m_backend.errorOccurred(QStringLiteral("a background retry failed"));
    QVERIFY(!root->property("errorsGoToTray").toBool());

    QObject *connect = nullptr;
    for (QObject *o : root->findChildren<QObject *>()) {
        if (o->property("text").toString() == QStringLiteral("Connect")
            && o->metaObject()->indexOfSignal("triggered()") >= 0)
            connect = o;
    }
    QVERIFY2(connect, "the tray's «Connect»");
    QVERIFY(QMetaObject::invokeMethod(connect, "triggered"));
    QVERIFY2(root->property("errorsGoToTray").toBool(), "its failure is worth saying while away");
    emit m_backend.errorOccurred(QStringLiteral("the server did not answer"));
    QVERIFY2(!root->property("errorsGoToTray").toBool(), "and said once");
    delete root;
}

// Qt makes the tray icon's platform half once, and only if a tray host is on the
// bus right then. When one turns up later, an icon without a tray is made again.
void TestQmlUi::aTrayHostThatAppearsLateGetsAnIcon()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QPointer<QObject> first = root->findChild<QObject *>(QStringLiteral("systemTray"));
    QVERIFY(first);
    QVERIFY2(!first->property("available").toBool(), "offscreen has no tray, which is the case here");
    emit m_desktop.trayHostAppeared();
    QTRY_VERIFY2(first.isNull(), "the icon made without a tray was not replaced");
    QObject *second = root->findChild<QObject *>(QStringLiteral("systemTray"));
    QVERIFY(second);
    QCOMPARE(root->property("tray").value<QObject *>(), second);
    delete root;
}

namespace {

// A component on its own, sized and on screen, for a test that clicks it.
QQuickItem *showInWindow(QObject *root, QQuickWindow &window, int width, int height)
{
    auto *item = qobject_cast<QQuickItem *>(root);
    if (!item)
        return nullptr;
    window.resize(width, height);
    item->setParentItem(window.contentItem());
    item->setWidth(width);
    item->setHeight(height);
    window.show();
    return QTest::qWaitForWindowExposed(&window) ? item : nullptr;
}

QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0)).toPoint();
}

} // namespace

// The empty list filled the same area as the "Add a config" hint and, being a
// Flickable, took its click; only the small + in the header worked.
void TestQmlUi::theEmptyConfigsPageOffersToAdd()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] { m_backend.setConfigs(saved); });
    m_backend.setConfigs({});
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 400));
    auto *hint = root->findChild<QQuickItem *>(QStringLiteral("addConfigHint"));
    QObject *menu = root->findChild<QObject *>(QStringLiteral("importMenu"));
    QVERIFY(hint && hint->isVisible() && menu);
    // A link, as the same words on Home are, not placeholder grey that ignores the
    // pointer.
    QTest::mouseMove(&window, centreOf(hint));
    QTRY_VERIFY(hint->property("font").value<QFont>().underline());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(hint));
    QVERIFY2(menu->property("open").toBool(), "clicking «Add a config» opens the add menu");
    // And out of the way of its menu, which at the default height cut it in half.
    QVERIFY(!hint->isVisible());
    delete root;
}

// The export menu and the delete confirmation outlive the row they came from,
// and an import prepends to the list. A remembered row number then exported the
// neighbouring config, password included, or deleted it.
void TestQmlUi::configActionsFollowTheConfigNotTheRow()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] { m_backend.setConfigs(saved); });
    m_backend.setConfigs({QStringLiteral("Alpha"), QStringLiteral("Beta")});
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);

    root->setProperty("exportPath", m_backend.configPath(1)); // Beta's menu opened
    root->setProperty("deletePath", m_backend.configPath(1)); // and Beta's delete asked
    m_backend.setConfigs({QStringLiteral("Imported"), QStringLiteral("Alpha"), QStringLiteral("Beta")});

    QMetaObject::invokeMethod(root, "exportPicked", Q_ARG(QVariant, QStringLiteral("link")));
    QCOMPARE(m_backend.lastDeepLinkRow, 2);
    QMetaObject::invokeMethod(root, "exportToml", Q_ARG(QVariant, QStringLiteral("file:///tmp/x.toml")));
    QCOMPARE(m_backend.lastExportRow, 2);
    QMetaObject::invokeMethod(root, "deleteConfirmed");
    QCOMPARE(m_backend.configs(),
             (QStringList{QStringLiteral("Imported"), QStringLiteral("Alpha")}));

    // Gone by the time the action runs: nothing happens to anything else.
    m_backend.lastDeepLinkRow = -1;
    QMetaObject::invokeMethod(root, "exportPicked", Q_ARG(QVariant, QStringLiteral("link")));
    QCOMPARE(m_backend.lastDeepLinkRow, -1);
    QCOMPARE(m_shell.lastToast(), QStringLiteral("That configuration is no longer there."));
    delete root;
}

// A name may hold '#' and '%' now. Glued into a URL, '#' began the fragment and
// '%' an escape, so "Work #2" was offered as "Work .toml".
void TestQmlUi::theExportDialogOffersTheWholeName()
{
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);
    QObject *dialog = root->findChild<QObject *>(QStringLiteral("configExportDialog"));
    QVERIFY(dialog);
    root->setProperty("exportName", QStringLiteral("Work #2 at 100%fast"));
    QCOMPARE(dialog->property("selectedFile").toUrl().fileName(), QStringLiteral("Work #2 at 100%fast.toml"));
    delete root;
}

// The import prompt names the server, the one line the user can trust, and a
// hostname has no space to break at: WordWrap let it run past both edges.
void TestQmlUi::aLongHostnameWrapsInsideTheConfirm()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 460));
    root->setProperty("text", QStringLiteral("Import this config?\nServer: "
                                             "fra1.de.nodes.premiumvpnprovider.example.net"));
    QMetaObject::invokeMethod(root, "open");
    auto *text = root->findChild<QQuickItem *>(QStringLiteral("confirmText"));
    QVERIFY(text);
    QTRY_VERIFY(text->property("contentWidth").toReal() <= text->width() + 0.5);
    delete root;
}

// Only the backdrop and Cancel dismiss. A click on the message, the card's
// padding or the gap between two buttons fell through to the backdrop.
void TestQmlUi::aClickInsideTheConfirmCardDoesNotCancelIt()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 400));
    root->setProperty("text", QStringLiteral("Delete config “Work”?"));
    QMetaObject::invokeMethod(root, "open");
    auto *card = root->findChild<QQuickItem *>(QStringLiteral("confirmCard"));
    QVERIFY(card);
    const QPoint onTheCard = card->mapToScene(QPointF(card->width() / 2.0, 8)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, onTheCard);
    QVERIFY2(root->property("visible").toBool(), "a click on the card cancelled the dialog");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4)); // the backdrop
    QVERIFY(!root->property("visible").toBool());
    delete root;
}

// Buttons cannot wrap the way text does, and three of them ran into the card's
// edges, or past them, when the window was narrow for the language and font:
// Russian at the default width on Linux, and wider fonts elsewhere. Checked at
// the widths where each way of fitting them has to work.
void TestQmlUi::threeButtonsStayInsideTheConfirmCard()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 600, 400));
    root->setProperty("text", QStringLiteral("«Работа» уже есть."));
    root->setProperty("confirmText", QStringLiteral("Заменить"));
    root->setProperty("altText", QStringLiteral("Добавить копию"));
    QMetaObject::invokeMethod(root, "open");
    const QStringList names{QStringLiteral("cancelButton"), QStringLiteral("alternateButton"),
                            QStringLiteral("confirmButton")};
    QList<QQuickItem *> buttons;
    qreal row = 16; // the two gaps between three buttons
    for (const QString &name : names) {
        auto *b = root->findChild<QQuickItem *>(name);
        QVERIFY(b);
        buttons << b;
        row += b->width();
    }
    auto *card = root->findChild<QQuickItem *>(QStringLiteral("confirmCard"));
    QVERIFY(card);
    const auto inside = [&]() {
        for (QQuickItem *b : std::as_const(buttons)) {
            const QPointF at = b->mapToItem(card, QPointF(0, 0));
            if (at.x() < 13 || at.x() + b->width() > card->width() - 13)
                return false;
        }
        return true;
    };
    // The dialog fills its window, so the window is what is narrowed.
    window.resize(int(row + 28 + 40), 400); // room for the row, not for the usual margin
    QTRY_VERIFY2(inside(), "full-size buttons run into the card's edges");
    window.resize(int(row * 0.8), 400); // not enough for them at all
    QTRY_VERIFY2(inside(), "compact buttons run into the card's edges");
    delete root;
}

namespace {

// The widest line of a message, as a QML Text of that pixel size lays it out.
qreal widestLine(const QString &text, int pixelSize, QFont::Weight weight = QFont::Normal)
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    const QFontMetricsF metrics(font);
    qreal widest = 0;
    for (const QString &line : text.split(QLatin1Char('\n')))
        widest = std::max(widest, metrics.horizontalAdvance(line));
    return widest;
}

// Text that grows a letter at a time from `start` until it is at least `width`
// wide in that font, whatever the font: the tests that use it need a name of a
// given width more than a given name.
QString textOfWidth(QString start, qreal width, int pixelSize, QFont::Weight weight = QFont::Normal)
{
    static const QString letters = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
    for (int i = 0; widestLine(start, pixelSize, weight) < width; ++i)
        start += letters.at(i % letters.size());
    return start;
}

// Every item under `item`, drawn ones included: a ListView's rows and a
// Repeater's chips are children of their view as items, and findChildren(),
// which follows QObject parents, does not reach them.
QList<QQuickItem *> itemsIn(QQuickItem *item)
{
    QList<QQuickItem *> out;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        out << child << itemsIn(child);
    return out;
}

// The visible Text under `item` that says `text`, or null.
QQuickItem *textIn(QQuickItem *item, const QString &text)
{
    const QList<QQuickItem *> all = itemsIn(item);
    for (QQuickItem *candidate : all) {
        if (candidate->inherits("QQuickText") && candidate->isVisible()
            && candidate->property("text").toString() == text)
            return candidate;
    }
    return nullptr;
}

QQuickItem *namedIn(QQuickItem *item, const QString &objectName)
{
    const QList<QQuickItem *> all = itemsIn(item);
    for (QQuickItem *candidate : all) {
        if (candidate->objectName() == objectName)
            return candidate;
    }
    return nullptr;
}

QRectF sceneRect(QQuickItem *item)
{
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

QVariant evaluateIn(QObject *root, const QString &expression)
{
    QQmlExpression e(qmlContext(root), root, expression);
    const QVariant value = e.evaluate();
    if (e.hasError())
        qWarning("%s", qPrintable(e.error().toString()));
    return value;
}

// A language whose words for `sources` are much longer than English's, which is
// what a wider font does to them as well.
class LongerWords : public QTranslator {
public:
    LongerWords(QByteArrayList sources, QString suffix)
        : m_sources(std::move(sources)), m_suffix(std::move(suffix)) {}
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *source, const char *, int) const override
    {
        if (m_sources.contains(QByteArray(source)))
            return QString::fromUtf8(source) + m_suffix;
        return QString();
    }

private:
    QByteArrayList m_sources;
    QString m_suffix;
};

} // namespace

// The card was sized from TextMetrics, which takes a message as one line,
// newlines and all: the two-line import question was sized as both lines end to
// end, and the card spread across the window around a short text.
void TestQmlUi::aShortQuestionGetsANarrowCard()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 400));
    const QString message = QStringLiteral("Import it?\nServer: a.example");
    root->setProperty("text", message);
    root->setProperty("confirmText", QStringLiteral("Import"));
    QMetaObject::invokeMethod(root, "open");
    auto *card = root->findChild<QQuickItem *>(QStringLiteral("confirmCard"));
    auto *cancel = root->findChild<QQuickItem *>(QStringLiteral("cancelButton"));
    auto *confirm = root->findChild<QQuickItem *>(QStringLiteral("confirmButton"));
    QVERIFY(card && cancel && confirm);
    const qreal buttons = cancel->width() + 8 + confirm->width();
    const qreal fits = std::max(buttons, widestLine(message, 14)) + 28;
    QTRY_VERIFY2(card->width() <= fits + 2,
                 qPrintable(QStringLiteral("a %1 px card for %2 px of content").arg(card->width()).arg(fits)));
    delete root;
}

// The toast was sized the same way.
void TestQmlUi::aTwoLineToastIsAsWideAsItsLongestLine()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *toast = root->findChild<QQuickItem *>(QStringLiteral("toast"));
    QVERIFY(toast);
    const QString message = QStringLiteral("Could not import.\nTry again.");
    QMetaObject::invokeMethod(root, "showToast", Q_ARG(QVariant, message));
    const qreal fits = std::max<qreal>(80, widestLine(message, 13) + 24);
    QTRY_VERIFY2(toast->width() <= fits + 2,
                 qPrintable(QStringLiteral("a %1 px toast for %2 px of text").arg(toast->width()).arg(fits)));
    delete root;
}

// A question that wraps was sized from its one-line width, so a card held to
// the window had broad empty sides around text that wrapped well short of them.
void TestQmlUi::aWrappedQuestionHasNoEmptySides()
{
    QObject *root = loadPage("components/ConfirmDialog.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 400));
    // Two words, each a little over half as wide as the card's text may be, so
    // the question wraps between them and each line leaves room beside it.
    const QString word = textOfWidth(QStringLiteral("Question"), 180, 14);
    const QString message = word + QLatin1Char(' ') + word;
    QVERIFY(widestLine(message, 14) > 400 - 84);
    root->setProperty("text", message);
    root->setProperty("confirmText", QStringLiteral("OK"));
    QMetaObject::invokeMethod(root, "open");
    auto *card = root->findChild<QQuickItem *>(QStringLiteral("confirmCard"));
    auto *cancel = root->findChild<QQuickItem *>(QStringLiteral("cancelButton"));
    auto *confirm = root->findChild<QQuickItem *>(QStringLiteral("confirmButton"));
    QVERIFY(card && cancel && confirm);
    const qreal buttons = cancel->width() + 8 + confirm->width();
    const qreal fits = std::max(buttons, widestLine(word, 14)) + 28;
    QTRY_VERIFY2(card->width() <= fits + 2,
                 qPrintable(QStringLiteral("a %1 px card for %2 px of content").arg(card->width()).arg(fits)));
    delete root;
}

// The toast was sized the same way.
void TestQmlUi::aWrappedToastHasNoEmptySides()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *toast = root->findChild<QQuickItem *>(QStringLiteral("toast"));
    QVERIFY(toast);
    const QString word = textOfWidth(QStringLiteral("Message"), 200, 13);
    const QString message = word + QLatin1Char(' ') + word;
    QVERIFY(widestLine(message, 13) > window->width() - 60);
    QMetaObject::invokeMethod(root, "showToast", Q_ARG(QVariant, message));
    const qreal fits = widestLine(word, 13) + 24;
    QTRY_VERIFY2(toast->width() <= fits + 2,
                 qPrintable(QStringLiteral("a %1 px toast for %2 px of text").arg(toast->width()).arg(fits)));
    delete root;
}

// A name with no spaces in it ran past both edges of the toast; a fourth line
// was cut, and it was usually the one saying what to do; and a long message was
// gone in the same three seconds as "Copied".
void TestQmlUi::aLongToastIsShownWholeAndForLonger()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *text = root->findChild<QQuickItem *>(QStringLiteral("toastText"));
    QObject *timer = root->findChild<QObject *>(QStringLiteral("toastTimer"));
    QVERIFY(text && timer);
    const auto show = [root](const QString &message) {
        QMetaObject::invokeMethod(root, "showToast", Q_ARG(QVariant, message));
    };

    const QString unbroken = textOfWidth(QStringLiteral("frankfurt"), 2.5 * window->width(), 13);
    show(unbroken);
    QVERIFY2(text->property("contentWidth").toReal() <= text->width() + 0.5,
             qPrintable(QStringLiteral("%1 px of text in a %2 px toast")
                                .arg(text->property("contentWidth").toReal()).arg(text->width())));
    QVERIFY(!text->property("truncated").toBool());

    const QString fiveLines = QStringLiteral("Could not connect.\nNo answer.\nIt may be down.\n"
                                             "Check the address.\nThen try again.");
    show(fiveLines);
    QCOMPARE(text->property("lineCount").toInt(), 5);
    QVERIFY(!text->property("truncated").toBool());

    show(QStringLiteral("Copied."));
    const int brief = timer->property("interval").toInt();
    show(fiveLines);
    QVERIFY2(timer->property("interval").toInt() > brief,
             qPrintable(QStringLiteral("%1 ms for five lines, as for one word").arg(brief)));
    delete root;
}

// On Home the toast sat across the middle of the speed tiles and cut their
// labels in half. A short one keeps to the gap between the config selector and
// the tiles; a longer one covers the tiles, whole, rather than the selector.
void TestQmlUi::aToastOnHomeKeepsOffTheSelectorAndTheTiles()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *page = root->findChild<QObject *>(QStringLiteral("pageLoader"))->property("item").value<QQuickItem *>();
    QVERIFY(page);
    auto *label = namedIn(page, QStringLiteral("activeConfigLabel"));
    auto *tiles = namedIn(page, QStringLiteral("speedTiles"));
    auto *toast = root->findChild<QQuickItem *>(QStringLiteral("toast"));
    QVERIFY(label && tiles && toast);
    const QRectF selector = sceneRect(label);
    const QRectF tileRow = sceneRect(tiles);
    const auto describe = [&] {
        const QRectF t = sceneRect(toast);
        return QStringLiteral("toast %1..%2, selector %3..%4, tiles %5..%6")
                .arg(t.top()).arg(t.bottom()).arg(selector.top()).arg(selector.bottom())
                .arg(tileRow.top()).arg(tileRow.bottom());
    };

    QMetaObject::invokeMethod(root, "showToast", Q_ARG(QVariant, QStringLiteral("Config imported.")));
    QVERIFY2(!sceneRect(toast).intersects(selector) && !sceneRect(toast).intersects(tileRow),
             qPrintable(describe()));

    QMetaObject::invokeMethod(root, "showToast",
                              Q_ARG(QVariant, QStringLiteral("Could not connect.\nNo answer.\nTry again.")));
    QVERIFY2(!sceneRect(toast).intersects(selector), qPrintable(describe()));
    QVERIFY2(sceneRect(toast).top() <= tileRow.top() && sceneRect(toast).bottom() >= tileRow.bottom(),
             qPrintable(describe()));
    // And opaque, or the tiles show through it.
    QTRY_COMPARE(toast->opacity(), 1.0);
    delete root;
}

// The picker was a fixed 250 px: a broad empty band around short names, and a
// long one cut all the same.
void TestQmlUi::theConfigPickerIsAsWideAsItsNames()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] { m_backend.setConfigs(saved); });
    m_backend.setConfigs({QStringLiteral("Home"), QStringLiteral("Work")});
    QObject *root = loadPage("pages/HomePage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 460));
    auto *label = root->findChild<QQuickItem *>(QStringLiteral("activeConfigLabel"));
    auto *picker = root->findChild<QQuickItem *>(QStringLiteral("configPicker"));
    QVERIFY(label && picker);
    const auto open = [&] {
        picker->setProperty("open", false);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(label));
        return picker->property("open").toBool();
    };
    QVERIFY(open());
    QVERIFY2(picker->width() <= 140.5, qPrintable(QStringLiteral("%1 px for two short names").arg(picker->width())));

    const QString longName = textOfWidth(QStringLiteral("Frankfurt premium"), 260, 14);
    QVERIFY(widestLine(longName, 14) + 30 < 400 - 16);
    m_backend.setConfigs({QStringLiteral("Home"), longName});
    QVERIFY(open());
    QQuickItem *row = nullptr;
    QTRY_VERIFY((row = textIn(picker, longName)) != nullptr);
    QVERIFY2(!row->property("truncated").toBool(),
             qPrintable(QStringLiteral("a %1 px name in a %2 px picker").arg(row->implicitWidth()).arg(picker->width())));
    QVERIFY(sceneRect(picker).left() >= 7.5 && sceneRect(picker).right() <= 400 - 7.5);
    delete root;
}

// The name above the logo was held to 260 px, which cut names the window had
// room for. Cut where it has to be, the ▾ keeps to the text.
void TestQmlUi::theActiveConfigNameUsesTheRoomItHas()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] {
        m_backend.setConfigs(saved);
        m_backend.selectConfig(0);
    });
    const QString name = textOfWidth(QStringLiteral("Frankfurt premium"), 275, 15, QFont::Medium);
    m_backend.setConfigs({name});
    m_backend.selectConfig(0);
    QObject *root = loadPage("pages/HomePage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 460);
    QVERIFY(page);
    auto *label = root->findChild<QQuickItem *>(QStringLiteral("activeConfigLabel"));
    auto *arrow = textIn(page, QStringLiteral("▾"));
    QVERIFY(label && arrow);
    QVERIFY2(!label->property("truncated").toBool(),
             qPrintable(QStringLiteral("a %1 px name given %2 px").arg(label->implicitWidth()).arg(label->width())));

    m_backend.setConfigs({textOfWidth(name, 500, 15, QFont::Medium)});
    m_backend.selectConfig(0);
    QVERIFY(label->property("truncated").toBool());
    const qreal textRight = sceneRect(label).left() + label->property("contentWidth").toReal();
    QVERIFY2(sceneRect(arrow).left() - textRight <= 7,
             qPrintable(QStringLiteral("the ▾ %1 px after the text").arg(sceneRect(arrow).left() - textRight)));
    // Room is left for the + beside it, and the page's margin. Once laid out: the
    // column centres the selector in its next polish.
    QTRY_VERIFY2(sceneRect(arrow).right() <= 400 - 18 - 6 - 22 + 0.5,
             qPrintable(QStringLiteral("the ▾ ends at %1").arg(sceneRect(arrow).right())));
    delete root;
}

// Connected, the row also holds the ping, the badge and three icons, and with a
// 30 px cell and the row's gap for each icon the name was left about 60 px at
// the default width: "Home server" was cut.
void TestQmlUi::theConnectedConfigKeepsRoomForItsName()
{
    const QString name = QStringLiteral("Home server");
    if (widestLine(name, 14, QFont::Medium) > 95)
        QSKIP("this font draws the name wider than the proportional fonts the row is laid out for");
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] {
        m_backend.setConnected(false);
        m_backend.setConfigs(saved);
        m_backend.selectConfig(0);
    });
    m_backend.setConfigs({name, QStringLiteral("Backup")});
    m_backend.selectConfig(0);
    m_backend.setConnected(true);
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 460);
    QVERIFY(page);
    QQuickItem *text = nullptr;
    QTRY_VERIFY((text = textIn(page, name)) != nullptr);
    QVERIFY(textIn(page, QStringLiteral("connected")));
    QVERIFY2(!text->property("truncated").toBool(),
             qPrintable(QStringLiteral("%1 px for a %2 px name").arg(text->width()).arg(text->implicitWidth())));
    delete root;
}

// The add menu was a fixed 240 px around three short labels.
void TestQmlUi::theImportMenuIsAsWideAsItsItems()
{
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 460);
    QVERIFY(page);
    auto *menu = root->findChild<QQuickItem *>(QStringLiteral("importMenu"));
    QVERIFY(menu);
    menu->setProperty("open", true);
    QTRY_VERIFY(menu->isVisible());
    qreal widest = 0;
    for (const QString &label : {QStringLiteral("Paste from clipboard"), QStringLiteral("From file…"),
                                 QStringLiteral("Create new…")}) {
        QQuickItem *text = textIn(menu, label);
        QVERIFY2(text, qPrintable(label));
        widest = std::max(widest, text->implicitWidth());
        QVERIFY2(!text->property("truncated").toBool() && sceneRect(text).right() <= sceneRect(menu).right(),
                 qPrintable(label));
    }
    QVERIFY2(menu->width() <= std::max<qreal>(140, std::ceil(widest) + 40) + 0.5,
             qPrintable(QStringLiteral("a %1 px menu for %2 px labels").arg(menu->width()).arg(widest)));

    // Longer than the page can hold: cut, and inside the menu.
    const QString suffix = QStringLiteral(" in a much longer language, too long for any window to hold");
    LongerWords longer({"Paste from clipboard"}, suffix);
    QCoreApplication::installTranslator(&longer);
    m_engine.retranslate();
    const auto untranslate = qScopeGuard([this, &longer] {
        QCoreApplication::removeTranslator(&longer);
        m_engine.retranslate();
    });
    QQuickItem *paste = textIn(menu, QStringLiteral("Paste from clipboard") + suffix);
    QVERIFY(paste);
    QVERIFY(paste->property("truncated").toBool());
    QVERIFY(sceneRect(paste).right() <= sceneRect(menu).right() + 0.5);
    QVERIFY(sceneRect(menu).left() >= -0.5 && sceneRect(menu).right() <= 400.5);
    delete root;
}

// The window's select popup is held to the window, and an option longer than
// that ran off its edge, under the check mark, with nothing to say it went on.
void TestQmlUi::theSelectPopupElidesWhatTheWindowCannotHold()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *popup = qobject_cast<QQuickItem *>(evaluateIn(root, QStringLiteral("selectPopup")).value<QObject *>());
    QVERIFY(popup);

    const QString longOption = textOfWidth(QStringLiteral("Profile"), 600, 14);
    evaluateIn(root, QStringLiteral("showSelect(pageLoader, [{v: 'a', t: '%1'}, {v: 'b', t: 'Short'}], 'a', null)")
                             .arg(longOption));
    QTRY_VERIFY(popup->isVisible());
    QQuickItem *option = nullptr;
    QTRY_VERIFY((option = textIn(popup, longOption)) != nullptr);
    QQuickItem *check = textIn(popup, QStringLiteral("✓"));
    QVERIFY(check);
    QVERIFY(option->property("truncated").toBool());
    QVERIFY2(option->mapToScene(QPointF(option->property("contentWidth").toReal(), 0)).x()
                     <= sceneRect(check).left() + 0.5,
             "the option runs under the check mark");

    // What the window can hold is not cut.
    const QString option2 = QStringLiteral("Everything else");
    evaluateIn(root, QStringLiteral("showSelect(pageLoader, [{v: 'a', t: 'Selective'}, {v: 'b', t: '%1'}], 'a', null)")
                             .arg(option2));
    QTRY_VERIFY((option = textIn(popup, option2)) != nullptr);
    QVERIFY(!option->property("truncated").toBool());
    delete root;
}

// Both are placed and sized when they open, and after a resize they hung away
// from the control they drop from.
void TestQmlUi::popupsCloseWhenTheWindowIsResized()
{
    {
        QObject *root = loadPage("pages/HomePage.qml");
        QVERIFY(root);
        QQuickWindow window;
        auto *page = showInWindow(root, window, 400, 460);
        QVERIFY(page);
        auto *label = root->findChild<QQuickItem *>(QStringLiteral("activeConfigLabel"));
        auto *picker = root->findChild<QQuickItem *>(QStringLiteral("configPicker"));
        QVERIFY(label && picker);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(label));
        QVERIFY(picker->property("open").toBool());
        page->setWidth(520);
        QVERIFY2(!picker->property("open").toBool(), "the config picker stayed open");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(label));
        QVERIFY(picker->property("open").toBool());
        page->setHeight(600);
        QVERIFY2(!picker->property("open").toBool(), "the config picker stayed open");
        delete root;
    }
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    evaluateIn(root, QStringLiteral("showSelect(pageLoader, [{v: 'a', t: 'A'}], 'a', null)"));
    QVERIFY(evaluateIn(root, QStringLiteral("selectPopup.open")).toBool());
    window->resize(520, 600);
    QTRY_VERIFY2(!evaluateIn(root, QStringLiteral("selectPopup.open")).toBool(), "the select popup stayed open");
    delete root;
}

// Save and Cancel were a fixed 88 px, and «Сохранить» all but touched the edges.
void TestQmlUi::theEditorsButtonsFitTheirLabels()
{
    LongerWords longer({"Save", "Cancel"}, QStringLiteral(" and close"));
    QCoreApplication::installTranslator(&longer);
    m_engine.retranslate();
    const auto untranslate = qScopeGuard([this, &longer] {
        QCoreApplication::removeTranslator(&longer);
        m_engine.retranslate();
    });
    QObject *root = loadPage("CreateConfigOverlay.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 700);
    QVERIFY(page);
    for (const QString &label : {QStringLiteral("Save and close"), QStringLiteral("Cancel and close")}) {
        QQuickItem *text = textIn(page, label);
        QVERIFY2(text, qPrintable(label));
        QVERIFY2(text->parentItem()->width() >= text->implicitWidth() + 24,
                 qPrintable(QStringLiteral("«%1», %2 px, on a %3 px button")
                                    .arg(label).arg(text->implicitWidth()).arg(text->parentItem()->width())));
    }
    delete root;
}

// A link as wide as its row took a click anywhere along the row, far from its
// words.
void TestQmlUi::linksActOnlyOverTheirWords()
{
    {
        m_backend.logPathOverride = QStringLiteral("/tmp/freetunnel.log");
        const auto restore = qScopeGuard([this] { m_backend.logPathOverride.clear(); });
        QObject *root = loadPage("pages/LogsPage.qml");
        QVERIFY(root);
        QQuickWindow window;
        auto *page = showInWindow(root, window, 400, 460);
        QVERIFY(page);
        auto *link = root->findChild<QQuickItem *>(QStringLiteral("logPathLink"));
        QVERIFY(link);
        QVERIFY2(link->width() <= std::ceil(link->parentItem()->implicitWidth()) + 0.5,
                 qPrintable(QStringLiteral("a %1 px link for a %2 px path")
                                    .arg(link->width()).arg(link->parentItem()->implicitWidth())));
        // Auto-scroll keeps to the right, where it was.
        QQuickItem *autoScroll = textIn(page, QStringLiteral("Auto-scroll"));
        QVERIFY(autoScroll);
        QVERIFY(sceneRect(autoScroll).left() > 200);
        delete root;
    }
    QObject *root = loadPage("AppPickerOverlay.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 700));
    auto *link = root->findChild<QQuickItem *>(QStringLiteral("chooseFileLink"));
    QVERIFY(link);
    QVERIFY2(link->width() <= std::ceil(link->implicitWidth()) + 0.5,
             qPrintable(QStringLiteral("a %1 px link for %2 px of words").arg(link->width()).arg(link->implicitWidth())));
    delete root;
}

// Chips cut their names at a fixed 190 px, 130 for a profile, with the row to
// spare; they are the only place the name is shown.
void TestQmlUi::chipsAreCutOnlyWhereTheRowEnds()
{
    const QStringList routes = m_backend.excludedRoutes();
    const QStringList profiles = m_backend.profiles();
    const QStringList rules = m_backend.appRules();
    const QStringList labels = m_backend.appRuleLabels();
    const auto restore = qScopeGuard([&] {
        m_backend.setDomains({});
        m_backend.setExcludedRoutes(routes);
        m_backend.setProfiles(profiles);
        m_backend.setAppRules(rules, labels);
    });
    const QString domain = textOfWidth(QStringLiteral("downloads.example."), 230, 13);
    const QString profile = textOfWidth(QStringLiteral("Streaming "), 170, 13);
    const QString app = textOfWidth(QStringLiteral("Firefox "), 230, 13);
    const QString route = textOfWidth(QStringLiteral("2001:db8:"), 230, 13);
    m_backend.setDomains({domain});
    m_backend.setProfiles({QStringLiteral("Default"), profile});
    m_backend.setAppRules({QStringLiteral("/usr/bin/app")}, {app});
    m_backend.setExcludedRoutes({route});

    const auto check = [](QQuickItem *page, const QString &name) {
        QQuickItem *text = textIn(page, name);
        if (!text)
            return QStringLiteral("«%1» is not on the page").arg(name);
        if (text->property("truncated").toBool())
            return QStringLiteral("«%1», %2 px, cut to %3 px").arg(name).arg(text->implicitWidth()).arg(text->width());
        return QString();
    };
    {
        QObject *root = loadPage("pages/SplitPage.qml");
        QVERIFY(root);
        QQuickWindow window;
        auto *page = showInWindow(root, window, 400, 1400);
        QVERIFY(page);
        for (const QString &name : {domain, profile, app}) {
            const QString problem = check(page, name);
            QVERIFY2(problem.isEmpty(), qPrintable(problem));
        }
        // Longer than the row: cut, and the chip inside the page.
        const QString longer = textOfWidth(domain, 600, 13);
        m_backend.setDomains({longer});
        QQuickItem *text = textIn(page, longer);
        QVERIFY(text);
        QVERIFY(text->property("truncated").toBool());
        QVERIFY(sceneRect(text->parentItem()).right() <= 400.5);
        delete root;
    }
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 1400);
    QVERIFY(page);
    const QString problem = check(page, route);
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
    delete root;
}

// A link arriving while "Discard unsaved changes?" is up puts the import prompt
// on top of it. The hidden prompt kept Return and Escape, so Return meant for the
// import threw the edits away.
void TestQmlUi::theWindowConfirmOwnsTheKeysOverTheEditorsPrompt()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    root->setProperty("overlay", QStringLiteral("create"));
    QObject *discard = nullptr;
    QTRY_VERIFY((discard = root->findChild<QObject *>(QStringLiteral("discardConfirm"))) != nullptr);
    QObject *prompt = root->findChild<QObject *>(QStringLiteral("windowConfirm"));
    QVERIFY(prompt);
    QMetaObject::invokeMethod(discard, "open");
    QMetaObject::invokeMethod(root, "showConfirm", Q_ARG(QVariant, QStringLiteral("Import it?")),
                              Q_ARG(QVariant, QStringLiteral("Import")), Q_ARG(QVariant, QVariant()));
    QVERIFY(prompt->property("visible").toBool());
    QTRY_VERIFY(prompt->property("armed").toBool() && discard->property("armed").toBool());
    QSignalSpy discarded(discard, SIGNAL(confirmed()));

    QTest::keyClick(window, Qt::Key_Escape);
    QVERIFY2(!prompt->property("visible").toBool(), "Escape answers the prompt on top");
    QVERIFY(discard->property("visible").toBool());
    QMetaObject::invokeMethod(root, "showConfirm", Q_ARG(QVariant, QStringLiteral("Import it?")),
                              Q_ARG(QVariant, QStringLiteral("Import")), Q_ARG(QVariant, QVariant()));
    QTRY_VERIFY(prompt->property("armed").toBool());
    QTest::keyClick(window, Qt::Key_Return);
    QVERIFY(!prompt->property("visible").toBool());
    QCOMPARE(discarded.count(), 0); // the edits are still there to decide on
    // A second Return right after, as from a double press or a held key, does not
    // answer the question that is underneath: it has only just got the keys back.
    QTest::keyClick(window, Qt::Key_Return);
    QCOMPARE(discarded.count(), 0);
    QTRY_VERIFY(discard->property("armed").toBool());

    // Nor does Tab leave it for the form behind.
    auto *name = root->findChild<QObject *>(QStringLiteral("nameField"))->property("input").value<QQuickItem *>();
    QVERIFY(name);
    QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY2(!name->hasActiveFocus(), "Tab went into the form behind the question");
    QVERIFY(qobject_cast<QQuickItem *>(discard)->hasActiveFocus());
    delete root;
}

// Where Qt draws the file dialog itself, the editor's window-wide Escape saw the
// dialog's Escape too, closed the editor and destroyed the dialog mid-key.
void TestQmlUi::escapeStandsDownForTheEditorsFileDialog()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    root->setProperty("overlay", QStringLiteral("create"));
    QObject *dialog = nullptr;
    QTRY_VERIFY((dialog = root->findChild<QObject *>(QStringLiteral("certificateDialog"))) != nullptr);
    QObject *overlay = root->findChild<QObject *>(QStringLiteral("createOverlay"));
    QVERIFY(overlay);
    QObject *escape = nullptr;
    const auto children = overlay->children();
    for (QObject *o : children) {
        if (o->inherits("QQuickShortcut")
            && o->property("sequences").toList().contains(QStringLiteral("Escape")))
            escape = o;
    }
    QVERIFY2(escape, "the editor's Escape shortcut");
    QVERIFY(escape->property("enabled").toBool());
    QMetaObject::invokeMethod(dialog, "open");
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY2(!escape->property("enabled").toBool(), "the file dialog's Escape is its own");
    QMetaObject::invokeMethod(dialog, "close");
    delete root;
}

// Tab did nothing in the editor: plain text inputs are not in the tab chain.
void TestQmlUi::tabMovesThroughTheEditorsFields()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    window->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(window));
    root->setProperty("overlay", QStringLiteral("create"));
    QObject *name = nullptr;
    QTRY_VERIFY((name = root->findChild<QObject *>(QStringLiteral("nameField"))) != nullptr);
    auto *nameInput = name->property("input").value<QQuickItem *>();
    auto *hostInput = root->findChild<QObject *>(QStringLiteral("hostField"))->property("input").value<QQuickItem *>();
    auto *cert = root->findChild<QQuickItem *>(QStringLiteral("certificateField"));
    QVERIFY(nameInput && hostInput && cert);

    nameInput->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY2(hostInput->hasActiveFocus(), "Tab moves from Name to Server host");
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ShiftModifier);
    QVERIFY(nameInput->hasActiveFocus());

    cert->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY2(!cert->hasActiveFocus(), "Tab leaves the certificate field too");
    QVERIFY(!cert->property("text").toString().contains(QLatin1Char('\t')));

    // And the field Tab reaches is brought into view: at the default size the
    // certificate sits below the fold, and typing went in unseen.
    auto *form = root->findChild<QQuickItem *>(QStringLiteral("editorForm"));
    QVERIFY(form);
    nameInput->forceActiveFocus();
    QCOMPARE(form->property("contentY").toReal(), 0.0);
    for (int i = 0; i < 20 && !cert->hasActiveFocus(); ++i)
        QTest::keyClick(window, Qt::Key_Tab);
    QVERIFY(cert->hasActiveFocus());
    const qreal top = cert->mapToItem(form, QPointF(0, 0)).y();
    QVERIFY2(top >= 0 && top < form->height(),
             qPrintable(QStringLiteral("the certificate field is at %1 in a %2 px view").arg(top).arg(form->height())));
    delete root;
}

// An error from Save showed at the bottom, on the editor's own Save and Cancel,
// and took the click meant for them.
void TestQmlUi::aToastStaysOffTheEditorsButtons()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = qobject_cast<QQuickWindow *>(root);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto *toast = root->findChild<QQuickItem *>(QStringLiteral("toast"));
    QVERIFY(toast);
    root->setProperty("overlay", QStringLiteral("create"));
    QMetaObject::invokeMethod(root, "showToast", Q_ARG(QVariant, QStringLiteral("Fill in host")));
    QVERIFY2(toast->y() + toast->height() < window->height() / 2.0, "over the editor, the toast is at the top");
    root->setProperty("overlay", QString());
    QVERIFY(toast->y() > window->height() / 2.0);
    delete root;
}

namespace {

// A text's line count and what it shows, as far as a test can see it.
QString shown(QObject *text) { return text ? text->property("text").toString() : QString(); }

// WCAG relative luminance, for a contrast check that means what it says.
double luminance(const QColor &c)
{
    const auto channel = [](double v) {
        return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

} // namespace

// The view keeps its text while a selection is held, so a live update cannot
// wipe what the user is copying. Clear honoured that too, and left the whole old
// log on screen with "Logs will appear after connecting" drawn over it.
void TestQmlUi::clearEmptiesTheLogEvenWithASelection()
{
    m_backend.appendLog(QStringLiteral("a line to select"));
    QObject *root = loadPage("pages/LogsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 460));
    auto *view = root->findChild<QQuickItem *>(QStringLiteral("logView"));
    auto *clear = root->findChild<QQuickItem *>(QStringLiteral("clearLogs"));
    QVERIFY(view && clear);
    QVERIFY(!shown(view).isEmpty());
    QMetaObject::invokeMethod(view, "selectAll");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(clear));
    QVERIFY2(shown(view).isEmpty(), "Clear left the old log on screen");
    delete root;
}

// And nothing retried once the selection went, so the view stayed stale for good.
void TestQmlUi::aHeldLogCatchesUpWhenTheSelectionGoes()
{
    m_backend.appendLog(QStringLiteral("before"));
    QObject *root = loadPage("pages/LogsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 460));
    auto *view = root->findChild<QQuickItem *>(QStringLiteral("logView"));
    QVERIFY(view);
    QMetaObject::invokeMethod(view, "selectAll");
    m_backend.appendLog(QStringLiteral("arrived while selected"));
    QTest::qWait(400); // past the refresh interval
    QVERIFY2(!shown(view).contains(QStringLiteral("arrived while selected")),
             "the selection holds the view");
    QMetaObject::invokeMethod(view, "deselect");
    QTRY_VERIFY(shown(view).contains(QStringLiteral("arrived while selected")));
    m_backend.clearLogs();
    delete root;
}

// Logging off writes nothing new, and the page promised lines that never came.
void TestQmlUi::theLogsPageSaysWhenLoggingIsOff()
{
    m_backend.clearLogs();
    m_backend.setLoggingEnabled(false);
    const auto restore = qScopeGuard([this] { m_backend.setLoggingEnabled(true); });
    QObject *root = loadPage("pages/LogsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 460));
    auto *notice = root->findChild<QQuickItem *>(QStringLiteral("loggingOffNotice"));
    auto *placeholder = root->findChild<QQuickItem *>(QStringLiteral("logsPlaceholder"));
    auto *turnOn = root->findChild<QQuickItem *>(QStringLiteral("turnLoggingOn"));
    QVERIFY(notice && placeholder && turnOn);
    QVERIFY(notice->isVisible());
    QVERIFY2(!placeholder->isVisible(), "no promise of logs that will not come");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(turnOn));
    QVERIFY(m_backend.loggingEnabled());
    QVERIFY(!notice->isVisible());
    delete root;
}

// Clicking the status line always started a new check: over "Version X is
// available" it downloaded nothing, and mid-download it started a check that
// then offered the same download again.
void TestQmlUi::theUpdateLineDoesWhatItOffers()
{
    const auto restore = qScopeGuard([this] { m_backend.setUpdate(QString(), QString()); });
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 1400));
    auto *line = root->findChild<QQuickItem *>(QStringLiteral("updateStatus"));
    QVERIFY(line);
    // On the words: the line spans the row so that it can wrap, and only the
    // words act.
    const auto click = [&] {
        const QPointF words(line->property("contentWidth").toReal() / 2, line->height() / 2);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, line->mapToScene(words).toPoint());
    };

    // Beside the words, nothing: the line spans the row, and a click far to the
    // right of a short "Check for updates" acted on it.
    QVERIFY(line->property("contentWidth").toReal() < line->width() - 8);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      line->mapToScene(QPointF(line->width() - 4, line->height() / 2)).toPoint());
    QCOMPARE(m_backend.updateChecks, 0);

    click();
    QCOMPARE(m_backend.updateChecks, 1);
    m_backend.setUpdate(QStringLiteral("available"), QStringLiteral("Version 9.9.9 is available"));
    click();
    QCOMPARE(m_backend.updateChecks, 1);
    QCOMPARE(m_backend.updateOffersTaken, 1);
    m_backend.setUpdate(QStringLiteral("downloading"), QStringLiteral("Downloading… 45%"));
    click();
    QCOMPARE(m_backend.updateChecks, 1);
    QCOMPARE(m_backend.updateOffersTaken, 1);
    // Downloaded: the installer or its folder has been opened, and neither the
    // line nor an arrow beside it has anything left to offer. The arrow was there,
    // and both opened the release web page.
    m_backend.setUpdate(QStringLiteral("ready"),
                        QStringLiteral("Update downloaded — install it from the disk image that opened"));
    click();
    QCOMPARE(m_backend.updateChecks, 1);
    QCOMPARE(m_backend.updateOffersTaken, 1);
    QVERIFY(!root->findChild<QQuickItem *>(QStringLiteral("updateIcon"))->isVisible());

    // Long reasons wrap rather than lose their end.
    m_backend.setUpdate(QStringLiteral("error"),
                        QStringLiteral("Update downloaded. Finish installing it from the file manager."));
    QTRY_VERIFY(line->property("lineCount").toInt() > 1);
    QVERIFY(!line->property("truncated").toBool());
    delete root;
}

// Restore defaults replaces the whole list with no undo, a few pixels from a
// Clear all that asks first. It asks too now.
void TestQmlUi::restoringDefaultRoutesAsksFirst()
{
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 1400));
    auto *restore = root->findChild<QQuickItem *>(QStringLiteral("restoreRoutes"));
    QVERIFY(restore);
    m_shell.lastConfirm.clear();
    const int before = m_backend.routeRestores;
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(restore));
    QCOMPARE(m_backend.routeRestores, before);
    QVERIFY2(!m_shell.lastConfirm.isEmpty(), "Restore defaults asks before replacing the list");
    delete root;
}

// What leaks is the active config's profile, which need not be the one on the
// page: "add a rule" under a profile that already had rules sent the user adding
// rules that could not change anything.
void TestQmlUi::theThroughVpnNoticeNamesTheConfigAndItsProfile()
{
    m_backend.setSplitEnabled(true);
    m_backend.setVpnMode(QStringLiteral("selective"));
    const QStringList domains = m_backend.domains();
    m_backend.setDomains({});
    const auto restore = qScopeGuard([this, domains] {
        m_backend.setVpnMode(QStringLiteral("general"));
        m_backend.setDomains(domains);
    });
    QVERIFY(m_backend.selectiveModeWouldLeak());
    QObject *root = loadPage("pages/SplitPage.qml");
    QVERIFY(root);
    QObject *notice = root->findChild<QObject *>(QStringLiteral("throughVpnNotice"));
    QVERIFY(notice);
    QVERIFY2(shown(notice).contains(QStringLiteral("Test Config")), qPrintable(shown(notice)));
    QVERIFY2(shown(notice).contains(QStringLiteral("Default")), qPrintable(shown(notice)));

    // With no config at all there is none to name. The backend then calls the
    // active one "No config", which the notice used to name as a config.
    const QStringList saved = m_backend.configs();
    m_backend.setConfigs({});
    QVERIFY2(shown(notice).startsWith(QStringLiteral("Add a rule")), qPrintable(shown(notice)));
    m_backend.setConfigs(saved);
    delete root;
}

// The built-in profile is stored under the key "Default", and was shown as that
// key in the Russian UI.
void TestQmlUi::theBuiltInProfileIsShownInTheUsersLanguage()
{
    QTranslator russian;
    QVERIFY(russian.load(QStringLiteral(":/i18n/freetunnel_ru.qm")));
    QCoreApplication::installTranslator(&russian);
    const auto restore = qScopeGuard([this, &russian] {
        QCoreApplication::removeTranslator(&russian);
        m_engine.retranslate();
    });
    m_engine.retranslate();
    QObject *root = loadPage("pages/SplitPage.qml");
    QVERIFY(root);
    const QStringList texts = everyText(root);
    QVERIFY2(texts.contains(QStringLiteral("По умолчанию")), qPrintable(texts.join(QLatin1String(" | "))));
    QVERIFY(!texts.contains(QStringLiteral("Default")));
    delete root;
}

// White on the dark theme's light-grey accent read at about 2:1, and the
// editor's Save looked disabled next to Cancel.
void TestQmlUi::textOnTheAccentIsReadableInTheDarkTheme()
{
    QObject *root = loadPage("CreateConfigOverlay.qml");
    QVERIFY(root);
    QObject *save = root->findChild<QObject *>(QStringLiteral("saveLabel"));
    QVERIFY(save);
    const QColor label = save->property("color").value<QColor>();
    const QColor fill = m_theme.property("accent").value<QColor>();
    QVERIFY2(contrast(label, fill) >= 4.5,
             qPrintable(QStringLiteral("contrast %1:1").arg(contrast(label, fill), 0, 'f', 1)));
    delete root;
}

void TestQmlUi::headingLinksStayOnANarrowPage_data()
{
    QTest::addColumn<QString>("page");
    QTest::newRow("Settings") << QStringLiteral("pages/SettingsPage.qml");
    QTest::newRow("Split") << QStringLiteral("pages/SplitPage.qml");
}

// A link beside a section heading that did not fill kept its full width however
// little room there was, and ran off the page: with the fonts the Windows tests
// get, «Restore defaults» sat past the right edge of a 400 px window.
void TestQmlUi::headingLinksStayOnANarrowPage()
{
    QFETCH(QString, page);
    LongerWords longLinks({"Restore defaults", "Clear all", "Recommended for Russia", "Choose…"},
                          QStringLiteral(" in a much longer language, too long for any window to hold"));
    QCoreApplication::installTranslator(&longLinks);
    m_engine.retranslate();
    const auto restore = qScopeGuard([this, &longLinks] {
        QCoreApplication::removeTranslator(&longLinks);
        m_engine.retranslate();
    });
    m_backend.setDomains({QStringLiteral("example.com")}); // so both rules links show
    const auto domains = qScopeGuard([this] { m_backend.setDomains({}); });

    QObject *root = loadPage(page.toUtf8().constData());
    QVERIFY(root);
    QQuickWindow window;
    auto *item = showInWindow(root, window, 400, 1400);
    QVERIFY(item);
    QStringList out;
    int links = 0;
    const auto texts = root->findChildren<QQuickItem *>();
    for (QQuickItem *text : texts) {
        if (!text->inherits("QQuickText") || !text->isVisible()
            || !text->property("text").toString().endsWith(QLatin1String("any window to hold")))
            continue;
        ++links;
        const qreal right = text->mapToItem(item, QPointF(text->width(), 0)).x();
        if (right > item->width() + 0.5)
            out << QStringLiteral("%1 (to %2)").arg(text->property("text").toString()).arg(right);
    }
    QVERIFY2(links >= 2, "the links were not found");
    QVERIFY2(out.isEmpty(), qPrintable(QStringLiteral("past the edge: ") + out.join(QStringLiteral(" | "))));
    delete root;
}

void TestQmlUi::russianFitsAtTheDefaultWidth_data()
{
    QTest::addColumn<QString>("page");
    QTest::newRow("Settings") << QStringLiteral("pages/SettingsPage.qml");
    QTest::newRow("Split") << QStringLiteral("pages/SplitPage.qml");
    QTest::newRow("Logs") << QStringLiteral("pages/LogsPage.qml");
    QTest::newRow("Configs") << QStringLiteral("pages/ConfigsPage.qml");
    QTest::newRow("config editor") << QStringLiteral("CreateConfigOverlay.qml");
}

// Russian runs longer than English, and at the default 400 px window labels,
// links and input hints were cut, among them the "then Enter" that is the only
// hint that Enter adds a rule. Measured with DejaVu Sans, the widest of the
// usual Linux fonts and the one this was found with.
void TestQmlUi::russianFitsAtTheDefaultWidth()
{
    QFETCH(QString, page);
    if (!QFontDatabase::families().contains(QStringLiteral("DejaVu Sans")))
        QSKIP("DejaVu Sans is not installed");
    const QFont savedFont = QGuiApplication::font();
    QTranslator russian;
    QVERIFY(russian.load(QStringLiteral(":/i18n/freetunnel_ru.qm")));
    QCoreApplication::installTranslator(&russian);
    QGuiApplication::setFont(QFont(QStringLiteral("DejaVu Sans")));
    m_engine.retranslate();
    const auto restore = qScopeGuard([this, &russian, savedFont] {
        QCoreApplication::removeTranslator(&russian);
        QGuiApplication::setFont(savedFont);
        m_engine.retranslate();
    });

    QObject *root = loadPage(page.toUtf8().constData());
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 1400));
    QStringList cut;
    const auto texts = root->findChildren<QQuickItem *>();
    for (QQuickItem *item : texts) {
        if (!item->inherits("QQuickText") || !item->isVisible())
            continue;
        // A path elided in the middle is shortened on purpose, whatever the language.
        if (item->property("elide").toInt() == Qt::ElideMiddle)
            continue;
        if (item->property("truncated").toBool())
            cut << item->property("text").toString();
    }
    QVERIFY2(cut.isEmpty(), qPrintable(QStringLiteral("cut: ") + cut.join(QStringLiteral(" | "))));
    delete root;
}

namespace {

// A property's values over a stretch of time, for telling a moving thing from a
// still one.
QList<qreal> sampled(const std::function<qreal()> &valueNow, int forMs, int everyMs = 40)
{
    QList<qreal> out;
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < forMs) {
        QTest::qWait(everyMs);
        out << valueNow();
    }
    return out;
}

bool strays(const QList<qreal> &values, qreal from, qreal by)
{
    return std::any_of(values.cbegin(), values.cend(), [from, by](qreal v) { return std::abs(v - from) >= by; });
}

// The visible item under `item` with this objectName: rows of a list each have
// one, and only the one on screen is the one that counts.
QQuickItem *visibleNamedIn(QQuickItem *item, const QString &objectName)
{
    const QList<QQuickItem *> all = itemsIn(item);
    for (QQuickItem *candidate : all) {
        if (candidate->objectName() == objectName && candidate->isVisible())
            return candidate;
    }
    return nullptr;
}

QQuickWindow *exposed(QObject *root)
{
    auto *window = qobject_cast<QQuickWindow *>(root);
    if (!window)
        return nullptr;
    window->show();
    return QTest::qWaitForWindowExposed(window) ? window : nullptr;
}

// Long enough for any hover animation in the app (120–140 ms) to have finished.
constexpr int kHoverSettles = 300;

} // namespace

// The update arrow is the offer. ↓ downloads, and under the pointer it bends round
// into ↻, the arrow of the work a click starts; with the pointer gone and no click
// it is ↓ again. ↻ on its own offers to try again, and leans the way it goes; ↗
// opens the page of a release with nothing for this platform. The arrow used to
// turn -30° under the pointer, which tipped ↓ onto its side and ↻ against itself.
void TestQmlUi::theUpdateArrowBendsUnderThePointerAndBack()
{
    const auto restore = qScopeGuard([this] {
        m_backend.updateErrorOpensPage = false;
        m_backend.setUpdate(QString(), QString());
    });
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 1400));
    auto *icon = root->findChild<QQuickItem *>(QStringLiteral("updateIcon"));
    auto *arrow = root->findChild<QQuickItem *>(QStringLiteral("updateArrow"));
    QVERIFY(icon && arrow);
    const QPoint away(10, 10);

    m_backend.setUpdate(QStringLiteral("available"), QStringLiteral("Version 9.9.9 is available"));
    QCOMPARE(icon->property("offer").toString(), QStringLiteral("download"));
    QTRY_COMPARE(arrow->property("bend").toReal(), 0.0);
    QTest::mouseMove(&window, centreOf(icon));
    QTRY_COMPARE(arrow->property("bend").toReal(), 1.0);
    QTest::mouseMove(&window, away);
    QTRY_COMPARE(arrow->property("bend").toReal(), 0.0);

    m_backend.setUpdate(QStringLiteral("error"), QStringLiteral("Download failed: timed out"));
    QCOMPARE(icon->property("offer").toString(), QStringLiteral("retry"));
    QTRY_COMPARE(arrow->property("bend").toReal(), 1.0);
    QCOMPARE(arrow->property("turn").toReal(), 0.0);
    QTest::mouseMove(&window, centreOf(icon));
    QTRY_VERIFY2(arrow->property("turn").toReal() > 10, "retry did not lean clockwise under the pointer");
    QTest::mouseMove(&window, away);

    m_backend.updateErrorOpensPage = true;
    m_backend.setUpdate(QStringLiteral("error"), QStringLiteral("No installer asset found for this platform"));
    QCOMPARE(icon->property("offer").toString(), QStringLiteral("page"));
    QVERIFY(!arrow->isVisible());
    QVERIFY(textIn(icon, QStringLiteral("↗")));
    delete root;
}

// The arrow's shape itself: straight down at rest, and at full bend most of a
// circle traced clockwise about the point it spins around, its head at the end.
void TestQmlUi::theUpdateArrowBendsFromDownIntoAClockwiseCircle()
{
    QObject *root = loadPage("components/UpdateArrow.qml");
    QVERIFY(root);
    struct Shape {
        QList<QPointF> shaft, head;
        QPointF pivot;
    };
    const auto shapeAt = [root](qreal bend) {
        root->setProperty("bend", bend);
        const QString json = evaluateIn(root, QStringLiteral(
                "JSON.stringify({ strokes: geometry.strokes.map(l => l.map(p => [p.x, p.y])),"
                "                 pivot: [geometry.pivot.x, geometry.pivot.y] })")).toString();
        const QJsonObject g = QJsonDocument::fromJson(json.toUtf8()).object();
        const auto points = [](const QJsonValue &line) {
            QList<QPointF> out;
            for (const QJsonValue &p : line.toArray())
                out << QPointF(p.toArray().at(0).toDouble(), p.toArray().at(1).toDouble());
            return out;
        };
        const QJsonArray strokes = g.value(QStringLiteral("strokes")).toArray();
        const QJsonArray pivot = g.value(QStringLiteral("pivot")).toArray();
        return Shape{points(strokes.at(0)), points(strokes.at(1)),
                     QPointF(pivot.at(0).toDouble(), pivot.at(1).toDouble())};
    };

    // ↓: one upright line going down, the head at its foot with both barbs above.
    Shape down = shapeAt(0);
    QVERIFY(down.shaft.size() > 2 && down.head.size() == 3);
    for (const QPointF &p : std::as_const(down.shaft))
        QVERIFY(qAbs(p.x() - down.shaft.first().x()) < 0.01);
    QVERIFY(down.shaft.last().y() > down.shaft.first().y());
    QCOMPARE(down.head.at(1), down.shaft.last());
    QVERIFY(down.head.at(0).y() < down.head.at(1).y() && down.head.at(2).y() < down.head.at(1).y());

    // ↻: the shaft on one circle about the pivot, winding clockwise on screen (y
    // grows downward, so a clockwise turn is a positive angle) through most of it.
    Shape round = shapeAt(1);
    const qreal radius = QLineF(round.pivot, round.shaft.first()).length();
    QVERIFY(radius > 3);
    qreal turned = 0;
    for (qsizetype i = 0; i + 1 < round.shaft.size(); ++i) {
        const QPointF a = round.shaft.at(i) - round.pivot;
        const QPointF b = round.shaft.at(i + 1) - round.pivot;
        QVERIFY(qAbs(QLineF(round.pivot, round.shaft.at(i)).length() - radius) < 0.01);
        turned += std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y());
    }
    QVERIFY2(turned > 4.5, qPrintable(QStringLiteral("turned %1 rad").arg(turned)));
    QCOMPARE(round.head.at(1), round.shaft.last());
    delete root;
}

// While the update comes down, or a check runs, the ↻ turns: a still "…" beside a
// percentage looked the same whether it moved or had stalled. The pointer leaving
// does not straighten it then, since the work is under way; and when the work
// ends it comes round to rest rather than stopping at an angle.
void TestQmlUi::theUpdateArrowTurnsWhileItWorks()
{
    const auto restore = qScopeGuard([this] { m_backend.setUpdate(QString(), QString()); });
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 1400));
    auto *icon = root->findChild<QQuickItem *>(QStringLiteral("updateIcon"));
    auto *arrow = root->findChild<QQuickItem *>(QStringLiteral("updateArrow"));
    QVERIFY(icon && arrow);
    const auto angle = [arrow] { return arrow->property("spinAngle").toReal(); };

    for (const QString &state : {QStringLiteral("downloading"), QStringLiteral("checking")}) {
        m_backend.setUpdate(QStringLiteral("available"), QStringLiteral("Version 9.9.9 is available"));
        QTest::mouseMove(&window, centreOf(icon));
        QTRY_COMPARE(arrow->property("bend").toReal(), 1.0);
        m_backend.setUpdate(state, QStringLiteral("Working… 42%"));
        QCOMPARE(icon->property("offer").toString(), QStringLiteral("busy"));
        QTest::mouseMove(&window, QPoint(10, 10));
        QVERIFY2(strays(sampled(angle, 900), 0.0, 30), qPrintable(state + QStringLiteral(": the arrow stood still")));
        QCOMPARE(arrow->property("bend").toReal(), 1.0);

        m_backend.setUpdate(QStringLiteral("error"), QStringLiteral("Download failed"));
        QTRY_COMPARE(angle(), 0.0);
    }
    delete root;
}

// The connecting pulse on the logo never moved: a Behavior on the property it
// shared with the press restarted on every frame of it and held the logo at size.
void TestQmlUi::theConnectingLogoPulses()
{
    const auto restore = qScopeGuard([this] { m_backend.setConnecting(false); });
    QObject *root = loadPage("pages/HomePage.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 460));
    auto *area = root->findChild<QQuickItem *>(QStringLiteral("connectionLogo"));
    QVERIFY(area);
    // The logo as drawn: whatever scales it, its box or the image itself.
    QQuickItem *box = area->parentItem();
    QQuickItem *image = nullptr;
    const QList<QQuickItem *> inBox = box->childItems();
    for (QQuickItem *child : inBox) {
        if (child->inherits("QQuickImage"))
            image = child;
    }
    QVERIFY(image);
    const auto drawn = [box, image] { return box->scale() * image->scale(); };

    m_backend.setConnecting(true);
    QVERIFY2(strays(sampled(drawn, 1000), 1.0, 0.01), "the logo held still while connecting");
    m_backend.setConnecting(false);
    QTRY_COMPARE(drawn(), 1.0);
    delete root;
}

// The logo's click area was the whole hero, wider than the logo and down over the
// session line: a click on the timer, or beside the logo, disconnected.
void TestQmlUi::onlyTheLogoConnects()
{
    const auto restore = qScopeGuard([this] { m_backend.setConnected(false); });
    m_backend.setConnected(true);
    QObject *root = loadPage("pages/HomePage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 460);
    QVERIFY(page);
    auto *logo = root->findChild<QQuickItem *>(QStringLiteral("connectionLogo"));
    QQuickItem *timer = nullptr;
    QTRY_VERIFY((timer = textIn(page, m_backend.sessionTime())) != nullptr);
    QVERIFY(logo);
    const int toggles = m_backend.toggleCount();
    const int doubleClick = QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50;

    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(timer));
    QCOMPARE(m_backend.toggleCount(), toggles);
    QTest::qWait(doubleClick);
    const QRectF drawn = sceneRect(logo);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      QPointF(drawn.left() - 20, drawn.center().y()).toPoint());
    QCOMPARE(m_backend.toggleCount(), toggles);
    QTest::qWait(doubleClick);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(logo));
    QCOMPARE(m_backend.toggleCount(), toggles + 1);
    delete root;
}

// With no configs the logo said "Select a config first", with none to select, and
// Home's + only switched pages: the add menu was a second click away on Configs.
void TestQmlUi::homeLeadsStraightToTheAddMenu()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] {
        m_backend.setConfigs(saved);
        m_shell.setCurrentPage(0);
        m_shell.openAddMenu = false;
    });
    {
        m_backend.setConfigs({});
        QObject *root = loadPage("pages/HomePage.qml");
        QVERIFY(root);
        QQuickWindow window;
        QVERIFY(showInWindow(root, window, 400, 460));
        const int toggles = m_backend.toggleCount();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                          centreOf(root->findChild<QQuickItem *>(QStringLiteral("connectionLogo"))));
        QCOMPARE(m_backend.toggleCount(), toggles);
        QCOMPARE(m_shell.currentPage(), 1);
        QVERIFY(m_shell.openAddMenu);
        // And the "Add a config" under it.
        m_shell.setCurrentPage(0);
        m_shell.openAddMenu = false;
        auto *label = root->findChild<QQuickItem *>(QStringLiteral("activeConfigLabel"));
        QVERIFY(label);
        QCOMPARE(label->property("text").toString(), QStringLiteral("Add a config"));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(label));
        QCOMPARE(m_shell.currentPage(), 1);
        QVERIFY(m_shell.openAddMenu);
        delete root;
    }
    m_shell.setCurrentPage(0);
    m_shell.openAddMenu = false;
    m_backend.setConfigs(saved);
    {
        QObject *root = loadPage("pages/HomePage.qml");
        QVERIFY(root);
        QQuickWindow window;
        QVERIFY(showInWindow(root, window, 400, 460));
        auto *plus = root->findChild<QQuickItem *>(QStringLiteral("addConfigButton"));
        QVERIFY(plus);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(plus));
        QCOMPARE(m_shell.currentPage(), 1);
        QVERIFY(m_shell.openAddMenu);
        delete root;
    }
    // Configs opens with the menu once, and clears the request.
    QObject *first = loadPage("pages/ConfigsPage.qml");
    QVERIFY(first);
    QVERIFY(first->findChild<QObject *>(QStringLiteral("importMenu"))->property("open").toBool());
    QVERIFY(!m_shell.openAddMenu);
    delete first;
    QObject *again = loadPage("pages/ConfigsPage.qml");
    QVERIFY(again);
    QVERIFY(!again->findChild<QObject *>(QStringLiteral("importMenu"))->property("open").toBool());
    delete again;
}

// A popup's click-away area and a dialog's dim let hover through: what they
// covered lit up under the pointer, though a click there only closed them.
void TestQmlUi::whatAPopupCoversDoesNotLightUp()
{
    {
        QObject *root = loadPage("pages/HomePage.qml");
        QVERIFY(root);
        QQuickWindow window;
        QVERIFY(showInWindow(root, window, 400, 460));
        auto *label = root->findChild<QQuickItem *>(QStringLiteral("activeConfigLabel"));
        auto *plus = root->findChild<QQuickItem *>(QStringLiteral("addConfigButton"));
        QVERIFY(label && plus);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centreOf(label));
        QVERIFY(root->findChild<QObject *>(QStringLiteral("configPicker"))->property("open").toBool());
        QTest::mouseMove(&window, centreOf(plus));
        QTest::qWait(50);
        QVERIFY2(!plus->property("containsMouse").toBool(), "Home's + lit up under the picker's backdrop");
        delete root;
    }
    {
        QObject *root = loadPage("pages/ConfigsPage.qml");
        QVERIFY(root);
        QQuickWindow window;
        QVERIFY(showInWindow(root, window, 400, 460));
        root->findChild<QObject *>(QStringLiteral("importMenu"))->setProperty("open", true);
        auto *ping = root->findChild<QQuickItem *>(QStringLiteral("pingButton"));
        QVERIFY(ping);
        QTest::mouseMove(&window, centreOf(ping));
        QTest::qWait(50);
        QVERIFY2(!ping->property("containsMouse").toBool(), "the ping button lit up under the add menu's backdrop");
        delete root;
    }
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = exposed(root);
    QVERIFY(window);
    auto *nav = root->findChild<QQuickItem *>(QStringLiteral("navRow"));
    QVERIFY(nav);
    QQuickItem *tile = nullptr;
    const QList<QQuickItem *> tiles = nav->childItems();
    for (QQuickItem *child : tiles) {
        if (child->property("index").toInt() == 2)
            tile = child;
    }
    QVERIFY(tile);
    const auto hoverTile = [&] {
        QTest::mouseMove(window, QPoint(window->width() / 2, window->height() - 10));
        QTest::qWait(kHoverSettles);
        QTest::mouseMove(window, centreOf(tile));
        QTest::qWait(kHoverSettles);
        return tile->scale();
    };
    evaluateIn(root, QStringLiteral("showConfirm('Delete this?', 'Delete', null)"));
    QCOMPARE(hoverTile(), 1.0);
    evaluateIn(root, QStringLiteral("winConfirm.visible = false"));
    evaluateIn(root, QStringLiteral("showSelect(pageLoader, [{v: 'a', t: 'A'}], 'a', null)"));
    QCOMPARE(hoverTile(), 1.0);
    evaluateIn(root, QStringLiteral("selectPopup.open = false"));
    // The config editor's dim, and the app picker's.
    for (const QString &overlay : {QStringLiteral("create"), QStringLiteral("apps")}) {
        root->setProperty("overlay", overlay);
        QCOMPARE(hoverTile(), 1.0);
        root->setProperty("overlay", QString());
    }
    delete root;
}

// The window's select popup vanished the moment it closed, while it fades in and
// the other popups fade out. While it fades, what is under it takes clicks again.
void TestQmlUi::theSelectPopupFadesOutAndLetsGo()
{
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = exposed(root);
    QVERIFY(window);
    auto *layer = root->findChild<QQuickItem *>(QStringLiteral("overlayLayer"));
    auto *nav = root->findChild<QQuickItem *>(QStringLiteral("navRow"));
    QVERIFY(layer && nav);
    QQuickItem *split = nullptr;
    const QList<QQuickItem *> tiles = nav->childItems();
    for (QQuickItem *child : tiles) {
        if (child->property("index").toInt() == 2)
            split = child;
    }
    QVERIFY(split);
    evaluateIn(root, QStringLiteral("showSelect(pageLoader, [{v: 'a', t: 'A'}], 'a', null)"));
    QTRY_VERIFY(evaluateIn(root, QStringLiteral("selectPopup.opacity")).toReal() > 0.99);

    evaluateIn(root, QStringLiteral("selectPopup.open = false"));
    QVERIFY2(layer->isVisible(), "the popup vanished instead of fading");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centreOf(split));
    QCOMPARE(root->property("currentPage").toInt(), 2);
    QTRY_VERIFY(!layer->isVisible());
    delete root;
}

// While connecting, the first tray item read "Connecting…" like a status line,
// and choosing it cancelled the connection; while disconnecting it did nothing.
void TestQmlUi::theTrayConnectItemSaysWhatItDoes()
{
    const auto restore = qScopeGuard([this] {
        m_backend.setConnecting(false);
        m_backend.setDisconnecting(false);
    });
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QObject *item = root->findChild<QObject *>(QStringLiteral("trayToggle"));
    QVERIFY(item);
    m_backend.setConnecting(true);
    QCOMPARE(item->property("text").toString(), QStringLiteral("Cancel connecting"));
    QVERIFY(item->property("enabled").toBool());
    m_backend.setConnecting(false);
    m_backend.setDisconnecting(true);
    QCOMPARE(item->property("text").toString(), QStringLiteral("Disconnecting…"));
    QVERIFY(!item->property("enabled").toBool());
    // The ticked config toggles too, and did nothing then but flick its tick.
    QObject *ticked = nullptr;
    const auto all = root->findChildren<QObject *>();
    for (QObject *o : all) {
        if (o->property("checkable").toBool() && o->property("checked").toBool())
            ticked = o;
    }
    QVERIFY(ticked);
    QVERIFY(!ticked->property("enabled").toBool());
    m_backend.setDisconnecting(false);
    QVERIFY(ticked->property("enabled").toBool());
    delete root;
}

// A change of language says the update line again, and each time it did, the
// window announced the update again.
void TestQmlUi::anUpdateIsAnnouncedOnce()
{
    const auto restore = qScopeGuard([this] { m_backend.setUpdate(QString(), QString()); });
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    QVERIFY(exposed(root));
    auto *toast = root->findChild<QQuickItem *>(QStringLiteral("toast"));
    QVERIFY(toast);
    m_backend.setUpdate(QStringLiteral("available"), QStringLiteral("Version 9.9.9 is available"));
    QTRY_VERIFY(toast->opacity() > 0.5);
    toast->setProperty("opacity", 0.0); // read, and gone
    QTest::qWait(kHoverSettles);
    m_backend.setUpdate(QStringLiteral("available"), QStringLiteral("Доступна версия 9.9.9"));
    QTest::qWait(kHoverSettles);
    QCOMPARE(toast->opacity(), 0.0);
    delete root;
}

// Connecting, or switching to another config, the list showed nothing about it:
// the "connected" badge went away until the tunnel was up.
void TestQmlUi::theConfigListSaysWhenItIsConnecting()
{
    const auto restore = qScopeGuard([this] {
        m_backend.setConnecting(false);
        m_backend.setConnected(false);
    });
    QObject *root = loadPage("pages/ConfigsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 460);
    QVERIFY(page);
    m_backend.setConnecting(true);
    QQuickItem *badge = nullptr;
    QTRY_VERIFY((badge = visibleNamedIn(page, QStringLiteral("connectionBadge"))) != nullptr);
    QVERIFY(textIn(badge, QStringLiteral("connecting…")));
    m_backend.setConnecting(false);
    m_backend.setConnected(true);
    QVERIFY(badge->isVisible());
    QVERIFY(textIn(badge, QStringLiteral("connected")));
    delete root;
}

// The theme's colour for text on the accent was called onAccent, which beside a
// property called accent QML takes for the handler of accent's change signal: it
// stayed black, and in the light theme the Save label and the chosen profile were
// black on dark grey. In the light theme, too, hover went from surface to border,
// three levels apart, and chips, hotkey fields and Cancel showed nothing.
void TestQmlUi::theWindowThemeWorksInBothModes()
{
    const QString saved = m_backend.themeMode();
    const auto restore = qScopeGuard([this, saved] { m_backend.setThemeMode(saved); });
    for (const QString &mode : {QStringLiteral("light"), QStringLiteral("dark")}) {
        m_backend.setThemeMode(mode);
        QObject *root = createMainWindow(m_engine);
        QVERIFY(root);
        const auto colour = [root](const char *name) {
            return evaluateIn(root, QStringLiteral("theme.") + QLatin1String(name)).value<QColor>();
        };
        const double onAccent = contrast(colour("accentText"), colour("accent"));
        QVERIFY2(onAccent >= 4.5, qPrintable(QStringLiteral("%1: text on the accent at %2:1").arg(mode).arg(onAccent, 0, 'f', 1)));
        const int hover = std::abs(qGray(colour("surfaceHover").rgb()) - qGray(colour("surface").rgb()));
        QVERIFY2(hover >= 8, qPrintable(QStringLiteral("%1: hover %2 levels from rest").arg(mode).arg(hover)));
        delete root;
    }
}

// The same trap anywhere else: no property may be named like a signal handler.
void TestQmlUi::noPropertyIsNamedLikeASignalHandler()
{
    static const QRegularExpression handlerLike(QStringLiteral("\\bproperty\\s+\\w+\\s+(on[A-Z]\\w*)"));
    QStringList found;
    QDirIterator it(QStringLiteral(":/"), {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString source = QString::fromUtf8(file.readAll());
        auto matches = handlerLike.globalMatch(source);
        while (matches.hasNext())
            found << path + QLatin1Char(' ') + matches.next().captured(1);
    }
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QStringLiteral(", "))));
}

// Capturing, the field's fill eased into a translucent colour, and a colour
// animation eases RGB and alpha apart: it blinked instead of fading.
void TestQmlUi::theHotkeyFieldFillStaysOpaque()
{
    QObject *root = loadPage("components/HotkeyField.qml");
    QVERIFY(root);
    auto *fill = root->findChild<QQuickItem *>(QStringLiteral("hotkeyFill"));
    QVERIFY(fill);
    root->setProperty("capturing", true);
    QTest::qWait(kHoverSettles);
    QCOMPARE(fill->property("color").value<QColor>().alpha(), 255);
    root->setProperty("capturing", false);
    delete root;
}

// Every text link underlines under the pointer; the two in the footer only
// changed colour.
void TestQmlUi::footerLinksUnderlineLikeTheOthers()
{
    QObject *root = loadPage("pages/SettingsPage.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 1400);
    QVERIFY(page);
    int checked = 0;
    for (const QString &label : {QStringLiteral("FreeTunnel ") + m_backend.appVersion(),
                                 QStringLiteral("TrustTunnel core ") + m_backend.coreVersion()}) {
        QQuickItem *link = textIn(page, label);
        QVERIFY2(link, qPrintable(label));
        // The footer is clipped where the page is too narrow for it, as with the
        // fonts the Windows tests get, and a link past the edge cannot be pointed at.
        if (sceneRect(link).right() > page->width())
            continue;
        QTest::mouseMove(&window, centreOf(link));
        QTRY_VERIFY2(link->property("font").value<QFont>().underline(), qPrintable(label));
        ++checked;
    }
    QVERIFY(checked > 0);
    delete root;
}

// The editor's ← answered only over the glyph itself: 3 px to its left it did
// nothing, where the app picker's identical arrow takes 6 px around.
void TestQmlUi::theEditorsBackArrowTakesANearMiss()
{
    m_shell.setOverlay(QStringLiteral("create"));
    const auto restore = qScopeGuard([this] { m_shell.setOverlay(QString()); });
    QObject *root = loadPage("CreateConfigOverlay.qml");
    QVERIFY(root);
    QQuickWindow window;
    QVERIFY(showInWindow(root, window, 400, 700));
    auto *back = root->findChild<QQuickItem *>(QStringLiteral("editorBack"));
    QVERIFY(back);
    QQuickItem *glyph = back->parentItem();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      glyph->mapToScene(QPointF(-4, glyph->height() / 2)).toPoint());
    QCOMPARE(m_shell.overlay(), QString());
    delete root;
}

// The first open of the picker scanned on the UI thread, and on Windows the
// window froze while every Start Menu shortcut was resolved.
void TestQmlUi::thePickerWaitsForTheScanWithoutFreezing()
{
    m_backend.installedAppsReady = false;
    const auto restore = qScopeGuard([this] {
        m_backend.installedAppsReady = true;
        emit m_backend.splitChanged();
    });
    QObject *root = loadPage("AppPickerOverlay.qml");
    QVERIFY(root);
    QQuickWindow window;
    auto *page = showInWindow(root, window, 400, 700);
    QVERIFY(page);
    QVERIFY(textIn(page, QStringLiteral("Looking for installed applications…")));
    evaluateIn(root, QStringLiteral("searchField.text = 'fire'"));

    m_backend.installedAppsReady = true;
    emit m_backend.splitChanged();
    QTRY_VERIFY(textIn(page, QStringLiteral("Firefox")));
    // What was typed while it waited is applied to the list that arrived.
    QVERIFY(!textIn(page, QStringLiteral("Some App")));
    QVERIFY(!textIn(page, QStringLiteral("Looking for installed applications…")));
    delete root;
}

namespace {

// A click at a moment of the test's choosing. QTest spaces its clicks so that two
// never make a double-click; a person's second click comes a moment after the
// first, and that is the case to test. On QTest's own clock, moved on past it.
void clickAt(QWindow *window, QPoint at, int timestamp)
{
    const QPointF global = window->mapToGlobal(QPointF(at));
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
            window, ulong(timestamp), QPointF(at), global, Qt::LeftButton, Qt::LeftButton,
            QEvent::MouseButtonPress);
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
            window, ulong(timestamp + 20), QPointF(at), global, Qt::NoButton, Qt::LeftButton,
            QEvent::MouseButtonRelease);
    QTest::lastMouseTimestamp = timestamp + 20 + QGuiApplication::styleHints()->mouseDoubleClickInterval() + 1;
}

} // namespace

// With no configs, the logo's first click opens the add menu, and the second of a
// double-click landed on it: it closed the menu at once, or ran the row under the
// pointer, which opened a file dialog or the editor, or imported the clipboard.
void TestQmlUi::aDoubleClickThatOpensTheAddMenuLeavesItOpen()
{
    const QStringList saved = m_backend.configs();
    const auto restore = qScopeGuard([this, saved] { m_backend.setConfigs(saved); });
    m_backend.setConfigs({});
    QObject *root = createMainWindow(m_engine);
    QVERIFY(root);
    auto *window = exposed(root);
    QVERIFY(window);
    auto *loader = root->findChild<QObject *>(QStringLiteral("pageLoader"));
    auto *logo = loader->property("item").value<QQuickItem *>()->findChild<QQuickItem *>(QStringLiteral("connectionLogo"));
    QVERIFY(logo);
    const QPoint at = centreOf(logo);
    const int start = QTest::lastMouseTimestamp + QGuiApplication::styleHints()->mouseDoubleClickInterval() + 1;

    clickAt(window, at, start);
    QTest::qWait(150); // the menu fades in under the pointer
    clickAt(window, at, start + 150);
    QTest::qWait(kHoverSettles);

    QCOMPARE(root->property("currentPage").toInt(), 1);
    auto *page = loader->property("item").value<QQuickItem *>();
    QVERIFY(page->findChild<QObject *>(QStringLiteral("importMenu"))->property("open").toBool());
    QCOMPARE(root->property("overlay").toString(), QString());
    QVERIFY(!page->findChild<QObject *>(QStringLiteral("configImportDialog"))->property("visible").toBool());
    delete root;
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
