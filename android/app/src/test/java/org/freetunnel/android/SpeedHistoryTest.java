package org.freetunnel.android;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class SpeedHistoryTest {
    @Test public void keepsTheLastMinuteOldestFirst() {
        SpeedHistory h = new SpeedHistory();
        for (int i = 0; i < SpeedHistory.CAPACITY + 5; i++) h.add(i, -i);
        assertEquals(SpeedHistory.CAPACITY, h.size());
        assertEquals(5, h.down(0), 0);
        assertEquals(SpeedHistory.CAPACITY + 4, h.down(SpeedHistory.CAPACITY - 1), 0);
        assertEquals("A negative speed is 0", 0, h.up(3), 0);
    }

    @Test public void theScaleIsTheHighestSpeedAndNeverBelowTheFloor() {
        SpeedHistory h = new SpeedHistory();
        assertEquals(SpeedHistory.FLOOR, h.scale(), 0);
        h.add(100, 200);
        assertEquals(SpeedHistory.FLOOR, h.scale(), 0);
        h.add(10, 2_000_000);
        h.add(1_000_000, 0);
        assertEquals(2_000_000, h.scale(), 0);
    }

    @Test public void clearStartsAfresh() {
        SpeedHistory h = new SpeedHistory();
        h.add(1, 1);
        h.clear();
        assertEquals(0, h.size());
        h.add(7, 8);
        assertEquals(7, h.down(0), 0);
        assertEquals(8, h.up(0), 0);
    }
}
