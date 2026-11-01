package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import org.junit.Test;

public final class ConnectionLogTest {
    @Test public void coreJsonUsesDesktopActionAndDomain() {
        for (String action : new String[] {"tunnel", "bypass", "reject", "unknown"}) {
            assertEquals(action + " example.com", ConnectionLog.format("{\"action\":\"" + action
                    + "\",\"domain\":\"example.com\",\"dst\":\"203.0.113.7:443\",\"proto\":\"TCP\","
                    + "\"src\":\"192.0.2.1:1234\",\"date\":\"2026-11-01T12:34:56Z\"}"));
        }
    }

    @Test public void addressOnlyAndMissingDestinationsMatchDesktop() {
        assertEquals("bypass 203.0.113.7:443", ConnectionLog.format("{\"action\":\"bypass\",\"dst\":\"203.0.113.7:443\"}"));
        assertEquals("tunnel [2001:db8::1]:443", ConnectionLog.format("{\"action\":\"tunnel\",\"domain\":\"\",\"dst\":\"[2001:db8::1]:443\"}"));
        assertEquals("reject unknown destination", ConnectionLog.format("{\"action\":\"reject\",\"domain\":null,\"dst\":null}"));
        assertEquals("unknown unknown destination", ConnectionLog.format("{\"action\":\"future\",\"domain\":123,\"dst\":{}}"));
    }

    @Test public void otherMessagesRemainAvailableForDiagnostics() {
        for (String raw : new String[] {"", "tunnel example.com", "{broken", "{}", "{\"error\":\"failed\"}",
                "{\"action\":null}", "{\"action\":12}"}) {
            assertEquals(raw, ConnectionLog.format(raw));
        }
    }

    @Test public void oldJsonEntriesAndNewEntriesHaveTheSameDisplayAndCopyText() {
        String json = "{\"action\":\"tunnel\",\"domain\":\"example.com\",\"proto\":\"TCP\"}";
        String old = "2026-11-01 12:34:56\tINFO\t" + json;
        String current = "2026-11-01 12:34:56\tINFO\ttunnel example.com";
        assertEquals("12:34:56 INFO tunnel example.com", LogLines.shownText(old, 1000));
        assertEquals(LogLines.shownText(current, 1000), LogLines.shownText(old, 1000));
        assertEquals("12:34:56 ERROR " + json, LogLines.shown(old.replace("\tINFO\t", "\tERROR\t")));
    }

    @Test public void decodedControlCharactersCannotForgeLogEntries() {
        String json = "{\"action\":\"tunnel\",\"domain\":\"one\\nERROR\\tforged\"}";
        assertEquals("12:34:56 INFO tunnel one ERROR forged", LogLines.shown("2026-11-01 12:34:56\tINFO\t" + json));
        String entry = LogLines.entry(0, java.time.ZoneId.of("UTC"), LogLines.INFO, ConnectionLog.format(json));
        assertEquals("1970-01-01 00:00:00\tINFO\ttunnel one ERROR forged", entry);
    }
}
