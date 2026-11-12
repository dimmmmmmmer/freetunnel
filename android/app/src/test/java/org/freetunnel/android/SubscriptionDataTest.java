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
}
