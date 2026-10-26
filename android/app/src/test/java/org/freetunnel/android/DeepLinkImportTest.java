package org.freetunnel.android;

import static org.freetunnel.android.CertificateVectors.CHAIN_DER;
import static org.freetunnel.android.CertificateVectors.CHAIN_PEM;
import static org.freetunnel.android.CertificateVectors.LEAF_DER;
import static org.freetunnel.android.CertificateVectors.LEAF_PEM;
import static org.freetunnel.android.CertificateVectors.PKCS1_DER;
import static org.freetunnel.android.CertificateVectors.PKCS8_DER;
import static org.freetunnel.android.CertificateVectors.SEC1_DER;
import static org.freetunnel.android.CertificateVectors.concat;
import static org.freetunnel.android.CertificateVectors.hex;
import static org.freetunnel.android.CertificateVectors.keyLabel;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
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
    // subscription last. They are byte for byte the desktop's own vectors
    // (tests/deeplink_v2_vectors.h), so both apps are held to the same links.
    // vectorsMatchTheirFields() rebuilds each one field by field.
    private static final String SUBSCRIPTION = "https://alice:s3cret@vpn.example.com/subscription";
    private static final String V2_PLAIN =
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz";
    private static final String V2_FULL =
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzAhFbMjAwMTpkYjg6OjFdOjQ0Mw"
            + "sNYWFiYmNjL2ZmMDBmZgMLZXhhbXBsZS5vcmcEAQAKAQEJAQIMB0V4YW1wbGUNHgcxLjEuMS4xFXRsczovL2Rucy5leGFtcG"
            + "xlLmNvbQ4xaHR0cHM6Ly9hbGljZTpzM2NyZXRAdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg";
    // What the endpoint exports for a self-signed server with subscriptions on: the
    // certificate (0x08, as DER) just before the address. The smallest DER with a
    // certificate's outer shape stands in for one: SEQUENCE { SEQUENCE { NULL },
    // SEQUENCE { NULL }, BIT STRING 00 }. It used to be a SEQUENCE holding an
    // INTEGER, which is how a key starts, and only certificates are kept now.
    private static final byte[] V2_SELF_SIGNED_CERTIFICATE = hex("300b3002050030020500030100");
    private static final String V2_SELF_SIGNED =
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzM2NyZXQCCzEuMi4zLjQ6NDQzCA0wCzACBQAwAgUAAwEADjFodHRwczov"
            + "L2FsaWNlOnMzY3JldEB2cG4uZXhhbXBsZS5jb20vc3Vic2NyaXB0aW9u";
    private static final String V2_SUBSCRIPTION_ONLY =
            "tt://?AAECDjFodHRwczovL2FsaWNlOnMzY3JldEB2cG4uZXhhbXBsZS5jb20vc3Vic2NyaXB0aW9u";
    private static final String V3 = "tt://?AAEDDiRodHRwczovL3Zwbi5leGFtcGxlLmNvbS9zdWJzY3JpcHRpb24";
    private static final String V2_HTTP_SUBSCRIPTION = "tt://?AAECDiNodHRwOi8vdnBuLmV4YW1wbGUuY29tL3N1YnNjcmlwdGlvbg";
    // Test links, not credentials: Semgrep takes any "key" given a literal for one.
    private static final String V2_AUTH_KEY = // nosemgrep: codacy.java.security.hard-coded-password
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzDwhhYWJiY2NkZA";
    private static final String V2_EMPTY_AUTH_KEY = // nosemgrep: codacy.java.security.hard-coded-password
            "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQzDwA";

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
        assertFalse(imported.toml.contains("https://alice"));
        assertFalse(imported.toml.contains("alice:"));
    }

    @Test public void vectorsMatchTheirFields() {
        assertEquals(V2_PLAIN, link(withStatic(2, 0, null)));
        ByteArrayOutputStream full = new ByteArrayOutputStream();
        field(full, 0, new byte[]{2});
        text(full, 1, "vpn.example.com");
        text(full, 5, "alice");
        text(full, 6, "s3cret");
        text(full, 2, "1.2.3.4:443");
        text(full, 2, "[2001:db8::1]:443");
        text(full, 11, "aabbcc/ff00ff");
        text(full, 3, "example.org");
        field(full, 4, new byte[]{0});
        field(full, 10, new byte[]{1});
        field(full, 9, new byte[]{2});
        text(full, 12, "Example");
        ByteArrayOutputStream dns = new ByteArrayOutputStream();
        for (String upstream : new String[]{"1.1.1.1", "tls://dns.example.com"}) {
            varint(dns, upstream.length());
            dns.write(upstream.getBytes(StandardCharsets.UTF_8), 0, upstream.length());
        }
        field(full, 13, dns.toByteArray());
        text(full, 14, SUBSCRIPTION);
        assertEquals(V2_FULL, link(full.toByteArray()));
        ByteArrayOutputStream selfSigned = new ByteArrayOutputStream();
        field(selfSigned, 0, new byte[]{2});
        text(selfSigned, 1, "vpn.example.com");
        text(selfSigned, 5, "alice");
        text(selfSigned, 6, "s3cret");
        text(selfSigned, 2, "1.2.3.4:443");
        field(selfSigned, 8, V2_SELF_SIGNED_CERTIFICATE);
        text(selfSigned, 14, SUBSCRIPTION);
        assertEquals(V2_SELF_SIGNED, link(selfSigned.toByteArray()));
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
        assertEquals(V2_EMPTY_AUTH_KEY, link(withStatic(2, 15, "")));
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
        assertEquals("Example", imported.name);
        assertTrue(imported.toml.contains("hostname = \"vpn.example.com\""));
        assertTrue(imported.toml.contains("addresses = [\"1.2.3.4:443\", \"[2001:db8::1]:443\"]"));
        assertTrue(imported.toml.contains("username = \"alice\""));
        assertTrue(imported.toml.contains("password = \"s3cret\""));
        assertTrue(imported.toml.contains("client_random = \"aabbcc/ff00ff\""));
        assertTrue(imported.toml.contains("custom_sni = \"example.org\""));
        assertTrue(imported.toml.contains("has_ipv6 = false"));
        assertTrue(imported.toml.contains("anti_dpi = true"));
        assertTrue(imported.toml.contains("upstream_protocol = \"http3\""));
        assertTrue(imported.toml.contains("dns_upstreams = [\"1.1.1.1\", \"tls://dns.example.com\"]"));
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
        assertTrue(DeepLinkImport.parse(link(withStatic(2, 14, SUBSCRIPTION))).subscriptionNotKept);
        StringBuilder longest = new StringBuilder("https://vpn.example.com/");
        while (longest.length() < DeepLinkImport.MAX_SUBSCRIPTION_URL_BYTES) longest.append('a');
        assertTrue(DeepLinkImport.parse(link(withStatic(2, 14, longest.toString()))).subscriptionNotKept);
        // The cap counts bytes, so a two-byte character at the end crosses it.
        String tooLong = longest.substring(0, longest.length() - 1) + "\u00e9";
        for (String bad : new String[]{
                "http://vpn.example.com/subscription", "HTTPS://vpn.example.com/subscription", "", "https:/x",
                "ftp://vpn.example.com", " https://vpn.example.com/subscription", longest + "a", tooLong,
                "https://vpn.example.com/subscription\n", SUBSCRIPTION + "\u200b"}) {
            refused(link(withStatic(2, 14, bad)), R.string.text_the_link_contains_an_invalid_subscription_address);
        }
        // The desktop's list, each in the middle of the address: control characters,
        // spaces of every kind, and invisible format characters (zero-width space,
        // right-to-left override, byte order mark, and U+E0001 from outside the BMP).
        for (String inserted : new String[]{
                "\u0000", "\n", "\r", "\t", "\u007f", "\u0085", " ", "\u00a0", "\u2028", "\u3000",
                "\u200b", "\u202e", "\ufeff", "\udb40\udc01"}) {
            String url = SUBSCRIPTION.substring(0, 20) + inserted + SUBSCRIPTION.substring(20);
            refused(link(withStatic(2, 14, url)), R.string.text_the_link_contains_an_invalid_subscription_address);
        }
        // Bytes that are not UTF-8 are refused as for any other text field.
        byte[] plain = withStatic(2, 0, null);
        ByteArrayOutputStream broken = new ByteArrayOutputStream();
        broken.write(plain, 0, plain.length);
        byte[] url = (SUBSCRIPTION + "x").getBytes(StandardCharsets.UTF_8);
        url[url.length - 1] = (byte) 0xff;
        field(broken, 14, url);
        refused(link(broken.toByteArray()), R.string.text_the_link_contains_invalid_text);
        // Percent-encoded and non-ASCII text that is neither whitespace, control nor
        // format is fine, a character outside the BMP included.
        assertTrue(DeepLinkImport.parse(link(withStatic(2, 14, "https://a%20b:p%40ss@\u0432\u043f\u043d.example/\ud83d\ude00")))
                .subscriptionNotKept);
        // Every copy of a repeated address is checked, as on the desktop: a good one
        // carries no bad one past the check, whichever of them comes last.
        for (String[] pair : new String[][]{{"http://vpn.example.com/old", SUBSCRIPTION},
                {SUBSCRIPTION, "http://vpn.example.com/new"}}) {
            ByteArrayOutputStream twice = new ByteArrayOutputStream();
            byte[] first = withStatic(2, 14, pair[0]);
            twice.write(first, 0, first.length);
            text(twice, 14, pair[1]);
            refused(link(twice.toByteArray()), R.string.text_the_link_contains_an_invalid_subscription_address);
        }
        ByteArrayOutputStream twoGood = new ByteArrayOutputStream();
        byte[] first = withStatic(2, 14, "https://vpn.example.com/old");
        twoGood.write(first, 0, first.length);
        text(twoGood, 14, SUBSCRIPTION);
        assertTrue(DeepLinkImport.parse(link(twoGood.toByteArray())).subscriptionNotKept);
    }

    @Test public void anEmptyAuthKeyIsTheSameAsNone() throws Exception {
        DeepLinkImport.Imported imported = DeepLinkImport.parse(V2_EMPTY_AUTH_KEY);
        assertEquals(DeepLinkImport.parse(V2_PLAIN).toml, imported.toml);
        assertFalse(imported.subscriptionNotKept);
        assertFalse(imported.toml.contains("auth_key"));
        // Of two well-formed copies the last one counts, as for every field and as on
        // the desktop and in TrustTunnel's own decoder.
        ByteArrayOutputStream twice = new ByteArrayOutputStream();
        byte[] keyed = withStatic(2, 15, "aabbccdd");
        twice.write(keyed, 0, keyed.length);
        field(twice, 15, new byte[0]);
        assertEquals(imported.toml, DeepLinkImport.parse(link(twice.toByteArray())).toml);
    }

    @Test public void refusesARealAuthKey() {
        refused(V2_AUTH_KEY, R.string.text_this_server_needs_a_newer_trusttunnel_core);
        refused(link(withStatic(2, 15, "AABBCCDD")), R.string.text_this_server_needs_a_newer_trusttunnel_core);
        refused(link(withStatic(1, 15, "aabbccdd")), R.string.text_this_server_needs_a_newer_trusttunnel_core);
        // Not hex, as in upstream's own test, or an odd length: the link is damaged.
        for (String bad : new String[]{"nothexx", "nothex", "aabbccgg", "abc", "\u0430\u0430", "aa bb", "0x00"})
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
        // Every copy is checked, as TrustTunnel's own decoder does: an empty copy
        // before or after a broken one does not excuse it.
        for (boolean brokenFirst : new boolean[]{true, false}) {
            ByteArrayOutputStream twice = new ByteArrayOutputStream();
            byte[] head = withStatic(2, 15, brokenFirst ? "zz" : "");
            twice.write(head, 0, head.length);
            text(twice, 15, brokenFirst ? "" : "zz");
            refused(link(twice.toByteArray()), R.string.text_the_link_is_damaged);
        }
    }

    // A newer format may mean something else by a field, so a link in one is refused
    // for its version, whatever its fields hold and wherever the version sits.
    @Test public void theVersionIsReadBeforeTheFields() {
        // Version 3 with a whole server, then a subscription address that is not UTF-8:
        // 00 01 03 | ... | 0e 02 ff fe.
        byte[] v3 = withStatic(3, 0, null);
        ByteArrayOutputStream odd = new ByteArrayOutputStream();
        odd.write(v3, 0, v3.length);
        field(odd, 14, new byte[]{(byte) 0xff, (byte) 0xfe});
        refused(link(odd.toByteArray()), R.string.text_unsupported_link_version);
        // Fields this app would refuse, with the version after them.
        for (int version : new int[]{3, 2}) {
            ByteArrayOutputStream late = new ByteArrayOutputStream();
            text(late, 14, "http://vpn.example.com/subscription");
            text(late, 15, "zz");
            field(late, 1, new byte[]{(byte) 0xff});
            field(late, 0, new byte[]{(byte) version});
            // In a version this app reads, the first bad field is what is refused.
            refused(link(late.toByteArray()), version == 3 ? R.string.text_unsupported_link_version
                    : R.string.text_the_link_contains_an_invalid_subscription_address);
        }
        // Damage to the records themselves still comes first: no version can be read
        // past it. Here a hostname record claims five bytes and has none.
        ByteArrayOutputStream cut = new ByteArrayOutputStream();
        field(cut, 0, new byte[]{3});
        cut.write(1); cut.write(5);
        refused(link(cut.toByteArray()), R.string.text_the_link_is_damaged);
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

    // V2_PLAIN with the given bytes in tag 0x08.
    private static String withCertificate(byte[] der) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] plain = withStatic(2, 0, null);
        out.write(plain, 0, plain.length);
        field(out, 8, der);
        return link(out.toByteArray());
    }

    private static int blocks(String toml) { return toml.split("-----BEGIN ", -1).length - 1; }

    @Test public void readsASelfSignedVersion2Link() throws Exception {
        DeepLinkImport.Imported imported = DeepLinkImport.parse(V2_SELF_SIGNED);
        assertTrue(imported.subscriptionNotKept);
        assertFalse(imported.certificateIgnored);
        assertTrue(imported.toml, imported.toml.contains("certificate = \"\"\"\n-----BEGIN CERTIFICATE-----\n"
                + "MAswAgUAMAIFAAMBAA==\n-----END CERTIFICATE-----\n\"\"\"\n"));
        assertNothingKept(imported);
    }

    @Test public void onlyTheCertificatesInALinkAreKept() throws Exception {
        // A chain comes back one block each, with nothing left out.
        DeepLinkImport.Imported chain = DeepLinkImport.parse(withCertificate(CHAIN_DER));
        assertFalse(chain.certificateIgnored);
        assertTrue(chain.toml, chain.toml.contains("certificate = \"\"\"\n" + CHAIN_PEM + "\"\"\"\n"));
        // A key beside the certificate, wherever it is, is left out, and the import says so.
        String pkcs8 = Base64.getEncoder().encodeToString(PKCS8_DER);
        for (byte[] der : new byte[][]{concat(LEAF_DER, PKCS8_DER), concat(PKCS8_DER, LEAF_DER),
                concat(SEC1_DER, LEAF_DER, hex("3003020101"))}) {
            DeepLinkImport.Imported imported = DeepLinkImport.parse(withCertificate(der));
            assertTrue(imported.certificateIgnored);
            assertFalse(imported.subscriptionNotKept);
            assertTrue(imported.toml, imported.toml.contains("certificate = \"\"\"\n" + LEAF_PEM + "\"\"\"\n"));
            assertEquals(1, blocks(imported.toml));
            assertFalse(imported.toml.contains(pkcs8));
        }
        // Next to a subscription, both are said.
        ByteArrayOutputStream both = new ByteArrayOutputStream();
        byte[] keyed = Base64.getUrlDecoder().decode(withCertificate(concat(LEAF_DER, PKCS8_DER)).substring(6));
        both.write(keyed, 0, keyed.length);
        text(both, 14, SUBSCRIPTION);
        DeepLinkImport.Imported imported = DeepLinkImport.parse(link(both.toByteArray()));
        assertTrue(imported.certificateIgnored);
        assertTrue(imported.subscriptionNotKept);
        // A link without a certificate has nothing to leave out.
        assertFalse(DeepLinkImport.parse(V2_PLAIN).certificateIgnored);
        assertTrue(DeepLinkImport.parse(V2_PLAIN).toml.contains("certificate = \"\"\n"));
        assertFalse(DeepLinkImport.parse(link(example())).certificateIgnored);
    }

    @Test public void aCertificateWithNoCertificateIsRefused() {
        // Whole DER elements, none of them a certificate: the link meant to pin one.
        // The one-INTEGER SEQUENCE is what the link vectors stood in with before.
        for (byte[] der : new byte[][]{PKCS8_DER, hex("3003020101"), concat(PKCS1_DER, SEC1_DER)})
            refused(withCertificate(der), R.string.text_the_link_contains_an_invalid_certificate);
        // Bytes that are not whole elements, refused as before.
        for (byte[] der : new byte[][]{hex("030100"), concat(V2_SELF_SIGNED_CERTIFICATE, hex("00")),
                Arrays.copyOf(LEAF_DER, LEAF_DER.length - 1), hex("30800000")})
            refused(withCertificate(der), R.string.text_the_link_contains_an_invalid_certificate);
        // Those are damage to the link itself, so every copy is checked, as on the
        // desktop: a good last one does not excuse a bad first one.
        for (boolean brokenFirst : new boolean[]{true, false}) {
            ByteArrayOutputStream twice = new ByteArrayOutputStream();
            byte[] plain = withStatic(2, 0, null);
            twice.write(plain, 0, plain.length);
            field(twice, 8, brokenFirst ? hex("030100") : LEAF_DER);
            field(twice, 8, brokenFirst ? LEAF_DER : hex("030100"));
            refused(link(twice.toByteArray()), R.string.text_the_link_contains_an_invalid_certificate);
        }
        // Whole elements with no certificate among them come after the other checks,
        // in the desktop's order: the auth key, then the server's own settings. Bytes
        // that are not whole elements are refused before any of those, as the
        // desktop refuses them while it reads the link.
        ByteArrayOutputStream noUsername = new ByteArrayOutputStream();
        field(noUsername, 0, new byte[]{2});
        text(noUsername, 1, "vpn.example.com");
        text(noUsername, 6, "secret");
        text(noUsername, 2, "1.2.3.4:443");
        ByteArrayOutputStream onlySubscription = new ByteArrayOutputStream();
        field(onlySubscription, 0, new byte[]{2});
        text(onlySubscription, 14, SUBSCRIPTION);
        Object[][] rows = {
            {withStatic(2, 15, "aabbccdd"), R.string.text_this_server_needs_a_newer_trusttunnel_core},
            {noUsername.toByteArray(), R.string.text_the_link_is_missing_required_server_settings},
            {onlySubscription.toByteArray(), R.string.text_subscription_links_are_not_supported_yet},
        };
        for (Object[] row : rows) {
            for (boolean whole : new boolean[]{true, false}) {
                ByteArrayOutputStream out = new ByteArrayOutputStream();
                byte[] head = (byte[]) row[0];
                out.write(head, 0, head.length);
                field(out, 8, whole ? PKCS8_DER : hex("030100"));
                refused(link(out.toByteArray()),
                        whole ? (Integer) row[1] : R.string.text_the_link_contains_an_invalid_certificate);
            }
        }
    }

    // Like the desktop's seeded mutation loop: damaged links, version 1 and 2 alike,
    // must fail with a message and never crash. A fixed seed repeats the inputs.
    @Test public void damagedLinksFailWithAMessage() {
        byte[][] seeds = {example(), Base64.getUrlDecoder().decode(V2_PLAIN.substring(6)),
                Base64.getUrlDecoder().decode(V2_FULL.substring(6)),
                Base64.getUrlDecoder().decode(V2_SELF_SIGNED.substring(6)),
                Base64.getUrlDecoder().decode(withCertificate(concat(LEAF_DER, PKCS8_DER)).substring(6)),
                Base64.getUrlDecoder().decode(V2_SUBSCRIPTION_ONLY.substring(6)),
                Base64.getUrlDecoder().decode(V2_AUTH_KEY.substring(6)),
                Base64.getUrlDecoder().decode(V2_EMPTY_AUTH_KEY.substring(6))};
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
                try { assertFalse(DeepLinkImport.parse(link(bytes)).toml.contains(keyLabel())); }
                catch (DeepLinkImport.ParseException expected) { assertTrue(expected.messageResource != 0); }
            }
        }
    }
}
