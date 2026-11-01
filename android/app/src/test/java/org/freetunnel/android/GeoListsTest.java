package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

// Geo lists (GeoLists): the .dat reader, the text reader, and what may be taken out of the tunnel. The same cases as the
// desktop's tests/test_georules.cpp, so that both apps read a list alike. The .dat files are built here, field by field, in the
// protobuf wire format the published geosite.dat and geoip.dat use.
public final class GeoListsTest {
    // ---------- protobuf wire format, the writing half ----------

    private static byte[] varint(long v) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        do {
            int b = (int) (v & 0x7f);
            v >>>= 7;
            if (v != 0) b |= 0x80;
            out.write(b);
        } while (v != 0);
        return out.toByteArray();
    }
    private static byte[] cat(byte[]... parts) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (byte[] p : parts) out.write(p, 0, p.length);
        return out.toByteArray();
    }
    private static byte[] b(String s) { return s.getBytes(StandardCharsets.UTF_8); }
    private static byte[] key(int field, int wire) { return varint(((long) field << 3) | wire); }
    private static byte[] lenField(int field, byte[] body) { return cat(key(field, 2), varint(body.length), body); }
    private static byte[] intField(int field, long v) { return cat(key(field, 0), varint(v)); }

    // Domain { Type type = 1; string value = 2; repeated Attribute attribute = 3; }
    private static byte[] domain(int type, String value) {
        return cat(intField(1, type), lenField(2, b(value)), lenField(3, lenField(1, b("ads"))));
    }
    // GeoSite { string country_code = 1; repeated Domain domain = 2; }, wrapped in GeoSiteList { repeated GeoSite entry = 1; }
    private static byte[] geosite(String code, byte[]... domains) {
        byte[] body = lenField(1, b(code));
        for (byte[] d : domains) body = cat(body, lenField(2, d));
        return lenField(1, body);
    }
    // CIDR { bytes ip = 1; uint32 prefix = 2; }
    private static byte[] cidr(byte[] ip, long prefix) { return cat(lenField(1, ip), intField(2, prefix)); }
    // GeoIP { string country_code = 1; repeated CIDR cidr = 2; bool reverse_match = 3; }
    private static byte[] geoip(String code, boolean reverse, byte[]... cidrs) {
        byte[] body = lenField(1, b(code));
        for (byte[] c : cidrs) body = cat(body, lenField(2, c));
        if (reverse) body = cat(body, intField(3, 1));
        return lenField(1, body);
    }
    private static byte[] v4(int a, int b, int c, int d) { return new byte[] {(byte) a, (byte) b, (byte) c, (byte) d}; }

    private static final class Read {
        final List<String> entries = new ArrayList<>();
        GeoLists.Stats stats;
        String error;
        boolean ok() { return error == null; }
        boolean has(String e) { return entries.contains(e); }
    }

    private static Read read(byte[] data, String spec, GeoLists.Kind kind, boolean bypass) {
        GeoLists.Source source = GeoLists.parseSource(spec);
        assertNotNull(spec, source);
        Read r = new Read();
        try {
            r.stats = GeoLists.append(data, source, kind, bypass, r.entries);
        } catch (GeoLists.Refused refused) {
            r.error = refused.getMessage();
        }
        return r;
    }
    private static Read read(byte[] data, String spec, GeoLists.Kind kind) { return read(data, spec, kind, false); }

    // ---------- sources ----------

    @Test public void sourcesAreHttpsAndADatFileNamesItsCategory() {
        GeoLists.Source text = GeoLists.parseSource("https://example.com/lists/ru-blocked.txt");
        assertNotNull(text);
        assertFalse(text.dat);
        assertEquals("", text.category);
        assertEquals("https://example.com/lists/ru-blocked.txt", text.file);

        GeoLists.Source dat = GeoLists.parseSource("https://example.com/geoip.dat#ru-blocked");
        assertNotNull(dat);
        assertTrue(dat.dat);
        assertEquals("ru-blocked", dat.category);
        assertEquals("https://example.com/geoip.dat", dat.file); // the category never reaches the server

        GeoLists.Source upper = GeoLists.parseSource("https://example.com/GEOSITE.DAT#Category-RU");
        assertNotNull(upper);
        assertTrue(upper.dat);
        assertEquals("Category-RU", upper.category);

        for (String bad : new String[] {
                "https://example.com/geoip.dat",               // a .dat file without a category
                "https://example.com/dlc.dat#category-ru@!cn",  // an attribute filter
                "http://example.com/list.txt",
                "file:///C:/lists/list.txt",
                "https://user:pw@example.com/list.txt",
                "ru-blocked",
                "",
        }) assertNull(bad, GeoLists.parseSource(bad));
        assertNull(GeoLists.parseSource(null));
    }

    @Test public void sourcePreservesEscapedPathAndQuery() {
        String url = "https://example.com/path%2Fname.dat?key=one%26two%3Dthree";
        GeoLists.Source source = GeoLists.parseSource(url + "#category-ru");
        assertNotNull(source);
        assertEquals(url, source.file);
        assertEquals("category-ru", source.category);
    }

    // ---------- .dat files ----------

    @Test public void geositeTakesOneCategoryInEveryCase() {
        byte[] data = cat(geosite("GOOGLE", domain(2, "google.com")),
                geosite("RU-BLOCKED", domain(2, "blocked.example"), domain(2, "other.example")),
                geosite("CATEGORY-RU", domain(2, "ya.ru")));
        Read r = read(data, "https://x.test/geosite.dat#ru-blocked", GeoLists.Kind.SITES);
        assertTrue(r.error, r.ok());
        assertEquals(2, r.stats.entries);
        assertTrue(r.has("blocked.example"));
        assertTrue(r.has("other.example"));
        assertFalse(r.has("google.com"));
        assertFalse(r.has("ya.ru"));
    }

    @Test public void geositeDomainTypesMapToWhatTheCoreMatches() {
        byte[] data = geosite("X", domain(0, "keyword"), domain(1, "^re.*gex$"), domain(2, "root.example"),
                domain(3, "full.example"));
        Read r = read(data, "https://x.test/geosite.dat#x", GeoLists.Kind.SITES);
        assertTrue(r.error, r.ok());
        // A root domain is itself and every name under it; a full one only itself.
        assertTrue(r.has("root.example"));
        assertTrue(r.has("*.root.example"));
        assertTrue(r.has("full.example"));
        assertFalse(r.has("*.full.example"));
        // The core matches neither keywords nor regular expressions.
        assertEquals(2, r.stats.entries);
        assertEquals(2, r.stats.skipped);
        assertEquals(3, r.entries.size());
    }

    @Test public void geoipGivesNormalisedSubnets() {
        byte[] v6 = new byte[16];
        v6[0] = 0x2a;
        v6[1] = 0x14;
        v6[2] = (byte) 0xe9;
        v6[15] = 0x01; // host bits, cleared by the /20, as is half of 0xe9
        byte[] data = geoip("RU", false, cidr(v4(5, 3, 7, 9), 24), cidr(v4(1, 1, 1, 1), 32), cidr(v6, 20));
        Read r = read(data, "https://x.test/geoip.dat#ru", GeoLists.Kind.IPS);
        assertTrue(r.error, r.ok());
        assertTrue(r.has("5.3.7.0/24"));
        assertTrue(r.has("1.1.1.1/32"));
        assertTrue(r.has("2a14:e000::/20"));
        assertEquals(3, r.stats.entries);
    }

    @Test public void aMissingCategoryIsAnError() {
        Read r = read(geosite("GOOGLE", domain(2, "google.com")), "https://x.test/geosite.dat#ru-blocked", GeoLists.Kind.SITES);
        assertFalse(r.ok());
        assertTrue(r.error, r.error.contains("ru-blocked"));
        assertTrue(r.entries.isEmpty());
    }

    // "Every address but these" cannot be handed to the core as a list; taking it as "these" would route the opposite of what
    // the list means.
    @Test public void anInvertedGeoipCategoryIsRefused() {
        Read r = read(geoip("NOT-RU", true, cidr(v4(5, 3, 7, 0), 24)), "https://x.test/geoip.dat#not-ru", GeoLists.Kind.IPS);
        assertFalse(r.ok());
        assertTrue(r.entries.isEmpty());
    }

    @Test public void aDamagedDatFileAddsNothing() {
        byte[] good = geosite("X", domain(2, "a.example"), domain(2, "b.example"));
        byte[] endless = new byte[16];
        Arrays.fill(endless, (byte) 0xff);
        byte[][] damaged = {
                Arrays.copyOf(good, good.length - 3),              // truncated
                cat(key(1, 2), varint(1000), b("abc")),            // a length past the end
                endless,                                           // a varint that never ends
                cat(key(7, 3), good),                              // a group
                cat(new byte[] {0}, good),                         // field zero
                b("<html><body>Not Found</body></html>"),          // not protobuf at all
        };
        for (byte[] data : damaged) {
            List<String> out = new ArrayList<>(Collections.singletonList("kept"));
            try {
                GeoLists.append(data, GeoLists.parseSource("https://x.test/geosite.dat#x"), GeoLists.Kind.SITES, false, out);
                fail("read a damaged file");
            } catch (GeoLists.Refused refused) {
                assertFalse(refused.getMessage().isEmpty());
            }
            assertEquals(Collections.singletonList("kept"), out);
        }
    }

    @Test public void unknownFieldsAreSkipped() {
        // A fixed64, a fixed32 and an unknown length field around the real ones, as a newer file format may add.
        byte[] body = cat(key(9, 1), new byte[8], lenField(1, b("X")), key(10, 5), new byte[4],
                lenField(2, domain(2, "a.example")), lenField(11, b("resource-hash")));
        byte[] data = cat(lenField(1, body), key(2, 0), varint(5));
        Read r = read(data, "https://x.test/geosite.dat#x", GeoLists.Kind.SITES);
        assertTrue(r.error, r.ok());
        assertTrue(r.has("a.example"));
    }

    // ---------- text lists ----------

    @Test public void textListsReadEveryPublishedForm() {
        String text = "# a comment\n"
                + "domain:Blocked.Example\n"
                + "full:exact.example @ads\n"
                + "keyword:tracker\n"
                + "regexp:^ads[0-9]+\\.example$\n"
                + "include:other-list\n"
                + "\r\n"
                + "plain.example   # trailing comment\r\n"
                + "// another comment style\n"
                + "203.0.113.7\n"
                + "198.51.100.77/24\n"
                + "2001:db8::1/48\n"
                + "bad..name\n";
        Read r = read(b(text), "https://x.test/list.txt", GeoLists.Kind.SITES);
        assertTrue(r.error, r.ok());
        assertTrue(r.has("blocked.example"));
        assertTrue(r.has("*.blocked.example"));
        assertTrue(r.has("exact.example"));
        assertFalse(r.has("*.exact.example"));
        assertTrue(r.has("plain.example"));
        assertTrue(r.has("*.plain.example"));
        assertTrue(r.has("203.0.113.7/32"));
        assertTrue(r.has("198.51.100.0/24"));
        assertTrue(r.has("2001:db8::/48"));
        assertEquals(6, r.stats.entries);
        assertEquals(4, r.stats.skipped); // keyword, regexp, include, bad..name
    }

    @Test public void internationalNamesBecomeTheirAsciiForm() {
        Read r = read(b("domain:пример.рф\nрф\n"), "https://x.test/l.txt", GeoLists.Kind.SITES);
        assertTrue(r.error, r.ok());
        assertTrue(r.has("xn--e1afmkfd.xn--p1ai"));
        assertTrue(r.has("*.xn--e1afmkfd.xn--p1ai"));
        // A whole zone is a list's to name: category-ru lists ru, su and рф.
        assertTrue(r.has("xn--p1ai"));
        assertTrue(r.has("*.xn--p1ai"));
    }

    // An empty or unusable answer is far likelier a broken download (an error page, a cut-off file) than a list meant to be
    // empty, and taking it would quietly stop routing what the list named.
    @Test public void aListWithNothingUsableIsAnError() {
        Read r = read(b("# nothing\n\nkeyword:x\n"), "https://x.test/l.txt", GeoLists.Kind.SITES);
        assertFalse(r.ok());
        assertTrue(r.entries.isEmpty());
    }

    // A list that takes traffic OUT of the tunnel is trusted with less: a few wide subnets would take most of the internet out
    // with them while the app says Connected.
    @Test public void bypassListsCannotTakeTheWholeInternetOut() {
        byte[] text = b("0.0.0.0/1\n128.0.0.0/1\n10.0.0.0/8\n::/1\n2000::/3\n2a14:e800::/29\n0.0.0.0/0\n");
        Read bypass = read(text, "https://x.test/l.txt", GeoLists.Kind.IPS, true);
        assertTrue(bypass.error, bypass.ok());
        assertTrue(bypass.has("10.0.0.0/8"));
        assertTrue(bypass.has("2a14:e800::/29"));
        assertFalse(bypass.has("0.0.0.0/1"));
        assertFalse(bypass.has("128.0.0.0/1"));
        assertFalse(bypass.has("::/1"));
        assertFalse(bypass.has("2000::/3"));
        assertEquals(5, bypass.stats.skipped);

        // Through the tunnel, a wide subnet only sends more through it; /0 still means nothing a list could intend.
        Read through = read(text, "https://x.test/l.txt", GeoLists.Kind.IPS, false);
        assertTrue(through.has("0.0.0.0/1"));
        assertFalse(through.has("0.0.0.0/0"));

        assertTrue(GeoLists.tooWideToBypass("0.0.0.0/7"));
        assertFalse(GeoLists.tooWideToBypass("10.0.0.0/8"));
        assertTrue(GeoLists.tooWideToBypass("2000::/15"));
        assertFalse(GeoLists.tooWideToBypass("2a00::/16"));
    }

    // ---------- names and subnets ----------

    @Test public void subnetsAreWrittenAsTheDesktopWritesThem() {
        assertEquals("10.0.0.0/8", GeoLists.subnet("10.1.2.3/8"));
        assertEquals("192.0.2.1/32", GeoLists.subnet("192.0.2.1"));
        assertEquals("2001:db8::/32", GeoLists.subnet("2001:0DB8:0000::/32"));
        assertEquals("2001:db8::1/128", GeoLists.subnet("2001:db8::1"));
        assertEquals("::1/128", GeoLists.subnet("::1"));
        assertEquals("2001:db8:0:1::/64", GeoLists.subnet("2001:db8:0:1:2:3:4:5/64"));
        // The first of two equally long runs of zeros is the one shortened.
        assertEquals("2001:db8::1:0:0:1/128", GeoLists.subnet("2001:db8:0:0:1:0:0:1"));
        // A single zero group is not shortened.
        assertEquals("2001:db8:0:1:1:1:1:1/128", GeoLists.subnet("2001:db8:0:1:1:1:1:1"));
        for (String bad : new String[] {"10.0.0.0/33", "10.0.0/8", "256.0.0.1", "1.2.3.4/", "1.2.3.4/x", "1::2::3", "::/129",
                "1:2:3:4:5:6:7:8:9", "example.com", "1.2.3.4/-1"})
            assertNull(bad, GeoLists.subnet(bad));
    }

    @Test public void domainsAreWhatTheCoreMatches() {
        assertEquals("example.com", GeoLists.domain("Example.COM."));
        assertEquals("ru", GeoLists.domain("ru"));
        assertEquals("_dmarc.example.com", GeoLists.domain("_dmarc.example.com"));
        for (String bad : new String[] {"", ".", "a..b", ".a", "-a.b", "a b", "a/b", "a*b"})
            assertNull(bad, GeoLists.domain(bad));
    }

    // ---------- the lists together ----------

    @Test public void joinedEntriesAreSortedAndUnique() {
        assertEquals("*.a.example\na.example\nb.example\n",
                GeoLists.join(Arrays.asList("b.example", "*.a.example", "a.example", "b.example")));
        assertEquals("", GeoLists.join(Collections.<String>emptyList()));
        assertEquals(Arrays.asList("*.a.example", "a.example"), GeoLists.lines("*.a.example\na.example\n"));
    }

    @Test public void invalidFieldNumbersAreRejected() {
        Read result = read(cat(geosite("x", domain(2, "example.com")), intField(0x20000000, 0)),
                "https://x.test/geosite.dat#x", GeoLists.Kind.SITES);
        assertFalse(result.ok());
    }

    @Test public void overflowingVarintsAreRejected() {
        byte[] overflow = new byte[] {0x78, (byte) 0xff, (byte) 0xff, (byte) 0xff,
                (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff, (byte) 0xff,
                (byte) 0xff, 0x02};
        Read result = read(cat(geosite("x", domain(2, "example.com")), overflow),
                "https://x.test/geosite.dat#x", GeoLists.Kind.SITES);
        assertFalse(result.ok());
    }
}
