package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.List;

// The log's lines (LogLines) and the file they go in (LogFile): written as the desktop writes them, shown as it shows
// them, an old line shown as it was, and the file kept to its tail.
public final class LogLinesTest {
    private static final ZoneId UTC = ZoneId.of("UTC");
    private static final ZoneId MOSCOW = ZoneId.of("Europe/Moscow");
    // 2026-10-27 12:34:56 UTC.
    private static final long NOON = 1_793_104_496_000L;

    private static File temp() throws Exception {
        File dir = Files.createTempDirectory("logfile").toFile();
        dir.deleteOnExit();
        File file = new File(dir, LogFile.NAME);
        file.deleteOnExit();
        return file;
    }

    // Writing.

    @Test public void aLineIsDateTimeLevelAndMessageSeparatedByTabs() {
        assertEquals("2026-10-27 12:34:56\tINFO\tTrustTunnel: starting connection", LogLines.entry(NOON, UTC, LogLines.INFO, "TrustTunnel: starting connection"));
        assertEquals("2026-10-27 15:34:56\tWARN\tx", LogLines.entry(NOON, MOSCOW, LogLines.WARN, "x"));
        assertEquals("2026-10-27 12:34:56\tERROR\tCould not start VPN", LogLines.entry(NOON, UTC, LogLines.ERROR, "Could not start VPN"));
    }

    @Test public void theTimeIsWrittenWithAsciiDigitsWhateverTheLocale() {
        java.util.Locale before = java.util.Locale.getDefault();
        try {
            java.util.Locale.setDefault(new java.util.Locale("ar", "SA"));
            assertEquals("2026-10-27 12:34:56\tINFO\tx", LogLines.entry(NOON, UTC, LogLines.INFO, "x"));
        } finally {
            java.util.Locale.setDefault(before);
        }
    }

    @Test public void aMessageThatHoldsSeveralLinesIsOneLine() {
        assertEquals("2026-10-27 12:34:56\tINFO\tone two three four", LogLines.entry(NOON, UTC, LogLines.INFO, "one\ntwo\r\nthree\tfour"));
        // So that no message can pass for a second entry.
        String forged = LogLines.entry(NOON, UTC, LogLines.INFO, "a\n2026-10-27 12:00:00\tERROR\tforged");
        assertFalse(forged.contains("\n"));
        assertEquals(2, forged.split("\t", -1).length - 1);
    }

    @Test public void everyControlCharacterAndLineSeparatorIsASpace() {
        assertEquals("a b c d e f g", LogLines.oneLine("a\u0000b\u0007c\u007fd\u2028e\u2029f\u0085g"));
        // A run of them is one space, and the spaces that were there are kept.
        assertEquals("a b  c", LogLines.oneLine("a\r\n\r\nb  c"));
        assertEquals("kept: ü, 日本, 😀", LogLines.oneLine("kept: ü, 日本, 😀"));
    }

    @Test public void whatBreaksALineAtEitherEndLeavesNoSpaceThere() {
        assertEquals("abc", LogLines.oneLine("\nabc\r\n"));
        assertEquals("abc", LogLines.oneLine("\t\tabc\t "));
        assertEquals("", LogLines.oneLine("\n\n"));
        assertEquals("", LogLines.oneLine(""));
    }

    @Test public void theLimitsAreTheseNumbers() {
        assertEquals(2000, LogLines.MAX_MESSAGE);
        assertEquals(1000, LogLines.SHOWN_LINES);
        assertEquals(256 * 1024, LogFile.MAX_BYTES);
        assertEquals(128 * 1024, LogFile.KEEP_BYTES);
        assertEquals("freetunnel.log", LogFile.NAME);
    }

    @Test public void aMessageIsCutWhereItIsTooLongToBeALine() {
        StringBuilder long5000 = new StringBuilder();
        for (int i = 0; i < 5000; i++) long5000.append('x');
        String line = LogLines.oneLine(long5000.toString());
        assertEquals(LogLines.MAX_MESSAGE + 1, line.length());
        assertTrue(line.endsWith("x…"));
        // One of exactly the limit is not cut.
        assertEquals(LogLines.MAX_MESSAGE, LogLines.oneLine(long5000.substring(0, LogLines.MAX_MESSAGE)).length());
    }

    // Showing.

    @Test public void aWrittenLineIsShownAsTimeLevelAndMessage() {
        assertEquals("12:34:56 INFO TrustTunnel: starting connection", LogLines.shown("2026-10-27 12:34:56\tINFO\tTrustTunnel: starting connection"));
        assertEquals("01:02:03 WARN a b", LogLines.shown("2026-01-02 01:02:03\tWARN\ta b"));
        // A tab inside the message, which the desktop's file allows, is kept: the first two split it.
        assertEquals("01:02:03 INFO a\tb", LogLines.shown("2026-01-02 01:02:03\tINFO\ta\tb"));
    }

    @Test public void anOldLineIsShownAsItIs() {
        assertEquals("1790000000000 VPN already running or reconnecting; start ignored", LogLines.shown("1790000000000 VPN already running or reconnecting; start ignored"));
        assertEquals("anything else", LogLines.shown("anything else"));
        assertEquals("", LogLines.shown(""));
    }

    @Test public void aLineThatOnlyLooksLikeOneIsShownAsItIs() {
        for (String line : new String[] {"2026-10-27 12:34:56 INFO spaces", "2026-10-27 12:34:56\tinfo\tlower", "26-10-27 12:34:56\tINFO\tshort year",
                "2026-10-27 12:34\tINFO\tno seconds", "2026-10-27 12:34:56\tINFO"})
            assertEquals(line, LogLines.shown(line));
    }

    @Test public void theShownTextIsTheLastLinesEachShownOneToALine() {
        String file = "1790000000000 old\n2026-10-27 12:00:00\tINFO\tone\n\n2026-10-27 12:00:01\tWARN\ttwo\r\n   \n2026-10-27 12:00:02\tERROR\tthree\n";
        assertEquals("1790000000000 old\n12:00:00 INFO one\n12:00:01 WARN two\n12:00:02 ERROR three", LogLines.shownText(file, 10));
        assertEquals("12:00:01 WARN two\n12:00:02 ERROR three", LogLines.shownText(file, 2));
        assertEquals("12:00:02 ERROR three", LogLines.shownText(file, 1));
        assertEquals("", LogLines.shownText(file, 0));
        assertEquals("", LogLines.shownText("", 5));
        assertEquals("", LogLines.shownText("\n\n", 5));
    }

    @Test public void thePageShowsAThousandLines() {
        StringBuilder file = new StringBuilder();
        for (int i = 0; i < 1500; i++) file.append("2026-10-27 12:00:00\tINFO\tline ").append(i).append('\n');
        String[] lines = LogLines.shownText(file.toString(), LogLines.SHOWN_LINES).split("\n");
        assertEquals(1000, lines.length);
        assertEquals("12:00:00 INFO line 500", lines[0]);
        assertEquals("12:00:00 INFO line 1499", lines[999]);
    }

    // The file.

    @Test public void entriesAreAppendedAsLinesInOrder() throws Exception {
        File file = temp();
        assertEquals("", LogFile.read(file));
        assertEquals(0, LogFile.stamp(file));
        LogFile.append(file, "one");
        long first = LogFile.stamp(file);
        LogFile.append(file, "two");
        assertEquals("one\ntwo\n", LogFile.read(file));
        assertTrue("a change shows in the stamp", first != LogFile.stamp(file));
    }

    @Test public void theStampFollowsTheTimeOfTheLastChangeAsWellAsTheLength() throws Exception {
        File file = temp();
        LogFile.append(file, "same");
        long before = LogFile.stamp(file);
        assertTrue(file.setLastModified(file.lastModified() + 5000));
        assertTrue("the same length, changed later", before != LogFile.stamp(file));
    }

    @Test public void clearingRemovesTheFileAndAppendingMakesItAgain() throws Exception {
        File file = temp();
        LogFile.append(file, "one");
        LogFile.clear(file);
        assertFalse(file.exists());
        assertEquals("", LogFile.read(file));
        LogFile.clear(file);
        LogFile.append(file, "two");
        assertEquals("two\n", LogFile.read(file));
    }

    @Test public void aFileThatPassesItsLimitIsCutToItsTailAtAWholeLine() throws Exception {
        File file = temp();
        StringBuilder text = new StringBuilder();
        String padding = "";
        for (int i = 0; i < 90; i++) padding += "p";
        int lines = 0;
        while (text.length() <= LogFile.MAX_BYTES) { text.append("line ").append(lines++).append(' ').append(padding).append('\n'); }
        Files.write(file.toPath(), text.toString().getBytes(StandardCharsets.UTF_8));
        LogFile.append(file, "the one that cuts it");
        String kept = LogFile.read(file);
        // What was kept of the old lines is at most KEEP_BYTES and over it by less than a line (about 100 bytes), and then the new entry (21).
        assertTrue("cut to about " + LogFile.KEEP_BYTES + ", is " + kept.length(), kept.length() <= LogFile.KEEP_BYTES + 21);
        assertTrue("and not cut more than a line further: " + kept.length(), kept.length() > LogFile.KEEP_BYTES - 120);
        assertTrue("starts at a line", kept.startsWith("line "));
        assertTrue("ends with the new entry", kept.endsWith("pp\nthe one that cuts it\n"));
        assertTrue("the newest lines are kept", kept.contains("line " + (lines - 1) + " "));
        assertFalse("the oldest are not", kept.contains("line 0 "));
        // Every kept line is whole.
        for (String line : kept.split("\n")) assertTrue(line, line.startsWith("line ") && line.endsWith("pp") || line.equals("the one that cuts it"));
    }

    @Test public void aFileJustUnderItsLimitIsNotCutEitherAndOneJustOverIs() throws Exception {
        File file = temp();
        StringBuilder text = new StringBuilder();
        while (text.length() < LogFile.MAX_BYTES - 1000) text.append("a line of a log file, kept whole\n");
        Files.write(file.toPath(), text.toString().getBytes(StandardCharsets.UTF_8));
        long before = file.length();
        LogFile.append(file, "x");
        assertEquals("nothing was cut", before + 2, file.length());
        while (file.length() <= LogFile.MAX_BYTES) LogFile.append(file, "padding padding padding padding");
        LogFile.append(file, "last");
        assertTrue("cut once it passed the limit: " + file.length(), file.length() < LogFile.MAX_BYTES - 100000);
    }

    @Test public void aDirectoryIsNoLogToRead() throws Exception {
        File dir = Files.createTempDirectory("alogdir").toFile();
        dir.deleteOnExit();
        assertEquals("", LogFile.read(dir));
        assertEquals(0, LogFile.stamp(dir));
    }

    @Test public void aFileUnderItsLimitIsNeverCut() throws Exception {
        File file = temp();
        for (int i = 0; i < 300; i++) LogFile.append(file, "entry " + i);
        List<String> kept = new ArrayList<>(java.util.Arrays.asList(LogFile.read(file).split("\n")));
        assertEquals(300, kept.size());
        assertEquals("entry 0", kept.get(0));
    }

    @Test public void multiByteTextSurvivesTheCut() throws Exception {
        File file = temp();
        StringBuilder text = new StringBuilder();
        while (text.length() <= LogFile.MAX_BYTES) text.append("日本語のログ — ü — 😀\n");
        Files.write(file.toPath(), text.toString().getBytes(StandardCharsets.UTF_8));
        LogFile.append(file, "end");
        String kept = LogFile.read(file);
        assertFalse("no broken character", kept.contains("�"));
        assertTrue(kept.startsWith("日本語のログ"));
    }

    @Test public void aLineLongerThanTheTailLeavesAnEmptyFileRatherThanHalfALine() throws Exception {
        File file = temp();
        // One huge line, no break in it, then the new entry: nothing of it is half kept.
        StringBuilder big = new StringBuilder();
        while (big.length() <= LogFile.MAX_BYTES) big.append('x');
        Files.write(file.toPath(), big.toString().getBytes(StandardCharsets.UTF_8));
        LogFile.append(file, "end");
        assertEquals("end\n", LogFile.read(file));
    }

    @Test public void writingToAPlaceThatCannotBeWrittenNeverThrows() throws Exception {
        File dir = Files.createTempDirectory("nolog").toFile();
        dir.deleteOnExit();
        LogFile.append(new File(dir, "missing/dir/freetunnel.log"), "x");
        LogFile.append(dir, "a directory is no file");
        assertEquals("", LogFile.read(new File(dir, "missing")));
    }
}
