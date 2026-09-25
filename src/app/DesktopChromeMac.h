// cppcheck-suppress-file missingIncludeSystem
#pragma once

// The macOS half of DesktopChrome, kept out of its public header so that nothing
// including it needs AppKit: see DesktopChromeMac.mm.

namespace freetunnel {

// Unhide the application and make it the active one, as a Dock click does.
void unhideMacApplication();

} // namespace freetunnel
