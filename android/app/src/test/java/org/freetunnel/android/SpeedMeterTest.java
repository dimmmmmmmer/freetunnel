package org.freetunnel.android;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

// The two tiles' speeds, from counters read once a second.
public final class SpeedMeterTest {
    @Test public void theFirstReadingIsOnlyAStartAndTheSpeedsAreZero() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(5_000_000, 1_000_000, 10_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
        assertEquals(0, meter.upBytesPerSecond(), 0);
    }

    @Test public void aSpeedIsWhatACounterGainedOverTheTimeBetweenReadings() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(0, 0, 0);
        meter.sample(1_200_000, 400_000, 1_000);
        assertEquals(1_200_000, meter.downBytesPerSecond(), 0.001);
        assertEquals(400_000, meter.upBytesPerSecond(), 0.001);
        // A late reading: the same bytes over two seconds are half the speed.
        meter.sample(2_400_000, 400_000, 3_000);
        assertEquals(600_000, meter.downBytesPerSecond(), 0.001);
        assertEquals(0, meter.upBytesPerSecond(), 0.001);
    }

    @Test public void theNumberIsMegabytesWithOneDecimalAndAPointInEveryLanguage() {
        assertEquals("0.0", SpeedMeter.megabytes(0));
        assertEquals("1.2", SpeedMeter.megabytes(1_200_000));
        assertEquals("0.4", SpeedMeter.megabytes(400_000));
        assertEquals("12.5", SpeedMeter.megabytes(12_500_000));
        java.util.Locale before = java.util.Locale.getDefault();
        try {
            java.util.Locale.setDefault(new java.util.Locale("ru", "RU"));
            assertEquals("1.2", SpeedMeter.megabytes(1_200_000));
        } finally {
            java.util.Locale.setDefault(before);
        }
    }

    @Test public void aCounterTheDeviceDoesNotKeepGivesZeroAndStartsAgainWhenItReturns() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(0, 0, 0);
        meter.sample(1_000_000, 1_000_000, 1_000);
        meter.sample(SpeedMeter.UNSUPPORTED, SpeedMeter.UNSUPPORTED, 2_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
        assertEquals(0, meter.upBytesPerSecond(), 0);
        // The reading after that is a start, not a speed over a gap nobody measured.
        meter.sample(9_000_000, 9_000_000, 3_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
        meter.sample(9_500_000, 9_100_000, 4_000);
        assertEquals(500_000, meter.downBytesPerSecond(), 0.001);
        assertEquals(100_000, meter.upBytesPerSecond(), 0.001);
    }

    @Test public void aDeviceThatKeepsOnlyOneOfTheTwoCountersGivesNeitherSpeed() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(0, 0, 0);
        meter.sample(1_000_000, 1_000_000, 1_000);
        meter.sample(2_000_000, SpeedMeter.UNSUPPORTED, 2_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
        assertEquals(0, meter.upBytesPerSecond(), 0);
        meter.sample(SpeedMeter.UNSUPPORTED, 3_000_000, 3_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
        assertEquals(0, meter.upBytesPerSecond(), 0);
    }

    @Test public void aCounterThatWentBackIsZeroNotNegative() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(5_000_000, 5_000_000, 0);
        meter.sample(100, 100, 1_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
        assertEquals(0, meter.upBytesPerSecond(), 0);
        meter.sample(1_000_100, 500_100, 2_000);
        assertEquals(1_000_000, meter.downBytesPerSecond(), 0.001);
        assertEquals(500_000, meter.upBytesPerSecond(), 0.001);
    }

    @Test public void resetForgetsEverything() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(0, 0, 0);
        meter.sample(1_000_000, 1_000_000, 1_000);
        meter.reset();
        assertEquals(0, meter.downBytesPerSecond(), 0);
        meter.sample(5_000_000, 5_000_000, 2_000);
        assertEquals("The first reading after a reset is a start", 0, meter.downBytesPerSecond(), 0);
    }

    @Test public void twoReadingsAtTheSameInstantChangeNothingBadly() {
        SpeedMeter meter = new SpeedMeter();
        meter.sample(0, 0, 1_000);
        meter.sample(1_000_000, 1_000_000, 2_000);
        meter.sample(2_000_000, 2_000_000, 2_000);
        assertEquals(0, meter.downBytesPerSecond(), 0);
    }
}
