// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The macOS half of DesktopChrome, kept out of its public header so that nothing
// including it needs AppKit: see DesktopChromeMac.mm.

#include <functional>

class QWindow;

namespace freetunnel {

// Unhide the application and make it the active one, as a Dock click does.
void unhideMacApplication();

// Run @p then once AppKit reports that @p window has finished leaving full
// screen. Only for a window on the cocoa platform, which has an NSView behind it.
void whenMacFullScreenEnds(QWindow *window, std::function<void()> then);

} // namespace freetunnel
