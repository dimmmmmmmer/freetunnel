// cppcheck-suppress-file missingIncludeSystem
#pragma once

// macOS-only: make the window's title bar transparent and let the content view
// extend underneath it, so the app background flows behind the traffic-light
// buttons (a "unified" title bar). No-op on other platforms.
#ifdef __APPLE__
#include <functional>

void applyMacUnifiedTitlebar(unsigned long long nsViewPtr);

// Retarget the window's red close button so it runs `onClose` — hide to tray —
// instead of closing the window. The close button calls -performClose:, which
// AppKit routes through this action; app termination (⌘Q / the Quit menu) closes
// windows a different way and is unaffected. This is the only reliable way to
// tell "user pressed the red button" apart from "user chose Quit", since on macOS
// both deliver a spontaneous close event to the Qt window. ⌘W is a Shortcut in
// Main.qml that does the same: with no Window menu, nothing turns it into
// -performClose:.
void installMacWindowCloseToTray(unsigned long long nsViewPtr, std::function<void()> onClose);

// Run `onReopen` when the user clicks the app's Dock icon (the kAEReopenApplication
// Apple Event). Unlike a generic app-activation observer, this fires ONLY on a Dock
// click — NOT on status-bar (menu-bar) icon clicks or Cmd-Tab — so a window hidden
// to the menu bar is not spuriously re-shown every time the app happens to activate.
void installMacDockReopenHandler(std::function<void()> onReopen);

// Where the three window buttons actually are, in the window's own coordinates:
// top-left origin, points — the same units QML works in. Empty (all zero) when
// there is nothing to keep clear of: no buttons, or full screen, where AppKit
// moves them into a titlebar that slides over the content only on demand.
//
// Asked rather than assumed because the answer is not a constant. Their size,
// inset and the titlebar height are decided by the SDK the app is linked against
// as much as by the OS it runs on — an app built with an older SDK is drawn in the
// older style on a newer macOS — so any number written into the QML is right for
// one pairing and quietly wrong for the next.
struct MacRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
};
MacRect macWindowControlsRect(unsigned long long nsViewPtr);

// A press on the window's title band, which the QML draws itself because the
// content view covers the whole window. Moves the window, or — on the second
// click of a double-click — does what the user chose in System Settings for a
// title-bar double-click: zoom, minimise, or nothing. Returns false when it could
// do neither, so the caller can fall back to QWindow::startSystemMove().
bool macHandleTitlebarPress(unsigned long long nsViewPtr);

// Keep a click on the menu-bar icon from crashing the app on macOS 26 and later.
// See the definition. Idempotent; call once, before the event loop.
void installMacStatusItemCrashGuard();
#endif
