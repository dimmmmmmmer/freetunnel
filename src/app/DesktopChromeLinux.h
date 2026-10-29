// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The Linux half of DesktopChrome, kept out of its public header so that nothing
// including it needs QtDBus or xcb: see DesktopChromeLinux.cpp.

#include <cstdint>
#include <optional>

class QDBusConnection;
class QWindow;
// xcb's own name for it, declared here rather than included for the same reason.
struct xcb_connection_t;

namespace freetunnel {

class DesktopChrome;

// Read the desktop's settings from the settings portal once, and keep following
// them: the title-bar ones, and whether it prefers dark. Silently does nothing
// where there is no portal.
//
// The first read blocks for at most kPortalReadTimeoutMs. A portal that has not
// answered by then is asked again without waiting and without a time limit, so
// its answer still arrives, just after the first frame instead of before it —
// unless the portal never starts at all.
void watchPortalSettings(DesktopChrome *desktop);
// The same on a given bus, for tests: a private bus with a stand-in portal on it.
void watchPortalSettings(DesktopChrome *desktop, const QDBusConnection &bus);

constexpr int kPortalReadTimeoutMs = 1000;

// Report through DesktopChrome::trayHostAppeared() when a tray host (a
// StatusNotifierWatcher) registers on the bus after the app has started.
void watchTrayHost(DesktopChrome *desktop, const QDBusConnection &bus);

// Whether a tray host is on the session bus, and not only a StatusNotifierWatcher:
// the watcher's IsStatusNotifierHostRegistered. Qt's tray.available means the
// watcher was there when the icon was made, which is also true of a bar that owns
// the watcher and shows nothing, and of one that has left the bus since.
//
// No watcher on the bus is false: there is nothing to show an icon. Empty only
// when the question could not be put or was not answered (no bus, a timeout, a
// watcher without the property), so the caller can fall back on what Qt says.
// Blocks for at most kTrayHostTimeoutMs.
std::optional<bool> trayHostRegistered();
// The same on a given bus, for tests: a private bus with a stand-in watcher on it.
std::optional<bool> trayHostRegistered(const QDBusConnection &bus);

constexpr int kTrayHostTimeoutMs = 500;

// Ask the X11 window manager for its window menu at the pointer. False on native
// Wayland, with no window, or when the window manager does not advertise support.
bool showX11WindowMenu(QWindow *window);

// Ask the X11 window manager to activate the window — restore, raise, focus — as
// a request made on the user's behalf, with the current server time. False on
// native Wayland, with no window, or when the window manager does not support
// _NET_ACTIVE_WINDOW.
bool activateX11Window(QWindow *window);

// Whether this application is on Qt's xcb platform: talking to an X server, which
// includes XWayland. The one display server on which x11WindowIconified() can
// answer; not offscreen, even where that has a connection of its own.
bool runsOnX11();

// Whether the X11 window manager has iconified the window, which is what
// "minimised" means to the user: ICCCM's WM_STATE says IconicState, or EWMH's
// _NET_WM_STATE lists _NET_WM_STATE_HIDDEN. Asked rather than assumed, because
// asking a window manager to minimise is a request it may drop without a word:
// Hyprland and sway have no minimise and read it as nothing. Empty on native
// Wayland, offscreen, with no window, or when the server cannot be asked; the
// caller reads that as "leave it alone".
//
// Deliberately not "does the window manager list _NET_WM_STATE_HIDDEN among what it
// supports": Hyprland lists it and still never minimises.
std::optional<bool> x11WindowIconified(QWindow *window);
// The same for a window on a given connection, for tests: a private X server.
std::optional<bool> windowIconified(xcb_connection_t *connection, std::uint32_t window);

// Whether the X11 window manager has withdrawn the window, which is what it does
// once the window is unmapped: ICCCM's WM_STATE says WithdrawnState, or is gone
// (Hyprland removes it). A window is waited on for that before it is mapped again
// (ICCCM 4.1.4): mapped earlier, it is taken for the window that was already there
// and nothing new happens. Empty on native Wayland, offscreen, with no window, or
// when the server cannot be asked; the caller reads that as "do not wait".
std::optional<bool> x11WindowWithdrawn(QWindow *window);
// The same for a window on a given connection, for tests: a private X server.
std::optional<bool> windowWithdrawn(xcb_connection_t *connection, std::uint32_t window);

} // namespace freetunnel
