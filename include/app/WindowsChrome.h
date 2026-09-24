// cppcheck-suppress-file missingIncludeSystem
#pragma once

class QQuickWindow;

namespace freetunnel {

// Windows only, and only in a build that carries QWindowKit (FT_HAVE_QWINDOWKIT).
//
// Gives the frameless window back what Windows gives a window with a real frame:
// the DWM shadow, rounded corners and thin border on Windows 11, Aero Snap when it
// is dragged to an edge, the Snap Layouts flyout over the maximise button, the
// system menu, double-click to maximise and a taskbar click that minimises. Every
// one of those is lost to a Qt::FramelessWindowHint window, and every one of them
// is Win32 behaviour that nobody working on this project can test by hand — which
// is why it comes from a library with years of field use behind it rather than
// from WM_NCCALCSIZE and WM_NCHITTEST handlers written here.
//
// Expects the window to have been created hidden and without a minimum size
// (Main.qml's windowsAgent): the agent must be set up before either, and this
// applies both afterwards.
void setupWindowsChrome(QQuickWindow *window);

} // namespace freetunnel
