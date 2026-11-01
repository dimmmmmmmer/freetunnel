package org.freetunnel.android;

import org.junit.Test;
import java.util.concurrent.TimeUnit;
import static org.junit.Assert.*;

public class GeoUpdateScheduleTest {
    @Test public void refreshUsesTheSelectedInterval() {
        long now = TimeUnit.DAYS.toMillis(30);
        for (int hours : new int[]{1, 2, 5, 24}) {
            long age = TimeUnit.HOURS.toMillis(hours);
            assertFalse(GeoUpdateSchedule.due(now - age + 1, now, hours));
            assertTrue(GeoUpdateSchedule.due(now - age, now, hours));
            assertFalse(GeoUpdateSchedule.due(now + age, now, hours));
        }
    }
    @Test public void neverKeepsTheCacheButAllowsInitialDownload() {
        assertFalse(GeoUpdateSchedule.due(0, TimeUnit.DAYS.toMillis(30), 0));
        assertTrue(GeoUpdateSchedule.due(-1, 0, 0));
    }
    @Test public void invalidSavedIntervalsUseTheDailyDefault() {
        assertEquals(24, GeoUpdateSchedule.normalizedHours(-1));
        assertEquals(24, GeoUpdateSchedule.normalizedHours(3));
        assertEquals(0, GeoUpdateSchedule.normalizedHours(0));
    }
}
