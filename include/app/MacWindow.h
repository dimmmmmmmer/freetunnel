#pragma once

// macOS-only: make the window's title bar transparent and let the content view
// extend underneath it, so the app background flows behind the traffic-light
// buttons (a "unified" title bar). No-op on other platforms.
#ifdef __APPLE__
void applyMacUnifiedTitlebar(unsigned long long nsViewPtr);
#endif
