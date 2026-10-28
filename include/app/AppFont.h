// cppcheck-suppress-file missingIncludeSystem
#pragma once

namespace freetunnel {

// Makes Inter the font of the application, the same on every system: Regular for
// all text and Medium where the QML says font.weight: Font.Medium, which are the
// two files in assets/fonts/inter, compiled in (assets/fonts/fonts.qrc). The
// application's own font is what a QML Text, TextInput or TextEdit takes when it
// names no family, so nothing in the QML does. The places that want a fixed pitch
// name one (Main.qml's monoFont) and keep it.
//
// Call it before the QML engine loads anything, so that the first frame and every
// TextMetrics are already set in Inter. The size stays the system's own: only
// text that names none depends on it.
//
// The font is looks, not function: if either file cannot be loaded this says so
// in the log, leaves the system's font alone and returns false, and the app runs.
// It adds the files to the font database once per process, however often it is
// called.
bool useInterFont();

} // namespace freetunnel
