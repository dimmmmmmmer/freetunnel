#include "app/MacWindow.h"

#import <AppKit/AppKit.h>

#include <functional>
#include <utility>

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
    // Dragging is handled explicitly by a top drag-bar in QML (startSystemMove),
    // so the whole background is not draggable.
}

// Target object for the retargeted close button. NSButton holds its target
// weakly, so we keep the single instance alive for the process lifetime below.
@interface FTCloseButtonTarget : NSObject {
    std::function<void()> _onClose;
}
- (instancetype)initWithHandler:(std::function<void()>)handler;
- (void)ftClosePressed:(id)sender;
@end

@implementation FTCloseButtonTarget
- (instancetype)initWithHandler:(std::function<void()>)handler {
    if ((self = [super init]))
        _onClose = std::move(handler);
    return self;
}
- (void)ftClosePressed:(id)sender {
    (void)sender;
    if (_onClose)
        _onClose();
}
@end

void installMacWindowCloseToTray(unsigned long long nsViewPtr, std::function<void()> onClose) {
    NSView *view = reinterpret_cast<NSView *>(nsViewPtr);
    if (!view)
        return;
    NSWindow *window = view.window;
    if (!window)
        return;
    NSButton *closeButton = [window standardWindowButton:NSWindowCloseButton];
    if (!closeButton)
        return;
    // Intentionally never released: one main window per process, and the button
    // keeps only a weak reference to its target.
    static FTCloseButtonTarget *target = nil;
    target = [[FTCloseButtonTarget alloc] initWithHandler:std::move(onClose)];
    closeButton.target = target;
    closeButton.action = @selector(ftClosePressed:);
}
