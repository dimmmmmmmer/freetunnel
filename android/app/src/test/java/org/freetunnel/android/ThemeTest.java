package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

// The theme mode: what a fresh install, an old preview's "dark" boolean and a device that
// cannot tell night from day each start with. tests/test_android_theme_matches_desktop.sh
// checks the colours themselves against qml/Main.qml.
public final class ThemeTest {
    private static final int NIGHT_NO = 0x10;
    private static final int NIGHT_YES = 0x20;
    private static final int NIGHT_UNDEFINED = 0;

    @Test public void aFreshInstallFollowsTheSystem() {
        assertEquals(Theme.SYSTEM, Theme.mode(null, null));
    }

    @Test public void aChoiceTheOldBooleanRecordedIsKept() {
        // That boolean was written only when the user picked Light or Dark.
        assertEquals(Theme.DARK_MODE, Theme.mode(null, true));
        assertEquals(Theme.LIGHT_MODE, Theme.mode(null, false));
    }

    @Test public void aStoredModeWinsOverTheOldBoolean() {
        assertEquals(Theme.SYSTEM, Theme.mode("system", false));
        assertEquals(Theme.LIGHT_MODE, Theme.mode("light", true));
        assertEquals(Theme.DARK_MODE, Theme.mode("dark", false));
    }

    @Test public void anUnknownStoredModeIsAsIfNoneWasStored() {
        assertEquals(Theme.SYSTEM, Theme.mode("sepia", null));
        assertEquals(Theme.LIGHT_MODE, Theme.mode("", false));
    }

    @Test public void systemFollowsTheDeviceAndAnUndefinedDeviceIsDark() {
        assertTrue(Theme.isDark(Theme.SYSTEM, NIGHT_YES));
        assertTrue(!Theme.isDark(Theme.SYSTEM, NIGHT_NO));
        assertTrue("A device that cannot tell is dark, as this app was before it followed the system",
            Theme.isDark(Theme.SYSTEM, NIGHT_UNDEFINED));
    }

    @Test public void lightAndDarkIgnoreTheDevice() {
        for (int night : new int[]{NIGHT_NO, NIGHT_YES, NIGHT_UNDEFINED}) {
            assertTrue(Theme.isDark(Theme.DARK_MODE, night));
            assertTrue(!Theme.isDark(Theme.LIGHT_MODE, night));
        }
    }

    @Test public void theTwoThemesAreTheDesktopsInfoBgOverItsPage() {
        // The selected tab is infoBg over bg: about #303030 in the dark theme, #d9d9d9 in the light.
        assertEquals(0xFF303030, Theme.over(Theme.DARK.infoBg, Theme.DARK.bg));
        assertEquals(0xFFD9D9D9, Theme.over(Theme.LIGHT.infoBg, Theme.LIGHT.bg));
        assertSame(Theme.DARK, Theme.of(true));
        assertSame(Theme.LIGHT, Theme.of(false));
        assertNotEquals(Theme.DARK.bg, Theme.LIGHT.bg);
    }
}
