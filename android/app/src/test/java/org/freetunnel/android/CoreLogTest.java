package org.freetunnel.android;
import org.junit.Test;
import java.time.ZoneOffset;
import static org.junit.Assert.*;
public class CoreLogTest {
    @Test public void coreKeepsItsTimestampAndWarningLevelWithoutDoublePrefixes() {
        String line = LogLines.coreEntry(0, ZoneOffset.UTC, "INFO", "12.11.2026 10:20:30.123 WARNING retrying");
        assertEquals("2026-11-12 10:20:30\tWARN\tretrying", line);
        assertEquals("10:20:30 WARN retrying", LogLines.shown(line));
    }
    @Test public void plainCoreMessagesCannotForgeExtraEntries() {
        assertEquals("1970-01-01 00:00:00\tCORE\tfirst second", LogLines.coreEntry(0, ZoneOffset.UTC, "DEBUG", "first\n\tsecond"));
        assertTrue(LogLines.coreEntry(0, ZoneOffset.UTC, "ERROR", "failed").contains("\tERROR\t"));
    }
}
