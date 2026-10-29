package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

// Home's timer, in the desktop's form, and the moments a ticker draws it at.
public final class SessionTimeTest {
    @Test public void showsHoursMinutesAndSecondsInTwoDigitsEach() {
        assertEquals("00:00:00", SessionTime.format(0));
        assertEquals("00:00:01", SessionTime.format(1_000));
        assertEquals("00:00:59", SessionTime.format(59_999));
        assertEquals("00:01:00", SessionTime.format(60_000));
        assertEquals("01:02:03", SessionTime.format(((1 * 60 + 2) * 60 + 3) * 1000L + 500));
        assertEquals("10:00:00", SessionTime.format(10 * 3_600_000L));
        assertEquals("100:00:00", SessionTime.format(100 * 3_600_000L));
    }

    @Test public void aClockThatWentBackShowsZeroNotANegativeTime() {
        assertEquals("00:00:00", SessionTime.format(-5_000));
    }

    @Test public void theTickerWakesJustAfterTheNextWholeSecond() {
        for (long elapsed : new long[]{0, 1, 499, 500, 999, 1_000, 61_234}) {
            long wait = SessionTime.untilNextSecond(elapsed);
            assertTrue("elapsed " + elapsed + " waits " + wait, wait > 0 && wait <= 1_015);
            assertEquals("It lands in the next second: " + elapsed, elapsed / 1000 + 1, (elapsed + wait) / 1000);
            // After the boundary, not on it, where a clock a hair slow would draw the old second again.
            assertTrue("It lands just after the boundary: " + elapsed, (elapsed + wait) % 1000 > 0);
        }
    }
}
