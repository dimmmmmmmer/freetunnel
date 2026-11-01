package org.freetunnel.android;

import java.net.IDN;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;

/**
 * Geo lists for split tunnelling, as the desktop reads them (src/core/GeoRules.cpp): geosite/geoip .dat files (the V2Ray
 * format) and plain text lists, turned into the entries the core's exclusion list takes. Downloading and routing are handled separately from parsing.
 *
 * <p>A list is named by one string, its source: the https address of the file, and for a .dat file the category to take from it
 * after a '#', as in {@code https://…/geoip.dat#ru-blocked}. The fragment never reaches the server. Any other address is read as
 * a text list, one entry per line.
 *
 * <p>All of this is plain Java, so that it is tested on a plain JVM (GeoListsTest).
 */
final class GeoLists {
    private GeoLists() { }

    /** What a list holds, which decides how a .dat file is read: geosite files hold domains, geoip files subnets. */
    enum Kind { SITES, IPS }

    /** A list's source: the file's address without the fragment, and the category the fragment names. */
    static final class Source {
        final String file;
        final String category;
        final boolean dat;
        Source(String file, String category, boolean dat) { this.file = file; this.category = category; this.dat = dat; }
    }

    /** What reading one list gave: entries made (a domain with its subdomains counts once), and entries the core cannot use. */
    static final class Stats {
        int entries;
        int skipped;
    }

    /** A list that cannot be read as asked: the reason, in words, for the log. */
    static final class Refused extends Exception {
        private static final long serialVersionUID = 1L;
        Refused(String reason) { super(reason); }
    }

    /**
     * Reads a source string; null when it is not an https address without a user name or password, or names a .dat file without
     * a category, or with an attribute filter ('@'), which this does not read.
     */
    static Source parseSource(String spec) {
        if (spec == null) return null;
        URI uri;
        try { uri = new URI(spec.trim()); } catch (Exception bad) { return null; }
        if (!"https".equalsIgnoreCase(uri.getScheme()) || uri.getHost() == null || uri.getHost().isEmpty()
                || uri.getRawUserInfo() != null) return null;
        String category = uri.getFragment() == null ? "" : uri.getFragment().trim();
        String file;
        try {
            file = new URI(uri.getScheme(), uri.getRawAuthority() == null ? null : uri.getAuthority(), uri.getPath(),
                    uri.getQuery(), null).toASCIIString();
        } catch (Exception bad) { return null; }
        boolean dat = uri.getPath() != null && uri.getPath().toLowerCase(Locale.ROOT).endsWith(".dat");
        if (dat && (category.isEmpty() || category.contains("@"))) return null;
        return new Source(file, category, dat);
    }

    // ---------- a minimal reader of the protobuf wire format ----------

    /**
     * Varints and length-delimited fields, which is all V2Ray's GeoSiteList and GeoIPList need; fixed-width fields are skipped.
     * Every length is checked against what is left, so a truncated or hostile file ends the read instead of reading past it.
     */
    private static final class Wire {
        final byte[] data;
        int pos;
        final int end;
        boolean failed;
        int field;
        int wire;
        Wire(byte[] data, int from, int to) { this.data = data; this.pos = from; this.end = to; }

        boolean next() {
            if (pos >= end || failed) return false;
            long key = varint();
            if (failed || (key >>> 3) == 0 || (key >>> 3) > Integer.MAX_VALUE) return fail();
            field = (int) (key >>> 3);
            wire = (int) (key & 7);
            return true;
        }
        long varint() {
            long value = 0;
            for (int shift = 0; shift < 64; shift += 7) {
                if (pos >= end) { fail(); return 0; }
                int b = data[pos++] & 0xff;
                if (shift == 63 && (b & 0xfe) != 0) { fail(); return 0; }
                value |= (long) (b & 0x7f) << shift;
                if ((b & 0x80) == 0) return value;
            }
            fail();
            return 0;
        }
        /** The span of a length-delimited field: [start, end) in data; null on bad data. */
        int[] bytes() {
            long len = varint();
            if (failed || len < 0 || len > end - pos) { fail(); return null; }
            int[] span = {pos, pos + (int) len};
            pos += (int) len;
            return span;
        }
        boolean skip() {
            switch (wire) {
                case 0: varint(); return !failed;
                case 1: return advance(8);
                case 2: return bytes() != null;
                case 5: return advance(4);
                default: return fail(); // groups are not in these files
            }
        }
        private boolean advance(int n) {
            if (n > end - pos) return fail();
            pos += n;
            return true;
        }
        private boolean fail() { failed = true; return false; }
    }

    // ---------- names and subnets ----------

    /**
     * A domain name as the core matches it: lower case, ASCII (an internationalised name in its xn-- form), labels of letters,
     * digits, '-' and '_', no empty one; null when it is not one. A single label is allowed: the lists name whole zones (ru, su,
     * xn--p1ai).
     */
    static String domain(String name) {
        String n = name.trim().toLowerCase(Locale.ROOT);
        if (n.endsWith(".")) n = n.substring(0, n.length() - 1);
        if (n.isEmpty() || n.length() > 253) return null;
        boolean ascii = true;
        for (int i = 0; i < n.length(); i++) if (n.charAt(i) > 0x7f) { ascii = false; break; }
        if (!ascii) {
            try { n = IDN.toASCII(n, IDN.ALLOW_UNASSIGNED).toLowerCase(Locale.ROOT); } catch (Exception bad) { return null; }
            if (n.isEmpty()) return null;
        }
        char last = '.';
        for (int i = 0; i < n.length(); i++) {
            char c = n.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
            if (!ok || (c == '.' && last == '.')) return null;
            last = c;
        }
        if (n.startsWith(".") || n.startsWith("-")) return null;
        return n;
    }

    /**
     * "a.b.c.d/n" or "x::/n", with the host bits cleared and IPv6 written in its short form (RFC 5952, as the desktop's Qt writes
     * it); an address alone gets its full length. Null when it is neither. Never looks a name up.
     */
    static String subnet(String text) {
        String t = text.trim();
        if (t.isEmpty()) return null;
        int slash = t.indexOf('/');
        String host = slash < 0 ? t : t.substring(0, slash);
        byte[] bytes = address(host);
        if (bytes == null) return null;
        int bits = bytes.length * 8;
        int prefix = bits;
        if (slash >= 0) {
            String p = t.substring(slash + 1);
            if (p.isEmpty() || p.length() > 3 || !p.chars().allMatch(Character::isDigit)) return null;
            prefix = Integer.parseInt(p);
            if (prefix > bits) return null;
        }
        return format(mask(bytes, prefix), prefix);
    }

    static String subnet(byte[] ip, long prefix) {
        if ((ip.length != 4 && ip.length != 16) || prefix < 0 || prefix > ip.length * 8L) return null;
        return format(mask(ip.clone(), (int) prefix), (int) prefix);
    }

    private static byte[] mask(byte[] bytes, int prefix) {
        for (int i = 0; i < bytes.length; i++) {
            int keep = Math.max(0, Math.min(8, prefix - i * 8));
            bytes[i] = (byte) (bytes[i] & (0xff00 >> keep));
        }
        return bytes;
    }

    // Four decimal octets, or an IPv6 literal (with an IPv4 tail allowed); null for anything else.
    private static byte[] address(String host) {
        if (host.indexOf(':') < 0) return ipv4(host);
        return ipv6(host);
    }

    private static byte[] ipv4(String host) {
        String[] parts = host.split("\\.", -1);
        if (parts.length != 4) return null;
        byte[] out = new byte[4];
        for (int i = 0; i < 4; i++) {
            String p = parts[i];
            if (p.isEmpty() || p.length() > 3 || !p.chars().allMatch(Character::isDigit)) return null;
            if (p.length() > 1 && p.charAt(0) == '0') return null;
            int v = Integer.parseInt(p);
            if (v > 255) return null;
            out[i] = (byte) v;
        }
        return out;
    }

    private static byte[] ipv6(String host) {
        String h = host.toLowerCase(Locale.ROOT);
        if (!h.matches("[0-9a-f:.]+")) return null;
        int gap = h.indexOf("::");
        if (gap >= 0 && h.indexOf("::", gap + 1) >= 0) return null;
        List<Integer> head = groups(gap >= 0 ? h.substring(0, gap) : h);
        List<Integer> tail = gap >= 0 ? groups(h.substring(gap + 2)) : new ArrayList<>();
        if (head == null || tail == null) return null;
        int total = head.size() + tail.size();
        if (gap < 0 ? total != 8 : total > 7) return null;
        byte[] out = new byte[16];
        int i = 0;
        for (int g : head) { out[i++] = (byte) (g >> 8); out[i++] = (byte) g; }
        i = 16 - tail.size() * 2;
        for (int g : tail) { out[i++] = (byte) (g >> 8); out[i++] = (byte) g; }
        return out;
    }

    // The 16-bit groups of one side of "::"; an IPv4 tail counts as two. Null when a group is not one.
    private static List<Integer> groups(String part) {
        List<Integer> out = new ArrayList<>();
        if (part.isEmpty()) return out;
        String[] items = part.split(":", -1);
        for (int k = 0; k < items.length; k++) {
            String g = items[k];
            if (k == items.length - 1 && g.indexOf('.') >= 0) {
                byte[] v4 = ipv4(g);
                if (v4 == null) return null;
                out.add(((v4[0] & 0xff) << 8) | (v4[1] & 0xff));
                out.add(((v4[2] & 0xff) << 8) | (v4[3] & 0xff));
                continue;
            }
            if (g.isEmpty() || g.length() > 4) return null;
            try { out.add(Integer.parseInt(g, 16)); } catch (NumberFormatException bad) { return null; }
        }
        return out;
    }

    private static String format(byte[] b, int prefix) {
        if (b.length == 4)
            return (b[0] & 0xff) + "." + (b[1] & 0xff) + "." + (b[2] & 0xff) + "." + (b[3] & 0xff) + "/" + prefix;
        int[] g = new int[8];
        for (int i = 0; i < 8; i++) g[i] = ((b[2 * i] & 0xff) << 8) | (b[2 * i + 1] & 0xff);
        // The longest run of two or more zero groups, the first of equal ones, becomes "::".
        int bestStart = -1, bestLen = 0;
        for (int i = 0; i < 8; ) {
            if (g[i] != 0) { i++; continue; }
            int j = i;
            while (j < 8 && g[j] == 0) j++;
            if (j - i > bestLen && j - i >= 2) { bestStart = i; bestLen = j - i; }
            i = j;
        }
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < 8; i++) {
            if (i == bestStart) {
                s.append("::");
                i += bestLen - 1;
                continue;
            }
            if (s.length() > 0 && s.charAt(s.length() - 1) != ':') s.append(':');
            s.append(Integer.toHexString(g[i]));
        }
        return s + "/" + prefix;
    }

    /**
     * A subnet that, taken out of the tunnel, would take most of the internet with it: an IPv4 prefix shorter than /8, an IPv6
     * one shorter than /16.
     */
    static boolean tooWideToBypass(String cidr) {
        int slash = cidr.lastIndexOf('/');
        if (slash < 0) return false;
        int prefix;
        try { prefix = Integer.parseInt(cidr.substring(slash + 1)); } catch (NumberFormatException bad) { return false; }
        return prefix < (cidr.indexOf(':') >= 0 ? 16 : 8);
    }

    // ---------- one list ----------

    // What one list adds, kept to itself until the whole list has been read: a list that fails half way adds nothing.
    private static final class Collector {
        final boolean bypass;
        final List<String> entries = new ArrayList<>();
        final Stats stats = new Stats();
        Collector(boolean bypass) { this.bypass = bypass; }

        void domain(String name, boolean withSubdomains) {
            String d = GeoLists.domain(name);
            if (d == null) { stats.skipped++; return; }
            entries.add(d);
            if (withSubdomains) entries.add("*." + d);
            stats.entries++;
        }
        void subnet(String cidr) {
            // Every address of its kind is refused either way: a list cannot mean it.
            if (cidr == null || cidr.endsWith("/0") || (bypass && tooWideToBypass(cidr))) { stats.skipped++; return; }
            entries.add(cidr);
            stats.entries++;
        }
    }

    /**
     * Appends the core's exclusion entries for one list file to {@code out}: a domain with its subdomains becomes "example.com"
     * and "*.example.com", an exact one only the first, a subnet its normalised CIDR form. {@code bypass} says the list takes
     * traffic out of the tunnel, which may not name a subnet wider than /8 or /16 (see {@link #tooWideToBypass}). Refused, with
     * nothing appended, when the data is not a list of that kind, a .dat file has no such category, or nothing in it can be used:
     * an empty answer is far likelier a broken download than a list meant to be empty.
     */
    static Stats append(byte[] data, Source source, Kind kind, boolean bypass, List<String> out) throws Refused {
        Collector c = new Collector(bypass);
        if (source.dat) readDat(data, source, kind, c);
        else readText(data, c);
        if (c.stats.entries == 0) throw new Refused("the list has no entries that can be used");
        out.addAll(c.entries);
        return c.stats;
    }

    // V2Ray's Domain.Type.
    private static final long PLAIN = 0, REGEX = 1, ROOT_DOMAIN = 2, FULL = 3;

    private static void readDat(byte[] data, Source source, Kind kind, Collector c) throws Refused {
        Wire r = new Wire(data, 0, data.length);
        boolean found = false;
        while (r.next()) {
            if (r.field != 1 || r.wire != 2) {
                if (!r.skip()) break;
                continue;
            }
            int[] entry = r.bytes();
            if (entry == null) break;
            found |= readEntry(data, entry, source.category, kind, c);
        }
        if (r.failed) throw new Refused("the file is damaged or not a " + (kind == Kind.SITES ? "geosite" : "geoip") + " file");
        if (!found) throw new Refused("the file has no category " + source.category);
    }

    // One GeoSite or GeoIP message: its code (field 1), and if it is the category asked for, its entries (field 2). The code comes
    // first in every file seen, but nothing in the format says so: the entries' spans are kept until it is known.
    private static boolean readEntry(byte[] data, int[] span, String category, Kind kind, Collector c) throws Refused {
        Wire r = new Wire(data, span[0], span[1]);
        String code = "";
        List<int[]> items = new ArrayList<>();
        boolean reverse = false;
        while (r.next()) {
            if (r.field == 1 && r.wire == 2) {
                int[] v = r.bytes();
                if (v == null) break;
                code = new String(data, v[0], v[1] - v[0], StandardCharsets.UTF_8);
            } else if (r.field == 2 && r.wire == 2) {
                int[] v = r.bytes();
                if (v == null) break;
                items.add(v);
            } else if (kind == Kind.IPS && r.field == 3 && r.wire == 0) {
                // GeoIP.reverse_match (inverse_match in V2Ray).
                reverse = r.varint() != 0;
            } else if (!r.skip()) {
                break;
            }
        }
        if (r.failed) throw new Refused("the file is damaged");
        if (!code.equalsIgnoreCase(category)) return false;
        // "Every address but these": nothing the core can be given as a list.
        if (reverse) throw new Refused("category " + code + " is an inverted match, which cannot be used");
        for (int[] item : items) {
            boolean ok = kind == Kind.SITES ? readDomain(data, item, c) : readCidr(data, item, c);
            if (!ok) throw new Refused("the file is damaged");
        }
        return true;
    }

    private static boolean readDomain(byte[] data, int[] span, Collector c) {
        Wire r = new Wire(data, span[0], span[1]);
        long type = PLAIN;
        String value = "";
        while (r.next()) {
            if (r.field == 1 && r.wire == 0) type = r.varint();
            else if (r.field == 2 && r.wire == 2) {
                int[] v = r.bytes();
                if (v == null) break;
                value = new String(data, v[0], v[1] - v[0], StandardCharsets.UTF_8);
            } else if (!r.skip()) break;
        }
        if (r.failed) return false;
        if (type == ROOT_DOMAIN) c.domain(value, true);
        else if (type == FULL) c.domain(value, false);
        else c.stats.skipped++; // a keyword or a regular expression: the core matches neither
        return true;
    }

    private static boolean readCidr(byte[] data, int[] span, Collector c) {
        Wire r = new Wire(data, span[0], span[1]);
        byte[] ip = new byte[0];
        long prefix = 0;
        while (r.next()) {
            if (r.field == 1 && r.wire == 2) {
                int[] v = r.bytes();
                if (v == null) break;
                ip = java.util.Arrays.copyOfRange(data, v[0], v[1]);
            } else if (r.field == 2 && r.wire == 0) prefix = r.varint();
            else if (!r.skip()) break;
        }
        if (r.failed) return false;
        c.subnet(subnet(ip, prefix));
        return true;
    }

    // One entry per line, in the forms domain lists are published in: V2Ray's "domain:", "full:", "keyword:" and "regexp:"
    // prefixes, a bare domain (with its subdomains), an address or a subnet. A '#' starts a comment, and anything after the first
    // blank (V2Ray's "@attribute" marks) is ignored.
    private static void readText(byte[] data, Collector c) {
        String text = new String(data, StandardCharsets.UTF_8);
        for (String raw : text.split("\n")) {
            String line = raw;
            int hash = line.indexOf('#');
            if (hash >= 0) line = line.substring(0, hash);
            line = line.trim();
            if (line.isEmpty() || line.startsWith("//")) continue;
            for (int i = 0; i < line.length(); i++) {
                if (Character.isWhitespace(line.charAt(i))) { line = line.substring(0, i); break; }
            }
            int colon = line.indexOf(':');
            String prefix = colon > 0 ? line.substring(0, colon).toLowerCase(Locale.ROOT) : "";
            if (prefix.equals("domain")) c.domain(line.substring(colon + 1), true);
            else if (prefix.equals("full")) c.domain(line.substring(colon + 1), false);
            else if (prefix.equals("keyword") || prefix.equals("regexp") || prefix.equals("include")) c.stats.skipped++;
            else {
                String cidr = subnet(line);
                if (cidr != null) c.subnet(cidr);
                else c.domain(line, true);
            }
        }
    }

    // ---------- the lists together ----------

    /** The entries as the core's exclusion list takes them: duplicates dropped, one per line, in a stable order. */
    static String join(Collection<String> entries) {
        StringBuilder out = new StringBuilder();
        for (String e : new TreeSet<>(entries)) out.append(e).append('\n');
        return out.toString();
    }

    /** The joined entries as a list, for the core's exclusions. */
    static List<String> lines(String joined) {
        List<String> out = new ArrayList<>();
        int start = 0;
        while (start < joined.length()) {
            int end = joined.indexOf('\n', start);
            if (end < 0) end = joined.length();
            if (end > start) out.add(joined.substring(start, end));
            start = end + 1;
        }
        return out;
    }
}
