package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.net.InetAddress;
import java.net.ServerSocket;

// The probe the Configs page's ping runs for each config: an address split as the desktop splits
// it, the time of a TCP handshake, and the same dash for every way of failing.
public final class PingProbeTest {
    private static void port(String address, String host, int port) {
        PingProbe.Target target = PingProbe.target(address);
        assertNotNull(address, target);
        assertEquals(address, host, target.host);
        assertEquals(address, port, target.port);
    }

    @Test public void splitsAHostAndAPortAsTheDesktopDoes() {
        port("vpn.example.com:443", "vpn.example.com", 443);
        port("203.0.113.10:8443", "203.0.113.10", 8443);
        port("[2001:db8::1]:443", "2001:db8::1", 443);
        port("  192.0.2.1:1  ", "192.0.2.1", 1);
        port("192.0.2.1:65535", "192.0.2.1", 65535);
        // No brackets: the last colon is the port's, which is how the desktop reads it.
        port("::1:443", "::1", 443);
    }

    @Test public void anAddressWithoutAUsablePortIsNoTarget() {
        for (String address : new String[]{null, "", "vpn.example.com", "vpn.example.com:", "vpn.example.com:0", "vpn.example.com:65536",
                "vpn.example.com:http", "vpn.example.com:-1", "vpn.example.com:+443", ":443", "[2001:db8::1]", "[2001:db8::1]443",
                "vpn.example.com:443443"}) {
            assertNull("\"" + address + "\"", PingProbe.target(address));
        }
    }

    @Test public void aListeningPortAnswersWithTheTimeItTook() throws Exception {
        try (ServerSocket server = new ServerSocket(0, 1, InetAddress.getLoopbackAddress())) {
            long millis = PingProbe.probe("127.0.0.1:" + server.getLocalPort(), PingProbe.TIMEOUT_MS);
            assertTrue("took " + millis, millis >= 0 && millis < 3_000);
        }
    }

    @Test public void everyKindOfFailureIsTheSameResult() throws Exception {
        int closed;
        try (ServerSocket server = new ServerSocket(0, 1, InetAddress.getLoopbackAddress())) { closed = server.getLocalPort(); }
        assertEquals("Connection refused", -1, PingProbe.probe("127.0.0.1:" + closed, PingProbe.TIMEOUT_MS));
        assertEquals("No port", -1, PingProbe.probe("127.0.0.1", PingProbe.TIMEOUT_MS));
        assertEquals("No such host", -1, PingProbe.probe("no-such-host.invalid:443", PingProbe.TIMEOUT_MS));
        assertEquals("Nothing", -1, PingProbe.probe(null, PingProbe.TIMEOUT_MS));
    }

    @Test public void theRowShowsTheTimeOrADashAndNothingElse() {
        assertEquals("42 ms", PingProbe.text(42, " ms"));
        assertEquals("0 мс", PingProbe.text(0, " мс"));
        assertEquals("—", PingProbe.text(-1, " ms"));
        assertEquals("…", PingProbe.PENDING);
    }
}
