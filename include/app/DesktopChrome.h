// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>
#include <utility>

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
//
// On X11 that is still a request, and a window manager may answer it with a flag
// on the window and nothing else. Hyprland does by default (misc:focus_on_activate
// is off): a window that is mapped but not in focus, on another workspace or
// behind a full-screen one, stays where it is. So there the request is checked on,
// and mapped again if it came to nothing; see bringWindowForwardChecked().
void bringWindowForward(QWindow *window);

// Minimise, keeping maximised. QWindow::showMinimized() replaces the window's
// states with Minimized, and on X11 Qt then asks the window manager to take the
// maximised state off first: the window visibly shrank to normal size before
// going, and came back at it.
void minimizeWindow(QWindow *window);

// Whether the window manager has actually iconified the window: yes, no, or empty
// when that cannot be told (native Wayland, no server to ask).
using IconifiedProbe = std::function<std::optional<bool>(QWindow *)>;

// Whether a host is registered for the tray icons on the session bus: yes, no, or
// empty when that cannot be told.
using TrayHostProbe = std::function<std::optional<bool>()>;

// How long the window manager gets to act on a minimise before it is asked
// whether it did. A request that is honoured shows within a few milliseconds; this
// is the margin for a window manager that is busy, and the pause the user sees
// where it was not.
constexpr int kMinimizeCheckMs = 300;

// minimizeWindow(), and then, checkMs later, a look at whether the window manager
// did it. It may not have: asking an X11 window manager to minimise is a request,
// and Hyprland and sway, which have no minimise, drop it without a word. Qt cannot
// tell. It records Minimized whatever happens, and with that state stuck it sends
// nothing for the next minimise either, so the close button did nothing for good.
//
// So when the probe says no, the window is taken out of the Minimized state Qt
// holds it in, and done(false) says the window is still on screen. Otherwise
// done(true): the window manager did it, or cannot be asked, or the window was
// restored in the meantime and there is nothing to put right. Called once, unless
// the window is destroyed first.
void minimizeWindowChecked(QWindow *window, const IconifiedProbe &probe, int checkMs,
                           std::function<void(bool honoured)> done);

// Whether the window is the one the user is working in. Empty means
// QWindow::isActive().
using ActiveProbe = std::function<bool(QWindow *)>;

// Whether the window manager has withdrawn the window, which is what it does once
// the window is unmapped: yes, no, or empty when that cannot be told (native
// Wayland, no server to ask).
using WithdrawnProbe = std::function<std::optional<bool>(QWindow *)>;

// How long the window manager gets to act on a bring-forward before it is asked
// whether it did: the same margin as kMinimizeCheckMs. One that honours the request
// has the window in focus within a few milliseconds.
constexpr int kActivateCheckMs = 300;
// And how a window that was unmapped to be mapped again is waited for: ICCCM 4.1.4
// has the window manager withdraw it first, and a window mapped before that is
// taken for the old one. Asked every kWithdrawPollMs, for at most kWithdrawWaitMs.
constexpr int kWithdrawPollMs = 20;
constexpr int kWithdrawWaitMs = 500;

// bringWindowForward(), and then, checkMs later, a look at whether the window
// manager gave the window focus. It may not have: asking an X11 window manager to
// activate a window is a request, and Hyprland, whose default is to flag the window
// as wanting attention, and sway, whose XWayland default is the same, answer it
// that way for a window that is mapped and not in focus. Nothing visibly happens,
// and Qt cannot tell.
//
// What they do give focus to is a window that is newly mapped. So when the window
// is still on screen, was not active at any moment since the request, and is not
// active now, it is unmapped, waited for until the window manager has withdrawn it
// (the probe; at most waitMs, asked every pollMs), and brought forward once more.
// A window manager that would refuse that too is not asked again: one remap per
// request, never a loop.
//
// A window that was hidden is only brought forward: that already maps it, and a
// window manager that refuses focus to a new map would refuse it again. A window
// hidden in the meantime (the X) is left hidden, and a newer request makes the
// check of an older one moot.
//
// done(true) says the window was taken off the screen and put back; done(false)
// that it was not, for whatever reason. Called once per request, unless the window
// is destroyed first.
void bringWindowForwardChecked(QWindow *window, const ActiveProbe &active,
                               const WithdrawnProbe &withdrawn, int checkMs,
                               std::function<void(bool remapped)> done = {},
                               int pollMs = kWithdrawPollMs, int waitMs = kWithdrawWaitMs);

// Hide the window to the tray (the menu bar on macOS). On macOS a full-screen
// window leaves full screen first, and is hidden once AppKit says it has:
// ordered out while in it, AppKit keeps its Space, and the user was left on an
// empty black one.
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
    //
    // minimize() on Linux is minimizeWindowChecked(), and says what came of it
    // through minimizeSettled() a moment later; on Windows and macOS it is
    // minimizeWindow() and minimizeSettled(window, true) at once.
    Q_INVOKABLE void bringToFront(QObject *window);
    Q_INVOKABLE void minimize(QObject *window);
    Q_INVOKABLE void hideToTray(QObject *window);

    // Whether the tray icon has a tray to sit in: its `available`, which Qt works
    // out when the icon is made. A function of the icon, not a property of this, so
    // that the QML tests can say what a desktop with a tray would answer.
    Q_INVOKABLE bool hasTray(QObject *trayIcon) const;
    // Whether a window hidden for the tray can be got back from it, now: a tray, and
    // on Linux a host for it. Qt's `available` was true when the icon was made, and
    // says nothing of a bar that has left the bus since or that owns a
    // StatusNotifierWatcher and shows no icons. So the host is asked, each time; where
    // it cannot be (a timeout), Qt's word stands.
    Q_INVOKABLE bool trayCanHoldWindow(QObject *trayIcon) const;
    // Whether minimize() can be checked on this display server, which on Linux means
    // an X server (XWayland included). Where it cannot, a window manager that drops
    // the request cannot be told from one that honoured it, and a close that waits
    // on the answer would wait for ever.
    Q_INVOKABLE bool minimizeCanBeChecked() const;

    // For tests: apply a setting as if the portal had just reported it.
    void applySetting(const QString &ns, const QString &key, const QVariant &value);

    // For tests: who is asked whether a minimise was honoured, and whether a tray
    // host is there. On Linux they start as the real X server and the real session
    // bus; elsewhere there is nothing to ask (an empty probe), and a minimise is
    // taken as honoured, a tray as holding.
    void setIconifiedProbe(IconifiedProbe probe) { m_iconified = std::move(probe); }
    void setTrayHostProbe(TrayHostProbe probe) { m_trayHost = std::move(probe); }

signals:
    void changed();
    // Linux: a tray host registered after start, so a tray icon made before it
    // should be made again. See watchTrayHost().
    void trayHostAppeared();
    // What came of a minimize(): honoured, or the window manager did nothing and the
    // window is still on screen, no longer marked minimised. See
    // minimizeWindowChecked().
    void minimizeSettled(QObject *window, bool honoured);

private:
    // The org.gnome.desktop.wm.preferences keys; whether one changed anything.
    bool applyWindowManagerSetting(const QString &key, const QString &text);

    QString m_style;
    ButtonLayout m_layout;
    QString m_doubleClick = QStringLiteral("toggle-maximize");
    QString m_middleClick = QStringLiteral("none");
    QString m_rightClick = QStringLiteral("menu");
    Qt::ColorScheme m_colorScheme = Qt::ColorScheme::Unknown;
    IconifiedProbe m_iconified;
    TrayHostProbe m_trayHost;
};

} // namespace freetunnel
