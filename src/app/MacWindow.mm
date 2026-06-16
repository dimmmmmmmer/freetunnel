#include "app/MacWindow.h"

#import <AppKit/AppKit.h>

// QWindow::winId() returns the backing NSView* on macOS. Reach its NSWindow and
// flip on the unified-title-bar look: transparent bar, hidden title text, and a
// full-size content view so our QML background paints behind the buttons.
void applyMacUnifiedTitlebar(unsigned long long nsViewPtr) {
    NSView *view = reinterpret_cast<NSView *>(nsViewPtr);
    if (!view)
        return;
    NSWindow *window = view.window;
    if (!window)
        return;
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    window.styleMask |= NSWindowStyleMaskFullSizeContentView;
}
