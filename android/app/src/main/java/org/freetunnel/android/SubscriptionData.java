package org.freetunnel.android;

import org.json.JSONObject;
import org.json.JSONArray;
import org.tomlj.Toml;
import org.tomlj.TomlVersion;
import org.tomlj.TomlTable;
import java.net.IDN;
import java.net.URI;
import java.net.HttpURLConnection;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

/** TrustTunnel v1 subscription documents. Addresses remain only in encrypted ConfigStore records. */
final class SubscriptionData {
    static final int MAX_BYTES = 65536;
    // The whole fetch, redirects included, as the desktop's kFetchTimeoutMs.
    static final int TIMEOUT_MS = 20000;
    static final class Document {
        final String name, toml;
        Document(String name, String toml) { this.name = name; this.toml = toml; }
    }
    static URI address(String source) throws Exception {
        if (source == null || !DeepLinkImport.isSubscriptionText(source)) throw new IOException("Invalid subscription address");
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
    /**
     * What a config trusts a subscription fetch with, as the desktop's trust source (subscriptionRequestFor): its
     * hostname, its addresses as typed and its certificate as PEM, all "" for none. A fetch is pinned to the
     * certificate only when its address is that of the server the config names; see requestFor.
     */
    static final class Trust {
        final String hostname, addresses, certificate;
        static final Trust NONE = new Trust("", "", "");
        Trust(String hostname, String addresses, String certificate) {
            this.hostname = hostname; this.addresses = addresses; this.certificate = certificate;
        }
        static Trust ofConfig(String toml) {
            ConfigForm.Fields f = ConfigForm.read(toml);
            return new Trust(f.hostname, f.addresses, f.certificate);
        }
    }
    /** A subscription address found in imported text, and what the same text trusts it with. */
    static final class Offer {
        final String url;
        final Trust trust;
        Offer(String url, Trust trust) { this.url = url; this.trust = trust; }
    }
    /**
     * One fetch, decided: the address, and whether it trusts the config's certificates alone (pinned) and where it
     * connects. Without pinning, the system's CAs and DNS, as before. The desktop's SubscriptionFetchRequest.
     */
    static final class Request {
        final String source;
        final boolean pinned;
        final List<X509Certificate> pins;
        // The name the certificate has to carry, and the SNI, as the config writes it.
        final String verifyName;
        // Where to connect instead of the address's own host and port: null and -1 for the address's own.
        final String connectHost;
        final int connectPort;
        Request(String source, boolean pinned, List<X509Certificate> pins, String verifyName, String connectHost, int connectPort) {
            this.source = source; this.pinned = pinned; this.pins = pins; this.verifyName = verifyName;
            this.connectHost = connectHost; this.connectPort = connectPort;
        }
    }
    /** What a server answered: the status, a redirect's Location, and the body of a 200 (null for any other status). */
    static final class Reply {
        final int status;
        final String location;
        final byte[] body;
        Reply(int status, String location, byte[] body) { this.status = status; this.location = location; this.body = body; }
    }
    /** A fetch that failed for a reason the user can act on. Its message never holds the address or its credentials. */
    static final class FetchException extends IOException {
        enum Failure {
            // The config pins certificates and none of them can be read.
            BAD_PIN,
            // The server's certificate is not one the config vouches for.
            UNTRUSTED
        }
        final Failure failure;
        FetchException(Failure failure) {
            super(failure == Failure.BAD_PIN ? "Pinned certificate unreadable" : "Certificate not trusted");
            this.failure = failure;
        }
    }

    // A host as it names a server: in ACE form and in lower case, as the desktop's hostKey, so that the address's host
    // and the config's hostname compare equal however each is written. One with no ACE form, such as an IP address, in
    // lower case alone.
    static String hostKey(String host) {
        String ace = host;
        try {
            String converted = IDN.toASCII(host, IDN.ALLOW_UNASSIGNED);
            if (!converted.isEmpty()) ace = converted;
        } catch (RuntimeException notAName) { }
        return ace.toLowerCase(Locale.ROOT);
    }

    /**
     * The fetch of source with what trust vouches for, by the desktop's rule (subscriptionRequestFor): when the
     * address's host is the config's hostname, in ACE form and in any case, and the config pins a certificate, the
     * fetch trusts that certificate alone and connects to the config's first address, as the core does: the
     * endpoint's name need not resolve and its certificate is often self-signed. A subdomain of the hostname is
     * another host and is not pinned. Never throws: an address that is none is not pinned, and the fetch refuses it; once the host matches, a
     * failure to read the config's certificates or address pins the fetch to none, which it refuses (BAD_PIN).
     */
    static Request requestFor(String source, Trust trust) {
        Request plain = new Request(source, false, Collections.<X509Certificate>emptyList(), "", null, -1);
        if (trust == null || Certificates.blank(trust.certificate)) return plain;
        try {
            String host = new URI(source).getHost();
            if (host != null && host.startsWith("[") && host.endsWith("]")) host = host.substring(1, host.length() - 1);
            if (host == null || host.isEmpty() || !hostKey(host).equals(hostKey(trust.hostname))) return plain;
        } catch (Exception notAnAddress) {
            return plain;
        }
        // The host is the config's: from here the fetch is pinned, and a failure is never a reason to fall back to the
        // system's CAs. Pins that cannot be read are none, which the fetch refuses as BAD_PIN before dialing.
        try {
            List<X509Certificate> pins = pinsFrom(trust.certificate);
            // The first address the core could use. Without one, the address's own host: still pinned.
            String first = trust.addresses.split(",", -1)[0].trim();
            String connectHost = null;
            int connectPort = -1;
            if (ConfigForm.validAddress(first)) {
                int colon = first.lastIndexOf(':');
                String name = first.substring(0, colon).trim();
                if (name.startsWith("[") && name.endsWith("]")) name = name.substring(1, name.length() - 1);
                if (!name.isEmpty()) { connectHost = name; connectPort = Integer.parseInt(first.substring(colon + 1)); }
            }
            return new Request(source, true, pins, trust.hostname, connectHost, connectPort);
        } catch (Exception unusable) {
            return new Request(source, true, Collections.<X509Certificate>emptyList(), trust.hostname, null, -1);
        }
    }

    // Each certificate of the config's certificate field that this platform can read, in the order given. The field's
    // certificates come as whole DER elements one after another (Certificates.fromPem keeps only those), and one that
    // has a certificate's outer shape and no more is skipped here, not a reason to trust the system's CAs instead.
    private static List<X509Certificate> pinsFrom(String pem) throws Exception {
        List<X509Certificate> pins = new ArrayList<>();
        CertificateFactory factory = CertificateFactory.getInstance("X.509");
        for (byte[] der : Certificates.split(Certificates.fromPem(pem).der)) {
            try { pins.add((X509Certificate) factory.generateCertificate(new ByteArrayInputStream(der))); }
            catch (Exception unreadable) { }
        }
        return pins;
    }

    /** The subscription address in imported text, with what that text trusts it with; null when it names none. */
    static Offer offer(String text) throws Exception {
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
        if (source == null) return null;
        address(source);
        // Read with get() and a type test: getString would throw on a value of another type, which counts as missing.
        Object hostname = table.get("hostname"), certificate = table.get("certificate"), addresses = table.get("addresses");
        List<String> listed = new ArrayList<>();
        if (addresses instanceof String) { if (!((String) addresses).trim().isEmpty()) listed.add(((String) addresses).trim()); }
        else if (addresses instanceof org.tomlj.TomlArray) {
            org.tomlj.TomlArray array = (org.tomlj.TomlArray) addresses;
            for (int i = 0; i < array.size(); i++)
                if (array.get(i) instanceof String && !((String) array.get(i)).trim().isEmpty()) listed.add(((String) array.get(i)).trim());
        }
        return new Offer(source, new Trust(hostname instanceof String ? (String) hostname : "", String.join(", ", listed),
                certificate instanceof String ? (String) certificate : ""));
    }
    interface Connections { HttpURLConnection open(URI uri) throws IOException; }
    static byte[] fetch(Request request) throws Exception {
        return fetch(request, uri -> (HttpURLConnection)uri.toURL().openConnection(), PinnedHttps.DIRECT, TIMEOUT_MS);
    }
    static byte[] fetch(String source, Connections connections) throws Exception {
        return fetch(requestFor(source, Trust.NONE), connections, PinnedHttps.DIRECT, TIMEOUT_MS);
    }
    /**
     * connections serves an ordinary fetch (the system's CAs, DNS, the platform's HTTP) and dialer a pinned one, which
     * speaks HTTP/1.1 over its own TLS (PinnedHttps) so that it can connect to an address and still name the server.
     */
    static byte[] fetch(Request request, Connections connections, PinnedHttps.Dialer dialer, int timeoutMs) throws Exception {
        URI original = address(request.source);
        // Before anything is dialed: certificates that cannot be read are never a reason to trust the system's instead.
        if (request.pinned && request.pins.isEmpty()) throw new FetchException(FetchException.Failure.BAD_PIN);
        String user = original.getUserInfo();
        if (user != null && !user.contains(":")) user += ":";
        String authorization = user == null ? null : "Basic " + Base64.getEncoder().encodeToString(user.getBytes(StandardCharsets.UTF_8));
        String authority = original.getRawAuthority();
        authority = authority.substring(authority.lastIndexOf('@') + 1);
        URI uri = new URI("https://" + authority + original.getRawPath()
                + (original.getRawQuery() == null ? "" : "?" + original.getRawQuery()));
        long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs);
        for (int redirects = 0; redirects <= 3; redirects++) {
            // A new connection for each hop. A redirect is judged against the address as given, never against where
            // the fetch connected to, and a pinned fetch stays pinned: it connects to the same address again.
            Reply reply = request.pinned ? PinnedHttps.get(uri, authorization, request, dialer, deadline)
                    : viaConnection(connections, uri, authorization, deadline);
            if (reply.status >= 300 && reply.status < 400) {
                if (reply.location == null) throw new IOException("Invalid redirect");
                URI next = resolveRedirect(uri, reply.location);
                if (!"https".equals(next.getScheme()) || next.getHost() == null || next.getRawUserInfo() != null
                        || !original.getHost().equalsIgnoreCase(next.getHost()) || port(original) != port(next)) throw new IOException("Cross-origin redirect refused");
                uri = next; continue;
            }
            if (reply.status != 200) throw new IOException("Subscription unavailable");
            return reply.body;
        }
        throw new IOException("Too many redirects");
    }
    // One request through the platform's HTTP, with the system's CAs.
    private static Reply viaConnection(Connections connections, URI uri, String authorization, long deadline) throws IOException {
        HttpURLConnection c = connections.open(uri);
        try {
            c.setInstanceFollowRedirects(false); c.setUseCaches(false); c.setAllowUserInteraction(false);
            c.setConnectTimeout(remaining(deadline)); c.setReadTimeout(remaining(deadline));
            c.setRequestProperty("Accept", "application/json"); c.setRequestProperty("Accept-Encoding", "identity");
            if (authorization != null) c.setRequestProperty("Authorization", authorization);
            int status = c.getResponseCode();
            if (status >= 300 && status < 400) return new Reply(status, c.getHeaderField("Location"), null);
            if (status != 200) return new Reply(status, null, null);
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
            return new Reply(status, null, bytes.toByteArray());
        } finally { c.disconnect(); }
    }
    // URI.resolve drops the slash when the base path is empty ("https://host" resolved
    // against "v1/sub" gives "https://hostv1/sub"), so an empty path is made "/" first.
    // The base query stays: resolve gives a reference with a path no query of the
    // base's (RFC 3986 5.2.2), and one with no path, such as "?a=1", its own or the base's.
    static URI resolveRedirect(URI base, String location) throws Exception {
        String path = base.getRawPath();
        if (path != null && !path.isEmpty()) return base.resolve(location);
        return new URI(base.getScheme() + "://" + base.getRawAuthority() + "/"
                + (base.getRawQuery() == null ? "" : "?" + base.getRawQuery())).resolve(location);
    }
    static int port(URI uri) { return uri.getPort() < 0 ? 443 : uri.getPort(); }
    // The milliseconds left to the deadline, at most the whole fetch's: never 0, which a socket would take as no limit.
    static int remaining(long deadline) throws IOException {
        long ms = TimeUnit.NANOSECONDS.toMillis(deadline - System.nanoTime());
        if (ms <= 0) throw new IOException("Subscription timed out");
        return (int)Math.min(ms, TIMEOUT_MS);
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
        if (json.has("tls_profile")) {
            String stored = TlsProfiles.canonical(text(json, "tls_profile", ""));
            if (stored == null) throw new IOException("Unsupported TLS profile");
            f.tlsProfile = stored;
        }
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
