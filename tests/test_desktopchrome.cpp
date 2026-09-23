// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <QSignalSpy>

#include "app/DesktopChrome.h"

using freetunnel::ButtonLayout;
using freetunnel::DesktopChrome;
using freetunnel::parseButtonLayout;

class TestDesktopChrome : public QObject {
    Q_OBJECT

private slots:
    void theLayoutsDesktopsActuallyShip_data();
    void theLayoutsDesktopsActuallyShip();
    void aLayoutWithNoColonIsAllLeftSide();
    void aButtonNamedTwiceIsKeptWhereItFirstAppears();
    void onlyAChangedSettingIsAnnounced();
    void theThemeDecidesBetweenPopAndLibadwaita();
    void eachPlatformStartsWithItsOwnLook();
};

void TestDesktopChrome::theLayoutsDesktopsActuallyShip_data()
{
    QTest::addColumn<QString>("layout");
    QTest::addColumn<QStringList>("left");
    QTest::addColumn<QStringList>("right");

    const QString min = QStringLiteral("minimize");
    const QString max = QStringLiteral("maximize");
    const QString close = QStringLiteral("close");
    // Read off a real Pop!_OS 22.04 — no maximise button, which our window drew
    // anyway and no other window on that desktop has.
    QTest::newRow("Pop!_OS") << QStringLiteral("appmenu:minimize,close")
                             << QStringList{} << QStringList{min, close};
    // Stock GNOME's schema default: close alone.
    QTest::newRow("GNOME default") << QStringLiteral("appmenu:close") << QStringList{}
                                   << QStringList{close};
    // What kde-gtk-config writes for default Plasma.
    QTest::newRow("KDE via kde-gtk-config") << QStringLiteral("icon:minimize,maximize,close")
                                            << QStringList{} << QStringList{min, max, close};
    // The macOS-like arrangement people set in Tweaks.
    QTest::newRow("buttons on the left") << QStringLiteral("close,minimize,maximize:")
                                         << QStringList{close, min, max} << QStringList{};
    QTest::newRow("no buttons") << QStringLiteral(":") << QStringList{} << QStringList{};
    // Things that are not our buttons, and spacing, are dropped rather than
    // misread as one.
    QTest::newRow("spacers and menus") << QStringLiteral("menu,spacer: minimize , appmenu,close ")
                                       << QStringList{} << QStringList{min, close};
}

void TestDesktopChrome::theLayoutsDesktopsActuallyShip()
{
    QFETCH(QString, layout);
    QFETCH(QStringList, left);
    QFETCH(QStringList, right);
    const ButtonLayout parsed = parseButtonLayout(layout);
    QCOMPARE(parsed.left, left);
    QCOMPARE(parsed.right, right);
}

// GTK's rule, not a guess: a layout with no colon is entirely the left side.
void TestDesktopChrome::aLayoutWithNoColonIsAllLeftSide()
{
    const ButtonLayout parsed = parseButtonLayout(QStringLiteral("close,maximize"));
    QCOMPARE(parsed.left, (QStringList{QStringLiteral("close"), QStringLiteral("maximize")}));
    QVERIFY(parsed.right.isEmpty());
}

// Two close buttons would be two close buttons on screen.
void TestDesktopChrome::aButtonNamedTwiceIsKeptWhereItFirstAppears()
{
    const ButtonLayout parsed = parseButtonLayout(QStringLiteral("close,close:close,minimize"));
    QCOMPARE(parsed.left, QStringList{QStringLiteral("close")});
    QCOMPARE(parsed.right, QStringList{QStringLiteral("minimize")});
}

// The portal re-sends settings, and a change announced for nothing re-lays out
// the title bar for nothing.
void TestDesktopChrome::onlyAChangedSettingIsAnnounced()
{
    DesktopChrome desktop;
    QSignalSpy changed(&desktop, &DesktopChrome::changed);
    const QString prefs = QStringLiteral("org.gnome.desktop.wm.preferences");

    desktop.applySetting(prefs, QStringLiteral("button-layout"), QStringLiteral("appmenu:minimize,close"));
    const int afterFirst = changed.count();
    QCOMPARE(desktop.controlsRight(), (QStringList{QStringLiteral("minimize"), QStringLiteral("close")}));

    desktop.applySetting(prefs, QStringLiteral("button-layout"), QStringLiteral("appmenu:minimize,close"));
    QCOMPARE(changed.count(), afterFirst);

    desktop.applySetting(prefs, QStringLiteral("action-double-click-titlebar"), QStringLiteral("minimize"));
    QCOMPARE(desktop.doubleClickAction(), QStringLiteral("minimize"));
    QCOMPARE(changed.count(), afterFirst + 1);

    // A key this does not use is ignored, and says nothing.
    desktop.applySetting(prefs, QStringLiteral("focus-mode"), QStringLiteral("sloppy"));
    QCOMPARE(changed.count(), afterFirst + 1);
}

void TestDesktopChrome::theThemeDecidesBetweenPopAndLibadwaita()
{
#ifndef Q_OS_LINUX
    QSKIP("the GTK theme only chooses the look on Linux; Windows and macOS have their own");
#else
    DesktopChrome desktop;
    const QString iface = QStringLiteral("org.gnome.desktop.interface");
    desktop.applySetting(iface, QStringLiteral("gtk-theme"), QStringLiteral("Pop-dark"));
    QCOMPARE(desktop.controlStyle(), QStringLiteral("pop"));
    desktop.applySetting(iface, QStringLiteral("gtk-theme"), QStringLiteral("Adwaita"));
    QCOMPARE(desktop.controlStyle(), QStringLiteral("adwaita"));
#endif
}

// Before any desktop has said anything — and, on Windows and macOS, for good: no
// setting ever reaches them. On Windows this default is the only thing that picks
// the Windows 11 caption buttons, and CI is the only place it is ever run there.
void TestDesktopChrome::eachPlatformStartsWithItsOwnLook()
{
    const DesktopChrome desktop;
    const QStringList all{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")};
#if defined(Q_OS_WIN)
    QCOMPARE(desktop.controlStyle(), QStringLiteral("windows"));
    QCOMPARE(desktop.controlsRight(), all);
#elif defined(Q_OS_MACOS)
    // AppKit draws the traffic lights; the window draws nothing of its own.
    QCOMPARE(desktop.controlStyle(), QStringLiteral("native"));
    QVERIFY(desktop.controlsRight().isEmpty());
#else
    // What the window looked like before it asked the desktop, so that where no
    // portal answers nothing changes.
    QCOMPARE(desktop.controlStyle(), QStringLiteral("adwaita"));
    QCOMPARE(desktop.controlsRight(), all);
#endif
    QVERIFY(desktop.controlsLeft().isEmpty());
    QCOMPARE(desktop.doubleClickAction(), QStringLiteral("toggle-maximize"));
}

QTEST_GUILESS_MAIN(TestDesktopChrome)
#include "test_desktopchrome.moc"
