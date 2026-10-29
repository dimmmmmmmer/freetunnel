package org.freetunnel.android;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

// What the import confirmation shows of a configuration's name and server, whoever
// wrote them: a link, a file picked on the TV or sent from a phone, or the clipboard.
// The lines after them, where the text was sent from and that certificate
// verification is off, must stay in sight and readable on a screen a remote cannot
// scroll. The device test checks the confirmation itself.
public final class ShownTextTest {
    private static void assertShown(String[][] texts) {
        for (String[] text : texts) assertEquals(text[0], text[1], ShownText.of(text[0]));
    }

    @Test public void itIsOneLineWithNothingThatReversesOrHidesText() {
        assertShown(new String[][]{
            {"Мой сервер", "Мой сервер"}, {"vpn.example.com", "vpn.example.com"}, {"", ""},
            // Nothing that breaks the line, reverses text or is invisible.
            {"a\u2028b\u2029c\u0085d\u0007\r\ne\tf", "abcdef"}, {"\u202Elmot.exe", "lmot.exe"},
            {"\u2066x\u2069.toml", "x.toml"}, {"\u200F\u061C\u00AD\u200B\uFEFF", ""},
            // Any other space as a space, and no letter that draws nothing.
            {"My\u3000VPN\u00A0.toml", "My VPN .toml"}, {"\u3164\u3164.toml", ".toml"}, {"\u115F\u1160\uFFA0\u2800", ""},
            // Half of an emoji is no character.
            {"a\uD83Db\uDD12c", "abc"}});
    }

    @Test public void aCharacterKeepsTwoMarksAtMost() {
        // A letter with 79 marks under it fits in 80 characters, and drawn, a stack
        // that tall would paint over the lines below it.
        String stack = "\u0316\u0317\u0318\u0319".repeat(20).substring(0, 79);
        assertShown(new String[][]{
            {"a" + stack, "a\u0316\u0317"}, {"vpn" + stack + ".example", "vpn\u0316\u0317.example"},
            {"a" + "\u0336".repeat(20) + "b" + "\u0301".repeat(3), "a\u0336\u0336b\u0301\u0301"},
            // As many as Vietnamese stacks on a letter. A Tibetan stack can have more,
            // and is shown with its first two; what is saved keeps them all.
            {"Vie\u0323\u0302t", "Vie\u0323\u0302t"}, {"\u0F66\u0F92\u0FB2\u0F74\u0F56", "\u0F66\u0F92\u0FB2\u0F56"},
            // None with nothing to sit on, and none that encloses its character.
            {"\u0301x", "x"}, {"a \u0301b", "a b"}, {" \uFE0F", " "}, {"1\u20DD", "1"},
            // What is dropped between marks leaves them on the same character.
            {"a\u0301\u0301\u200B\u0301\u3164\u0301\u2028\u0301", "a\u0301\u0301"}});
    }

    @Test public void itIsCutTo80Characters() {
        String lock = "\uD83D\uDD12";
        assertShown(new String[][]{
            {"x".repeat(80), "x".repeat(80)}, {"x".repeat(81), "x".repeat(79) + "…"},
            // An emoji counts once, and what is dropped not at all.
            {lock.repeat(80), lock.repeat(80)}, {lock.repeat(81), lock.repeat(79) + "…"},
            {"\u2028\u202E".repeat(100) + "y".repeat(80), "y".repeat(80)}});
    }

    // qml/Main.qml's elideMiddle(s, n): a name that fits is kept; a longer one keeps both its ends,
    // the head one longer when n - 1 is odd, and an ellipsis between. The delete confirmation uses 36.
    @Test public void aLongNameKeepsBothEndsWhenItIsElidedInTheMiddle() {
        assertEquals("short", ShownText.elideMiddle("short", 36));
        assertEquals("x".repeat(36), ShownText.elideMiddle("x".repeat(36), 36));
        // n = 36 keeps 35: 18 before the ellipsis and 17 after it.
        assertEquals("a".repeat(18) + "…" + "c".repeat(17), ShownText.elideMiddle("a".repeat(18) + "b".repeat(10) + "c".repeat(17), 36));
        assertEquals(36, ShownText.elideMiddle("x".repeat(500), 36).length());
        // n = 6 keeps 5: 3 and 2.
        assertEquals("abc…ij", ShownText.elideMiddle("abcdefghij", 6));
        assertEquals("…", ShownText.elideMiddle("abc", 1));
        // An emoji counts once, and is never cut in half.
        String lock = "\uD83D\uDD12";
        assertEquals(lock.repeat(18) + "…" + lock.repeat(17), ShownText.elideMiddle(lock.repeat(60), 36));
    }
}
