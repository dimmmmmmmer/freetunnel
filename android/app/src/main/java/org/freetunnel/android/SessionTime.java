package org.freetunnel.android;

/**
 * The time a session has run for, as Home's line under the logo shows it. The desktop's
 * (Backend::sessionTime) is hours, minutes and seconds, each of two digits: 00:00:01.
 *
 * <p>No Android classes: a plain JVM can load and test it.
 */
final class SessionTime {
    private SessionTime() { }

    static String format(long elapsedMillis) {
        long seconds = Math.max(0, elapsedMillis) / 1000;
        return two(seconds / 3600) + ":" + two(seconds % 3600 / 60) + ":" + two(seconds % 60);
    }

    /**
     * How long until the line next changes, for a session that has run this long: to just
     * after the next whole second, so that a ticker draws each second once and none twice.
     */
    static long untilNextSecond(long elapsedMillis) {
        long into = Math.max(0, elapsedMillis) % 1000;
        return 1000 - into + 15;
    }

    private static String two(long n) { return n < 10 ? "0" + n : Long.toString(n); }
}
