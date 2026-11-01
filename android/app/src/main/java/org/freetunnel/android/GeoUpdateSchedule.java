package org.freetunnel.android;

import java.util.concurrent.TimeUnit;

final class GeoUpdateSchedule {
    private GeoUpdateSchedule() {}
    static int normalizedHours(int hours) {
        return hours == 0 || hours == 1 || hours == 2 || hours == 5 || hours == 24 ? hours : 24;
    }
    static boolean due(long updatedMillis, long nowMillis, int hours) {
        if (updatedMillis < 0) return true; // Initial download also works with automatic updates off.
        int interval = normalizedHours(hours);
        return interval > 0 && nowMillis - updatedMillis >= TimeUnit.HOURS.toMillis(interval);
    }
}
