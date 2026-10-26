package org.freetunnel.android

import java.util.Random

/**
 * Delays between automatic reconnect attempts. It mirrors the desktop client's
 * QtTrustTunnelClient::scheduleReconnect (src/vpn/qt_trusttunnel_client.cpp):
 * 1 s, doubling to 30 s, each delay ±20 % and never under 250 ms. The jitter
 * keeps phones that lost the same server from all retrying at once.
 */
class ReconnectBackoff(
    private val initialMs: Long = 1_000,
    private val maxMs: Long = 30_000,
    private val random: Random = Random(),
) {
    private var baseMs = initialMs

    fun next(): Long {
        val jitter = baseMs / 5
        val delay = baseMs - jitter + (random.nextDouble() * (2 * jitter + 1)).toLong()
        baseMs = minOf(baseMs * 2, maxMs)
        return maxOf(250L, delay)
    }

    fun reset() {
        baseMs = initialMs
    }
}
