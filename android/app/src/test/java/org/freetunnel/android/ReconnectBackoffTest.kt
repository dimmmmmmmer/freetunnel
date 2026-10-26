package org.freetunnel.android

import org.junit.Assert.*
import org.junit.Test
import java.util.Random

class ReconnectBackoffTest {
    private fun assertNear(base: Long, delay: Long) =
        assertTrue("$delay is not within 20 % of $base", delay in base - base / 5..base + base / 5)
    // A Random whose every draw is the same fraction of the jitter range.
    private fun fixed(fraction: Double) = object : Random() { override fun nextDouble() = fraction }

    @Test fun doublesFromOneSecondToThirtySecondsWithJitter() {
        val backoff = ReconnectBackoff(random = Random(1))
        for (base in longArrayOf(1_000, 2_000, 4_000, 8_000, 16_000, 30_000, 30_000)) assertNear(base, backoff.next())
    }

    @Test fun jitterSpansTwentyPercentEachWay() {
        // The desktop's bounds exactly: QRandomGenerator::bounded(-jitter, jitter + 1).
        assertEquals(800L, ReconnectBackoff(random = fixed(0.0)).next())
        assertEquals(1_200L, ReconnectBackoff(random = fixed(0.99999999)).next())
        // One generator for all: new Random(n) for consecutive n starts almost alike.
        val random = Random(5)
        val delays = List(200) { ReconnectBackoff(random = random).next() }
        assertTrue(delays.all { it in 800L..1_200L })
        assertTrue("Jitter must spread delays below and above the base", delays.any { it < 900 } && delays.any { it > 1_100 })
    }

    @Test fun resetStartsAgainAtOneSecond() {
        val backoff = ReconnectBackoff(random = Random(2))
        repeat(6) { backoff.next() }
        backoff.reset()
        assertNear(1_000, backoff.next())
        assertNear(2_000, backoff.next())
    }

    @Test fun neverReturnsUnderAQuarterSecond() {
        val backoff = ReconnectBackoff(100, 100, Random(3))
        repeat(50) { assertTrue(backoff.next() >= 250) }
        assertEquals(250L, ReconnectBackoff(100, 100, fixed(0.0)).next())
    }

    @Test fun aLongBackoffStaysLong() {
        // VpnDeviceTest swaps this in to show that a network appearing retries
        // without waiting it out.
        val backoff = ReconnectBackoff(600_000, 600_000, Random(4))
        repeat(5) { assertTrue(backoff.next() in 480_000L..720_000L) }
    }
}
