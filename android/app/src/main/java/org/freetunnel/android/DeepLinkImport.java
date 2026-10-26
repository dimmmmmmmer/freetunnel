package org.freetunnel.android;

import org.tomlj.TomlArray;
import org.tomlj.TomlTable;

import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;

/**
 * Decodes TrustTunnel's version 0-2 QUIC-varint TLV share links, and reads the TOML
 * file a TrustTunnel endpoint exports by the same rules.
 */
final class DeepLinkImport {
    private static final int MAX_LINK_CHARS = 131072;
    private static final int MAX_PAYLOAD_BYTES = 65536;
    // TrustTunnel endpoint 1.3.0 writes version 2 into every link, even one without
    // a version 2 field, so stopping at 1 refused all of them. This app never writes
    // links; the desktop's share links stay at version 1, which older readers accept.
    static final long MAX_READ_VERSION = 2;
    // A subscription address is a short https URL. This and the rest of the rule
    // for one are the desktop's (DEEP_LINK.md, tag 0x0E), so that both apps refuse
    // the same links.
    static final int MAX_SUBSCRIPTION_URL_BYTES = 2048;

    static final class Imported {
        // A link's name, or its hostname when it has none. An export's name, or
        // empty when it has none, so that the caller names it as it names any
        // other TOML file: after the file, or as pasted from the clipboard.
        final String name;
        final String toml;
        // The link also named a subscription. Only its static settings are imported:
        // this version cannot follow a subscription, and its address, which carries
        // the VPN password, is not kept anywhere.
        final boolean subscriptionNotKept;
        // The link's certificate held more than certificates, and only those were kept.
        final boolean certificateIgnored;

        Imported(String name, String toml, boolean subscriptionNotKept, boolean certificateIgnored) {
            this.name = name;
            this.toml = toml;
            this.subscriptionNotKept = subscriptionNotKept;
            this.certificateIgnored = certificateIgnored;
        }
    }

    static final class ParseException extends Exception {
        final int messageResource;
        ParseException(int messageResource) { super("Invalid TrustTunnel link"); this.messageResource = messageResource; }
    }

    private static final class Fields {
        String hostname;
        final List<String> addresses = new ArrayList<>();
        String customSni = "";
        boolean hasIpv6 = true;
        String username;
        String password;
        boolean skipVerification;
        byte[] certificate = new byte[0];
        // Set when the certificate came as PEM text, as in an endpoint's export, and
        // something in it that is not a certificate was left out as it was read.
        boolean certificateDropped;
        long protocol = 1;
        boolean antiDpi;
        String clientRandom = "";
        String displayName = "";
        final List<String> dnsUpstreams = new ArrayList<>();
        String subscriptionUrl;
        // The key from tag 0x0F, empty when the link has none. Not a secret
        // baked in: Semgrep takes any "key" given a literal for one.
        String authKey = ""; // nosemgrep: codacy.java.security.hard-coded-password
    }

    private DeepLinkImport() { }

    static boolean looksLikeLink(String text) {
        String s = text == null ? "" : text.trim();
        return s.startsWith("tt://") || s.startsWith("https://trusttunnel.org/qr.html")
                || s.startsWith("https://www.trusttunnel.org/qr.html");
    }

    static Imported parse(String link) throws ParseException {
        String payload = extractPayload(link);
        if (payload.isEmpty() || !payload.matches("[A-Za-z0-9_-]+={0,2}"))
            throw new ParseException(R.string.text_invalid_trusttunnel_link);
        byte[] bytes;
        try { bytes = Base64.getUrlDecoder().decode(payload); }
        catch (IllegalArgumentException e) { throw new ParseException(R.string.text_invalid_trusttunnel_link); }
        if (bytes.length == 0 || bytes.length > MAX_PAYLOAD_BYTES)
            throw new ParseException(R.string.text_unsupported_link_size);

        if (declaredVersion(bytes) > MAX_READ_VERSION) throw new ParseException(R.string.text_unsupported_link_version);
        Fields fields = decode(bytes);
        return finish(fields, fields.subscriptionUrl != null, true);
    }

    /**
     * The TOML file a TrustTunnel endpoint exports, as RoutingConfig recognises it:
     * the keys of [endpoint] at the top with no table, and the server's name,
     * client_random_prefix, client_random_auth_key and subscription address beside
     * them. It is read into the fields a link carries and goes the link's way from
     * there, so it gives the configuration a link with the same settings gives, and
     * is refused for the same reasons. Keys a link has no field for are left out, as
     * a link's unknown tags are, and a value of the wrong type counts as missing.
     */
    static Imported fromEndpointExport(TomlTable root) throws ParseException {
        // As in any TOML file: the endpoint writes an empty key into every export,
        // and only that is no key. Anything else is refused, blank or not a string,
        // since no core would read it as none. The desktop is more lenient here: it
        // trims the key and reads one that is not a string as none. The endpoint
        // writes only an empty key or a hex one, so the two differ only on a file
        // edited by hand, and Android keeps one rule for every TOML file it imports.
        Object auth = root.get("client_random_auth_key");
        if (auth != null && !"".equals(auth)) throw new ParseException(R.string.text_this_server_needs_a_newer_trusttunnel_core);
        Fields fields = new Fields();
        fields.hostname = string(root, "hostname");
        fields.addresses.addAll(strings(root, "addresses"));
        fields.customSni = string(root, "custom_sni");
        fields.hasIpv6 = flag(root, "has_ipv6", true);
        fields.username = string(root, "username");
        fields.password = string(root, "password");
        fields.skipVerification = flag(root, "skip_verification", false);
        // As the desktop writes it: http3 or else http2.
        fields.protocol = "http3".equals(root.get("upstream_protocol")) ? 2 : 1;
        fields.antiDpi = flag(root, "anti_dpi", false);
        // The endpoint calls it client_random_prefix. client_random, the client's own
        // name for it, is read when that is empty, as the desktop does.
        String prefix = string(root, "client_random_prefix");
        fields.clientRandom = prefix.isEmpty() ? string(root, "client_random") : prefix;
        fields.displayName = string(root, "name");
        fields.dnsUpstreams.addAll(strings(root, "dns_upstreams"));
        // PEM here, where a link has DER. The configuration is written from the DER
        // of its certificates, as a link's is, so both give the same text.
        Certificates.Kept certificate = Certificates.fromPem(string(root, "certificate"));
        fields.certificate = certificate.der;
        fields.certificateDropped = certificate.dropped;
        return finish(fields, root.get("subscription_url") != null, false);
    }

    // What a link or an export goes through once read, in the desktop's order: the
    // auth key, then the server's own settings, then whether the certificate holds
    // any certificate. Damage to a link's certificate was refused while decoding.
    private static Imported finish(Fields fields, boolean subscription, boolean link) throws ParseException {
        // An empty key is how a server without one is exported. A real key needs
        // TrustTunnel 1.3: core 1.1.7 would ignore it, and the server would then
        // refuse every connection without saying why.
        if (!fields.authKey.isEmpty()) throw new ParseException(R.string.text_this_server_needs_a_newer_trusttunnel_core);
        String hostname = fields.hostname == null ? "" : clean(fields.hostname).trim();
        if (hostname.isEmpty() || fields.addresses.isEmpty()
                || fields.username == null || fields.username.isEmpty()
                || fields.password == null || fields.password.isEmpty()) {
            // With a subscription the static fields are optional, and a link without
            // them can only work once the subscription is fetched.
            if (subscription) throw new ParseException(R.string.text_subscription_links_are_not_supported_yet);
            // An export is refused for this as its link is, in a file's words; the
            // desktop refuses one without addresses or a user name too. Left to
            // validation, an empty user name or password would be saved, and the
            // server would only refuse the connection later.
            throw new ParseException(link ? R.string.text_the_link_is_missing_required_server_settings
                    : R.string.text_the_configuration_is_missing_required_server_settings);
        }
        // Only the certificates are kept, so nothing else from the file the server's
        // certificate came from is stored. A certificate with none in it is refused:
        // the link or file meant to pin one, and the connection would only fail later.
        Certificates.Kept certificate = Certificates.fromDer(fields.certificate);
        boolean dropped = certificate.dropped || fields.certificateDropped;
        if (dropped && certificate.der.length == 0)
            throw new ParseException(link ? R.string.text_the_link_contains_an_invalid_certificate
                    : R.string.text_the_configuration_contains_an_invalid_certificate);
        String name = clean(fields.displayName).trim();
        if (name.isEmpty() && link) name = hostname;
        if (name.length() > 80) name = name.substring(0, 80);
        return new Imported(name, toToml(fields, certificate.pem), subscription, dropped);
    }

    // A string at the top of an export, or "" when it is missing or not a string.
    private static String string(TomlTable root, String key) {
        Object value = root.get(key);
        return value instanceof String ? (String) value : "";
    }

    // The strings of an array at the top of an export, trimmed and without empty
    // ones, as the desktop reads a list; items of another type are skipped.
    private static List<String> strings(TomlTable root, String key) {
        List<String> out = new ArrayList<>();
        Object value = root.get(key);
        if (!(value instanceof TomlArray)) return out;
        TomlArray array = (TomlArray) value;
        for (int i = 0; i < array.size(); i++) {
            Object item = array.get(i);
            String text = item instanceof String ? ((String) item).trim() : "";
            if (!text.isEmpty()) out.add(text);
        }
        return out;
    }

    private static boolean flag(TomlTable root, String key, boolean otherwise) {
        Object value = root.get(key);
        return value instanceof Boolean ? (Boolean) value : otherwise;
    }

    // TrustTunnel requires https://. The address is not used or shown in this
    // version, but a link that carries a malformed one is refused, not half-read.
    // Every copy of a repeated tag is checked, so a good last one cannot carry a
    // bad first one past the check.
    private static String subscriptionUrl(String url) throws ParseException {
        if (!isValidSubscriptionUrl(url))
            throw new ParseException(R.string.text_the_link_contains_an_invalid_subscription_address);
        return url;
    }

    private static boolean isValidSubscriptionUrl(String url) {
        if (!url.startsWith("https://")) return false;
        if (url.getBytes(StandardCharsets.UTF_8).length > MAX_SUBSCRIPTION_URL_BYTES) return false;
        // Control characters include tab and line breaks; space characters include
        // no-break and other Unicode spaces. Together they cover all whitespace.
        // Format characters are invisible ones such as U+200B and U+202E. By code
        // point, so one outside the BMP is not let through as two surrogates.
        for (int i = 0; i < url.length(); ) {
            int c = url.codePointAt(i);
            if (Character.isISOControl(c) || Character.isSpaceChar(c) || Character.getType(c) == Character.FORMAT)
                return false;
            i += Character.charCount(c);
        }
        return true;
    }

    // DER elements, one after another. Bytes that do not split into whole ones are
    // damage, as the desktop and TrustTunnel's own decoder take them, and every copy
    // is checked, as tags 0x0E and 0x0F are. Which of the elements are certificates
    // is finish()'s question.
    private static byte[] certificate(byte[] der) throws ParseException {
        if (Certificates.fromDer(der) == null)
            throw new ParseException(R.string.text_the_link_contains_an_invalid_certificate);
        return der;
    }

    // Whole bytes of hex, checked in every copy as TrustTunnel's own decoder does.
    // Empty is valid and means no key.
    private static String authKey(String key) throws ParseException {
        if (!isHex(key)) throw new ParseException(R.string.text_the_link_is_damaged);
        return key;
    }

    // Character.digit() would also accept other scripts' digits.
    private static boolean isHex(String text) {
        if (text.length() % 2 != 0) return false;
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        return true;
    }

    private static String extractPayload(String link) throws ParseException {
        if (link != null && link.length() > MAX_LINK_CHARS) throw new ParseException(R.string.text_the_link_is_too_long);
        String text = link == null ? "" : link.replaceAll("[\\t\\r\\n ]", "").trim();
        if (text.length() > MAX_LINK_CHARS) throw new ParseException(R.string.text_the_link_is_too_long);
        if (text.startsWith("tt://?")) return text.substring(6);
        if (text.startsWith("tt://")) return text.substring(5);
        try {
            URI uri = new URI(text);
            String host = uri.getHost();
            if (!"https".equals(uri.getScheme()) || host == null
                    || !("trusttunnel.org".equalsIgnoreCase(host)
                    || "www.trusttunnel.org".equalsIgnoreCase(host))
                    || !"/qr.html".equals(uri.getPath()))
                throw new ParseException(R.string.text_expected_a_trusttunnel_link);
            String payload = queryValue(uri.getRawFragment());
            if (payload == null) payload = queryValue(uri.getRawQuery());
            if (payload != null) return payload;
        } catch (java.net.URISyntaxException | IllegalArgumentException e) {
            throw new ParseException(R.string.text_invalid_trusttunnel_link);
        }
        throw new ParseException(R.string.text_the_link_contains_no_configuration);
    }

    private static String queryValue(String part) {
        if (part == null) return null;
        for (String item : part.split("&"))
            if (item.startsWith("tt=")) {
                try { return java.net.URLDecoder.decode(item.substring(3), "UTF-8"); }
                catch (java.io.UnsupportedEncodingException impossible) { throw new AssertionError(impossible); }
            }
        return null;
    }

    // Reads a record's length, leaving pos at its value, and returns where it ends.
    private static int recordEnd(byte[] bytes, int[] pos) throws ParseException {
        long length = readVarint(bytes, pos, bytes.length);
        if (length > bytes.length - pos[0]) throw new ParseException(R.string.text_the_link_is_damaged);
        return pos[0] + (int) length;
    }

    // The version a payload declares, read before any field: a newer format may
    // mean something else by a field, so a link in one is refused as newer, not as
    // damaged by what that field holds. Only damage to the records themselves
    // comes first. The last version record wins, as the desktop reads it.
    private static long declaredVersion(byte[] bytes) throws ParseException {
        long version = 0;
        int[] pos = {0};
        while (pos[0] < bytes.length) {
            long tag = readVarint(bytes, pos, bytes.length);
            int end = recordEnd(bytes, pos);
            if (tag == 0) version = scalar(bytes, pos, end);
            pos[0] = end;
        }
        return version;
    }

    private static Fields decode(byte[] bytes) throws ParseException {
        Fields fields = new Fields();
        int[] pos = {0};
        while (pos[0] < bytes.length) {
            long tag = readVarint(bytes, pos, bytes.length);
            int end = recordEnd(bytes, pos);
            if (tag > 15) { pos[0] = end; continue; }
            switch ((int) tag) {
                case 0: break; // Read and checked by declaredVersion().
                case 1: fields.hostname = utf8(bytes, pos[0], end); break;
                case 2: fields.addresses.add(utf8(bytes, pos[0], end)); break;
                case 3: fields.customSni = utf8(bytes, pos[0], end); break;
                case 4: fields.hasIpv6 = bool(bytes, pos[0], end); break;
                case 5: fields.username = utf8(bytes, pos[0], end); break;
                case 6: fields.password = utf8(bytes, pos[0], end); break;
                case 7: fields.skipVerification = bool(bytes, pos[0], end); break;
                case 8: fields.certificate = certificate(copy(bytes, pos[0], end)); break;
                case 9: fields.protocol = scalar(bytes, pos, end); break;
                case 10: fields.antiDpi = bool(bytes, pos[0], end); break;
                case 11: fields.clientRandom = utf8(bytes, pos[0], end); break;
                case 12: fields.displayName = utf8(bytes, pos[0], end); break;
                case 13: decodeDns(bytes, pos[0], end, fields.dnsUpstreams); break;
                // Read in any version, as TrustTunnel's own decoder does.
                case 14: fields.subscriptionUrl = subscriptionUrl(utf8(bytes, pos[0], end)); break;
                case 15: fields.authKey = authKey(utf8(bytes, pos[0], end)); break;
                default: break; // Ignore future fields, as the official decoder does.
            }
            pos[0] = end;
        }
        return fields;
    }

    private static long scalar(byte[] bytes, int[] pos, int end) throws ParseException {
        long value = readVarint(bytes, pos, end);
        if (pos[0] != end) throw new ParseException(R.string.text_the_link_is_damaged);
        return value;
    }

    private static boolean bool(byte[] bytes, int start, int end) throws ParseException {
        if (end - start != 1) throw new ParseException(R.string.text_the_link_is_damaged);
        return bytes[start] != 0;
    }

    private static byte[] copy(byte[] bytes, int start, int end) {
        byte[] result = new byte[end - start];
        System.arraycopy(bytes, start, result, 0, result.length);
        return result;
    }

    private static String utf8(byte[] bytes, int start, int end) throws ParseException {
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes, start, end - start)).toString();
        } catch (CharacterCodingException e) { throw new ParseException(R.string.text_the_link_contains_invalid_text); }
    }

    private static void decodeDns(byte[] bytes, int start, int end, List<String> dns) throws ParseException {
        int[] pos = {start};
        while (pos[0] < end) {
            long length = readVarint(bytes, pos, end);
            if (length > end - pos[0]) throw new ParseException(R.string.text_the_link_is_damaged);
            int next = pos[0] + (int) length;
            dns.add(utf8(bytes, pos[0], next));
            pos[0] = next;
        }
    }

    private static long readVarint(byte[] bytes, int[] pos, int end) throws ParseException {
        if (pos[0] >= end) throw new ParseException(R.string.text_the_link_is_damaged);
        int first = bytes[pos[0]++] & 0xff;
        int width = 1 << (first >>> 6);
        if (width - 1 > end - pos[0]) throw new ParseException(R.string.text_the_link_is_damaged);
        long value = first & 0x3f;
        for (int i = 1; i < width; i++) value = (value << 8) | (bytes[pos[0]++] & 0xffL);
        return value;
    }

    private static String clean(String text) {
        StringBuilder out = new StringBuilder(text.length());
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (!Character.isISOControl(c)) out.append(c);
        }
        return out.toString();
    }

    private static String quoted(String text) {
        StringBuilder out = new StringBuilder(text.length() + 2).append('"');
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (Character.isISOControl(c)) continue;
            if (c == '\\' || c == '"') out.append('\\');
            out.append(c);
        }
        return out.append('"').toString();
    }

    private static String list(List<String> values) {
        StringBuilder out = new StringBuilder("[");
        for (int i = 0; i < values.size(); i++) {
            if (i > 0) out.append(", ");
            out.append(quoted(values.get(i)));
        }
        return out.append(']').toString();
    }

    // The PEM is the link's certificates alone, as Certificates.fromDer renders them.
    private static String toToml(Fields f, String pem) {
        StringBuilder out = new StringBuilder();
        out.append("loglevel = \"info\"\nvpn_mode = \"general\"\nkillswitch_enabled = true\n");
        out.append("post_quantum_group_enabled = true\n");
        out.append("\n[endpoint]\n");
        out.append("hostname = ").append(quoted(f.hostname)).append('\n');
        out.append("addresses = ").append(list(f.addresses)).append('\n');
        out.append("username = ").append(quoted(f.username)).append('\n');
        out.append("password = ").append(quoted(f.password)).append('\n');
        out.append("client_random = ").append(quoted(f.clientRandom)).append('\n');
        out.append("dns_upstreams = ").append(list(f.dnsUpstreams)).append('\n');
        out.append("custom_sni = ").append(quoted(f.customSni)).append('\n');
        out.append("has_ipv6 = ").append(f.hasIpv6).append('\n');
        out.append("skip_verification = ").append(f.skipVerification).append('\n');
        out.append("upstream_protocol = ").append(quoted(f.protocol == 2 ? "http3" : "http2")).append('\n');
        out.append("anti_dpi = ").append(f.antiDpi).append('\n');
        if (pem.isEmpty()) out.append("certificate = \"\"\n");
        else out.append("certificate = \"\"\"\n").append(pem).append("\"\"\"\n");
        out.append("\n[listener.tun]\nbound_if = \"\"\nmtu_size = 1500\nchange_system_dns = true\n");
        out.append("included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n");
        out.append("excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", ");
        out.append("\"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n");
        return out.toString();
    }
}
