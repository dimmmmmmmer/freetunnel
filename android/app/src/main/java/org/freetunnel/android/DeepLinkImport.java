package org.freetunnel.android;

import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;

/** Decodes TrustTunnel's version 0-2 QUIC-varint TLV share links. */
final class DeepLinkImport {
    private static final int MAX_LINK_CHARS = 131072;
    private static final int MAX_PAYLOAD_BYTES = 65536;
    // TrustTunnel endpoint 1.3.0 writes version 2 into every link, even one without
    // a version 2 field, so stopping at 1 refused all of them. This app never writes
    // links; the desktop's share links stay at version 1, which older readers accept.
    static final long MAX_READ_VERSION = 2;
    // A subscription address is a short https URL. Keep this equal to the desktop's
    // cap, so that both apps refuse the same links.
    static final int MAX_SUBSCRIPTION_URL_BYTES = 2048;

    static final class Imported {
        final String name;
        final String toml;
        // The link also named a subscription. Only its static settings are imported:
        // this version cannot follow a subscription, and its address, which carries
        // the VPN password, is not kept anywhere.
        final boolean subscriptionNotKept;

        Imported(String name, String toml, boolean subscriptionNotKept) {
            this.name = name;
            this.toml = toml;
            this.subscriptionNotKept = subscriptionNotKept;
        }
    }

    static final class ParseException extends Exception {
        final int messageResource;
        ParseException(int messageResource) { super("Invalid TrustTunnel link"); this.messageResource = messageResource; }
    }

    private static final class Fields {
        long version;
        String hostname;
        final List<String> addresses = new ArrayList<>();
        String customSni = "";
        boolean hasIpv6 = true;
        String username;
        String password;
        boolean skipVerification;
        byte[] certificate = new byte[0];
        long protocol = 1;
        boolean antiDpi;
        String clientRandom = "";
        String displayName = "";
        final List<String> dnsUpstreams = new ArrayList<>();
        String subscriptionUrl;
        String authKey = "";
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

        Fields fields = decode(bytes);
        if (fields.version > MAX_READ_VERSION) throw new ParseException(R.string.text_unsupported_link_version);
        boolean subscription = fields.subscriptionUrl != null;
        if (subscription && !isValidSubscriptionUrl(fields.subscriptionUrl))
            throw new ParseException(R.string.text_the_link_contains_an_invalid_subscription_address);
        // An empty key is how a server without one is exported. A real key needs
        // TrustTunnel 1.3: core 1.1.7 would ignore it, and the server would then
        // refuse every connection without saying why.
        if (!fields.authKey.isEmpty()) {
            if (!isHex(fields.authKey)) throw new ParseException(R.string.text_the_link_is_damaged);
            throw new ParseException(R.string.text_this_server_needs_a_newer_trusttunnel_core);
        }
        String hostname = fields.hostname == null ? "" : clean(fields.hostname).trim();
        if (hostname.isEmpty() || fields.addresses.isEmpty()
                || fields.username == null || fields.username.isEmpty()
                || fields.password == null || fields.password.isEmpty())
            // With a subscription the static fields are optional, and a link without
            // them can only work once the subscription is fetched.
            throw new ParseException(subscription ? R.string.text_subscription_links_are_not_supported_yet
                    : R.string.text_the_link_is_missing_required_server_settings);
        String name = clean(fields.displayName).trim();
        if (name.isEmpty()) name = hostname;
        if (name.length() > 80) name = name.substring(0, 80);
        return new Imported(name, toToml(fields), subscription);
    }

    // TrustTunnel requires https://. The address is not used or shown in this
    // version, but a link that carries a malformed one is refused, not half-read.
    private static boolean isValidSubscriptionUrl(String url) {
        if (!url.startsWith("https://")) return false;
        if (url.getBytes(StandardCharsets.UTF_8).length > MAX_SUBSCRIPTION_URL_BYTES) return false;
        // Control characters include tab and line breaks; space characters include
        // no-break and other Unicode spaces. Together they cover all whitespace.
        for (int i = 0; i < url.length(); i++) {
            char c = url.charAt(i);
            if (Character.isISOControl(c) || Character.isSpaceChar(c)) return false;
        }
        return true;
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

    private static Fields decode(byte[] bytes) throws ParseException {
        Fields fields = new Fields();
        int[] pos = {0};
        while (pos[0] < bytes.length) {
            long tag = readVarint(bytes, pos, bytes.length);
            long length = readVarint(bytes, pos, bytes.length);
            if (length > bytes.length - pos[0]) throw new ParseException(R.string.text_the_link_is_damaged);
            int end = pos[0] + (int) length;
            if (tag > 15) { pos[0] = end; continue; }
            switch ((int) tag) {
                case 0: fields.version = scalar(bytes, pos, end); break;
                case 1: fields.hostname = utf8(bytes, pos[0], end); break;
                case 2: fields.addresses.add(utf8(bytes, pos[0], end)); break;
                case 3: fields.customSni = utf8(bytes, pos[0], end); break;
                case 4: fields.hasIpv6 = bool(bytes, pos[0], end); break;
                case 5: fields.username = utf8(bytes, pos[0], end); break;
                case 6: fields.password = utf8(bytes, pos[0], end); break;
                case 7: fields.skipVerification = bool(bytes, pos[0], end); break;
                case 8: fields.certificate = copy(bytes, pos[0], end); break;
                case 9: fields.protocol = scalar(bytes, pos, end); break;
                case 10: fields.antiDpi = bool(bytes, pos[0], end); break;
                case 11: fields.clientRandom = utf8(bytes, pos[0], end); break;
                case 12: fields.displayName = utf8(bytes, pos[0], end); break;
                case 13: decodeDns(bytes, pos[0], end, fields.dnsUpstreams); break;
                // Read in any version, as TrustTunnel's own decoder does.
                case 14: fields.subscriptionUrl = utf8(bytes, pos[0], end); break;
                case 15: fields.authKey = utf8(bytes, pos[0], end); break;
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

    private static String certificatePem(byte[] der) throws ParseException {
        if (der.length == 0) return "";
        StringBuilder out = new StringBuilder();
        int pos = 0;
        while (pos < der.length) {
            if (pos + 2 > der.length || (der[pos] & 0xff) != 0x30)
                throw new ParseException(R.string.text_the_link_contains_an_invalid_certificate);
            int first = der[pos + 1] & 0xff;
            int header = 2;
            long length = first;
            if ((first & 0x80) != 0) {
                int count = first & 0x7f;
                if (count == 0 || count > 4 || pos + 2 + count > der.length)
                    throw new ParseException(R.string.text_the_link_contains_an_invalid_certificate);
                length = 0;
                for (int i = 0; i < count; i++) length = (length << 8) | (der[pos + 2 + i] & 0xff);
                header += count;
            }
            long end = pos + header + length;
            if (end > der.length) throw new ParseException(R.string.text_the_link_contains_an_invalid_certificate);
            String b64 = Base64.getEncoder().encodeToString(copy(der, pos, (int) end));
            out.append("-----BEGIN CERTIFICATE-----\n");
            for (int i = 0; i < b64.length(); i += 64)
                out.append(b64, i, Math.min(i + 64, b64.length())).append('\n');
            out.append("-----END CERTIFICATE-----\n");
            pos = (int) end;
        }
        return out.toString();
    }

    private static String toToml(Fields f) throws ParseException {
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
        String pem = certificatePem(f.certificate);
        if (pem.isEmpty()) out.append("certificate = \"\"\n");
        else out.append("certificate = \"\"\"\n").append(pem).append("\"\"\"\n");
        out.append("\n[listener.tun]\nbound_if = \"\"\nmtu_size = 1500\nchange_system_dns = true\n");
        out.append("included_routes = [\"0.0.0.0/0\", \"2000::/3\"]\n");
        out.append("excluded_routes = [\"0.0.0.0/8\", \"10.0.0.0/8\", \"169.254.0.0/16\", ");
        out.append("\"172.16.0.0/12\", \"192.168.0.0/16\", \"224.0.0.0/3\"]\n");
        return out.toString();
    }
}
