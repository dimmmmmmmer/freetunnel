package org.freetunnel.android;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

/**
 * A pinned subscription fetch against a real TLS server on this machine (TestTls): the name in the address does not
 * resolve (.invalid never does), so every connection that works went to the config's address, as the desktop's does.
 * The desktop's own rows are in tests/test_subscription_fetcher.cpp; these hold Android to them.
 */
public class PinnedFetchTest {
    private static final String NAME = "sub.test.invalid";
    private static final String URL = "https://alice:s3cret@" + NAME + "/s";
    private static final String BODY = "{\"version\":1}";
    // Well over what a fetch on this machine takes, well under a hung build: only the deadline tests are short.
    private static final int TIMEOUT_MS = 5000;

    private static final SubscriptionData.Connections NO_SYSTEM = uri -> { throw new AssertionError("the system's connection was used"); };
    private static final DialerCount NEVER = new DialerCount();

    private static final class DialerCount implements PinnedHttps.Dialer {
        final AtomicInteger calls = new AtomicInteger();
        @Override public java.net.Socket connect(String host, int port, int timeoutMs) throws IOException {
            calls.incrementAndGet();
            throw new IOException("dialed");
        }
    }

    private static SubscriptionData.Request pinned(String url, String name, TestTls.Server server, String pem) {
        SubscriptionData.Request request = SubscriptionData.requestFor(url,
                new SubscriptionData.Trust(name, "127.0.0.1:" + server.port(), pem));
        assertTrue(request.pinned);
        return request;
    }

    private static byte[] fetch(SubscriptionData.Request request) throws Exception {
        return SubscriptionData.fetch(request, NO_SYSTEM, PinnedHttps.DIRECT, TIMEOUT_MS);
    }

    private static SubscriptionData.FetchException.Failure failure(SubscriptionData.Request request) throws Exception {
        try { fetch(request); }
        catch (SubscriptionData.FetchException e) { return e.failure; }
        fail("the fetch should have been refused");
        return null;
    }

    private static String text(byte[] bytes) { return new String(bytes, StandardCharsets.UTF_8); }

    @Test public void aPinnedFetchGoesToTheConfigsAddressAndNamesTheServer() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.ok(BODY))) {
            assertEquals(BODY, text(fetch(pinned(URL, NAME, server, cert.pem()))));
            assertEquals(1, server.requests.size());
            TestTls.Seen seen = server.requests.get(0);
            assertEquals(NAME, seen.sni);
            assertEquals(NAME, seen.headers.get("host"));
            assertEquals("GET /s HTTP/1.1", seen.requestLine);
            assertEquals("Basic " + Base64.getEncoder().encodeToString("alice:s3cret".getBytes(StandardCharsets.UTF_8)), seen.headers.get("authorization"));
            assertEquals("identity", seen.headers.get("accept-encoding"));
            assertEquals("application/json", seen.headers.get("accept"));
            assertEquals("close", seen.headers.get("connection"));
        }
    }

    @Test public void theAddressesPortBeatsTheUrlsWhileTheHostHeaderKeepsTheUrls() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.ok(BODY))) {
            // The URL says 1111, which nothing listens on: the address says where.
            assertEquals(BODY, text(fetch(pinned("https://" + NAME + ":1111/s?x=%2B1", NAME, server, cert.pem()))));
            TestTls.Seen seen = server.requests.get(0);
            assertEquals(NAME + ":1111", seen.headers.get("host"));
            assertEquals("GET /s?x=%2B1 HTTP/1.1", seen.requestLine);
            assertNull(seen.headers.get("authorization"));
        }
    }

    @Test public void anotherCertificateForTheSameNameIsUntrustedAndGetsNoRequest() throws Exception {
        TestTls.Cert pinnedCert = TestTls.leaf(NAME), served = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(served, (n, seen) -> TestTls.ok(BODY))) {
            assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(pinned(URL, NAME, server, pinnedCert.pem())));
            assertEquals(0, server.requests.size());
        }
    }

    @Test public void aCertificateForAnotherNameIsUntrustedEvenWhenPinned() throws Exception {
        TestTls.Cert cert = TestTls.leaf("other.test.invalid");
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.ok(BODY))) {
            assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(pinned(URL, NAME, server, cert.pem())));
            assertEquals(0, server.requests.size());
        }
    }

    @Test public void aPinnedCertificateOutOfItsDatesIsUntrusted() throws Exception {
        long now = System.currentTimeMillis(), day = 24L * 3600 * 1000;
        TestTls.Cert expired = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).valid(new Date(now - 30 * day), new Date(now - day)));
        TestTls.Cert early = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).valid(new Date(now + day), new Date(now + 30 * day)));
        for (TestTls.Cert cert : new TestTls.Cert[]{expired, early}) {
            try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.ok(BODY))) {
                assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(pinned(URL, NAME, server, cert.pem())));
                assertEquals(0, server.requests.size());
            }
        }
    }

    // The desktop's pinningOnlyTheCaIsNotEnough: the server's own certificate has to be a pin, and a CA that signed it
    // is not, however it is served.
    @Test public void aLeafSignedByAPinnedCaIsUntrustedWithOrWithoutTheCaServedAndGetsNoRequest() throws Exception {
        TestTls.Cert ca = TestTls.make(TestTls.spec().cn("Test CA").ca());
        TestTls.Cert leaf = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).signedBy(ca));
        for (List<X509Certificate> chain : Arrays.asList(Collections.singletonList(leaf.certificate), Arrays.asList(leaf.certificate, ca.certificate))) {
            try (TestTls.Server server = new TestTls.Server(leaf.keys.getPrivate(), chain, (n, seen) -> TestTls.ok(BODY))) {
                assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(pinned(URL, NAME, server, ca.pem())));
                assertEquals(0, server.requests.size());
            }
        }
    }

    @Test public void aLeafIsTrustedWhenTheLeafAndItsCaAreBothPinned() throws Exception {
        TestTls.Cert ca = TestTls.make(TestTls.spec().cn("Test CA").ca());
        TestTls.Cert leaf = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).signedBy(ca));
        try (TestTls.Server server = new TestTls.Server(leaf.keys.getPrivate(), Arrays.asList(leaf.certificate, ca.certificate), (n, seen) -> TestTls.ok(BODY))) {
            assertEquals(BODY, text(fetch(pinned(URL, NAME, server, leaf.pem() + ca.pem()))));
        }
    }

    @Test public void aPinnedLeafIsTrustedWhateverSignedItAndWhetherOrNotItsCaIsServed() throws Exception {
        TestTls.Cert ca = TestTls.make(TestTls.spec().cn("Test CA").ca());
        TestTls.Cert leaf = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).signedBy(ca));
        for (List<X509Certificate> chain : Arrays.asList(Collections.singletonList(leaf.certificate), Arrays.asList(leaf.certificate, ca.certificate))) {
            try (TestTls.Server server = new TestTls.Server(leaf.keys.getPrivate(), chain, (n, seen) -> TestTls.ok(BODY))) {
                assertEquals(BODY, text(fetch(pinned(URL, NAME, server, leaf.pem()))));
            }
        }
    }

    @Test public void aLeafOfAnUnpinnedCaIsUntrusted() throws Exception {
        TestTls.Cert ca = TestTls.make(TestTls.spec().cn("Test CA").ca()), other = TestTls.make(TestTls.spec().cn("Other CA").ca());
        TestTls.Cert leaf = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).signedBy(ca));
        try (TestTls.Server server = new TestTls.Server(leaf.keys.getPrivate(), Arrays.asList(leaf.certificate, ca.certificate), (n, seen) -> TestTls.ok(BODY))) {
            assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(pinned(URL, NAME, server, other.pem())));
            assertEquals(0, server.requests.size());
        }
    }

    @Test public void aCertificateThatIsNotForAServerIsUntrustedAndAnyUsageIsNot() throws Exception {
        TestTls.Cert client = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).eku(TestTls.CLIENT_AUTH));
        try (TestTls.Server server = TestTls.Server.of(client, (n, seen) -> TestTls.ok(BODY))) {
            assertEquals(SubscriptionData.FetchException.Failure.UNTRUSTED, failure(pinned(URL, NAME, server, client.pem())));
        }
        for (String usage : new String[]{TestTls.SERVER_AUTH, TestTls.ANY_USAGE}) {
            TestTls.Cert fine = TestTls.make(TestTls.spec().cn(NAME).dns(NAME).eku(TestTls.CLIENT_AUTH, usage));
            try (TestTls.Server server = TestTls.Server.of(fine, (n, seen) -> TestTls.ok(BODY))) {
                assertEquals(BODY, text(fetch(pinned(URL, NAME, server, fine.pem()))));
            }
        }
    }

    @Test public void certificatesThatCannotBeReadAreBadPinNeverTheSystemsCas() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        // The desktop's rows: 00 00 00, which no certificate has the shape of; the shape of one that nothing can read.
        byte[] shape = {0x30, 0x0b, 0x30, 0x02, 0x05, 0x00, 0x30, 0x02, 0x05, 0x00, 0x03, 0x01, 0x00};
        String garbage = TestTls.pem(new byte[3]), shaped = TestTls.pem(shape);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.ok(BODY))) {
            for (String pem : new String[]{garbage, shaped, garbage + shaped}) {
                SubscriptionData.Request request = pinned(URL, NAME, server, pem);
                assertTrue(request.pins.isEmpty());
                DialerCount dialer = new DialerCount();
                try { SubscriptionData.fetch(request, NO_SYSTEM, dialer, TIMEOUT_MS); fail("should be BAD_PIN"); }
                catch (SubscriptionData.FetchException e) { assertEquals(SubscriptionData.FetchException.Failure.BAD_PIN, e.failure); }
                assertEquals(0, dialer.calls.get());
            }
            assertEquals(0, server.accepted.get());
            // Followed by a certificate that can be read, the one is a pin and the fetch goes through.
            SubscriptionData.Request request = pinned(URL, NAME, server, shaped + cert.pem());
            assertEquals(1, request.pins.size());
            assertArrayEquals(cert.der(), request.pins.get(0).getEncoded());
            assertEquals(BODY, text(fetch(request)));
        }
    }

    // The deadline is a hard limit: a peer that sends a byte now and then keeps every read of the handshake alive.
    @Test public void aServerDrippingTheHandshakeCannotStretchTheDeadline() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (java.net.ServerSocket slow = new java.net.ServerSocket(0, 1, java.net.InetAddress.getByName("127.0.0.1"))) {
            Thread dripper = new Thread(() -> {
                try (java.net.Socket peer = slow.accept()) {
                    // A record header that promises a long body, then the body a byte every 200 ms.
                    peer.getOutputStream().write(new byte[]{0x16, 0x03, 0x03, 0x40, 0x00});
                    for (int i = 0; i < 100; i++) {
                        peer.getOutputStream().write(0);
                        peer.getOutputStream().flush();
                        Thread.sleep(200);
                    }
                } catch (Exception ended) { }
            });
            dripper.setDaemon(true);
            dripper.start();
            SubscriptionData.Request request = SubscriptionData.requestFor(URL, new SubscriptionData.Trust(NAME, "127.0.0.1:" + slow.getLocalPort(), cert.pem()));
            long start = System.nanoTime();
            try { SubscriptionData.fetch(request, NO_SYSTEM, PinnedHttps.DIRECT, 1000); fail("should have timed out"); }
            catch (SubscriptionData.FetchException e) { fail("a slow network is not the certificate's fault"); }
            catch (IOException expected) { }
            long took = (System.nanoTime() - start) / 1000000;
            assertTrue("took " + took + " ms", took < 2000);
        }
    }

    // Once the host is the config's, whatever goes wrong in reading the config fails closed, never to the system's CAs.
    @Test public void aConfigThatCannotBeReadPinsToNothingAndIsBadPin() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        SubscriptionData.Request request = SubscriptionData.requestFor(URL, new SubscriptionData.Trust(NAME, null, cert.pem()));
        assertTrue(request.pinned);
        assertTrue(request.pins.isEmpty());
        DialerCount dialer = new DialerCount();
        try { SubscriptionData.fetch(request, NO_SYSTEM, dialer, TIMEOUT_MS); fail("should be BAD_PIN"); }
        catch (SubscriptionData.FetchException e) { assertEquals(SubscriptionData.FetchException.Failure.BAD_PIN, e.failure); }
        assertEquals(0, dialer.calls.get());
    }

    @Test public void aPinnedRequestRefusesABadAddressBeforeAnythingIsDialed() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        SubscriptionData.Request request = SubscriptionData.requestFor("http://" + NAME + "/s", new SubscriptionData.Trust(NAME, "127.0.0.1:1", cert.pem()));
        DialerCount dialer = new DialerCount();
        try { SubscriptionData.fetch(request, NO_SYSTEM, dialer, TIMEOUT_MS); fail(); }
        catch (SubscriptionData.FetchException e) { fail("an address that is none is not a certificate's fault"); }
        catch (IOException expected) { }
        assertEquals(0, dialer.calls.get());
    }

    @Test public void aRedirectIsFollowedOnANewPinnedConnection() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> n == 1 ? TestTls.redirect("/next?y=2") : TestTls.ok(BODY))) {
            assertEquals(BODY, text(fetch(pinned(URL, NAME, server, cert.pem()))));
            assertEquals(2, server.accepted.get());
            assertEquals(NAME, server.requests.get(0).sni);
            assertEquals(NAME, server.requests.get(1).sni);
            assertEquals("GET /next?y=2 HTTP/1.1", server.requests.get(1).requestLine);
            // The credentials go with each request, as they do on the system's path.
            assertNotNull(server.requests.get(1).headers.get("authorization"));
        }
    }

    @Test public void aRelativeRedirectFromAnEmptyPathKeepsTheSlash() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> n == 1 ? TestTls.redirect("v1/sub") : TestTls.ok(BODY))) {
            assertEquals(BODY, text(fetch(pinned("https://" + NAME, NAME, server, cert.pem()))));
            assertEquals("GET / HTTP/1.1", server.requests.get(0).requestLine);
            assertEquals("GET /v1/sub HTTP/1.1", server.requests.get(1).requestLine);
        }
    }

    @Test public void aRedirectToAnotherOriginIsRefusedAndNothingIsSentThere() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        for (String location : new String[]{"https://other.test.invalid/x", "http://" + NAME + "/x", "https://" + NAME + ":8443/x", "https://bob@" + NAME + "/x"}) {
            try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.redirect(location))) {
                try { fetch(pinned(URL, NAME, server, cert.pem())); fail(location); }
                catch (SubscriptionData.FetchException e) { fail("a refused redirect is not the certificate's fault"); }
                catch (IOException expected) { assertFalse(expected.getMessage().contains("s3cret")); }
                assertEquals(1, server.accepted.get());
            }
        }
    }

    @Test public void threeRedirectsAreFollowedAndTheFourthIsRefused() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> n <= 3 ? TestTls.redirect("/n" + n) : TestTls.ok(BODY))) {
            assertEquals(BODY, text(fetch(pinned(URL, NAME, server, cert.pem()))));
            assertEquals(4, server.accepted.get());
        }
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.redirect("/n" + n))) {
            try { fetch(pinned(URL, NAME, server, cert.pem())); fail(); }
            catch (IOException expected) { assertEquals("Too many redirects", expected.getMessage()); }
            assertEquals(4, server.accepted.get());
        }
    }

    @Test public void otherStatusesAreRefusedWithoutReadingAnythingMore() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        for (int status : new int[]{401, 403, 404, 500}) {
            try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.raw("HTTP/1.1 " + status + " No\r\nConnection: close\r\n\r\nbody"))) {
                try { fetch(pinned(URL, NAME, server, cert.pem())); fail(); }
                catch (IOException expected) { assertEquals("Subscription unavailable", expected.getMessage()); }
            }
        }
    }

    private static String chunk(String data) { return Integer.toHexString(data.length()) + "\r\n" + data + "\r\n"; }

    @Test public void aChunkedBodyIsDecoded() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        String response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\nConnection: close\r\n\r\n"
                + chunk("{\"version\"") + "5;ext=1\r\n:1,\"a\r\n" + chunk("\":2}") + "0\r\nX-Trailer: 1\r\n\r\n";
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.raw(response))) {
            assertEquals("{\"version\":1,\"a\":2}", text(fetch(pinned(URL, NAME, server, cert.pem()))));
        }
    }

    @Test public void interimResponsesAreSkippedAndASwitchOfProtocolIsNot() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> concat(TestTls.raw("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: </x>\r\n\r\n"), TestTls.ok(BODY)))) {
            assertEquals(BODY, text(fetch(pinned(URL, NAME, server, cert.pem()))));
        }
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.raw("HTTP/1.1 101 Switching Protocols\r\n\r\n"))) {
            try { fetch(pinned(URL, NAME, server, cert.pem())); fail(); } catch (IOException expected) { }
        }
    }

    private static byte[] concat(byte[] a, byte[] b) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        out.write(a, 0, a.length);
        out.write(b, 0, b.length);
        return out.toByteArray();
    }

    private static byte[] bytes(int n) {
        byte[] b = new byte[n];
        Arrays.fill(b, (byte) ' ');
        return b;
    }

    private static void refused(TestTls.Cert cert, byte[] response, String message) throws Exception {
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> response)) {
            try { fetch(pinned(URL, NAME, server, cert.pem())); fail(message); }
            catch (SubscriptionData.FetchException e) { fail("not the certificate's fault: " + message); }
            catch (IOException expected) { }
        }
    }

    @Test public void theBodyHasTheCapAndTheLengthsItsGiven() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> TestTls.ok(bytes(SubscriptionData.MAX_BYTES)))) {
            assertEquals(SubscriptionData.MAX_BYTES, fetch(pinned(URL, NAME, server, cert.pem())).length);
        }
        // A length over the cap is refused on the head alone: not a byte of the body is sent, and the fetch still ends.
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nContent-Length: 65537\r\nConnection: close\r\n\r\n"), "declared too large");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999999999\r\n\r\n"), "huge length");
        refused(cert, TestTls.ok(bytes(SubscriptionData.MAX_BYTES + 1)), "too large with a length");
        // No length: read until the connection ends, and no more than the cap.
        refused(cert, concat(TestTls.raw("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"), bytes(SubscriptionData.MAX_BYTES + 1)), "too large without a length");
        try (TestTls.Server server = TestTls.Server.of(cert, (n, seen) -> concat(TestTls.raw("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"), bytes(SubscriptionData.MAX_BYTES)))) {
            assertEquals(SubscriptionData.MAX_BYTES, fetch(pinned(URL, NAME, server, cert.pem())).length);
        }
        // A chunked body is stopped as the total passes the cap.
        String twoChunks = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n10000\r\n" + new String(bytes(65536), StandardCharsets.ISO_8859_1) + "\r\n1\r\n \r\n0\r\n\r\n";
        refused(cert, TestTls.raw(twoChunks), "chunked too large");
    }

    @Test public void aBodyThatIsCutShortOrMalformedIsAnError() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\nshort"), "cut short");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nabcdef"), "two lengths");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nContent-Length: -5\r\n\r\nabcde"), "negative length");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nContent-Length: 0x5\r\n\r\nabcde"), "hex length");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n0\r\n\r\n"), "another coding");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n"), "bad chunk size");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabcXX0\r\n\r\n"), "no line end after a chunk");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nab"), "chunk cut short");
        refused(cert, TestTls.raw("HTTP/2 200\r\n\r\n"), "not HTTP/1");
        refused(cert, TestTls.raw("HTTP/1.1 20 OK\r\n\r\n"), "short status");
        refused(cert, TestTls.raw("garbage\r\n\r\n"), "garbage");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\n" + "X: " + new String(bytes(9000), StandardCharsets.ISO_8859_1) + "\r\n\r\n"), "line too long");
        StringBuilder many = new StringBuilder("HTTP/1.1 200 OK\r\n");
        for (int i = 0; i < 400; i++) many.append("X-").append(i).append(": ").append("0123456789012345678901234567890123456789\r\n");
        refused(cert, TestTls.raw(many.append("\r\n").toString()), "head too large");
        refused(cert, TestTls.raw("HTTP/1.1 200 OK\r\n"), "no end of head");
    }

    @Test public void aServerThatStallsEndsAtTheDeadline() throws Exception {
        TestTls.Cert cert = TestTls.leaf(NAME);
        // One that never answers the handshake; then one that takes the request and says nothing back.
        for (TestTls.Script script : new TestTls.Script[]{null, (n, seen) -> null}) {
            try (TestTls.Server server = TestTls.Server.of(cert, script)) {
                long start = System.nanoTime();
                try { SubscriptionData.fetch(pinned(URL, NAME, server, cert.pem()), NO_SYSTEM, PinnedHttps.DIRECT, 500); fail("should time out"); }
                catch (SubscriptionData.FetchException e) { fail("a stall is not the certificate's fault"); }
                catch (IOException expected) { }
                assertTrue(System.nanoTime() - start < 5_000_000_000L);
            }
        }
    }

    // ---- Names ----

    private static boolean matches(TestTls.Spec spec, String name) throws Exception {
        return PinnedHttps.nameMatches(TestTls.make(spec).certificate, name);
    }

    @Test public void aWildcardIsOneLabelOfAtLeastThree() throws Exception {
        TestTls.Spec wild = TestTls.spec().cn("ignored.example").dns("*.test.invalid");
        assertTrue(matches(wild, "sub.test.invalid"));
        assertTrue(matches(wild, "SUB.Test.Invalid"));
        assertFalse(matches(wild, "a.b.test.invalid"));
        assertFalse(matches(wild, "test.invalid"));
        assertFalse(matches(wild, ".test.invalid"));
        assertFalse(matches(wild, "127.0.0.1"));
        assertFalse(matches(TestTls.spec().cn("x").dns("*.invalid"), "test.invalid"));
        assertFalse(matches(TestTls.spec().cn("x").dns("*.invalid"), "invalid"));
        assertFalse(matches(TestTls.spec().cn("x").dns("*"), "test"));
        // Only as the last character of the first label, and only one.
        assertFalse(matches(TestTls.spec().cn("x").dns("sub.*.invalid"), "sub.test.invalid"));
        assertFalse(matches(TestTls.spec().cn("x").dns("*b.*.invalid"), "sub.test.invalid"));
        assertFalse(matches(TestTls.spec().cn("x").dns("s*b.test.invalid"), "sub.test.invalid"));
        assertFalse(matches(TestTls.spec().cn("x").dns("xn--*.test.invalid"), "xn--a.test.invalid"));
        // A prefix before the star has to be there.
        assertTrue(matches(TestTls.spec().cn("x").dns("su*.test.invalid"), "sub.test.invalid"));
        assertFalse(matches(TestTls.spec().cn("x").dns("zu*.test.invalid"), "sub.test.invalid"));
    }

    @Test public void anIpAddressIsMatchedByTheIpAddressesAlone() throws Exception {
        TestTls.Spec ip = TestTls.spec().cn("127.0.0.2").dns("127.0.0.3").ip("127.0.0.1", "2001:db8::1");
        assertTrue(matches(ip, "127.0.0.1"));
        assertTrue(matches(ip, "2001:db8::1"));
        assertTrue(matches(ip, "2001:0DB8:0:0:0:0:0:1"));
        assertFalse(matches(ip, "127.0.0.2"));
        assertFalse(matches(ip, "127.0.0.3"));
        assertFalse(matches(ip, "10.0.0.1"));
        // And a name is not matched by an address.
        assertFalse(matches(TestTls.spec().cn("x").ip("127.0.0.1"), "test.invalid"));
    }

    @Test public void theCommonNameCountsAlongWithTheAlternativeNames() throws Exception {
        assertTrue(matches(TestTls.spec().cn("sub.test.invalid"), "sub.test.invalid"));
        assertTrue(matches(TestTls.spec().cn("sub.test.invalid").dns("other.test.invalid"), "sub.test.invalid"));
        assertTrue(matches(TestTls.spec().cn("sub.test.invalid").dns("other.test.invalid"), "other.test.invalid"));
        assertTrue(matches(TestTls.spec().cn("*.test.invalid"), "sub.test.invalid"));
        assertFalse(matches(TestTls.spec().cn("sub.test.invalid"), "x.test.invalid"));
        assertFalse(matches(TestTls.spec().cn("sub.test.invalid"), "127.0.0.1"));
        assertFalse(matches(TestTls.spec().cn("test"), ""));
    }

    @Test public void anInternationalizedNameIsMatchedInItsAceForm() throws Exception {
        String ace = java.net.IDN.toASCII("пример.test");
        assertTrue(matches(TestTls.spec().cn("x").dns(ace), "пример.test"));
        assertTrue(matches(TestTls.spec().cn("x").dns(ace), ace.toUpperCase(java.util.Locale.ROOT)));
        assertFalse(matches(TestTls.spec().cn("x").dns(ace), "example.test"));
    }
}
