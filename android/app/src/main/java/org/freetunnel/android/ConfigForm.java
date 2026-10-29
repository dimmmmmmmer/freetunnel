package org.freetunnel.android;

import org.tomlj.Toml;
import org.tomlj.TomlArray;
import org.tomlj.TomlParseResult;
import org.tomlj.TomlTable;

import java.net.InetAddress;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The editor's form (qml/CreateConfigOverlay.qml) and the text of the config it reads from and writes to. The form owns the fields it shows
 * and nothing else, as the desktop's does (keepWhatTheFormDoesNotShow in BackendConfigCreate.cpp): an edit changes the values of the
 * endpoint's keys in the file and leaves the rest as it was written, with its comments, its routes ([listener.tun]), the rules a file brings
 * (exclusions) and every key the form has no field for. Where that cannot be done in place (an endpoint written as an inline table, say),
 * the file is kept as it was but for its endpoint, which the form's replaces, with the endpoint's other keys put back in it: nothing the
 * form does not show is lost, and where that cannot be shown to hold, {@link #write} refuses (null) rather than rewrite the file
 * silently. A new config is the same text a link makes.
 *
 * <p>The checks are the desktop's (Backend::emitCreateConfigError): the four required fields, an address as host:port, a DNS list of
 * addresses and DoT/DoH URLs, a client random in whole bytes of hex, and a certificate that holds a certificate.
 * Plain Java but for the keys in the messages, so that a JVM tests it.
 */
final class ConfigForm {
    private ConfigForm() { }

    /** What the form holds. The lists are as typed (comma-separated); the file's own are lists. */
    static final class Fields {
        String hostname = "";
        String addresses = "";
        String username = "";
        String password = "";
        /** "http2" or "http3". */
        String protocol = "http2";
        String dns = "";
        String customSni = "";
        String clientRandom = "";
        /** PEM text. */
        String certificate = "";
        boolean allowIpv6 = true;
        boolean skipVerification;
        boolean antiDpi;
    }

    /** The longest config name, in characters (code points), as the desktop's kMaxConfigNameLength: the name is a file's name there. */
    static final int NAME_LIMIT = 50;

    /** Whether a name is over the limit, counted in code points: a character beyond the first plane is one, though it is two UTF-16 units. */
    static boolean nameTooLong(String name) { return name.codePointCount(0, name.length()) > NAME_LIMIT; }

    /** A name nobody typed (a host standing in for one) cut to the limit, never through a character (ConfigPaths::clippedConfigName). */
    static String clippedName(String name) {
        return nameTooLong(name) ? name.substring(0, name.offsetByCodePoints(0, NAME_LIMIT)) : name;
    }

    private static final Pattern DIGITS = Pattern.compile("\\d{1,5}");
    private static final Pattern IPV4 = Pattern.compile("(?:(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)\\.){3}(?:25[0-5]|2[0-4]\\d|1?\\d?\\d)");
    private static final Pattern IPV6 = Pattern.compile("[0-9A-Fa-f:.]+");
    private static final Pattern DNS_SCHEME = Pattern.compile("^(tls|https|quic|h3|sdns|udp|tcp)://", Pattern.CASE_INSENSITIVE);
    private static final Pattern DNS_SEPARATORS = Pattern.compile("[\\s,;]+");
    private static final Pattern CLIENT_RANDOM = Pattern.compile("^(?:[0-9a-fA-F]{2}){1,32}(?:/(?:[0-9a-fA-F]{2}){1,32})?$");
    private static final Pattern ENDPOINT_HEADER = Pattern.compile("(?m)^[ \\t]*\\[[ \\t]*endpoint[ \\t]*\\][ \\t]*(?:#[^\\n]*)?\\r?\\n");

    // ---- Reading ----

    /** What the form shows of this config's text: the defaults of a new one for text that holds no endpoint. */
    static Fields read(String toml) {
        Fields f = new Fields();
        TomlTable root = parse(toml);
        TomlTable endpoint = root == null ? null : root.getTable("endpoint");
        if (endpoint == null) return f;
        f.hostname = string(endpoint, "hostname");
        f.addresses = String.join(", ", strings(endpoint, "addresses"));
        f.username = string(endpoint, "username");
        f.password = string(endpoint, "password");
        f.protocol = "http3".equals(endpoint.get(Collections.singletonList("upstream_protocol"))) ? "http3" : "http2";
        // Where the core looks for them: the endpoint's list when the endpoint has one, however short, and the top of the file's when not
        // (readDns in ConfigToml.cpp).
        f.dns = String.join(", ", strings(endpoint.get(Collections.singletonList("dns_upstreams")) instanceof TomlArray ? endpoint : root, "dns_upstreams"));
        f.customSni = string(endpoint, "custom_sni");
        f.clientRandom = string(endpoint, "client_random");
        f.certificate = string(endpoint, "certificate");
        f.allowIpv6 = flag(endpoint, "has_ipv6", true);
        f.skipVerification = flag(endpoint, "skip_verification", false);
        f.antiDpi = flag(endpoint, "anti_dpi", false);
        return f;
    }

    private static TomlTable parse(String toml) {
        if (toml == null) return null;
        try {
            TomlParseResult parsed = Toml.parse(toml);
            return parsed.hasErrors() ? null : parsed;
        } catch (RuntimeException e) {
            return null;
        }
    }

    private static String string(TomlTable table, String key) {
        Object value = table.get(Collections.singletonList(key));
        return value instanceof String ? (String) value : "";
    }

    private static List<String> strings(TomlTable table, String key) {
        List<String> out = new ArrayList<>();
        Object value = table.get(Collections.singletonList(key));
        if (!(value instanceof TomlArray)) return out;
        TomlArray array = (TomlArray) value;
        for (int i = 0; i < array.size(); i++) {
            Object item = array.get(i);
            String text = item instanceof String ? ((String) item).trim() : "";
            if (!text.isEmpty()) out.add(text);
        }
        return out;
    }

    private static boolean flag(TomlTable table, String key, boolean otherwise) {
        Object value = table.get(Collections.singletonList(key));
        return value instanceof Boolean ? (Boolean) value : otherwise;
    }

    // ---- Checking ----

    /**
     * What is wrong with the form, as the string resource that says it (the desktop's words), or 0 for nothing. In the desktop's order: a
     * required field empty, an address that is no host:port, a DNS list that is not one, a client random that is not hex in whole bytes,
     * and a certificate field that holds no certificate.
     */
    static int problem(Fields f) {
        // An address list with no address in it (the desktop lets " , " through, and saves a config that has none) is as missing as none.
        if (f.hostname.trim().isEmpty() || addressList(f.addresses).isEmpty() || f.username.trim().isEmpty() || f.password.isEmpty())
            return R.string.editor_error_missing;
        for (String address : addressList(f.addresses)) if (!validAddress(address)) return R.string.editor_error_address;
        for (String server : dnsList(f.dns)) if (!validDns(server)) return R.string.editor_error_dns;
        if (!f.clientRandom.trim().isEmpty() && !CLIENT_RANDOM.matcher(f.clientRandom.trim()).matches()) return R.string.editor_error_random;
        if (!f.certificate.trim().isEmpty() && Certificates.fromPem(f.certificate.trim()).pem.trim().isEmpty()) return R.string.editor_error_certificate;
        return 0;
    }

    /** The addresses as typed, comma-separated, trimmed and without the empty ones. */
    static List<String> addressList(String typed) {
        List<String> out = new ArrayList<>();
        for (String address : typed.split(",")) if (!address.trim().isEmpty()) out.add(address.trim());
        return out;
    }

    /** The DNS servers as typed: separated by white space, commas or semicolons, as the core's list is written. */
    static List<String> dnsList(String typed) {
        List<String> out = new ArrayList<>();
        for (String server : DNS_SEPARATORS.split(typed.trim())) if (!server.isEmpty()) out.add(server);
        return out;
    }

    /** host:port, with a port from 1 to 65535 and something before it (isValidEndpointAddress). */
    static boolean validAddress(String address) {
        String a = address.trim();
        int colon = a.lastIndexOf(':');
        if (colon <= 0) return false;
        String port = a.substring(colon + 1);
        if (!DIGITS.matcher(port).matches()) return false;
        int number = Integer.parseInt(port);
        return number >= 1 && number <= 65535;
    }

    /** A DNS server is an IP address, with a port or not, or a DoT, DoH or other URL of the core's kinds (validateDnsList). */
    static boolean validDns(String server) {
        String d = server.trim();
        if (DNS_SCHEME.matcher(d).find()) return true;
        String host = d;
        int colon = host.lastIndexOf(':');
        if (colon > 0 && host.indexOf('[') < 0 && host.indexOf(':') == colon) host = host.substring(0, colon);
        return ipLiteral(host);
    }

    // Only a literal is parsed: an address is never looked up.
    private static boolean ipLiteral(String host) {
        if (IPV4.matcher(host).matches()) return true;
        if (host.indexOf(':') < 0 || !IPV6.matcher(host).matches()) return false;
        try { InetAddress.getByName(host); return true; }
        catch (Exception e) { return false; }
    }

    /** The client random as the core takes it: no trailing slash, and nothing at all where there is nothing before a slash. */
    static String clientRandomForCore(String value) {
        String v = value.trim();
        while (v.endsWith("/")) v = v.substring(0, v.length() - 1);
        return v.startsWith("/") ? "" : v;
    }

    /** The certificates of what was typed, as PEM ending a line, and whether anything else was left out; empty where there was none. */
    static Certificates.Kept certificates(String typed) {
        String text = typed.trim();
        if (text.isEmpty()) return new Certificates.Kept(new byte[0], "", false);
        Certificates.Kept kept = Certificates.fromPem(text);
        String pem = kept.pem.endsWith("\n") || kept.pem.isEmpty() ? kept.pem : kept.pem + "\n";
        return new Certificates.Kept(kept.der, pem, kept.dropped);
    }

    // ---- Writing ----

    /**
     * The config's text with these fields: the file's own with the form's keys given the form's values, or, for a new config (null), or a
     * text that is no config at all (it holds nothing to keep), the text a link makes. Where the file's endpoint cannot be edited in place
     * the file is kept as it is but for the endpoint ({@link #rewrite}); and where even that cannot be shown to lose nothing, null: the
     * save is refused, and the file is as it was. The form is checked ({@link #problem}) before this.
     */
    static String write(String existing, Fields f) {
        Certificates.Kept certificate = certificates(f.certificate);
        if (existing != null) {
            String edited = edit(existing, f, certificate.pem);
            if (edited != null) return edited;
            if (parse(existing) != null) return rewrite(existing, f, certificate.pem);
        }
        return DeepLinkImport.configText(f.hostname.trim(), addressList(f.addresses), f.username.trim(), f.password, clientRandomForCore(f.clientRandom),
                dnsList(f.dns), f.customSni, f.allowIpv6, f.skipVerification, "http3".equals(f.protocol), f.antiDpi, certificate.pem);
    }

    /** The endpoint's keys the form shows, which it writes: the rest of the endpoint is the file's. */
    private static final Set<String> FORM_KEYS = new HashSet<>(Arrays.asList("hostname", "addresses", "username", "password", "client_random",
            "custom_sni", "has_ipv6", "skip_verification", "upstream_protocol", "anti_dpi", "certificate", "dns_upstreams"));
    /** What a rewrite takes out of the file: the endpoint, and the DNS list at the top, which the endpoint's list is read before. */
    private static final Set<List<String>> REPLACED = new HashSet<>(Arrays.asList(
            Collections.singletonList("endpoint"), Collections.singletonList("dns_upstreams")));

    /**
     * Where the endpoint cannot be edited in place (written as an inline table or in a form the line reader does not read, or with
     * something in it that cannot be written back): the file as it is, with its endpoint and DNS list taken out and the form's endpoint put
     * after it, with every key the old one had that the form has no field for. As the desktop's keepWhatTheFormDoesNotShow carries
     * the tunnel's [listener.tun], the other root and endpoint keys and the other tables, so does this, with the file's own text. A tunnel
     * the file does not have is the default. The result is read back and compared with the file, apart from what the form owns, so that
     * whatever this misread is never written; null then.
     */
    private static String rewrite(String existing, Fields f, String pem) {
        TomlTable old = parse(existing);
        if (old == null) return null;
        String rest = TomlLines.without(existing, REPLACED);
        if (rest == null) return null;
        StringBuilder text = new StringBuilder(rest);
        if (text.length() > 0 && text.charAt(text.length() - 1) != '\n') text.append('\n');
        text.append('\n').append(DeepLinkImport.endpointText(f.hostname.trim(), addressList(f.addresses), f.username.trim(), f.password,
                clientRandomForCore(f.clientRandom), dnsList(f.dns), f.customSni, f.allowIpv6, f.skipVerification, "http3".equals(f.protocol), f.antiDpi, pem));
        TomlTable endpoint = old.getTable("endpoint");
        if (endpoint != null) {
            for (String key : sorted(endpoint)) {
                if (FORM_KEYS.contains(key)) continue;
                String value = valueText(endpoint.get(Collections.singletonList(key)));
                if (value == null) return null;
                text.append(keyText(key)).append(" = ").append(value).append('\n');
            }
        }
        boolean hadTunnel = old.get(Arrays.asList("listener", "tun")) instanceof TomlTable;
        if (!hadTunnel) text.append('\n').append(DeepLinkImport.tunnelText());
        String result = text.toString();
        TomlTable written = parse(result);
        if (written == null) return null;
        String before = signature(old, false);
        return before != null && before.equals(signature(written, !hadTunnel)) && same(read(result), f, pem) ? result : null;
    }

    // What the file holds that the form does not own, as one text, to tell by whether a rewrite changed any of it. The endpoint counts only
    // for the keys it has beyond the form's, and the tunnel not at all when a rewrite added the default one. Null for a value that cannot
    // be written down.
    private static String signature(TomlTable root, boolean withoutTunnel) {
        StringBuilder out = new StringBuilder();
        for (String key : sorted(root)) {
            if (key.equals("dns_upstreams")) continue;
            Object value = root.get(Collections.singletonList(key));
            String text;
            if (key.equals("endpoint") && value instanceof TomlTable) text = inlineTable((TomlTable) value, FORM_KEYS);
            else if (key.equals("listener") && withoutTunnel && value instanceof TomlTable) text = inlineTable((TomlTable) value, Collections.singleton("tun"));
            else text = valueText(value);
            if (text == null) return null;
            if ((key.equals("endpoint") || (key.equals("listener") && withoutTunnel)) && text.equals("{}")) continue;
            out.append(keyText(key)).append('=').append(text).append(';');
        }
        return out.toString();
    }

    private static List<String> sorted(TomlTable table) {
        List<String> keys = new ArrayList<>(table.keySet());
        Collections.sort(keys);
        return keys;
    }

    // A TOML value as one line of text, or null for a kind this does not write. Tables are written inline, with their keys in order.
    private static String valueText(Object value) {
        if (value instanceof String) return basic((String) value);
        if (value instanceof Boolean || value instanceof Long || value instanceof Integer) return value.toString();
        if (value instanceof Double) {
            double d = (Double) value;
            if (Double.isNaN(d)) return "nan";
            if (Double.isInfinite(d)) return d > 0 ? "inf" : "-inf";
            return Double.toString(d);
        }
        if (value instanceof TomlTable) return inlineTable((TomlTable) value, Collections.<String>emptySet());
        if (value instanceof TomlArray) {
            TomlArray array = (TomlArray) value;
            StringBuilder out = new StringBuilder("[");
            for (int i = 0; i < array.size(); i++) {
                String item = valueText(array.get(i));
                if (item == null) return null;
                if (i > 0) out.append(", ");
                out.append(item);
            }
            return out.append(']').toString();
        }
        // The dates and times: the text the parser's own types give for them, which the read-back checks.
        return value instanceof java.time.temporal.Temporal ? value.toString() : null;
    }

    private static String inlineTable(TomlTable table, Set<String> skip) {
        StringBuilder out = new StringBuilder("{");
        boolean first = true;
        for (String key : sorted(table)) {
            if (skip.contains(key)) continue;
            String value = valueText(table.get(Collections.singletonList(key)));
            if (value == null) return null;
            out.append(first ? " " : ", ").append(keyText(key)).append(" = ").append(value);
            first = false;
        }
        return out.append(first ? "}" : " }").toString();
    }

    private static String keyText(String key) {
        return BARE_KEY.matcher(key).matches() ? key : basic(key);
    }

    private static final Pattern BARE_KEY = Pattern.compile("[A-Za-z0-9_-]+");

    // A basic string, written whole: what DeepLinkImport.quoted leaves out of a value of the form is kept here, since this is the file's.
    private static String basic(String text) {
        StringBuilder out = new StringBuilder(text.length() + 2).append('"');
        for (int i = 0; i < text.length(); i++) {
            char c = text.charAt(i);
            if (c == '\\' || c == '"') out.append('\\').append(c);
            else if (c == '\n') out.append("\\n");
            else if (c == '\t') out.append("\\t");
            else if (c == '\r') out.append("\\r");
            else if (c == '\b') out.append("\\b");
            else if (c == '\f') out.append("\\f");
            else if (c < 0x20 || c == 0x7f) out.append(String.format("\\u%04X", (int) c));
            else out.append(c);
        }
        return out.append('"').toString();
    }

    // In place, or null. Each key of [endpoint] the file has is given its new value where it is; each it has not is added under the
    // header; and the result is read back, so that whatever this misread of the file is no config that was not asked for.
    private static String edit(String existing, Fields f, String pem) {
        TomlTable root = parse(existing);
        TomlTable endpoint = root == null ? null : root.getTable("endpoint");
        if (endpoint == null) return null;
        Map<String, String> values = new LinkedHashMap<>();
        values.put("hostname", DeepLinkImport.quoted(f.hostname.trim()));
        values.put("addresses", DeepLinkImport.listText(addressList(f.addresses)));
        values.put("username", DeepLinkImport.quoted(f.username.trim()));
        values.put("password", DeepLinkImport.quoted(f.password));
        values.put("client_random", DeepLinkImport.quoted(clientRandomForCore(f.clientRandom)));
        values.put("custom_sni", DeepLinkImport.quoted(f.customSni));
        values.put("has_ipv6", String.valueOf(f.allowIpv6));
        values.put("skip_verification", String.valueOf(f.skipVerification));
        values.put("upstream_protocol", DeepLinkImport.quoted("http3".equals(f.protocol) ? "http3" : "http2"));
        values.put("anti_dpi", String.valueOf(f.antiDpi));
        values.put("certificate", pem.isEmpty() ? "\"\"" : "\"\"\"\n" + pem + "\"\"\"");
        Map<List<String>, String> replace = new LinkedHashMap<>();
        StringBuilder added = new StringBuilder();
        for (Map.Entry<String, String> value : values.entrySet()) {
            if (endpoint.get(Collections.singletonList(value.getKey())) != null) replace.put(Arrays.asList("endpoint", value.getKey()), value.getValue());
            else added.append(value.getKey()).append(" = ").append(value.getValue()).append('\n');
        }
        // The DNS list is where the core reads it: the endpoint's, else the top of the file's, and the endpoint's where there is none.
        String dns = DeepLinkImport.listText(dnsList(f.dns));
        if (endpoint.get(Collections.singletonList("dns_upstreams")) instanceof TomlArray) replace.put(Arrays.asList("endpoint", "dns_upstreams"), dns);
        else if (root.get(Collections.singletonList("dns_upstreams")) != null) replace.put(Collections.singletonList("dns_upstreams"), dns);
        else added.append("dns_upstreams = ").append(dns).append('\n');

        String text = TomlLines.edit(existing, Collections.emptySet(), replace);
        if (text == null) return null;
        if (added.length() > 0) {
            Matcher header = ENDPOINT_HEADER.matcher(text);
            if (!header.find()) return null;
            text = text.substring(0, header.end()) + added + text.substring(header.end());
        }
        return same(read(text), f, pem) ? text : null;
    }

    // Whether what is read from the text is what the form was given, as it is written.
    private static boolean same(Fields read, Fields f, String pem) {
        return read.hostname.equals(f.hostname.trim()) && addressList(read.addresses).equals(addressList(f.addresses))
                && read.username.equals(f.username.trim()) && read.password.equals(f.password)
                && dnsList(read.dns).equals(dnsList(f.dns)) && read.customSni.equals(f.customSni)
                && read.clientRandom.equals(clientRandomForCore(f.clientRandom)) && read.protocol.equals("http3".equals(f.protocol) ? "http3" : "http2")
                && read.allowIpv6 == f.allowIpv6 && read.skipVerification == f.skipVerification && read.antiDpi == f.antiDpi
                && read.certificate.trim().equals(pem.trim());
    }
}
