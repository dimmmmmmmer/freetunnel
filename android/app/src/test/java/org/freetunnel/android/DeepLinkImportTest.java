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
        assertEquals("vpn.example.com", fromPage.hostname);
        assertEquals(fromDirect.toml, fromPage.toml);
        assertTrue(fromPage.toml.contains("addresses = [\"192.0.2.1:443\", \"192.0.2.2:443\"]"));
        assertTrue(fromPage.toml.contains("dns_upstreams = [\"https://dns.example/dns-query\"]"));
        assertTrue(fromPage.toml.indexOf("[endpoint]") < fromPage.toml.indexOf("dns_upstreams ="));
        assertTrue(fromPage.toml.contains("password = \"secret\""));
        assertFalse(fromPage.skipVerification);
    }

    @Test public void rejectsMalformedAndUnsupportedLinks() throws Exception {
        for (String invalid : new String[]{
                "tt://?!!!", "tt://?AQ", "https://other.example/qr.html#tt=" + link(example()).substring(6)
        }) {
            try { DeepLinkImport.parse(invalid); fail("Accepted invalid link"); }
            catch (DeepLinkImport.ParseException expected) { /* expected */ }
        }
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        field(out, 0, new byte[]{2});
        text(out, 1, "vpn.example.com");
        text(out, 2, "192.0.2.1:443");
        text(out, 5, "user");
        text(out, 6, "secret");
        try { DeepLinkImport.parse(link(out.toByteArray())); fail("Accepted future version"); }
        catch (DeepLinkImport.ParseException expected) { /* expected */ }
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
}
