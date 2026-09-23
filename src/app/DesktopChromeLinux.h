// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The Linux half of DesktopChrome, kept out of its public header so that nothing
// including it needs QtDBus or xcb: see DesktopChromeLinux.cpp.

class QWindow;

namespace freetunnel {

class DesktopChrome;

// Read the desktop's title-bar settings from the settings portal once, and keep
// following them. Silently does nothing where there is no portal.
void watchPortalSettings(DesktopChrome *desktop);

// Ask the X11 window manager for its window menu at the pointer. False on native
// Wayland, with no window, or when the window manager does not advertise support.
bool showX11WindowMenu(QWindow *window);

} // namespace freetunnel
