// cppcheck-suppress-file missingIncludeSystem
#include <QtTest>

#include <algorithm>
#include <cmath>

#include <QDirIterator>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QRawFont>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QXmlStreamReader>

#include "app/AppFont.h"

// The desktop's font is Inter, bundled: Regular for all text and Medium where the
// QML says Font.Medium (assets/fonts/inter, assets/fonts/fonts.qrc). What these
// tests are for is what Qt's font databases make of those two files, which differs
// from system to system and cannot be seen from the files: a static Medium face can
// come out as Regular on one of them, and the QML would still say Medium and every
// other test still pass. On Windows this runs on the real platform plugin, with
// DirectWrite, which is what users have; the offscreen plugin that the rest of
// the suite runs on uses another font database there.
class TestAppFont : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void bothWeightsAreInTheFontDatabase();
    void theApplicationFontIsInter();
    void textThatNamesNoFamilyGetsInter_data();
    void textThatNamesNoFamilyGetsInter();
    void eachWeightIsItsOwnFile_data();
    void eachWeightIsItsOwnFile();
    void whatTheSystemFontSaysOfItsStyleAndWeightIsNotKept_data();
    void whatTheSystemFontSaysOfItsStyleAndWeightIsNotKept();
    void loadingAgainAddsNothing();
    void everyCharacterTheInterfaceDrawsIsInTheFont();
    void theFixedPitchPlacesStayFixedPitch_data();
    void theFixedPitchPlacesStayFixedPitch();
    void theQmlUsesOnlyTheBundledWeightsAndNoOtherFamily();
};

namespace {

// What the QML takes from the application: the two weights, at the sizes it uses.
const QList<QFont::Weight> kWeights = {QFont::Normal, QFont::Medium};

QByteArray resourceBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// `text` with the comments of a QML (or JavaScript) source taken out, strings left
// as they are: a "//" inside a string is not a comment, and a quotation mark in a
// comment does not open a string.
QString withoutComments(const QString &source)
{
    QString out;
    const qsizetype n = source.size();
    qsizetype i = 0;
    while (i < n) {
        const QChar c = source.at(i);
        if (c == u'"' || c == u'\'' || c == u'`') {
            qsizetype j = i + 1;
            while (j < n && source.at(j) != c) {
                if (source.at(j) == u'\\')
                    ++j;
                ++j;
            }
            out += source.mid(i, j + 1 - i);
            i = j + 1;
        } else if (source.mid(i, 2) == QLatin1String("//")) {
            while (i < n && source.at(i) != u'\n')
                ++i;
        } else if (source.mid(i, 2) == QLatin1String("/*")) {
            const qsizetype end = source.indexOf(QLatin1String("*/"), i + 2);
            i = end < 0 ? n : end + 2;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// Every QML source in the resource, comments out, by its path in the resource.
QMap<QString, QString> qmlSources()
{
    QMap<QString, QString> out;
    QDirIterator it(QStringLiteral(":/"), {QStringLiteral("*.qml")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        out.insert(path.mid(2), withoutComments(QString::fromUtf8(resourceBytes(path))));
    }
    return out;
}

// The width of every letter and digit of both alphabets, each measured on its own
// (no kerning, no substitutions), at a size where a rounded advance is a small part
// of it. Where Qt's database gives back the file asked for, this is that file's width.
QString sample()
{
    QString out = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
    for (char16_t c = u'\u0410'; c <= u'\u044F'; ++c) // А to я
        out += QChar(c);
    out += QChar(u'\u0401'); // Ё
    out += QChar(u'\u0451'); // ё
    return out;
}
const QString kSample = sample();
constexpr int kSampleSize = 40;

qreal widthInQt(QFont::Weight weight)
{
    QFont font = QGuiApplication::font();
    font.setPixelSize(kSampleSize);
    font.setWeight(weight);
    const QFontMetricsF metrics(font);
    qreal sum = 0;
    for (const QChar ch : kSample)
        sum += metrics.horizontalAdvance(ch);
    return sum;
}

qreal widthInFile(const QString &resource)
{
    const QRawFont raw(resourceBytes(resource), kSampleSize, QFont::PreferNoHinting);
    if (!raw.isValid())
        return -1;
    const QList<quint32> glyphs = raw.glyphIndexesForString(kSample);
    qreal sum = 0;
    for (const QPointF &advance : raw.advancesForGlyphIndexes(glyphs, QRawFont::UseDesignMetrics))
        sum += advance.x();
    return sum;
}

} // namespace

void TestAppFont::initTestCase()
{
    QVERIFY2(freetunnel::useInterFont(), "the Inter files did not load");
}

void TestAppFont::bothWeightsAreInTheFontDatabase()
{
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Inter")));
    QSet<int> weights;
    const QStringList styles = QFontDatabase::styles(QStringLiteral("Inter"));
    for (const QString &style : styles)
        weights.insert(QFontDatabase::weight(QStringLiteral("Inter"), style));
    QVERIFY2(weights.contains(QFont::Normal) && weights.contains(QFont::Medium),
             qPrintable(QStringLiteral("Inter has the styles %1").arg(styles.join(QLatin1String(", ")))));
}

void TestAppFont::theApplicationFontIsInter()
{
    QCOMPARE(QGuiApplication::font().family(), QStringLiteral("Inter"));
    // What a QML Text builds its font from: a font that names nothing.
    const QFont plain;
    QCOMPARE(QFontInfo(plain).family(), QStringLiteral("Inter"));
}

void TestAppFont::textThatNamesNoFamilyGetsInter_data()
{
    QTest::addColumn<int>("weight");
    QTest::addColumn<int>("pixelSize");
    for (const int size : {11, 14, 15, 20}) {
        QTest::newRow(qPrintable(QStringLiteral("Normal %1 px").arg(size))) << int(QFont::Normal) << size;
        QTest::newRow(qPrintable(QStringLiteral("Medium %1 px").arg(size))) << int(QFont::Medium) << size;
    }
}

void TestAppFont::textThatNamesNoFamilyGetsInter()
{
    QFETCH(int, weight);
    QFETCH(int, pixelSize);
    QFont font; // as a QML Text's is
    font.setPixelSize(pixelSize);
    font.setWeight(QFont::Weight(weight));
    const QFontInfo info(font);
    QCOMPARE(info.family(), QStringLiteral("Inter"));
    // The weight asked for, or the closest the font has: not a bold made up from
    // the Regular, which would be heavier than a Medium.
    QVERIFY2(std::abs(info.weight() - weight) <= 50,
             qPrintable(QStringLiteral("weight %1 asked, %2 drawn").arg(weight).arg(info.weight())));
}

// A Medium that Qt does not find among the faces is made from the Regular, and
// draws exactly as wide: everything still says Inter and 500, and nothing is Medium.
// So each weight is measured, by Qt, against the width of the file that is meant
// to draw it, and has to be nearer to that than to half the way to the other.
void TestAppFont::eachWeightIsItsOwnFile_data()
{
    QTest::addColumn<int>("weight");
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("otherFile");
    QTest::newRow("Regular") << int(QFont::Normal) << QStringLiteral(":/fonts/Inter-Regular.ttf")
                             << QStringLiteral(":/fonts/Inter-Medium.ttf");
    QTest::newRow("Medium") << int(QFont::Medium) << QStringLiteral(":/fonts/Inter-Medium.ttf")
                            << QStringLiteral(":/fonts/Inter-Regular.ttf");
}

namespace {

// Qt's width of `weight` against the width of the file meant to draw it and of the
// other one. Called in a test function, for the application font as it is then.
void verifyWeightIsItsOwnFile(int weight, const QString &file, const QString &otherFile)
{
    const qreal mine = widthInFile(file);
    const qreal other = widthInFile(otherFile);
    QVERIFY2(mine > 0 && other > 0, "a font file could not be read");
    const qreal gap = std::abs(mine - other);
    // If the two files drew alike this could not tell them apart.
    QVERIFY2(gap > 10, qPrintable(QStringLiteral("the files differ by only %1 px").arg(gap)));
    const qreal drawn = widthInQt(QFont::Weight(weight));
    QVERIFY2(std::abs(drawn - mine) < gap / 2,
             qPrintable(QStringLiteral("Qt draws weight %1 %2 px wide; its file %3 px, the other weight's %4 px")
                                .arg(weight).arg(drawn).arg(mine).arg(other)));
}

} // namespace

void TestAppFont::eachWeightIsItsOwnFile()
{
    QFETCH(int, weight);
    QFETCH(QString, file);
    QFETCH(QString, otherFile);
    verifyWeightIsItsOwnFile(weight, file, otherFile);
}

// useInterFont() starts from the system's font, for the size it picked for this
// display, and Qt matches a font's style name before its weight. KDE writes the
// style of the font it was given, usually "Regular", into the font of every
// application, and a Text asking for Font.Medium would then be drawn in the Regular
// file, with Qt still reporting weight 500. A system weight other than the Regular
// is passed to every text that asks for none, and from DemiBold up Qt makes a bold
// from the Medium. So the family and the size are the system's, and nothing else.
void TestAppFont::whatTheSystemFontSaysOfItsStyleAndWeightIsNotKept_data()
{
    QTest::addColumn<QString>("styleName");
    QTest::addColumn<int>("weight");
    QTest::addColumn<bool>("italic");
    QTest::newRow("style name Regular") << QStringLiteral("Regular") << int(QFont::Normal) << false;
    QTest::newRow("style name Regular, DemiBold") << QStringLiteral("Regular") << int(QFont::DemiBold) << false;
    QTest::newRow("DemiBold") << QString() << int(QFont::DemiBold) << false;
    QTest::newRow("Bold") << QString() << int(QFont::Bold) << false;
    QTest::newRow("italic") << QStringLiteral("Italic") << int(QFont::Normal) << true;
}

void TestAppFont::whatTheSystemFontSaysOfItsStyleAndWeightIsNotKept()
{
    QFETCH(QString, styleName);
    QFETCH(int, weight);
    QFETCH(bool, italic);

    const QFont before = QGuiApplication::font();
    const auto restore = qScopeGuard([&before] { QGuiApplication::setFont(before); });

    QFont system = before;
    system.setFamilies({QStringLiteral("DejaVu Sans")});
    system.setPointSizeF(13.5);
    system.setStyleName(styleName);
    system.setWeight(QFont::Weight(weight));
    system.setItalic(italic);
    QGuiApplication::setFont(system);

    QVERIFY(freetunnel::useInterFont()); // it applies the font on every call
    const QFont applied = QGuiApplication::font();
    QCOMPARE(applied.family(), QStringLiteral("Inter"));
    QCOMPARE(applied.pointSizeF(), 13.5); // the size is the system's
    QVERIFY2(applied.styleName().isEmpty(),
             qPrintable(QStringLiteral("style name %1 kept").arg(applied.styleName())));
    QCOMPARE(int(applied.weight()), int(QFont::Normal));
    QVERIFY(!applied.italic());

    verifyWeightIsItsOwnFile(QFont::Normal, QStringLiteral(":/fonts/Inter-Regular.ttf"),
                             QStringLiteral(":/fonts/Inter-Medium.ttf"));
    if (QTest::currentTestFailed())
        return;
    verifyWeightIsItsOwnFile(QFont::Medium, QStringLiteral(":/fonts/Inter-Medium.ttf"),
                             QStringLiteral(":/fonts/Inter-Regular.ttf"));
}

// useInterFont() is called by the wiring of every start and by tests; a font added
// again is a font in memory twice, and nothing in the database shows it but the
// number of fonts. Qt numbers the ones it is given from 0 in the order added: Inter's
// two are 0 and 1 (nothing else in this test adds a font), and a copy would be 2.
void TestAppFont::loadingAgainAddsNothing()
{
    QVERIFY(freetunnel::useInterFont());
    QVERIFY(freetunnel::useInterFont());
    QVERIFY(!QFontDatabase::applicationFontFamilies(0).isEmpty());
    QVERIFY(!QFontDatabase::applicationFontFamilies(1).isEmpty());
    QVERIFY2(QFontDatabase::applicationFontFamilies(2).isEmpty(), "the files were added again");
    QCOMPARE(QGuiApplication::font().family(), QStringLiteral("Inter"));
}

namespace {

// The characters of the string literals of the QML and of the Russian translations
// that are not ASCII. What a comment mentions is not drawn, and an escape in a string
// (the code points of the caption icons, which have a font of their own) is not a
// character in the file: neither is read here.
QSet<char32_t> nonAsciiCharactersDrawn(QStringList *places)
{
    QSet<char32_t> out;
    static const QRegularExpression literal(QStringLiteral(R"re("((?:[^"\\\n]|\\.)*)")re"));
    const QMap<QString, QString> sources = qmlSources();
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        auto matches = literal.globalMatch(it.value());
        while (matches.hasNext()) {
            const QString text = matches.next().captured(1);
            for (const char32_t ch : text.toUcs4()) {
                if (ch > 127 && !out.contains(ch)) {
                    out.insert(ch);
                    places->append(QStringLiteral("%1 in %2").arg(QChar::fromUcs4(ch)).arg(it.key()));
                }
            }
        }
    }
    QFile ts(QStringLiteral(FT_I18N_DIR "/freetunnel_ru.ts"));
    if (ts.open(QIODevice::ReadOnly)) {
        QXmlStreamReader xml(&ts);
        while (!xml.atEnd()) {
            if (xml.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (xml.name() != QLatin1String("source") && xml.name() != QLatin1String("translation"))
                continue;
            for (const char32_t ch : xml.readElementText().toUcs4()) {
                if (ch > 127 && !out.contains(ch)) {
                    out.insert(ch);
                    places->append(QStringLiteral("%1 in freetunnel_ru.ts").arg(QChar::fromUcs4(ch)));
                }
            }
        }
    }
    return out;
}

} // namespace

// A character the font lacks is drawn by another font, a different size and weight
// from its neighbours, or as a box. The whole of Russian is in Inter, and so is
// everything else the interface says; the one that is not is ▾ (U+25BE, Inter has
// ▼ U+25BC only), which the system's font draws, as it did before Inter. A new
// translation or a new symbol is checked here before a user sees it.
void TestAppFont::everyCharacterTheInterfaceDrawsIsInTheFont()
{
    static const QSet<char32_t> drawnByAnotherFont = {U'\u25BE'};
    QStringList places;
    const QSet<char32_t> characters = nonAsciiCharactersDrawn(&places);
    // Reading nothing would pass for everything.
    QVERIFY2(characters.size() > 60, "the QML and the translations were not read");
    QVERIFY(characters.contains(U'\u044F') && characters.contains(U'\u2713')); // я and ✓

    QStringList lacking;
    for (const QString &file : {QStringLiteral(":/fonts/Inter-Regular.ttf"),
                                QStringLiteral(":/fonts/Inter-Medium.ttf")}) {
        const QRawFont raw(resourceBytes(file), 15);
        QVERIFY(raw.isValid());
        for (const char32_t ch : characters) {
            if (!raw.supportsCharacter(ch) && !drawnByAnotherFont.contains(ch))
                lacking << QStringLiteral("U+%1 %2 (%3)")
                                   .arg(uint(ch), 4, 16, QLatin1Char('0')).arg(QChar::fromUcs4(ch)).arg(file.mid(8));
        }
    }
    QVERIFY2(lacking.isEmpty(), qPrintable(QStringLiteral("not in Inter: ") + lacking.join(QStringLiteral(", "))));
}

// Main.qml's monoFont. Making Inter the application font must not take the log
// and the certificate box out of a fixed pitch: a family that exists is asked for
// by name, and "monospace" is fontconfig's own.
void TestAppFont::theFixedPitchPlacesStayFixedPitch_data()
{
    QTest::addColumn<QString>("family");
    QTest::newRow("Windows") << QStringLiteral("Consolas");
    QTest::newRow("macOS") << QStringLiteral("Menlo");
    QTest::newRow("elsewhere") << QStringLiteral("monospace");
}

void TestAppFont::theFixedPitchPlacesStayFixedPitch()
{
    QFETCH(QString, family);
    const QString main = QString::fromUtf8(resourceBytes(QStringLiteral(":/Main.qml")));
    QVERIFY2(main.contains(QLatin1Char('"') + family + QLatin1Char('"')),
             "Main.qml's monoFont no longer names this family: update this test with it");
    if (family == QLatin1String("monospace")) {
#ifndef Q_OS_LINUX
        QSKIP("the generic name is the one Linux's fontconfig resolves");
#endif
        // fontconfig answers "monospace" with a fixed-pitch font if the system has one.
        // The database lists a generic "Monospace" of its own even where it has no font
        // at all, and that is not one.
        const QStringList families = QFontDatabase::families();
        const bool anyFixed = std::any_of(families.cbegin(), families.cend(), [](const QString &f) {
            return QFontDatabase::isFixedPitch(f)
                    && f.compare(QLatin1String("Monospace"), Qt::CaseInsensitive) != 0;
        });
        if (!anyFixed)
            QSKIP("this system has no fixed-pitch font at all");
    } else if (!QFontDatabase::families().contains(family)) {
        QSKIP("this system does not have the family");
    }
    QFont font(family);
    font.setPixelSize(11);
    const QFontInfo info(font);
    QVERIFY2(info.fixedPitch() && info.family() != QLatin1String("Inter"),
             qPrintable(QStringLiteral("%1 became %2").arg(family, info.family())));
}

// Only Normal and Medium are bundled. Another weight in the QML would not be
// refused: Qt makes it from the nearest face, a Bold from the Medium, and it
// would look wrong on every system at once. Nor does any text name a family of
// its own (the fixed pitch ones and the caption icons apart), which would take it
// out of the one font; and rich text, which sets bold and italic from inside a
// string, has no place here either.
void TestAppFont::theQmlUsesOnlyTheBundledWeightsAndNoOtherFamily()
{
    const QMap<QString, QString> sources = qmlSources();
    QVERIFY(sources.size() >= 20);
    static const QRegularExpression fontEnum(QStringLiteral(R"re(\bFont\.(\w+))re"));
    static const QRegularExpression numberedWeight(QStringLiteral(R"re(\bweight\s*:\s*\d)re"));
    static const QRegularExpression boldOrItalic(QStringLiteral(R"re(\b(?:font\.)?(?:bold|italic)\s*:)re"));
    static const QRegularExpression stringLiteral(QStringLiteral(R"re("(?:[^"\\\n]|\\.)*")re"));
    static const QRegularExpression richText(QStringLiteral(R"re(</?(?:b|i|u|strong|em|font)\b[^>]*>)re"),
                                             QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression family(QStringLiteral(R"re(\bfont\.family\s*:\s*([^;}\n]+))re"));
    static const QSet<QString> allowedFamilies = {QStringLiteral("shell.monoFont"),
                                                  QStringLiteral("controls.captionFont")};

    QStringList wrong;
    int weights = 0;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        const QString &text = it.value();
        auto fonts = fontEnum.globalMatch(text);
        while (fonts.hasNext()) {
            const QString name = fonts.next().captured(1);
            ++weights;
            if (name != QLatin1String("Normal") && name != QLatin1String("Medium"))
                wrong << QStringLiteral("%1: Font.%2").arg(it.key(), name);
        }
        if (numberedWeight.match(text).hasMatch())
            wrong << QStringLiteral("%1: a numbered weight").arg(it.key());
        if (boldOrItalic.match(text).hasMatch())
            wrong << QStringLiteral("%1: bold or italic").arg(it.key());
        auto literals = stringLiteral.globalMatch(text);
        while (literals.hasNext()) {
            if (richText.match(literals.next().captured(0)).hasMatch())
                wrong << QStringLiteral("%1: rich text").arg(it.key());
        }
        auto families = family.globalMatch(text);
        while (families.hasNext()) {
            const QString name = families.next().captured(1).trimmed();
            if (!allowedFamilies.contains(name))
                wrong << QStringLiteral("%1: font.family: %2").arg(it.key(), name);
        }
    }
    // Reading no weights at all would pass for any QML.
    QVERIFY2(weights >= 8, "the font weights of the QML were not found");
    QVERIFY2(wrong.isEmpty(), qPrintable(QStringLiteral("not bundled: ") + wrong.join(QStringLiteral("; "))));
}

QTEST_MAIN(TestAppFont)
#include "test_app_font.moc"
