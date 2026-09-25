// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The Linux half of DesktopChrome, kept out of its public header so that nothing
// including it needs QtDBus or xcb: see DesktopChromeLinux.cpp.

class QDBusConnection;
class QWindow;

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

// Ask the X11 window manager for its window menu at the pointer. False on native
// Wayland, with no window, or when the window manager does not advertise support.
bool showX11WindowMenu(QWindow *window);

// Ask the X11 window manager to activate the window — restore, raise, focus — as
// a request made on the user's behalf, with the current server time. False on
// native Wayland, with no window, or when the window manager does not support
// _NET_ACTIVE_WINDOW.
bool activateX11Window(QWindow *window);

} // namespace freetunnel
