package org.freetunnel.android

import java.net.InetAddress
import org.junit.Assert.*
import org.junit.Test

class AppConnectionRoutingTest {
    @Test fun onlySelectedUidsOverrideDestinationRules() {
        assertEquals(AppConnectionRouting.VPN, AppConnectionRouting.decide(10001, setOf(10001)))
        assertEquals(AppConnectionRouting.DEFAULT, AppConnectionRouting.decide(10002, setOf(10001)))
        assertEquals(AppConnectionRouting.REJECT, AppConnectionRouting.decide(-1, setOf(10001)))
        assertEquals(AppConnectionRouting.DEFAULT, AppConnectionRouting.decide(-1, emptySet()))
    }

    private fun row(local: String, remote: String, uid: Int) =
        "0: $local $remote 01 00000000:00000000 00:00000000 00000000 $uid 0 12345"
    private fun ip(value: String) = InetAddress.getByName(value).address

    @Test fun legacyLookupUsesBothEndpointsAndDoesNotConfuseSameLocalPorts() {
        val rows = listOf(row("0100007F:1234", "010200C0:01BB", 10001),
                          row("0100007F:1234", "020200C0:01BB", 10002))
        assertEquals(10002, AppConnectionRouting.legacyOwner(rows, ip("127.0.0.1"), 0x1234, ip("192.0.2.2"), 443))
        assertEquals(-1, AppConnectionRouting.legacyOwner(rows, ip("127.0.0.1"), 0x1234, ip("192.0.2.3"), 443))
    }

    @Test fun legacyUdpWildcardAndConnectedSocketsHaveStablePriority() {
        val rows = listOf(row("00000000:1234", "00000000:0000", 10001),
                          row("0100007F:1234", "020200C0:01BB", 10002))
        for (order in listOf(rows, rows.reversed())) {
            assertEquals(10002, AppConnectionRouting.legacyOwner(order, ip("127.0.0.1"), 0x1234, ip("192.0.2.2"), 443))
            assertEquals(10001, AppConnectionRouting.legacyOwner(order, ip("127.0.0.1"), 0x1234, ip("192.0.2.3"), 443))
        }
    }

    @Test fun legacyIpv6UsesHostOrderWords() {
        val rows = listOf(row("00000000000000000000000001000000:1234",
            "B80D0120000000000000000001000000:01BB", 10001))
        assertEquals(10001, AppConnectionRouting.legacyOwner(rows, ip("::1"), 0x1234, ip("2001:db8::1"), 443))
    }

    @Test fun closedSocketsCannotAnswerForAReusedTuple() {
        val closed = row("0100007F:1234", "020200C0:01BB", 0).replace("12345", "0")
        val live = row("0100007F:1234", "020200C0:01BB", 10001)
        assertEquals(10001, AppConnectionRouting.legacyOwner(listOf(closed, live),
            ip("127.0.0.1"), 0x1234, ip("192.0.2.2"), 443))
    }

    @Test fun ambiguousAndMalformedLegacyRowsAreNotGuessed() {
        val rows = listOf("invalid", row("00000000:1234", "00000000:0000", 10001),
                          row("00000000:1234", "00000000:0000", 10002))
        assertEquals(-1, AppConnectionRouting.legacyOwner(rows, ip("127.0.0.1"), 0x1234, ip("192.0.2.2"), 443))
    }
}
