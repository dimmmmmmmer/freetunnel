#include "DesktopChromeMac.h"

#import <AppKit/AppKit.h>

namespace freetunnel {

// Hidden with ⌘H or Hide Others, the app has ordered its windows out while Qt
// still counts them as visible, so showing one again changes nothing and raising
// it is skipped (QCocoaWindow::raise acts only on a window AppKit is showing).
// A menu-bar menu does not activate the app that owns it either. Only a Dock
// click did both, which is why «Show FreeTunnel» and a second launch did nothing
// after ⌘H. Both are harmless when the app is neither hidden nor inactive.
void unhideMacApplication()
{
    [NSApp unhide:nil];
    if (@available(macOS 14.0, *))
        [NSApp activate];
    else
        [NSApp activateIgnoringOtherApps:YES];
}

} // namespace freetunnel
