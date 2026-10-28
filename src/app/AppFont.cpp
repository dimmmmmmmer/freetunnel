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
    // The system's font with only its family changed, so that the size it picked
    // for this display and the rest of its settings stay.
    QFont font = QGuiApplication::font();
    font.setFamilies({QStringLiteral("Inter")});
    QGuiApplication::setFont(font);
    return true;
}

} // namespace freetunnel
