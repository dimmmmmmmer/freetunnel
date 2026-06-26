// cppcheck-suppress-file missingIncludeSystem
#pragma once

// macOS-only: make the window's title bar transparent and let the content view
// extend underneath it, so the app background flows behind the traffic-light
// buttons (a "unified" title bar). No-op on other platforms.
#ifdef __APPLE__
#include <functional>

void applyMacUnifiedTitlebar(unsigned long long nsViewPtr);

// Retarget the window's red close button (and ⌘W) so it runs `onClose` — hide to
// tray — instead of closing the window. The close button calls -performClose:,
// which AppKit routes through this action; app termination (⌘Q / the Quit menu)
// closes windows a different way and is unaffected. This is the only reliable way
// to tell "user pressed the red button" apart from "user chose Quit", since on
// macOS both deliver a spontaneous close event to the Qt window.
void installMacWindowCloseToTray(unsigned long long nsViewPtr, std::function<void()> onClose);
#endif
