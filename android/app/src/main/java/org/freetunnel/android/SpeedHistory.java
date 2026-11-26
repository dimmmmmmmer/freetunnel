package org.freetunnel.android;

/**
 * The last {@link #CAPACITY} speeds of the tunnel, one a second, for the small graph over Home's speed tiles: what came in and what
 * went out, oldest first. A new reading pushes the oldest out. The graph's scale is the highest speed in it, of either direction, and
 * never less than {@link #FLOOR}, so that a few bytes of keep-alive do not fill it.
 *
 * <p>No Android classes: a plain JVM can load and test it.
 */
final class SpeedHistory {
    static final int CAPACITY = 60;
    /** 64 KB/s: the least the graph's top stands for. */
    static final double FLOOR = 64_000;

    private final double[] down = new double[CAPACITY];
    private final double[] up = new double[CAPACITY];
    private int start;
    private int size;

    void clear() { start = 0; size = 0; }

    void add(double downBytesPerSecond, double upBytesPerSecond) {
        int at = (start + size) % CAPACITY;
        down[at] = Math.max(0, downBytesPerSecond);
        up[at] = Math.max(0, upBytesPerSecond);
        if (size < CAPACITY) size++;
        else start = (start + 1) % CAPACITY;
    }

    int size() { return size; }
    /** The i-th reading, 0 the oldest. */
    double down(int i) { return down[(start + i) % CAPACITY]; }
    double up(int i) { return up[(start + i) % CAPACITY]; }

    double scale() {
        double top = FLOOR;
        for (int i = 0; i < size; i++) top = Math.max(top, Math.max(down(i), up(i)));
        return top;
    }
}
