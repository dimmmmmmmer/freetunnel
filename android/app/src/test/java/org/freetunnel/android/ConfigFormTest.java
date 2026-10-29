package org.freetunnel.android;

import static org.freetunnel.android.CertificateVectors.CA_PEM;
import static org.freetunnel.android.CertificateVectors.LEAF_PEM;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.tomlj.Toml;
import org.tomlj.TomlParseResult;

import java.util.Arrays;

// What the editor's form reads from a config and writes to it (ConfigForm): the fields of qml/CreateConfigOverlay.qml, the desktop's
// checks, and that an edit changes the endpoint's values and leaves the rest of the file as it was.
public final class ConfigFormTest {
    private static ConfigForm.Fields server() {
        ConfigForm.Fields f = new ConfigForm.Fields();
        f.hostname = "vpn.example.com";
        f.addresses = "1.2.3.4:443, [2001:db8::1]:443";
        f.username = "alice";
        f.password = "s3\"cr\\et";
        f.dns = "1.1.1.1, tls://dns.example.com";
        f.customSni = "front.example.com";
        f.clientRandom = "aabbcc/ff00ff";
        f.protocol = "http3";
        f.allowIpv6 = false;
        f.skipVerification = true;
        f.antiDpi = true;
        f.certificate = LEAF_PEM;
        return f;
    }

    private static void assertSame(ConfigForm.Fields expected, ConfigForm.Fields read) {
        assertEquals(expected.hostname, read.hostname);
        assertEquals(Arrays.asList(expected.addresses.split(", ")), ConfigForm.addressList(read.addresses));
        assertEquals(expected.username, read.username);
        assertEquals(expected.password, read.password);
        assertEquals(ConfigForm.dnsList(expected.dns), ConfigForm.dnsList(read.dns));
        assertEquals(expected.customSni, read.customSni);
        assertEquals(expected.clientRandom, read.clientRandom);
        assertEquals(expected.protocol, read.protocol);
        assertEquals(expected.allowIpv6, read.allowIpv6);
        assertEquals(expected.skipVerification, read.skipVerification);
        assertEquals(expected.antiDpi, read.antiDpi);
        assertEquals(expected.certificate.trim(), read.certificate.trim());
    }

    @Test public void aNewConfigIsWhatALinkMakes() {
        ConfigForm.Fields f = server();
        String text = ConfigForm.write(null, f);
        assertSame(f, ConfigForm.read(text));
        // Every setting the form has, in the endpoint, and the tunnel's routes, which a form never shows.
        assertTrue(text.contains("[endpoint]\nhostname = \"vpn.example.com\"\n"));
        assertTrue(text.contains("[listener.tun]\n"));
        assertTrue(text.contains("has_ipv6 = false\nskip_verification = true\nupstream_protocol = \"http3\"\nanti_dpi = true\n"));
    }

    @Test public void theDefaultsAreTheDesktops() {
        ConfigForm.Fields f = ConfigForm.read("not toml at all [");
        assertTrue(f.allowIpv6);
        assertFalse(f.skipVerification);
        assertFalse(f.antiDpi);
        assertEquals("http2", f.protocol);
        assertEquals("", f.hostname);
        // A new config with only what is required has no DNS, no SNI and no random, and no certificate.
        ConfigForm.Fields minimal = new ConfigForm.Fields();
        minimal.hostname = "h.example.com";
        minimal.addresses = "9.9.9.9:443";
        minimal.username = "u";
        minimal.password = "p";
        String text = ConfigForm.write(null, minimal);
        assertSame(minimal, ConfigForm.read(text));
        assertTrue(text.contains("certificate = \"\"\n"));
    }

    private static final String FILE = ""
            + "# A provider's file, with its own comment\n"
            + "loglevel = \"info\"\nvpn_mode = \"selective\"\nkillswitch_enabled = true\n"
            + "dns_upstreams = [\"8.8.8.8\"]\n"
            + "exclusions = [\"example.org\"]\n"
            + "\n[endpoint]\n"
            + "hostname = \"old.example.com\"   # the host\n"
            + "addresses = [\n  \"5.6.7.8:443\",\n  \"5.6.7.9:443\"\n]\n"
            + "username = \"bob\"\npassword = \"old\"\n"
            + "has_ipv6 = true\n"
            + "something_else = \"kept\"\n"
            + "\n[listener.tun]\nbound_if = \"\"\nmtu_size = 1400\nchange_system_dns = true\n"
            + "included_routes = [\"0.0.0.0/0\"]\nexcluded_routes = [\"10.0.0.0/8\"]\n";

    @Test public void anEditChangesTheEndpointsValuesAndLeavesTheRestOfTheFile() {
        ConfigForm.Fields f = ConfigForm.read(FILE);
        assertEquals("old.example.com", f.hostname);
        assertEquals("5.6.7.8:443, 5.6.7.9:443", f.addresses);
        assertEquals("8.8.8.8", f.dns);
        f.hostname = "new.example.com";
        f.password = "fresh";
        f.antiDpi = true;
        String text = ConfigForm.write(FILE, f);
        assertTrue(text, text.contains("# A provider's file, with its own comment\n"));
        assertTrue(text, text.contains("exclusions = [\"example.org\"]\n"));
        assertTrue(text, text.contains("something_else = \"kept\"\n"));
        assertTrue(text, text.contains("mtu_size = 1400\n"));
        assertTrue(text, text.contains("included_routes = [\"0.0.0.0/0\"]\nexcluded_routes = [\"10.0.0.0/8\"]\n"));
        assertTrue(text, text.contains("vpn_mode = \"selective\"\n"));
        assertFalse(text, text.contains("old.example.com"));
        assertFalse(text, text.contains("password = \"old\""));
        // The comment after a value goes with the value, and a key the file never had is added under the header.
        assertTrue(text, text.contains("anti_dpi = true\n"));
        ConfigForm.Fields read = ConfigForm.read(text);
        assertSame(f, read);
    }

    @Test public void theDnsListIsChangedWhereTheFileKeepsIt() {
        ConfigForm.Fields f = ConfigForm.read(FILE);
        f.dns = "1.1.1.1 9.9.9.9";
        String text = ConfigForm.write(FILE, f);
        // At the top of the file, where the core reads it when the endpoint has none, and not copied into the endpoint.
        assertTrue(text, text.contains("dns_upstreams = [\"1.1.1.1\", \"9.9.9.9\"]\nexclusions"));
        assertFalse(text, text.substring(text.indexOf("[endpoint]")).contains("dns_upstreams"));
        assertEquals(Arrays.asList("1.1.1.1", "9.9.9.9"), ConfigForm.dnsList(ConfigForm.read(text).dns));
        String own = FILE.replace("has_ipv6 = true\n", "has_ipv6 = true\ndns_upstreams = [\"4.4.4.4\"]\n");
        ConfigForm.Fields g = ConfigForm.read(own);
        assertEquals("The endpoint's own list wins", "4.4.4.4", g.dns);
        g.dns = "";
        String cleared = ConfigForm.write(own, g);
        assertTrue(cleared, cleared.contains("dns_upstreams = []\n"));
        assertEquals("8.8.8.8", ConfigForm.read(cleared.replace("dns_upstreams = []\n", "")).dns);
    }

    @Test public void aFileWhoseEndpointCannotBeEditedInPlaceIsWrittenAfresh() {
        // No endpoint table at all: nothing to edit, so the text a link makes.
        ConfigForm.Fields f = server();
        String text = ConfigForm.write("loglevel = \"info\"\n", f);
        assertSame(f, ConfigForm.read(text));
        assertTrue(text.contains("[listener.tun]\n"));
    }

    // The endpoint written as an inline table cannot be edited in place. The file is kept as it was but for the endpoint, as the desktop's
    // keepWhatTheFormDoesNotShow keeps it: the tunnel's routes, the rules, the other root keys, the other tables and the endpoint's own other keys.
    private static final String INLINE = ""
            + "# A provider's file\n"
            + "vpn_mode = \"selective\"\nkillswitch_enabled = true\n"
            + "dns_upstreams = [\"8.8.8.8\"]\n"
            + "exclusions = [\"example.org\"]\n"
            + "endpoint = { hostname = \"old.example.com\", addresses = [\"5.6.7.8:443\"], username = \"bob\", password = \"old\", "
            + "upstream_fallback_protocol = \"http2\", extra = { depth = 2, list = [1, 2] } }\n"
            + "\n[listener.tun]\nbound_if = \"\"\nmtu_size = 1400\nincluded_routes = [\"0.0.0.0/0\"]\nexcluded_routes = [\"10.0.0.0/8\"]\n"
            + "\n[routing]\n# about this\nlimit = 7\n";

    @Test public void anInlineEndpointIsReplacedAndTheRestOfTheFileIsKept() {
        ConfigForm.Fields f = ConfigForm.read(INLINE);
        assertEquals("old.example.com", f.hostname);
        assertEquals("8.8.8.8", f.dns);
        f.hostname = "new.example.com";
        f.dns = "1.1.1.1";
        f.antiDpi = true;
        String text = ConfigForm.write(INLINE, f);
        assertNotNull("The file can be written back", text);
        assertFalse(text, text.contains("old.example.com"));
        for (String kept : new String[]{"# A provider's file\n", "vpn_mode = \"selective\"\n", "exclusions = [\"example.org\"]\n",
                "mtu_size = 1400\n", "excluded_routes = [\"10.0.0.0/8\"]\n", "[routing]\n# about this\nlimit = 7\n"})
            assertTrue(kept + " in " + text, text.contains(kept));
        assertFalse("The DNS list is in the endpoint only", text.contains("dns_upstreams = [\"8.8.8.8\"]"));
        TomlParseResult parsed = Toml.parse(text);
        assertFalse(text, parsed.hasErrors());
        assertEquals("new.example.com", parsed.getString("endpoint.hostname"));
        assertEquals("http2", parsed.getString("endpoint.upstream_fallback_protocol"));
        assertEquals(Long.valueOf(2), parsed.getLong("endpoint.extra.depth"));
        assertEquals(Long.valueOf(1400), parsed.getLong("listener.tun.mtu_size"));
        assertEquals(Long.valueOf(7), parsed.getLong("routing.limit"));
        assertSame(f, ConfigForm.read(text));
    }

    @Test public void aFileWithNoTunnelGetsTheDefaultOneAndKeepsItsOtherKeys() {
        String file = "loglevel = \"debug\"\nsomething = 1\n";
        ConfigForm.Fields f = server();
        String text = ConfigForm.write(file, f);
        assertNotNull(text);
        assertTrue(text, text.startsWith("loglevel = \"debug\"\nsomething = 1\n"));
        assertTrue(text, text.contains("[listener.tun]\n"));
        assertSame(f, ConfigForm.read(text));
    }

    @Test public void aFileThatCannotBeKeptWholeIsRefusedNotRewritten() {
        // A quoted key with an escape in it is a key the line reader does not read, so the file's text cannot be carried over.
        String file = "\"a\\\"b\" = 1\nendpoint = { hostname = \"h\" }\n";
        assertFalse(Toml.parse(file).hasErrors());
        assertNull(ConfigForm.write(file, server()));
        // A text that is no config at all holds nothing to keep.
        assertNotNull(ConfigForm.write("not toml at all [", server()));
    }

    @Test public void aTypedNameIsHeldToFiftyCharactersAndAHostIsClippedByCodePoints() {
        assertEquals(50, ConfigForm.NAME_LIMIT);
        String fifty = "a".repeat(50);
        assertFalse(ConfigForm.nameTooLong(fifty));
        assertTrue(ConfigForm.nameTooLong(fifty + "a"));
        // An emoji is two UTF-16 units and one character: fifty of them are 100 units and within the limit.
        String emoji = new String(Character.toChars(0x1F600));
        assertFalse(ConfigForm.nameTooLong(emoji.repeat(50)));
        assertTrue(ConfigForm.nameTooLong(emoji.repeat(51)));
        assertEquals(fifty, ConfigForm.clippedName(fifty + "bcd"));
        // Cut after the fiftieth character, never between the halves of one.
        String clipped = ConfigForm.clippedName("x".repeat(49) + emoji + "tail");
        assertEquals("x".repeat(49) + emoji, clipped);
        assertEquals(50, clipped.codePointCount(0, clipped.length()));
        assertEquals("short.example.com", ConfigForm.clippedName("short.example.com"));
    }

    @Test public void theCertificateIsTheCertificatesAlone() {
        ConfigForm.Fields f = server();
        f.certificate = "Bag Attributes\n" + CA_PEM + "\n" + LEAF_PEM;
        assertEquals(0, ConfigForm.problem(f));
        assertTrue(ConfigForm.certificates(f.certificate).dropped);
        String text = ConfigForm.write(null, f);
        assertEquals(CA_PEM + LEAF_PEM, ConfigForm.read(text).certificate);
        assertTrue(text.contains("certificate = \"\"\"\n-----BEGIN CERTIFICATE-----\n"));
        // Edited in place, the old value, however many lines it ran to, goes.
        String file = FILE.replace("something_else = \"kept\"\n", "certificate = \"\"\"\n" + CA_PEM + "\"\"\"\nsomething_else = \"kept\"\n");
        ConfigForm.Fields g = ConfigForm.read(file);
        assertEquals(CA_PEM.trim(), g.certificate.trim());
        g.certificate = "";
        String cleared = ConfigForm.write(file, g);
        assertTrue(cleared, cleared.contains("certificate = \"\"\nsomething_else = \"kept\"\n"));
        assertFalse(cleared, cleared.contains("BEGIN CERTIFICATE"));
    }

    @Test public void theChecksAreTheDesktops() {
        ConfigForm.Fields f = server();
        assertEquals(0, ConfigForm.problem(f));
        for (String field : new String[]{"hostname", "addresses", "username", "password"}) {
            ConfigForm.Fields empty = server();
            switch (field) {
                case "hostname": empty.hostname = "  "; break;
                case "addresses": empty.addresses = " , "; break;
                case "username": empty.username = ""; break;
                default: empty.password = ""; break;
            }
            assertEquals(field, R.string.editor_error_missing, ConfigForm.problem(empty));
        }
        for (String bad : new String[]{"1.2.3.4", "1.2.3.4:0", "1.2.3.4:65536", "1.2.3.4:http", ":443", "1.2.3.4:443, host"}) {
            f.addresses = bad;
            assertEquals(bad, R.string.editor_error_address, ConfigForm.problem(f));
        }
        f.addresses = "host.example.com:1";
        assertEquals(0, ConfigForm.problem(f));
        for (String good : new String[]{"1.1.1.1", "8.8.8.8:53", "2606:4700:4700::1111", "tls://dns.example.com", "https://x/dns-query", "quic://q", "sdns://abc", "1.1.1.1, 8.8.4.4; 9.9.9.9"}) {
            f.dns = good;
            assertEquals(good, 0, ConfigForm.problem(f));
        }
        for (String bad : new String[]{"dns.example.com", "1.1.1", "256.1.1.1", "ftp://x", "1.1.1.1, nope"}) {
            f.dns = bad;
            assertEquals(bad, R.string.editor_error_dns, ConfigForm.problem(f));
        }
        f.dns = "";
        for (String good : new String[]{"", "aabb", "aabb/ccdd", "AABBCC", "aa/"}) {
            f.clientRandom = good;
            // "aa/" is not a valid random as typed (an empty mask), which the core would refuse: the check is on what was typed.
            assertEquals(good, good.equals("aa/") ? R.string.editor_error_random : 0, ConfigForm.problem(f));
        }
        for (String bad : new String[]{"a", "abc", "zz", "aabb/", "/aa", "aa".repeat(33)}) {
            f.clientRandom = bad;
            assertEquals(bad, R.string.editor_error_random, ConfigForm.problem(f));
        }
        f.clientRandom = "";
        f.certificate = "just words";
        assertEquals(R.string.editor_error_certificate, ConfigForm.problem(f));
        f.certificate = "";
        assertEquals(0, ConfigForm.problem(f));
    }

    @Test public void aClientRandomIsAsTheCoreTakesIt() {
        assertEquals("aabb", ConfigForm.clientRandomForCore(" aabb "));
        assertEquals("aabb", ConfigForm.clientRandomForCore("aabb//"));
        assertEquals("", ConfigForm.clientRandomForCore("/aa"));
        assertEquals("aabb/ccdd", ConfigForm.clientRandomForCore("aabb/ccdd"));
    }
}
