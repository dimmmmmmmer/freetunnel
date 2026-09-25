// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

class QWindow;

namespace freetunnel {

// Which of the three window buttons go on which side of the title bar.
struct ButtonLayout {
    QStringList left;
    QStringList right;
};

// Bring the window in front of the user: restored if minimised, shown, raised and
// given focus. For when the user has asked to see it — the tray menu, a second
// launch — which reaches the app as a D-Bus call or a socket message rather than
// as input to the window itself.
//
// On X11 that difference decides it. Qt asks the window manager to activate the
// window with the time of the last X event the app received, which is older than
// the click on the panel that led here; Mutter reads such a request as focus
// stealing, refuses it and only flags the window for attention — it stays
// minimised, which is where our own close button leaves it. So there it is asked
// with the current server time instead, as a request made on the user's behalf.
// A window minimised from maximised also comes back maximised, which show() —
// showNormal() in Qt 6 — would not do.
void bringWindowForward(QWindow *window);

// Minimise, keeping maximised. QWindow::showMinimized() replaces the window's
// states with Minimized, and on X11 Qt then asks the window manager to take the
// maximised state off first: the window visibly shrank to normal size before
// going, and came back at it.
void minimizeWindow(QWindow *window);

// Hide the window to the tray (the menu bar on macOS). A full-screen window
// leaves full screen first: ordered out while in it, AppKit keeps its Space, and
// the user was left on an empty black one.
void hideWindowToTray(QWindow *window);

// A GNOME/GTK decoration layout — "appmenu:minimize,close", "close:", ":" — read
// into the buttons this window draws. Anything else in it (appmenu, icon, menu,
// spacer) is not a button we have and is dropped, and a name given twice is kept
// once, where it first appeared.
ButtonLayout parseButtonLayout(const QString &layout);

// What the desktop the app is running on expects of a window that draws its own
// title bar, for the QML that draws it.
//
// The window is frameless on Windows and Linux, which means it gives up everything
// the system would have done for a title bar it drew itself: which buttons exist
// and on which side, what they look like, what a double-, middle- or right-click
// on the bar does. On Windows QWindowKit hands most of that back natively. On
// Linux there is no such library that does not also swallow the presses, so the
// desktop is simply asked, through the settings portal, and followed — including
// when the user changes a setting while the app is open.
//
// The same portal answers one more question Qt leaves open on Linux: whether the
// desktop is dark (colorScheme, below).
class DesktopChrome : public QObject {
    Q_OBJECT
    // "windows" | "pop" | "adwaita" | "native" (macOS: AppKit draws them).
    Q_PROPERTY(QString controlStyle READ controlStyle NOTIFY changed)
    Q_PROPERTY(QStringList controlsLeft READ controlsLeft NOTIFY changed)
    Q_PROPERTY(QStringList controlsRight READ controlsRight NOTIFY changed)
    // GNOME's GDesktopTitlebarAction names: "toggle-maximize", "minimize", "lower",
    // "menu", "none", and a few this window cannot do, which it treats as "none".
    Q_PROPERTY(QString doubleClickAction READ doubleClickAction NOTIFY changed)
    Q_PROPERTY(QString middleClickAction READ middleClickAction NOTIFY changed)
    Q_PROPERTY(QString rightClickAction READ rightClickAction NOTIFY changed)
    // The desktop's own light/dark preference (org.freedesktop.appearance's
    // color-scheme), for when Qt does not know it. Qt 6.8 outside Flatpak picks its
    // "gnome" theme on GNOME and Pop!_OS, and that theme starts out Unknown and
    // learns the scheme only from a later change of GTK theme name, never from
    // color-scheme itself. Unknown until the desktop says otherwise, and always on
    // Windows and macOS, where Qt asks the system directly.
    Q_PROPERTY(Qt::ColorScheme colorScheme READ colorScheme NOTIFY changed)

public:
    explicit DesktopChrome(QObject *parent = nullptr);

    const QString &controlStyle() const { return m_style; }
    const QStringList &controlsLeft() const { return m_layout.left; }
    const QStringList &controlsRight() const { return m_layout.right; }
    const QString &doubleClickAction() const { return m_doubleClick; }
    const QString &middleClickAction() const { return m_middleClick; }
    const QString &rightClickAction() const { return m_rightClick; }
    Qt::ColorScheme colorScheme() const { return m_colorScheme; }

    // Start following the desktop: read its settings now and keep them current.
    // Linux only (the settings portal); nothing anywhere else. Separate from the
    // constructor so that constructing one never depends on the desktop it runs on.
    //
    // The first read blocks, briefly and with a limit, as GTK's and libadwaita's own
    // does. Everything this reports shapes the first frame the window draws, and a
    // read that answers after it shows a light window turning dark, or three
    // buttons becoming two.
    void followDesktop();

    // Ask the window manager for its own window menu (Move, Resize, Always on Top,
    // Close...) at the pointer, as a right-click on a real title bar would. Linux on
    // X11 only, and only where the window manager says it understands the request;
    // false everywhere else, so the caller can decide what to do instead.
    Q_INVOKABLE bool showWindowMenu(QObject *window);

    // bringWindowForward(), minimizeWindow() and hideWindowToTray(), for the QML.
    Q_INVOKABLE void bringToFront(QObject *window);
    Q_INVOKABLE void minimize(QObject *window);
    Q_INVOKABLE void hideToTray(QObject *window);

    // For tests: apply a setting as if the portal had just reported it.
    void applySetting(const QString &ns, const QString &key, const QVariant &value);

signals:
    void changed();
    // Linux: a tray host registered after start, so a tray icon made before it
    // should be made again. See watchTrayHost().
    void trayHostAppeared();

private:
    // The org.gnome.desktop.wm.preferences keys; whether one changed anything.
    bool applyWindowManagerSetting(const QString &key, const QString &text);

    QString m_style;
    ButtonLayout m_layout;
    QString m_doubleClick = QStringLiteral("toggle-maximize");
    QString m_middleClick = QStringLiteral("none");
    QString m_rightClick = QStringLiteral("menu");
    Qt::ColorScheme m_colorScheme = Qt::ColorScheme::Unknown;
};

} // namespace freetunnel
