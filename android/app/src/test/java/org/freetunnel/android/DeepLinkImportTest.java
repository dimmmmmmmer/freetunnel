package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Base64;

public final class DeepLinkImportTest {
    private static void varint(ByteArrayOutputStream out, int value) {
        if (value < 64) out.write(value);
        else { out.write(0x40 | (value >>> 8)); out.write(value & 0xff); }
    }

    private static void field(ByteArrayOutputStream out, int tag, byte[] value) {
        varint(out, tag);
        varint(out, value.length);
        out.write(value, 0, value.length);
    }

    private static void text(ByteArrayOutputStream out, int tag, String value) {
        field(out, tag, value.getBytes(StandardCharsets.UTF_8));
    }

    private static String link(byte[] bytes) {
        return "tt://?" + Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
    }

    private static byte[] example() {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        field(out, 0, new byte[]{1});
        text(out, 1, "vpn.example.com");
        text(out, 2, "192.0.2.1:443");
        text(out, 2, "192.0.2.2:443");
        text(out, 5, "user");
        text(out, 6, "secret");
        text(out, 12, "Test VPN");
        ByteArrayOutputStream dns = new ByteArrayOutputStream();
        byte[] upstream = "https://dns.example/dns-query".getBytes(StandardCharsets.UTF_8);
        varint(dns, upstream.length);
        dns.write(upstream, 0, upstream.length);
        field(out, 13, dns.toByteArray());
        return out.toByteArray();
    }

    @Test public void importsDirectAndQrPageLinks() throws Exception {
        String direct = link(example());
        String page = "https://trusttunnel.org/qr.html#tt=" + direct.substring(6);
        DeepLinkImport.Imported fromDirect = DeepLinkImport.parse(direct);
        DeepLinkImport.Imported fromPage = DeepLinkImport.parse(page);
        assertEquals("Test VPN", fromDirect.name);
        assertTrue(fromPage.toml.contains("hostname = \"vpn.example.com\""));
        assertEquals(fromDirect.toml, fromPage.toml);
        assertTrue(fromPage.toml.contains("addresses = [\"192.0.2.1:443\", \"192.0.2.2:443\"]"));
        assertTrue(fromPage.toml.contains("dns_upstreams = [\"https://dns.example/dns-query\"]"));
        assertTrue(fromPage.toml.indexOf("[endpoint]") < fromPage.toml.indexOf("dns_upstreams ="));
        assertTrue(fromPage.toml.contains("password = \"secret\""));
        assertTrue(fromPage.toml.contains("skip_verification = false"));
        assertTrue(fromPage.toml.contains("included_routes = [\"0.0.0.0/0\", \"2000::/3\"]"));
        assertFalse(fromPage.toml.contains("\"::/0\""));
    }

    @Test public void rejectsMalformedAndUnsupportedLinks() throws Exception {
        for (String invalid : new String[]{
                "tt://?!!!", "tt://?AQ", "https://other.example/qr.html#tt=" + link(example()).substring(6)
        }) {
            try { DeepLinkImport.parse(invalid); fail("Accepted invalid link"); }
            catch (DeepLinkImport.ParseException expected) { /* expected */ }
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        field(out, 0, new byte[]{3});
        text(out, 1, "vpn.example.com");
        text(out, 2, "192.0.2.1:443");
        text(out, 5, "user");
        text(out, 6, "secret");
        refused(link(out.toByteArray()), R.string.text_unsupported_link_version);
    }

    @Test public void controlCharactersCannotInjectTomlKeys() throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        text(out, 1, "vpn.example.com");
        text(out, 2, "192.0.2.1:443");
        text(out, 5, "user\nanti_dpi = true");
        text(out, 6, "secret");
        DeepLinkImport.Imported parsed = DeepLinkImport.parse(link(out.toByteArray()));
        assertTrue(parsed.toml.contains("username = \"useranti_dpi = true\""));
        assertFalse(parsed.toml.contains("\nanti_dpi = true\n"));
    }
    @Test public void paddedWrappedAndPercentEncodedQrLinksWork() throws Exception {
        String payload = Base64.getUrlEncoder().encodeToString(example());
        String wrapped = payload.substring(0, 20) + "\r\n" + payload.substring(20);
        assertEquals(DeepLinkImport.parse(link(example())).toml, DeepLinkImport.parse("tt://?" + wrapped).toml);
        assertEquals(DeepLinkImport.parse(link(example())).toml,
                DeepLinkImport.parse("https://trusttunnel.org/qr.html#tt=" + payload.replace("=", "%3D")).toml);
        assertTrue(DeepLinkImport.parse(link(example())).toml.contains("killswitch_enabled = true"));
    }

    // Version 2 links as TrustTunnel endpoint 1.3.0-beta.2 writes them, built by hand
    // from its DEEP_LINK.md in the field order of its encoder: version 2 first, then
    // hostname, username, password and addresses, then the optional fields, and the
    // subscription last. vectorsMatchTheirFields() rebuilds each one field by field.
    private static final String SUBSCRIPTION = "https://alice:s3cret@vpn.example.com/subscription";
    private static final String V2_PLAIN =
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz";
    private static final String V2_FULL =
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzAwtleGFtcGxlLm9yZwQBAAkBAgwLRXhhbXBsZSBW"
            + "UE4NEAc4LjguOC44BzEuMS4xLjEOMWh0dHBzOi8vYWxpY2U6czNjcmV0QHZwbi5leGFtcGxlLmNvbS9zdWJzY3JpcHRpb24";
    private static final String V2_SUBSCRIPTION_ONLY =
            "tt://?AAECDjFodHRwczovL2FsaWNlOnMzY3JldEB2cG4uZXhhbXBsZS5jb20vc3Vic2NyaXB0aW9u";
    private static final String V3 = "tt://?AAEDDiRodHRwczovL3Zwbi5leGFtcGxlLmNvbS9zdWJzY3JpcHRpb24";
    private static final String V2_HTTP_SUBSCRIPTION = "tt://?AAECDiNodHRwOi8vdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg";
    private static final String V2_AUTH_KEY =
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzDwhhYWJiY2NkZA";

    private static void staticServer(ByteArrayOutputStream out) {
        text(out, 1, "vpn.example.com");
        text(out, 5, "alice");
        text(out, 6, "secret");
        text(out, 2, "1.2.3.4:443");
    }

    private static byte[] withStatic(int version, int tag, String value) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        field(out, 0, new byte[]{(byte) version});
        staticServer(out);
        if (value != null) text(out, tag, value);
        return out.toByteArray();
    }

    private static DeepLinkImport.ParseException refused(String link, int messageResource) {
        try { DeepLinkImport.parse(link); }
        catch (DeepLinkImport.ParseException e) {
            assertEquals(messageResource, e.messageResource);
            // Nothing from the link reaches a message: the address carries a password.
            assertFalse(e.getMessage().contains("https://"));
            assertFalse(e.getMessage().contains("s3cret"));
            return e;
        }
        fail("Accepted " + link.substring(0, 12));
        return null;
    }

    private static void assertNothingKept(DeepLinkImport.Imported imported) {
        assertFalse(imported.toml.contains("subscription"));
        assertFalse(imported.toml.contains("s3cret"));
        assertFalse(imported.toml.contains("alice:"));
    }

    @Test public void vectorsMatchTheirFields() {
        assertEquals(V2_PLAIN, link(withStatic(2, 0, null)));
        ByteArrayOutputStream full = new ByteArrayOutputStream();
        field(full, 0, new byte[]{2});
        staticServer(full);
        text(full, 3, "example.org");
        field(full, 4, new byte[]{0});
        field(full, 9, new byte[]{2});
        text(full, 12, "Example VPN");
        ByteArrayOutputStream dns = new ByteArrayOutputStream();
        for (String upstream : new String[]{"8.8.8.8", "1.1.1.1"}) {
            varint(dns, upstream.length());
            dns.write(upstream.getBytes(StandardCharsets.UTF_8), 0, upstream.length());
        }
        field(full, 13, dns.toByteArray());
        text(full, 14, SUBSCRIPTION);
        assertEquals(V2_FULL, link(full.toByteArray()));
        ByteArrayOutputStream only = new ByteArrayOutputStream();
        field(only, 0, new byte[]{2});
        text(only, 14, SUBSCRIPTION);
        // As upstream's own test of a subscription-only payload: 00 01 02, then 0x0E.
        byte[] onlyBytes = only.toByteArray();
        assertEquals(0x00, onlyBytes[0]); assertEquals(0x01, onlyBytes[1]); assertEquals(0x02, onlyBytes[2]);
        assertEquals(0x0E, onlyBytes[3]);
        assertEquals(V2_SUBSCRIPTION_ONLY, link(onlyBytes));
        ByteArrayOutputStream v3 = new ByteArrayOutputStream();
        field(v3, 0, new byte[]{3});
        text(v3, 14, "https://vpn.example.com/subscription");
        assertEquals(V3, link(v3.toByteArray()));
        ByteArrayOutputStream http = new ByteArrayOutputStream();
        field(http, 0, new byte[]{2});
        text(http, 14, "http://vpn.example.com/subscription");
        assertEquals(V2_HTTP_SUBSCRIPTION, link(http.toByteArray()));
        assertEquals(V2_AUTH_KEY, link(withStatic(2, 15, "aabbccdd")));
    }

    @Test public void acceptsVersion2LinksWithoutTheNewFields() throws Exception {
        DeepLinkImport.Imported imported = DeepLinkImport.parse(V2_PLAIN);
        assertEquals("vpn.example.com", imported.name);
        assertFalse(imported.subscriptionNotKept);
        assertTrue(imported.toml.contains("hostname = \"vpn.example.com\""));
        assertTrue(imported.toml.contains("addresses = [\"1.2.3.4:443\"]"));
        assertTrue(imported.toml.contains("username = \"alice\""));
        assertTrue(imported.toml.contains("password = \"secret\""));
        // The version number changes nothing else: versions 0, 1 and 2 of the same
        // fields import the same configuration.
        assertEquals(DeepLinkImport.parse(link(withStatic(1, 0, null))).toml, imported.toml);
        ByteArrayOutputStream v0 = new ByteArrayOutputStream();
        staticServer(v0);
        assertEquals(DeepLinkImport.parse(link(v0.toByteArray())).toml, imported.toml);
    }

    @Test public void importsTheStaticSettingsOfALinkWithASubscription() throws Exception {
        DeepLinkImport.Imported imported = DeepLinkImport.parse(V2_FULL);
        assertTrue(imported.subscriptionNotKept);
        assertEquals("Example VPN", imported.name);
        assertTrue(imported.toml.contains("hostname = \"vpn.example.com\""));
        assertTrue(imported.toml.contains("custom_sni = \"example.org\""));
        assertTrue(imported.toml.contains("has_ipv6 = false"));
        assertTrue(imported.toml.contains("upstream_protocol = \"http3\""));
        assertTrue(imported.toml.contains("dns_upstreams = [\"8.8.8.8\", \"1.1.1.1\"]"));
        assertNothingKept(imported);
        // The QR page form of the same link behaves the same.
        DeepLinkImport.Imported page = DeepLinkImport.parse("https://trusttunnel.org/qr.html#tt=" + V2_FULL.substring(6));
        assertTrue(page.subscriptionNotKept);
        assertEquals(imported.toml, page.toml);
        // Version 1 links carry no subscription.
        assertFalse(DeepLinkImport.parse(link(example())).subscriptionNotKept);
    }

    @Test public void refusesSubscriptionOnlyLinks() {
        refused(V2_SUBSCRIPTION_ONLY, R.string.text_subscription_links_are_not_supported_yet);
        // Static fields that are only partly there are no usable server either.
        ByteArrayOutputStream partial = new ByteArrayOutputStream();
        field(partial, 0, new byte[]{2});
        text(partial, 1, "vpn.example.com");
        text(partial, 2, "1.2.3.4:443");
        text(partial, 5, "alice");
        text(partial, 14, SUBSCRIPTION);
        refused(link(partial.toByteArray()), R.string.text_subscription_links_are_not_supported_yet);
        // Without a subscription the same link is just incomplete, as before.
        ByteArrayOutputStream incomplete = new ByteArrayOutputStream();
        field(incomplete, 0, new byte[]{2});
        text(incomplete, 1, "vpn.example.com");
        text(incomplete, 2, "1.2.3.4:443");
        text(incomplete, 5, "alice");
        refused(link(incomplete.toByteArray()), R.string.text_the_link_is_missing_required_server_settings);
    }

    @Test public void refusesVersionsAboveTwo() {
        refused(V3, R.string.text_unsupported_link_version);
        refused(link(withStatic(3, 0, null)), R.string.text_unsupported_link_version);
        // An eight-byte varint version.
        ByteArrayOutputStream huge = new ByteArrayOutputStream();
        field(huge, 0, new byte[]{(byte) 0xC0, 0, 0, 0, 0, 0, 0, 3});
        staticServer(huge);
        refused(link(huge.toByteArray()), R.string.text_unsupported_link_version);
    }

    @Test public void refusesMalformedSubscriptionAddresses() throws Exception {
        refused(V2_HTTP_SUBSCRIPTION, R.string.text_the_link_contains_an_invalid_subscription_address);
        StringBuilder longest = new StringBuilder("https://vpn.example.com/");
        while (longest.length() < DeepLinkImport.MAX_SUBSCRIPTION_URL_BYTES) longest.append('a');
        assertTrue(DeepLinkImport.parse(link(withStatic(2, 14, longest.toString()))).subscriptionNotKept);
        // The cap counts bytes, so a two-byte character at the end crosses it.
        String tooLong = longest.substring(0, longest.length() - 1) + "é";
        for (String bad : new String[]{
                "http://vpn.example.com/subscription", "HTTPS://vpn.example.com/subscription", "", "https:/x",
                "ftp://vpn.example.com", " https://vpn.example.com/subscription", longest + "a", tooLong,
                "https://vpn.example.com/sub\u0000scription", "https://vpn.example.com/sub\nscription",
                "https://vpn.example.com/sub\rscription", "https://vpn.example.com/sub\tscription",
                "https://vpn.example.com/sub\u007fscription", "https://vpn.example.com/sub\u0085scription",
                "https://vpn.example.com/sub scription", "https://vpn.example.com/sub scription",
                "https://vpn.example.com/sub scription", "https://vpn.example.com/sub　scription",
                "https://vpn.example.com/subscription\n"}) {
            refused(link(withStatic(2, 14, bad)), R.string.text_the_link_contains_an_invalid_subscription_address);
        }
        // Bytes that are not UTF-8 are refused as for any other text field.
        byte[] plain = withStatic(2, 0, null);
        ByteArrayOutputStream broken = new ByteArrayOutputStream();
        broken.write(plain, 0, plain.length);
        byte[] url = (SUBSCRIPTION + "x").getBytes(StandardCharsets.UTF_8);
        url[url.length - 1] = (byte) 0xff;
        field(broken, 14, url);
        refused(link(broken.toByteArray()), R.string.text_the_link_contains_invalid_text);
        // Percent-encoded and non-ASCII text that is neither whitespace nor control is fine.
        assertTrue(DeepLinkImport.parse(link(withStatic(2, 14, "https://a%20b:p%40ss@впн.example/подписка")))
                .subscriptionNotKept);
        // The last of repeated fields wins, as for every other field.
        ByteArrayOutputStream twice = new ByteArrayOutputStream();
        byte[] first = withStatic(2, 14, "http://vpn.example.com/old");
        twice.write(first, 0, first.length);
        text(twice, 14, SUBSCRIPTION);
        assertTrue(DeepLinkImport.parse(link(twice.toByteArray())).subscriptionNotKept);
    }

    @Test public void anEmptyAuthKeyIsTheSameAsNone() throws Exception {
        DeepLinkImport.Imported imported = DeepLinkImport.parse(link(withStatic(2, 15, "")));
        assertEquals(DeepLinkImport.parse(V2_PLAIN).toml, imported.toml);
        assertFalse(imported.subscriptionNotKept);
        assertFalse(imported.toml.contains("auth_key"));
    }

    @Test public void refusesARealAuthKey() {
        refused(V2_AUTH_KEY, R.string.text_this_server_needs_a_newer_trusttunnel_core);
        refused(link(withStatic(2, 15, "AABBCCDD")), R.string.text_this_server_needs_a_newer_trusttunnel_core);
        refused(link(withStatic(1, 15, "aabbccdd")), R.string.text_this_server_needs_a_newer_trusttunnel_core);
        // Not hex, as in upstream's own test, or an odd length: the link is damaged.
        for (String bad : new String[]{"nothexx", "nothex", "aabbccgg", "abc", "аа", "aa bb", "0x00"})
            refused(link(withStatic(2, 15, bad)), R.string.text_the_link_is_damaged);
        byte[] plain = withStatic(2, 0, null);
        ByteArrayOutputStream broken = new ByteArrayOutputStream();
        broken.write(plain, 0, plain.length);
        field(broken, 15, new byte[]{'a', (byte) 0xff});
        refused(link(broken.toByteArray()), R.string.text_the_link_contains_invalid_text);
        // A subscription link with a key would not work after a fetch either.
        ByteArrayOutputStream both = new ByteArrayOutputStream();
        field(both, 0, new byte[]{2});
        text(both, 14, SUBSCRIPTION);
        text(both, 15, "aabbccdd");
        refused(link(both.toByteArray()), R.string.text_this_server_needs_a_newer_trusttunnel_core);
    }

    @Test public void unknownTagsAboveFifteenAreStillIgnored() throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] plain = withStatic(2, 0, null);
        out.write(plain, 0, plain.length);
        field(out, 16, new byte[]{1, 2, 3});
        varint(out, 300); varint(out, 2); out.write(0xff); out.write(0xfe);
        field(out, 63, "https://ignored.example".getBytes(StandardCharsets.UTF_8));
        DeepLinkImport.Imported imported = DeepLinkImport.parse(link(out.toByteArray()));
        assertEquals(DeepLinkImport.parse(V2_PLAIN).toml, imported.toml);
        assertFalse(imported.subscriptionNotKept);
    }

    // Like the desktop's seeded mutation loop: damaged links, version 1 and 2 alike,
    // must fail with a message and never crash. A fixed seed repeats the inputs.
    @Test public void damagedLinksFailWithAMessage() {
        byte[][] seeds = {example(), Base64.getUrlDecoder().decode(V2_PLAIN.substring(6)),
                Base64.getUrlDecoder().decode(V2_FULL.substring(6)),
                Base64.getUrlDecoder().decode(V2_SUBSCRIPTION_ONLY.substring(6)),
                Base64.getUrlDecoder().decode(V2_AUTH_KEY.substring(6))};
        java.util.Random random = new java.util.Random(0x7475);
        for (byte[] seed : seeds) {
            for (int round = 0; round < 3000; round++) {
                byte[] bytes = seed.clone();
                int edits = 1 + random.nextInt(4);
                for (int i = 0; i < edits && bytes.length > 0; i++) {
                    int at = random.nextInt(bytes.length);
                    switch (random.nextInt(3)) {
                        case 0: bytes[at] = (byte) random.nextInt(256); break;
                        case 1: bytes = java.util.Arrays.copyOf(bytes, at); break;
                        default: bytes[at] ^= (byte) (1 << random.nextInt(8)); break;
                    }
                }
                if (bytes.length == 0) continue;
                try { DeepLinkImport.parse(link(bytes)); }
                catch (DeepLinkImport.ParseException expected) { assertTrue(expected.messageResource != 0); }
            }
        }
    }
}
