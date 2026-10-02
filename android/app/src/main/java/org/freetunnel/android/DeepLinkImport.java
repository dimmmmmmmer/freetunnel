package org.freetunnel.android;

import java.net.URI;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;

/** Decodes TrustTunnel's version 0/1 QUIC-varint TLV share links. */
final class DeepLinkImport {
    private static final int MAX_LINK_CHARS = 131072;
    private static final int MAX_PAYLOAD_BYTES = 65536;

    static final class Imported {
        final String name;
        final String hostname;
        final String toml;
        final boolean skipVerification;

        Imported(String name, String hostname, String toml, boolean skipVerification) {
            this.name = name;
            this.hostname = hostname;
            this.toml = toml;
            this.skipVerification = skipVerification;
        }
    }

    static final class ParseException extends Exception {
        ParseException(String message) { super(message); }
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
    }

    private DeepLinkImport() { }

    static boolean looksLikeLink(String text) {
        String s = text == null ? "" : text.trim();
        return s.startsWith("tt://") || s.startsWith("https://trusttunnel.org/qr.html")
                || s.startsWith("https://www.trusttunnel.org/qr.html");
    }

    static Imported parse(String link) throws ParseException {
        String payload = extractPayload(link);
        if (payload.isEmpty() || !payload.matches("[A-Za-z0-9_-]+"))
            throw new ParseException("Некорректная ссылка TrustTunnel");
        byte[] bytes;
        try { bytes = Base64.getUrlDecoder().decode(payload); }
        catch (IllegalArgumentException e) { throw new ParseException("Некорректная ссылка TrustTunnel"); }
        if (bytes.length == 0 || bytes.length > MAX_PAYLOAD_BYTES)
            throw new ParseException("Размер ссылки не поддерживается");

        Fields fields = decode(bytes);
        if (fields.version > 1) throw new ParseException("Версия ссылки пока не поддерживается");
        if (fields.hostname == null || fields.hostname.isEmpty() || fields.addresses.isEmpty()
                || fields.username == null || fields.username.isEmpty()
                || fields.password == null || fields.password.isEmpty())
            throw new ParseException("В ссылке не хватает данных сервера");
        String hostname = clean(fields.hostname).trim();
        if (hostname.isEmpty()) throw new ParseException("В ссылке не хватает данных сервера");
        String name = clean(fields.displayName).trim();
        if (name.isEmpty()) name = hostname;
        if (name.length() > 80) name = name.substring(0, 80);
        return new Imported(name, hostname, toToml(fields), fields.skipVerification);
    }

    private static String extractPayload(String link) throws ParseException {
        String text = link == null ? "" : link.trim();
        if (text.length() > MAX_LINK_CHARS) throw new ParseException("Ссылка слишком длинная");
        if (text.startsWith("tt://?")) return text.substring(6);
        if (text.startsWith("tt://")) return text.substring(5);
        try {
            URI uri = new URI(text);
            String host = uri.getHost();
            if (!"https".equals(uri.getScheme()) || host == null
                    || !("trusttunnel.org".equalsIgnoreCase(host)
                    || "www.trusttunnel.org".equalsIgnoreCase(host))
                    || !"/qr.html".equals(uri.getPath()))
                throw new ParseException("Ожидается ссылка TrustTunnel");
            String payload = queryValue(uri.getRawFragment());
            if (payload == null) payload = queryValue(uri.getRawQuery());
            if (payload != null) return payload;
        } catch (java.net.URISyntaxException e) {
            throw new ParseException("Некорректная ссылка TrustTunnel");
        }
        throw new ParseException("В ссылке нет конфигурации");
    }

    private static String queryValue(String part) {
        if (part == null) return null;
        for (String item : part.split("&"))
            if (item.startsWith("tt=")) return item.substring(3);
        return null;
    }

    private static Fields decode(byte[] bytes) throws ParseException {
        Fields fields = new Fields();
        int[] pos = {0};
        while (pos[0] < bytes.length) {
            long tag = readVarint(bytes, pos, bytes.length);
            long length = readVarint(bytes, pos, bytes.length);
            if (length > bytes.length - pos[0]) throw new ParseException("Ссылка повреждена");
            int end = pos[0] + (int) length;
            if (tag > 13) { pos[0] = end; continue; }
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
                default: break; // Ignore future fields, as the official decoder does.
            }
            pos[0] = end;
        }
        return fields;
    }

    private static long scalar(byte[] bytes, int[] pos, int end) throws ParseException {
        long value = readVarint(bytes, pos, end);
        if (pos[0] != end) throw new ParseException("Ссылка повреждена");
        return value;
    }

    private static boolean bool(byte[] bytes, int start, int end) throws ParseException {
        if (end - start != 1) throw new ParseException("Ссылка повреждена");
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
        } catch (CharacterCodingException e) { throw new ParseException("Некорректный текст в ссылке"); }
    }

    private static void decodeDns(byte[] bytes, int start, int end, List<String> dns) throws ParseException {
        int[] pos = {start};
        while (pos[0] < end) {
            long length = readVarint(bytes, pos, end);
            if (length > end - pos[0]) throw new ParseException("Ссылка повреждена");
            int next = pos[0] + (int) length;
            dns.add(utf8(bytes, pos[0], next));
            pos[0] = next;
        }
    }

    private static long readVarint(byte[] bytes, int[] pos, int end) throws ParseException {
        if (pos[0] >= end) throw new ParseException("Ссылка повреждена");
        int first = bytes[pos[0]++] & 0xff;
        int width = 1 << (first >>> 6);
        if (width - 1 > end - pos[0]) throw new ParseException("Ссылка повреждена");
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
                throw new ParseException("Некорректный сертификат в ссылке");
            int first = der[pos + 1] & 0xff;
            int header = 2;
            long length = first;
            if ((first & 0x80) != 0) {
                int count = first & 0x7f;
                if (count == 0 || count > 4 || pos + 2 + count > der.length)
                    throw new ParseException("Некорректный сертификат в ссылке");
                length = 0;
                for (int i = 0; i < count; i++) length = (length << 8) | (der[pos + 2 + i] & 0xff);
                header += count;
            }
            long end = pos + header + length;
            if (end > der.length) throw new ParseException("Некорректный сертификат в ссылке");
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
        out.append("loglevel = \"info\"\nvpn_mode = \"general\"\nkillswitch_enabled = false\n");
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
