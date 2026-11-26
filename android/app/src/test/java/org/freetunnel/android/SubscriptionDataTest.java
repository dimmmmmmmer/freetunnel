package org.freetunnel.android;
import org.junit.Test;
import static org.junit.Assert.*;
import org.json.JSONObject;
import java.net.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
public class SubscriptionDataTest {
    private JSONObject doc() throws Exception { return new JSONObject("{\"version\":1,\"hostname\":\"vpn.example.org\",\"address\":\"192.0.2.1:443\",\"username\":\"user\",\"password\":\"secret\"}"); }
    @Test public void updatePreservesLocalRoutingDnsAndTransport() throws Exception {
        String original = SubscriptionData.apply(doc().toString().getBytes(StandardCharsets.UTF_8), null).toml;
        ConfigForm.Fields local = ConfigForm.read(original); local.dns = "1.1.1.1"; local.protocol = "http3"; local.antiDpi = true; local.allowIpv6 = false;
        original = ConfigForm.write(original, local);
        JSONObject next = doc().put("password", "changed").put("dns_upstreams", new org.json.JSONArray("[\"8.8.8.8\"]"));
        String updated = SubscriptionData.apply(next.toString().getBytes(StandardCharsets.UTF_8), original).toml;
        ConfigForm.Fields result = ConfigForm.read(updated);
        assertEquals("changed", result.password); assertEquals("1.1.1.1", result.dns);
        assertEquals("http3", result.protocol); assertTrue(result.antiDpi); assertFalse(result.allowIpv6);
    }
    @Test public void eachConnectionKeepsItsOwnTlsProfile() throws Exception {
        JSONObject firefox = doc().put("tls_profile", "firefox");
        String created = SubscriptionData.apply(firefox.toString().getBytes(StandardCharsets.UTF_8), null).toml;
        assertTrue(created.contains("tls_profile = \"firefox\"\n"));
        String kept = SubscriptionData.apply(doc().toString().getBytes(StandardCharsets.UTF_8), created).toml;
        assertTrue(kept.contains("tls_profile = \"firefox\"\n"));
        JSONObject alias = doc().put("tls_profile", "default");
        String boring = SubscriptionData.apply(alias.toString().getBytes(StandardCharsets.UTF_8), created).toml;
        assertTrue(boring.contains("tls_profile = \"boringssl\"\n"));
        assertFalse(boring.contains("default"));
        String chrome = SubscriptionData.apply(doc().put("tls_profile", "").toString().getBytes(StandardCharsets.UTF_8), boring).toml;
        assertFalse(chrome.contains("tls_profile"));
        assertThrows(Exception.class, () -> SubscriptionData.apply(doc().put("tls_profile", "netscape").toString().getBytes(StandardCharsets.UTF_8), null));
    }
    @Test public void badTypesAndSecurityDowngradesAreRejected() throws Exception {
        for (Object[] value : new Object[][]{{"version",true},{"version",2},{"skip_verification",true},{"has_ipv6","false"},{"password",23},{"client_random_auth_key","aabb"},{"address","example.org/path"}}) {
            JSONObject invalid = doc().put((String)value[0],value[1]);
            assertThrows(Exception.class, () -> SubscriptionData.apply(invalid.toString().getBytes(StandardCharsets.UTF_8), null));
        }
        assertThrows(Exception.class, () -> SubscriptionData.address("http://user:secret@example.org"));
        assertThrows(Exception.class, () -> SubscriptionData.address("https://user%3Aextra:secret@example.org"));
    }
    @Test public void credentialsNeverReachARedirectAndEscapedPathIsPreserved() throws Exception {
        List<URI> urls = new ArrayList<>();
        assertThrows(Exception.class, () -> SubscriptionData.fetch("https://user:p%2Bass@example.org/a%2Fb?key=a%2Bb", uri -> {
            urls.add(uri);
            return new HttpURLConnection(uri.toURL()) {
                @Override public int getResponseCode() { return 302; }
                @Override public String getHeaderField(String name) { return "https://other.example.org/"; }
                @Override public void connect() { }
                @Override public void disconnect() { }
                @Override public boolean usingProxy() { return false; }
            };
        }));
        assertEquals(1, urls.size()); assertNull(urls.get(0).getUserInfo());
        assertEquals("/a%2Fb", urls.get(0).getRawPath()); assertEquals("key=a%2Bb", urls.get(0).getRawQuery());
    }
    @Test public void addressHoldsTheDesktopsLimitAndTextRules() throws Exception {
        String head = "https://example.org/";
        StringBuilder fill = new StringBuilder(head);
        while (fill.length() < 2048) fill.append('a');
        assertNotNull(SubscriptionData.address(fill.toString()));
        String tooLong = fill + "a";
        assertThrows(Exception.class, () -> SubscriptionData.address(tooLong));
        // Bytes, not characters: 1000 two-byte letters (2020 bytes) pass, 1020 (2060) do not.
        StringBuilder wide = new StringBuilder(head); for (int i = 0; i < 1000; i++) wide.append('\u00e9');
        assertNotNull(SubscriptionData.address(wide.toString()));
        for (int i = 0; i < 20; i++) wide.append('\u00e9');
        String tooWide = wide.toString();
        assertThrows(Exception.class, () -> SubscriptionData.address(tooWide));
        for (String bad : new String[]{"https://example.org/a b", "https://example.org/a\tb", "https://example.org/a\u00a0b",
                "https://example.org/a\u200bb", "https://example.org/a\u202eb", "https://example.org/a\u0085b", "https://example.org/a\ud800b"})
            assertThrows(bad, Exception.class, () -> SubscriptionData.address(bad));
    }
    @Test public void redirectFromAnEmptyPathKeepsItsSlash() throws Exception {
        assertEquals("https://sub.example.com/v1/sub", SubscriptionData.resolveRedirect(new URI("https://sub.example.com"), "v1/sub").toString());
        assertEquals("https://sub.example.com/v1/sub", SubscriptionData.resolveRedirect(new URI("https://sub.example.com?x=1"), "v1/sub").toString());
        assertEquals("https://sub.example.com:8443/a?b=2", SubscriptionData.resolveRedirect(new URI("https://sub.example.com:8443?x=1"), "/a?b=2").toString());
        assertEquals("https://sub.example.com/?b=2", SubscriptionData.resolveRedirect(new URI("https://sub.example.com?x=1"), "?b=2").toString());
        // A base with a path resolves as it always did.
        assertEquals("https://sub.example.com/a/c", SubscriptionData.resolveRedirect(new URI("https://sub.example.com/a/b?x=1"), "c").toString());
    }
    @Test public void relativeRedirectFromAnEmptyPathIsFollowedOnTheSameOrigin() throws Exception {
        List<URI> urls = new ArrayList<>();
        byte[] body = "{\"version\":1}".getBytes(StandardCharsets.UTF_8);
        byte[] got = SubscriptionData.fetch("https://sub.example.com", uri -> {
            urls.add(uri);
            final boolean first = urls.size() == 1;
            return new HttpURLConnection(uri.toURL()) {
                @Override public int getResponseCode() { return first ? 302 : 200; }
                @Override public String getHeaderField(String name) { return first ? "v1/sub" : null; }
                @Override public InputStream getInputStream() { return new ByteArrayInputStream(body); }
                @Override public void connect() { }
                @Override public void disconnect() { }
                @Override public boolean usingProxy() { return false; }
            };
        });
        assertArrayEquals(body, got);
        assertEquals(2, urls.size()); assertEquals("https://sub.example.com/v1/sub", urls.get(1).toString());
    }

    // ---- Pinning (the desktop's rows, tests/test_subscription_fetcher.cpp) ----

    private static SubscriptionData.Trust trust(String hostname, String addresses, String certificate) {
        return new SubscriptionData.Trust(hostname, addresses, certificate);
    }

    @Test public void aPinnedRequestNeedsTheConfigsHostnameAndACertificate() throws Exception {
        String url = "https://alice:s3cret@vpn.test/sub";
        SubscriptionData.Request request = SubscriptionData.requestFor(url, trust("VPN.Test", "127.0.0.1:8443, 10.0.0.1:443", CertificateVectors.LEAF_PEM));
        assertEquals(url, request.source);
        assertTrue(request.pinned);
        assertEquals(1, request.pins.size());
        assertArrayEquals(CertificateVectors.LEAF_DER, request.pins.get(0).getEncoded());
        assertEquals("VPN.Test", request.verifyName);
        assertEquals("127.0.0.1", request.connectHost);
        assertEquals(8443, request.connectPort);

        // Another host, even one under the hostname: the system's CAs, and DNS.
        SubscriptionData.Request other = SubscriptionData.requestFor("https://alice:s3cret@sub.vpn.test/sub", trust("VPN.Test", "127.0.0.1:8443", CertificateVectors.LEAF_PEM));
        assertFalse(other.pinned);
        assertTrue(other.pins.isEmpty());
        assertEquals("", other.verifyName);
        assertNull(other.connectHost);
        assertEquals(-1, other.connectPort);

        // No certificate, or blank lines for one: nothing to pin.
        assertFalse(SubscriptionData.requestFor(url, trust("vpn.test", "127.0.0.1:8443", "\n  \n")).pinned);
        assertFalse(SubscriptionData.requestFor(url, trust("vpn.test", "127.0.0.1:8443", "")).pinned);
        assertFalse(SubscriptionData.requestFor(url, SubscriptionData.Trust.NONE).pinned);
        assertFalse(SubscriptionData.requestFor(url, null).pinned);

        // An IDN is the same host whichever form either side writes it in, and so is an IPv6 literal.
        String ace = java.net.IDN.toASCII("\u043f\u0440\u0438\u043c\u0435\u0440.test");
        assertTrue(SubscriptionData.requestFor("https://" + ace + "/s", trust("\u043f\u0440\u0438\u043c\u0435\u0440.test", "", CertificateVectors.LEAF_PEM)).pinned);
        assertTrue(SubscriptionData.requestFor("https://[2001:db8::1]/s", trust("2001:db8::1", "", CertificateVectors.LEAF_PEM)).pinned);
        assertFalse(SubscriptionData.requestFor("https://[2001:db8::2]/s", trust("2001:db8::1", "", CertificateVectors.LEAF_PEM)).pinned);
        // An address that is none is not pinned and not an error here: the fetch refuses it.
        assertFalse(SubscriptionData.requestFor("not a url", trust("vpn.test", "", CertificateVectors.LEAF_PEM)).pinned);
        assertFalse(SubscriptionData.requestFor(null, trust("vpn.test", "", CertificateVectors.LEAF_PEM)).pinned);
    }

    @Test public void aPinnedRequestConnectsToTheFirstAddress() throws Exception {
        String[][] rows = {
                {"1.2.3.4:443, 5.6.7.8:443", "1.2.3.4", "443"},
                {"[2001:db8::1]:8443", "2001:db8::1", "8443"},
                {" vpn.example.com:9443 ", "vpn.example.com", "9443"},
                // Not one the core could use: the address's own host and port, still pinned.
                {"1.2.3.4", null, "-1"},
                {"", null, "-1"},
                {"1.2.3.4:0", null, "-1"},
                {"1.2.3.4:65536", null, "-1"},
                {"[]:443", null, "-1"},
                // Only the first counts: a bad one first is not skipped for a good second.
                {"nonsense, 5.6.7.8:443", null, "-1"},
        };
        for (String[] row : rows) {
            SubscriptionData.Request request = SubscriptionData.requestFor("https://alice:s3cret@vpn.test:1111/sub", trust("vpn.test", row[0], CertificateVectors.LEAF_PEM));
            assertTrue(row[0], request.pinned);
            assertEquals(row[0], row[1], request.connectHost);
            assertEquals(row[0], Integer.parseInt(row[2]), request.connectPort);
        }
    }

    @Test public void certificatesThatCannotBeReadArePinnedWithNoPins() throws Exception {
        // 00 00 00; the outer shape of a certificate and no more; and that shape then a certificate.
        String garbage = TestTls.pem(new byte[3]), shaped = TestTls.pem(new byte[]{0x30, 0x0b, 0x30, 0x02, 0x05, 0x00, 0x30, 0x02, 0x05, 0x00, 0x03, 0x01, 0x00});
        for (String pem : new String[]{garbage, shaped, garbage + shaped}) {
            SubscriptionData.Request request = SubscriptionData.requestFor("https://vpn.test/sub", trust("vpn.test", "127.0.0.1:8443", pem));
            assertTrue(request.pinned);
            assertTrue(request.pins.isEmpty());
        }
        assertEquals(1, SubscriptionData.requestFor("https://vpn.test/sub", trust("vpn.test", "127.0.0.1:8443", shaped + CertificateVectors.LEAF_PEM)).pins.size());
        assertEquals(2, SubscriptionData.requestFor("https://vpn.test/sub", trust("vpn.test", "127.0.0.1:8443", CertificateVectors.CHAIN_PEM)).pins.size());
    }

    @Test public void hostKeyIsAceInLowerCase() {
        assertEquals("vpn.test", SubscriptionData.hostKey("VPN.Test"));
        assertEquals("xn--e1afmkfd.test", SubscriptionData.hostKey("\u043f\u0440\u0438\u043c\u0435\u0440.TEST"));
        assertEquals("2001:db8::1", SubscriptionData.hostKey("2001:DB8::1"));
        assertEquals("", SubscriptionData.hostKey(""));
    }

    // A link as TrustTunnel writes one: version 2, a server, its certificate when it has one, and the subscription.
    private static String link(byte[] certificate) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        field(out, 0, new byte[]{2});
        field(out, 1, "vpn.example.com".getBytes(StandardCharsets.UTF_8));
        field(out, 5, "alice".getBytes(StandardCharsets.UTF_8));
        field(out, 6, "secret".getBytes(StandardCharsets.UTF_8));
        field(out, 2, "1.2.3.4:443".getBytes(StandardCharsets.UTF_8));
        field(out, 2, "5.6.7.8:443".getBytes(StandardCharsets.UTF_8));
        if (certificate != null) field(out, 8, certificate);
        field(out, 14, "https://alice:s3cret@vpn.example.com/subscription".getBytes(StandardCharsets.UTF_8));
        return "tt://?" + Base64.getUrlEncoder().withoutPadding().encodeToString(out.toByteArray());
    }

    private static void field(ByteArrayOutputStream out, int tag, byte[] value) {
        out.write(tag);
        if (value.length < 64) out.write(value.length);
        else { out.write(0x40 | (value.length >>> 8)); out.write(value.length & 0xff); }
        out.write(value, 0, value.length);
    }

    @Test public void aLinksSubscriptionComesWithTheServerItNames() throws Exception {
        SubscriptionData.Offer pinned = SubscriptionData.offer(link(CertificateVectors.LEAF_DER));
        assertEquals("https://alice:s3cret@vpn.example.com/subscription", pinned.url);
        assertEquals("vpn.example.com", pinned.trust.hostname);
        assertEquals("1.2.3.4:443, 5.6.7.8:443", pinned.trust.addresses);
        assertEquals(CertificateVectors.LEAF_PEM, pinned.trust.certificate);
        SubscriptionData.Request request = SubscriptionData.requestFor(pinned.url, pinned.trust);
        assertTrue(request.pinned);
        assertEquals(1, request.pins.size());
        assertEquals("1.2.3.4", request.connectHost);
        assertEquals(443, request.connectPort);

        // A link with no certificate, or whose certificate is a key (which is dropped), pins nothing.
        for (byte[] none : new byte[][]{null, CertificateVectors.PKCS8_DER}) {
            SubscriptionData.Offer open = SubscriptionData.offer(link(none));
            assertEquals("", open.trust.certificate);
            assertFalse(SubscriptionData.requestFor(open.url, open.trust).pinned);
        }
        // A certificate that is only the shape of one is kept, so the fetch is pinned and refused, not sent to the system's CAs.
        SubscriptionData.Offer shaped = SubscriptionData.offer(link(new byte[]{0x30, 0x0b, 0x30, 0x02, 0x05, 0x00, 0x30, 0x02, 0x05, 0x00, 0x03, 0x01, 0x00}));
        SubscriptionData.Request refused = SubscriptionData.requestFor(shaped.url, shaped.trust);
        assertTrue(refused.pinned);
        assertTrue(refused.pins.isEmpty());
    }

    @Test public void aLinkWithNoSubscriptionOffersNothing() throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        field(out, 0, new byte[]{1});
        field(out, 1, "vpn.example.com".getBytes(StandardCharsets.UTF_8));
        field(out, 5, "alice".getBytes(StandardCharsets.UTF_8));
        field(out, 6, "secret".getBytes(StandardCharsets.UTF_8));
        field(out, 2, "1.2.3.4:443".getBytes(StandardCharsets.UTF_8));
        assertNull(SubscriptionData.offer("tt://?" + Base64.getUrlEncoder().withoutPadding().encodeToString(out.toByteArray())));
    }

    @Test public void aTomlsSubscriptionComesWithTheServerItNames() throws Exception {
        String toml = "[endpoint]\nhostname = \"vpn.example.com\"\naddresses = [\" 1.2.3.4:443 \", \"\", \"5.6.7.8:443\"]\n"
                + "subscription_url = \"https://alice:s3cret@vpn.example.com/subscription\"\n"
                + "certificate = \"\"\"\n" + CertificateVectors.LEAF_PEM + "\"\"\"\n";
        SubscriptionData.Offer offer = SubscriptionData.offer(toml);
        assertEquals("https://alice:s3cret@vpn.example.com/subscription", offer.url);
        assertEquals("vpn.example.com", offer.trust.hostname);
        assertEquals("1.2.3.4:443, 5.6.7.8:443", offer.trust.addresses);
        assertEquals(CertificateVectors.LEAF_PEM.trim(), offer.trust.certificate.trim());
        assertTrue(SubscriptionData.requestFor(offer.url, offer.trust).pinned);
        // The values of another type are missing, not an error; the subscription still reads.
        SubscriptionData.Offer odd = SubscriptionData.offer("[endpoint]\nhostname = 5\naddresses = \"9.9.9.9:443\"\ncertificate = [1]\n"
                + "subscription_url = \"https://vpn.example.com/s\"\n");
        assertEquals("", odd.trust.hostname);
        assertEquals("9.9.9.9:443", odd.trust.addresses);
        assertEquals("", odd.trust.certificate);
        assertEquals("", SubscriptionData.offer("[endpoint]\naddresses = [1, 2]\nsubscription_url = \"https://vpn.example.com/s\"\n").trust.addresses);
        // No subscription, and a table of the old shape.
        assertNull(SubscriptionData.offer("[endpoint]\nhostname = \"vpn.example.com\"\n"));
        assertEquals("https://vpn.example.com/s", SubscriptionData.offer("subscription_url = \"https://vpn.example.com/s\"\n").url);
        assertEquals("https://vpn.example.com/s", SubscriptionData.offer("[endpoint.subscription]\nurl = \"https://vpn.example.com/s\"\n").url);
        assertThrows(Exception.class, () -> SubscriptionData.offer("[endpoint]\nsubscription_url = \"http://vpn.example.com/s\"\n"));
        assertThrows(Exception.class, () -> SubscriptionData.offer("[endpoint]\nclient_random_auth_key = \"aa\"\nsubscription_url = \"https://vpn.example.com/s\"\n"));
    }

    @Test public void aConfigTrustsItsOwnHostnameAddressesAndCertificate() throws Exception {
        ConfigForm.Fields f = new ConfigForm.Fields();
        f.hostname = "vpn.test"; f.addresses = "127.0.0.1:8443, 10.0.0.1:443"; f.username = "alice"; f.password = "secret";
        f.certificate = CertificateVectors.LEAF_PEM;
        SubscriptionData.Trust trust = SubscriptionData.Trust.ofConfig(ConfigForm.write(null, f));
        assertEquals("vpn.test", trust.hostname);
        assertEquals("127.0.0.1:8443, 10.0.0.1:443", trust.addresses);
        assertEquals(CertificateVectors.LEAF_PEM.trim(), trust.certificate.trim());
        SubscriptionData.Trust none = SubscriptionData.Trust.ofConfig("");
        assertEquals("", none.hostname); assertEquals("", none.addresses); assertEquals("", none.certificate);
    }

    @Test public void anUnpinnedFetchStillGoesThroughTheSystemAndNeverDials() throws Exception {
        List<URI> urls = new ArrayList<>();
        byte[] body = "{\"version\":1}".getBytes(StandardCharsets.UTF_8);
        PinnedHttps.Dialer never = (host, port, timeout) -> { throw new AssertionError("dialed"); };
        SubscriptionData.Connections stub = uri -> {
            urls.add(uri);
            return new HttpURLConnection(uri.toURL()) {
                @Override public int getResponseCode() { return 200; }
                @Override public InputStream getInputStream() { return new ByteArrayInputStream(body); }
                @Override public void connect() { }
                @Override public void disconnect() { }
                @Override public boolean usingProxy() { return false; }
            };
        };
        // No trust, and a certificate for another host: both the system's.
        assertArrayEquals(body, SubscriptionData.fetch(SubscriptionData.requestFor("https://sub.example.com/s", SubscriptionData.Trust.NONE), stub, never, 1000));
        assertArrayEquals(body, SubscriptionData.fetch(SubscriptionData.requestFor("https://sub.example.com/s", trust("vpn.test", "127.0.0.1:8443", CertificateVectors.LEAF_PEM)), stub, never, 1000));
        assertEquals(2, urls.size());
    }

    @Test public void theFetchHasTheDesktopsDeadline() {
        assertEquals(20000, SubscriptionData.TIMEOUT_MS);
    }
}
