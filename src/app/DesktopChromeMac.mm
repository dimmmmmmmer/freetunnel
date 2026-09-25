#include "DesktopChromeMac.h"

#import <AppKit/AppKit.h>

#include <QWindow>

#include <memory>
#include <utility>

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
    if (@available(macOS 14.0, *)) {
        [NSApp activate];
    } else {
        // Deprecated from macOS 14, and only reached before it. Clang judges
        // deprecation by the deployment target, not by the @available branch.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
    }
}

// Leaving full screen is an animation, and Qt reports the new state the moment
// it asks for it, not when AppKit is done. Ordered out before then, the window
// left its full-screen Space behind: an empty black screen.
void whenMacFullScreenEnds(QWindow *window, std::function<void()> then)
{
    NSView *view = reinterpret_cast<NSView *>(window->winId());
    NSWindow *nsWindow = view ? view.window : nil;
    if (!nsWindow) {
        then();
        return;
    }
    auto handler = std::make_shared<std::function<void()>>(std::move(then));
    __block id observer = nil;
    observer = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSWindowDidExitFullScreenNotification
                        object:nsWindow
                         queue:nil
                    usingBlock:^(NSNotification *) {
                        [[NSNotificationCenter defaultCenter] removeObserver:observer];
                        (*handler)();
                    }];
}

} // namespace freetunnel
