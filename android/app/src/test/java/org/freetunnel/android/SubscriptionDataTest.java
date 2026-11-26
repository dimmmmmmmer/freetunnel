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
}
