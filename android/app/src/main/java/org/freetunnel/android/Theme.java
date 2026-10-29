package org.freetunnel.android;

/**
 * The desktop's colours, in both themes, as ARGB ints. The sixteen tokens are the
 * ones qml/Main.qml's {@code theme} object defines, under the same names, and
 * tests/test_android_theme_matches_desktop.sh fails when a value here differs from
 * that file. The values after them are what the desktop works out from those, with
 * the QML's own expression named beside each; the same test checks them too.
 *
 * <p>This class has no Android classes in it, so a plain JVM can load it and test it.
 * {@code pick(dark, 0xAARRGGBB, 0xAARRGGBB)} is the only form the test reads: keep
 * each token on one line of it.
 */
final class Theme {
    static final Theme DARK = new Theme(true);
    static final Theme LIGHT = new Theme(false);

    static Theme of(boolean dark) { return dark ? DARK : LIGHT; }

    final boolean dark;

    // The tokens. Pure neutral greys, and three status hues.
    final int bg;
    final int surface;
    final int tile;
    final int inputBg;
    final int inputBorder;
    final int text;
    final int textDim;
    final int textFaint;
    final int accent;
    final int accentText;
    final int border;
    final int surfaceHover;
    final int toggleOff;
    final int success;
    final int warn;
    final int danger;
    /** Translucent: Qt.rgba(0.69, 0.69, 0.69, 0.16) in the dark theme, Qt.rgba(0.31, 0.31, 0.31, 0.12) in the light. */
    final int infoBg;

    // Derived from them, as the QML derives them.
    /** Qt.darker(accent, 1.12): a primary button's hover. */
    final int primaryHover;
    /** Qt.darker(danger, 1.15): a destructive button's hover. */
    final int destructiveHover;
    /** Qt.lighter(danger, 1.25): a danger link's hover. */
    final int dangerLinkHover;
    /** Qt.lighter(toggleOff, 1.35): a toggle's knob while it is off. */
    final int offKnob;
    /** Qt.rgba(0, 0, 0, 0.18): a toggle's border while it is off. */
    final int toggleOffBorder;
    /** A toggle's knob while it is on. */
    final int onKnob;
    /** Text on a destructive fill. */
    final int onDanger;
    /** The dim behind a card, an editor or a dialog: black at 0.45. */
    final int backdrop;
    /** What a popup's shadow is drawn with: black at 0.5 in the dark theme, 0.2 in the light. */
    final int popupShadow;
    /** The credential banner's fill, Qt.rgba(1, 0.75, 0.2, 0.12) dark and 0.18 light, and its border at 0.45. */
    final int bannerFill;
    final int bannerBorder;

    private Theme(boolean dark) {
        this.dark = dark;
        bg = pick(dark, 0xFF181818, 0xFFECECEC);
        surface = pick(dark, 0xFF262626, 0xFFE2E2E2);
        tile = pick(dark, 0xFF202020, 0xFFD8D8D8);
        inputBg = pick(dark, 0xFF101010, 0xFFD6D6D6);
        inputBorder = pick(dark, 0xFF3A3A3A, 0xFFC2C2C2);
        text = pick(dark, 0xFFEAEAEA, 0xFF1B1B1B);
        textDim = pick(dark, 0xFF9A9A9A, 0xFF6B6B6B);
        textFaint = pick(dark, 0xFF6A6A6A, 0xFF9A9A9A);
        accent = pick(dark, 0xFFB0B0B0, 0xFF4F4F4F);
        accentText = pick(dark, 0xFF181818, 0xFFFFFFFF);
        border = pick(dark, 0xFF2E2E2E, 0xFFE5E5E5);
        surfaceHover = pick(dark, 0xFF2E2E2E, 0xFFD8D8D8);
        toggleOff = pick(dark, 0xFF3A3A3A, 0xFFC4C4C4);
        success = pick(dark, 0xFF3FBF93, 0xFF1D9E75);
        warn = pick(dark, 0xFFD99634, 0xFFBA7517);
        danger = pick(dark, 0xFFE06A6A, 0xFFA32D2D);
        infoBg = pick(dark, 0x29B0B0B0, 0x1F4F4F4F);

        primaryHover = pick(dark, 0xFF9D9D9D, 0xFF474747);
        destructiveHover = pick(dark, 0xFFC35C5C, 0xFF8E2727);
        dangerLinkHover = pick(dark, 0xFFFF9292, 0xFFCC3838);
        offKnob = pick(dark, 0xFF4E4E4E, 0xFFFFFFFF);
        toggleOffBorder = 0x2E000000;
        onKnob = 0xFFFFFFFF;
        onDanger = 0xFFFFFFFF;
        backdrop = 0x73000000;
        popupShadow = dark ? 0x80000000 : 0x33000000;
        bannerFill = dark ? 0x1FFFBF33 : 0x2EFFBF33;
        bannerBorder = 0x73FFBF33;
    }

    private static int pick(boolean dark, int inDark, int inLight) { return dark ? inDark : inLight; }

    /** The same colour with this alpha, 0 to 255. */
    static int withAlpha(int color, int alpha) { return (color & 0x00FFFFFF) | (alpha << 24); }

    /** {@code over} drawn on the opaque {@code under}, as the screen shows it. */
    static int over(int over, int under) {
        int a = over >>> 24;
        int r = ((over >> 16 & 0xFF) * a + (under >> 16 & 0xFF) * (255 - a) + 127) / 255;
        int g = ((over >> 8 & 0xFF) * a + (under >> 8 & 0xFF) * (255 - a) + 127) / 255;
        int b = ((over & 0xFF) * a + (under & 0xFF) * (255 - a) + 127) / 255;
        return 0xFF000000 | r << 16 | g << 8 | b;
    }

    // The theme setting: "system" (the default), "light" or "dark", as the desktop's
    // theme_mode, with the old "dark" boolean this app kept before it had one.

    static final String SYSTEM = "system";
    static final String LIGHT_MODE = "light";
    static final String DARK_MODE = "dark";

    /**
     * The mode to start with. {@code stored} is theme_mode, or null where it has never
     * been written; {@code legacyDark} is the old boolean, or null where that was never
     * written either. Only an explicit choice wrote it, so one that exists is a choice.
     */
    static String mode(String stored, Boolean legacyDark) {
        if (LIGHT_MODE.equals(stored) || DARK_MODE.equals(stored) || SYSTEM.equals(stored)) return stored;
        if (legacyDark != null) return legacyDark ? DARK_MODE : LIGHT_MODE;
        return SYSTEM;
    }

    /**
     * Whether a mode is the dark theme. {@code nightMode} is Configuration.uiMode with
     * UI_MODE_NIGHT_MASK applied: 0x20 is night, 0x10 is day, and 0, which a device that
     * cannot tell reports, counts as dark: this app was dark before it followed the system.
     */
    static boolean isDark(String mode, int nightMode) {
        if (DARK_MODE.equals(mode)) return true;
        if (LIGHT_MODE.equals(mode)) return false;
        return nightMode != 0x10;
    }
}
