// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QDirIterator>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>

#include "ui/MockBackend.h"
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
    void everyComponentLoadsOnItsOwn();
    void everyComponentLoadsOnItsOwn_data();
    void confirmDialogShowsTheThirdButtonOnlyWhenItHasOne();
    void confirmDialogAnswersReturnAndEscape();
    void aSecondConfirmQueuesInsteadOfReplacingTheLiveOne();

private:
    QObject *loadPage(const char *qmlPath);

    QQmlEngine m_engine;
    MockBackend m_backend;
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
    for (int page = 0; page < 5; ++page) {
        root->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        QCOMPARE(root->property("currentPage").toInt(), page);
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
