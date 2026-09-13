// cppcheck-suppress-file missingIncludeSystem
#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace freetunnel {

// One program as a person would recognise it, paired with the name a rule can
// actually match.
struct InstalledApp {
    QString name;           // "Firefox Web Browser"
    QString executablePath; // "/usr/lib/firefox/firefox"
};

// Everything the system lists as an installed application: .desktop entries on
// Linux (including Flatpak and Snap, since those register entries like anything
// else), Start Menu shortcuts on Windows, .app bundles on macOS.
//
// Sorted by name and deduplicated by executable, because one program routinely
// has several entries — a Flatpak and a distribution package, or a Start Menu
// shortcut in both the machine-wide and the per-user tree.
//
// Called synchronously, from the interface thread, when the picker opens. The
// cost is measured rather than assumed on Linux — about 8 ms for a hundred
// applications — but NOT on Windows, where every Start Menu shortcut is
// resolved through the shell to find out what it points at. If that turns out
// to stall the picker, this is the call to move to a worker.
QList<InstalledApp> installedApplications();

// Where this system keeps the things a person launches.
//
// Not simply QStandardPaths::ApplicationsLocation, because on two of the three
// platforms that answer is incomplete in a way that is invisible until someone
// looks for a program that is not in the list. Read out of Qt's own source
// rather than guessed at:
//
//  - macOS adds NSSystemDomainMask only for fonts and caches, so the list is
//    /Applications and ~/Applications and NOT /System/Applications, which is
//    where Safari, Mail, Messages and the rest of the system's own programs
//    have lived since Catalina.
//  - Windows returns writableLocation() alone, which is the CURRENT USER's
//    Start Menu. Most installers write to the machine-wide one, so the list was
//    missing the majority of what is installed.
//
// Exported so those two facts are checked by a test on the platform they are
// about, rather than by reading a list on a screen.
QStringList applicationDirectories();

// Whether an entry should be offered to a person at all. Entries exist that are
// deliberately not shown in menus — MIME handlers, per-session helpers — and
// listing those would bury the programs someone is actually looking for.
bool desktopEntryIsVisibleApplication(const QString &contents);

// The Name= of a .desktop entry. Separated from the file handling so the
// parsing is tested on every platform, not only where .desktop files are native.
QString displayNameFromDesktopEntry(const QString &contents);

} // namespace freetunnel
