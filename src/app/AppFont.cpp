// cppcheck-suppress-file missingIncludeSystem
#include "app/AppFont.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLatin1String>
#include <QStringList>

namespace freetunnel {

namespace {

// Both faces into the font database, or neither is counted: Medium without
// Regular, or the other way round, would draw one weight in the other's place.
// Only once per process. Qt adds a font again on every call, and the second copy
// is megabytes of memory for nothing.
bool loadInterFaces()
{
    static const char *const kFaces[] = {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf"};
    bool ok = true;
    for (const char *face : kFaces) {
        const int id = QFontDatabase::addApplicationFont(QLatin1String(face));
        // The family it registers under is what the font is asked for by below, so a
        // file that loads and calls itself something else is as good as no file.
        if (id < 0
            || !QFontDatabase::applicationFontFamilies(id).contains(QLatin1String("Inter"))) {
            qWarning("Could not load the Inter font %s; using the system font", face);
            ok = false;
        }
    }
    return ok;
}

} // namespace

bool useInterFont()
{
    static const bool loaded = loadInterFaces();
    if (!loaded)
        return false;
    // The system's font for the size it picked for this display, and nothing else
    // of it. Its style name goes: Qt matches that before the weight, and a KDE font
    // that says "Regular" would draw every Font.Medium in the Regular file. Its
    // weight and slant go, as every text that asks for none would take them, and a
    // weight from DemiBold up is a bold made from the Medium.
    QFont font = QGuiApplication::font();
    font.setFamilies({QStringLiteral("Inter")});
    font.setStyleName(QString());
    font.setWeight(QFont::Normal);
    font.setItalic(false);
    QGuiApplication::setFont(font);
    return true;
}

} // namespace freetunnel
