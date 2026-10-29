package org.freetunnel.android;

import java.util.Locale;

/**
 * The speeds of the two tiles at the foot of Home, from a pair of byte counters read once a
 * second (TrafficStats.getUidRxBytes and getUidTxBytes for this app, which carries the
 * tunnel's own traffic). A rate is what the counter gained since the last reading, over the
 * time between them. A counter a device does not keep (TrafficStats.UNSUPPORTED, -1) or
 * that went back (a reboot of the statistics) gives 0.
 *
 * <p>No Android classes: a plain JVM can load and test it.
 */
final class SpeedMeter {
    static final long UNSUPPORTED = -1;

    private long lastDown = UNSUPPORTED;
    private long lastUp = UNSUPPORTED;
    private long lastAt = -1;
    private double down;
    private double up;

    /** Forgets what was read: the next reading is a new start, and the speeds are 0 until the one after. */
    void reset() {
        lastDown = UNSUPPORTED;
        lastUp = UNSUPPORTED;
        lastAt = -1;
        down = 0;
        up = 0;
    }

    /** Counters in bytes, and the time of the reading in milliseconds, on any clock that only goes forward. */
    void sample(long downBytes, long upBytes, long nowMillis) {
        boolean supported = downBytes >= 0 && upBytes >= 0;
        if (!supported) {
            reset();
            return;
        }
        long span = nowMillis - lastAt;
        if (lastAt >= 0 && lastDown >= 0 && span > 0) {
            down = rate(downBytes - lastDown, span);
            up = rate(upBytes - lastUp, span);
        } else {
            down = 0;
            up = 0;
        }
        lastDown = downBytes;
        lastUp = upBytes;
        lastAt = nowMillis;
    }

    private static double rate(long gained, long spanMillis) {
        return gained <= 0 ? 0 : gained * 1000.0 / spanMillis;
    }

    double downBytesPerSecond() { return down; }
    double upBytesPerSecond() { return up; }

    /** The desktop's number: megabytes (10^6 bytes) a second with one decimal, always with a point. */
    static String megabytes(double bytesPerSecond) {
        return String.format(Locale.ROOT, "%.1f", bytesPerSecond / 1.0e6);
    }
}
