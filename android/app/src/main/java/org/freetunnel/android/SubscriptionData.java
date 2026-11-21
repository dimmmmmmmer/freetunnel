package org.freetunnel.android;

import org.json.JSONObject;
import org.json.JSONArray;
import org.tomlj.Toml;
import org.tomlj.TomlVersion;
import org.tomlj.TomlTable;
import java.net.URI;
import java.net.HttpURLConnection;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.concurrent.TimeUnit;

/** TrustTunnel v1 subscription documents. Addresses remain only in encrypted ConfigStore records. */
final class SubscriptionData {
    static final int MAX_BYTES = 65536;
    static final class Document {
        final String name, toml;
        Document(String name, String toml) { this.name = name; this.toml = toml; }
    }
    static URI address(String source) throws Exception {
        if (source == null || source.getBytes(StandardCharsets.UTF_8).length > 2560) throw new IOException("Invalid subscription address");
        URI uri = new URI(source);
        if (!"https".equals(uri.getScheme()) || uri.getHost() == null || uri.getPort() == 0 || uri.getPort() > 65535) throw new IOException("Invalid subscription address");
        String info = uri.getUserInfo();
        if (uri.getRawUserInfo() != null) {
            String rawUser = uri.getRawUserInfo().split(":", 2)[0];
            String user = java.net.URLDecoder.decode(rawUser.replace("+", "%2B"), "UTF-8");
            if (user.contains(":")) throw new IOException("Invalid subscription username");
        }
        if (info != null && info.chars().anyMatch(c -> c < 32 || c == 127)) throw new IOException("Invalid subscription credentials");
        return uri;
    }
    static String source(String text) throws Exception {
        if (DeepLinkImport.looksLikeLink(text)) return DeepLinkImport.subscription(text);
        org.tomlj.TomlParseResult root = Toml.parse(text, TomlVersion.V1_0_0);
        if (root.hasErrors()) throw new IOException("Invalid subscription configuration");
        TomlTable endpoint = root.getTable("endpoint");
        TomlTable table = endpoint == null ? root : endpoint;
        Object key = table.get("client_random_auth_key");
        if (key != null && !"".equals(key)) throw new IOException("Newer core required");
        String source = table.getString("subscription_url");
        if (source == null && table.getTable("subscription") != null)
            source = table.getTable("subscription").getString("url");
        if (source != null) address(source);
        return source;
    }
    interface Connections { HttpURLConnection open(URI uri) throws IOException; }
    static byte[] fetch(String source) throws Exception {
        return fetch(source, uri -> (HttpURLConnection)uri.toURL().openConnection());
    }
    static byte[] fetch(String source, Connections connections) throws Exception {
        URI original = address(source);
        String user = original.getUserInfo();
        if (user != null && !user.contains(":")) user += ":";
        String authority = original.getRawAuthority();
        authority = authority.substring(authority.lastIndexOf('@') + 1);
        URI uri = new URI("https://" + authority + original.getRawPath()
                + (original.getRawQuery() == null ? "" : "?" + original.getRawQuery()));
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(30);
        for (int redirects = 0; redirects <= 3; redirects++) {
            HttpURLConnection c = connections.open(uri);
            try {
                c.setInstanceFollowRedirects(false); c.setUseCaches(false); c.setAllowUserInteraction(false);
                c.setConnectTimeout(remaining(deadline)); c.setReadTimeout(remaining(deadline));
                c.setRequestProperty("Accept", "application/json"); c.setRequestProperty("Accept-Encoding", "identity");
                if (user != null) c.setRequestProperty("Authorization", "Basic " + Base64.getEncoder().encodeToString(user.getBytes(StandardCharsets.UTF_8)));
                int status = c.getResponseCode();
                if (status >= 300 && status < 400) {
                    String location = c.getHeaderField("Location");
                    if (location == null) throw new IOException("Invalid redirect");
                    URI next = uri.resolve(location);
                    if (!"https".equals(next.getScheme()) || next.getHost() == null || next.getRawUserInfo() != null
                            || !original.getHost().equalsIgnoreCase(next.getHost()) || port(original) != port(next)) throw new IOException("Cross-origin redirect refused");
                    uri = next; continue;
                }
                if (status != 200) throw new IOException("Subscription unavailable");
                ByteArrayOutputStream bytes = new ByteArrayOutputStream();
                try (InputStream stream = c.getInputStream()) {
                    byte[] block = new byte[4096];
                    for (;;) {
                        c.setReadTimeout(remaining(deadline));
                        int n = stream.read(block); if (n < 0) break;
                        if (bytes.size() + n > MAX_BYTES) throw new IOException("Subscription too large");
                        bytes.write(block, 0, n);
                    }
                }
                return bytes.toByteArray();
            } finally { c.disconnect(); }
        }
        throw new IOException("Too many redirects");
    }
    private static int port(URI uri) { return uri.getPort() < 0 ? 443 : uri.getPort(); }
    private static int remaining(long deadline) throws IOException {
        long ms = TimeUnit.NANOSECONDS.toMillis(deadline - System.nanoTime());
        if (ms <= 0) throw new IOException("Subscription timed out");
        return (int)Math.min(ms, 30000);
    }
    static Document apply(byte[] bytes, String previous) throws Exception {
        if (bytes.length > MAX_BYTES) throw new IOException("Subscription too large");
        JSONObject json = new JSONObject(new String(bytes, StandardCharsets.UTF_8));
        Object version = json.opt("version");
        if (!(version instanceof Number) || ((Number)version).doubleValue() != 1) throw new IOException("Unsupported subscription version");
        if (flag(json, "skip_verification", false) || !text(json, "client_random_auth_key", "").isEmpty()) throw new IOException("Unsupported subscription security");
        ConfigForm.Fields f = previous == null ? new ConfigForm.Fields() : ConfigForm.read(previous);
        f.hostname = required(json, "hostname"); f.addresses = required(json, "address");
        f.username = required(json, "username"); f.password = required(json, "password");
        boolean ipv6 = flag(json, "has_ipv6", true), antiDpi = flag(json, "anti_dpi", false);
        String protocol = text(json, "upstream_protocol", "http2");
        if (!protocol.equals("http2") && !protocol.equals("http3")) throw new IOException("Unsupported protocol");
        if (previous == null) { f.allowIpv6 = ipv6; f.antiDpi = antiDpi; f.protocol = protocol; }
        f.skipVerification = false;
        f.customSni = text(json, "custom_sni", ""); f.clientRandom = text(json, "client_random_prefix", "");
        f.certificate = text(json, "certificate", "");
        String name = text(json, "name", f.hostname);
        if (json.has("dns_upstreams")) {
            Object value = json.get("dns_upstreams");
            if (!(value instanceof JSONArray)) throw new IOException("Invalid DNS list");
            JSONArray dns = (JSONArray)value; java.util.List<String> servers = new java.util.ArrayList<>();
            for (int i = 0; i < dns.length(); i++) {
                if (!(dns.get(i) instanceof String)) throw new IOException("Invalid DNS entry");
                servers.add(dns.getString(i));
            }
            if (previous == null) f.dns = String.join(", ", servers);
        }
        if (ConfigForm.problem(f) != 0) throw new IOException("Invalid server configuration");
        String toml = ConfigForm.write(previous, f);
        if (toml == null) throw new IOException("Could not preserve configuration");
        RoutingConfig.validate(toml);
        return new Document(ConfigForm.clippedName(name), toml);
    }
    private static String required(JSONObject json, String key) throws Exception {
        String value = text(json, key, ""); if (value.isEmpty()) throw new IOException("Missing server field"); return value;
    }
    private static String text(JSONObject json, String key, String fallback) throws Exception {
        if (!json.has(key)) return fallback;
        Object value = json.get(key);
        if (!(value instanceof String)) throw new IOException("Invalid string field");
        String text = (String)value;
        if (text.getBytes(StandardCharsets.UTF_8).length > (key.equals("certificate") ? MAX_BYTES : 8192) || (!key.equals("certificate") && text.chars().anyMatch(c -> c < 32 || c == 127))) throw new IOException("Invalid string field");
        return text;
    }
    private static boolean flag(JSONObject json, String key, boolean fallback) throws Exception {
        if (!json.has(key)) return fallback;
        Object value = json.get(key);
        if (!(value instanceof Boolean)) throw new IOException("Invalid flag");
        return (Boolean)value;
    }
}
