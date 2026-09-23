#include "app/MacWindow.h"

#import <AppKit/AppKit.h>
#import <CoreServices/CoreServices.h> // kCoreEventClass / kAEReopenApplication

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

MacRect macWindowControlsRect(unsigned long long nsViewPtr) {
    MacRect out;
    NSView *view = reinterpret_cast<NSView *>(nsViewPtr);
    if (!view)
        return out;
    NSWindow *window = view.window;
    if (!window || (window.styleMask & NSWindowStyleMaskFullScreen))
        return out;
    NSView *content = window.contentView;
    if (!content)
        return out;
    NSRect all = NSZeroRect;
    for (NSWindowButton kind : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
        NSButton *button = [window standardWindowButton:kind];
        if (!button || button.hidden || !button.superview)
            continue;
        const NSRect frame = [content convertRect:button.frame fromView:button.superview];
        all = NSIsEmptyRect(all) ? frame : NSUnionRect(all, frame);
    }
    if (NSIsEmptyRect(all))
        return out;
    // The content view spans the whole window here (full-size content view), so
    // its coordinates are the window's. Qt's own view is flipped; the content view
    // need not be, so the flip is done explicitly rather than assumed.
    const double top = content.isFlipped ? all.origin.y
                                         : content.bounds.size.height - NSMaxY(all);
    out.x = all.origin.x;
    out.y = top;
    out.width = all.size.width;
    out.height = all.size.height;
    return out;
}

// What a double-click on a title bar does, as the user set it in Desktop & Dock.
// AppKit applies this itself to a real title bar; ours is QML, so it has to be
// asked. "Fill" has no public API of its own and zooms, as the agent of the same
// name did before it existed.
static void performTitlebarDoubleClick(NSWindow *window) {
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    NSString *action = [defaults stringForKey:@"AppleActionOnDoubleClick"];
    if (!action && [defaults boolForKey:@"AppleMiniaturizeOnDoubleClick"])
        action = @"Minimize"; // the older, boolean spelling of the same setting
    if ([action isEqualToString:@"None"])
        return;
    if ([action isEqualToString:@"Minimize"]) {
        [window miniaturize:nil];
        return;
    }
    [window zoom:nil]; // "Maximize", "Fill", or never set: the system default
}

bool macHandleTitlebarPress(unsigned long long nsViewPtr) {
    NSView *view = reinterpret_cast<NSView *>(nsViewPtr);
    NSWindow *window = view ? view.window : nil;
    if (!window || (window.styleMask & NSWindowStyleMaskFullScreen))
        return false;
    NSEvent *event = NSApp.currentEvent;
    // Type first: asking a non-mouse event for its click count raises.
    if (event && event.type == NSEventTypeLeftMouseDown) {
        if (event.clickCount == 2) {
            performTitlebarDoubleClick(window);
            return true;
        }
        [window performWindowDragWithEvent:event];
        return true;
    }
    // The current event is not the press. On a trackpad it is often a pressure
    // event, and from macOS 27 a gesture recogniser can deliver the press after
    // the event that caused it; Qt 6.8's startSystemMove gives up in both cases
    // and the window simply does not move. Qt 6.12 answers this by making the
    // mouse-down the drag needs, which is what happens here — but only while the
    // button really is down, or the window would follow a pointer nobody holds.
    if ((NSEvent.pressedMouseButtons & 1) == 0)
        return false;
    NSEvent *down = [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                       location:window.mouseLocationOutsideOfEventStream
                                  modifierFlags:0
                                      timestamp:NSProcessInfo.processInfo.systemUptime
                                   windowNumber:window.windowNumber
                                        context:nil
                                    eventNumber:0
                                     clickCount:1
                                       pressure:1.0];
    if (!down)
        return false;
    [window performWindowDragWithEvent:down];
    return true;
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

// Handler object for the Dock-icon reopen Apple Event ('rapp').
@interface FTReopenTarget : NSObject {
    std::function<void()> _onReopen;
}
- (instancetype)initWithHandler:(std::function<void()>)handler;
- (void)handleReopen:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply;
@end

@implementation FTReopenTarget
- (instancetype)initWithHandler:(std::function<void()>)handler {
    if ((self = [super init]))
        _onReopen = std::move(handler);
    return self;
}
- (void)handleReopen:(NSAppleEventDescriptor *)event withReplyEvent:(NSAppleEventDescriptor *)reply {
    (void)event;
    (void)reply;
    if (_onReopen)
        _onReopen();
}
@end

void installMacDockReopenHandler(std::function<void()> onReopen) {
    // kAEReopenApplication ('rapp') is delivered ONLY on a Dock-icon click, so a
    // window hidden to the menu bar is reopened by that gesture alone — not by the
    // app merely becoming active (status-bar clicks, Cmd-Tab), which is what used
    // to yank the window back open. Isolated to this one event: launch ('oapp'),
    // URL open ('GURL') and quit are separate and unaffected.
    // Intentionally never released: one per process, lives for the app's lifetime.
    static FTReopenTarget *target = nil;
    target = [[FTReopenTarget alloc] initWithHandler:std::move(onReopen)];
    void (^install)(void) = ^{
        [[NSAppleEventManager sharedAppleEventManager]
                setEventHandler:target
                    andSelector:@selector(handleReopen:withReplyEvent:)
                  forEventClass:kCoreEventClass
                     andEventID:kAEReopenApplication];
    };
    // -[NSApplication finishLaunching] installs the system's own Apple event
    // handlers, silently replacing any earlier registration for the same event,
    // so ours has to land after that point.
    //
    // Do NOT gate on NSApp.running to decide whether that already happened: it is
    // NO both *before* finishLaunching and *after* a completed-then-stopped
    // [NSApp run]. Qt produces that second state from
    // QCocoaEventDispatcherPrivate::ensureNSAppInitialized() for any nested
    // QEventLoop::exec(ExcludeUserInputEvents) on the GUI thread, and
    // runGuiApplication() spins two before reaching us — forwardToRunningInstance()
    // and startSingleInstanceServer() both go through CredentialStore, whose
    // withoutFreezingTheUi() wrapper is exactly such a loop. The old check
    // therefore took the deferred branch and waited for a DidFinishLaunching
    // notification that had already been posted: the handler was never installed
    // and Dock clicks reopened nothing.
    //
    // NSAppleEventManager keeps one handler per (eventClass, eventID), so
    // registering repeatedly is idempotent and last-writer-wins. Install now, and
    // again from both launch notifications with queue:nil so the block runs
    // synchronously on the posting thread — inside finishLaunching, right after
    // AppKit's own registration — rather than at some later main-queue drain.
    install();
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserverForName:NSApplicationWillFinishLaunchingNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification *note) {
                        (void)note;
                        install();
                    }];
    [center addObserverForName:NSApplicationDidFinishLaunchingNotification
                        object:nil
                         queue:nil
                    usingBlock:^(NSNotification *note) {
                        (void)note;
                        install();
                    }];
}
