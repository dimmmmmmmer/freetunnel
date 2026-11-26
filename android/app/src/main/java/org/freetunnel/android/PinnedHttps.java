package org.freetunnel.android;

import android.annotation.SuppressLint;

import org.bouncycastle.asn1.ASN1Encodable;
import org.bouncycastle.asn1.ASN1ObjectIdentifier;
import org.bouncycastle.asn1.ASN1String;
import org.bouncycastle.asn1.x500.AttributeTypeAndValue;
import org.bouncycastle.asn1.x500.RDN;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x500.style.BCStyle;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Proxy;
import java.net.Socket;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyStore;
import java.security.cert.CertificateException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.SNIHostName;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

/**
 * One HTTPS GET for a subscription whose server the config names: TLS that trusts only the config's certificates, to
 * an address given as an IP number while the request still names the server, as the desktop does with Qt
 * (SubscriptionFetcher). HttpsURLConnection cannot do that: its name check and its SNI come from the URL, and its trust
 * from the system. So the TLS is the platform's SSLSocket, the name check is our own (the same on Conscrypt and on a
 * JVM, which differ in how they check one), and the HTTP/1.1 is written here, as LinkReceiver writes the server's side:
 * a request line, a few headers, and a response read with a limit on everything. Plain JDK but for the CN of a
 * certificate, so a JVM tests it with real sockets.
 */
final class PinnedHttps {
    /** Where a connection comes from. The app dials directly; a test dials its own server wherever it likes. */
    interface Dialer {
        Socket connect(String host, int port, int timeoutMs) throws IOException;
    }

    // No proxy, as the core uses none: a proxy would see the request, and one that is down would stop every refresh.
    static final Dialer DIRECT = (host, port, timeoutMs) -> {
        Socket socket = new Socket(Proxy.NO_PROXY);
        try {
            socket.connect(new InetSocketAddress(host, port), timeoutMs);
            return socket;
        } catch (IOException | RuntimeException failed) {
            try { socket.close(); } catch (IOException ignored) { }
            throw failed;
        }
    };

    private static final int MAX_HEAD = 16 * 1024;
    private static final int MAX_LINE = 8 * 1024;
    private static final Pattern STATUS = Pattern.compile("HTTP/1\\.[01] (\\d{3})( .*)?");
    private static final Pattern HEX = Pattern.compile("[0-9a-fA-F]+");
    private static final Pattern DIGITS = Pattern.compile("[0-9]+");
    private static final String SERVER_AUTH = "1.3.6.1.5.5.7.3.1";
    private static final String ANY_USAGE = "2.5.29.37.0";

    private PinnedHttps() { }

    /**
     * Asks uri's server, at the address the request names or else at uri's own host, for uri's path, trusting the
     * request's pins alone and the request's verify name as the server's name. The reply is judged by the caller; the
     * body is read only for a 200. Throws FetchException(UNTRUSTED) when the server's certificate was refused.
     */
    static SubscriptionData.Reply get(URI uri, String authorization, SubscriptionData.Request request, Dialer dialer, long deadline) throws IOException {
        PinTrust trust = new PinTrust(request.pins, request.verifyName);
        SSLContext context;
        try {
            // One context for each fetch, with our trust manager and no other: nothing is cached across fetches.
            context = SSLContext.getInstance("TLS");
            context.init(null, new TrustManager[]{trust}, null);
        } catch (GeneralSecurityException unavailable) {
            throw new IOException("TLS is unavailable");
        }
        String host = request.connectHost != null ? request.connectHost : bracketless(uri.getHost());
        int port = request.connectHost != null ? request.connectPort : SubscriptionData.port(uri);
        String name = SubscriptionData.hostKey(request.verifyName);
        Socket raw = dialer.connect(host, port, SubscriptionData.remaining(deadline));
        SSLSocket ssl = null;
        try {
            ssl = (SSLSocket) context.getSocketFactory().createSocket(raw, name, port, true);
            SSLParameters parameters = ssl.getSSLParameters();
            // The SNI is the server's name, not the address connected to. No SNI names an IP number (RFC 6066), and a
            // name that is not a valid one is sent without. No endpoint identification algorithm is set: the name is
            // checked by PinTrust, which is the same on every platform.
            if (!ConfigForm.ipLiteral(name)) {
                try { parameters.setServerNames(Collections.singletonList(new SNIHostName(name))); }
                catch (IllegalArgumentException notAHostName) { }
            }
            ssl.setSSLParameters(parameters);
            ssl.setSoTimeout(SubscriptionData.remaining(deadline));
            try { ssl.startHandshake(); }
            catch (IOException failed) {
                if (trust.refused) throw new SubscriptionData.FetchException(SubscriptionData.FetchException.Failure.UNTRUSTED);
                throw failed;
            }
            OutputStream out = ssl.getOutputStream();
            out.write(requestBytes(uri, authorization));
            out.flush();
            return readReply(new Wire(ssl.getInputStream(), ssl, deadline));
        } finally {
            // Closing the TLS socket closes the one under it; when there is none yet, the raw one is closed alone.
            try { (ssl != null ? ssl : raw).close(); } catch (IOException ignored) { }
        }
    }

    private static String bracketless(String host) {
        return host.startsWith("[") && host.endsWith("]") ? host.substring(1, host.length() - 1) : host;
    }

    // The request as the desktop's sends it, but for the User-Agent: HTTP/1.1, so that the Host header names the server
    // while the connection goes to an address; the response is not compressed; and the connection ends with it, so a
    // body with no length ends where it does. The credentials are a header of our own, never a URL's userinfo.
    static byte[] requestBytes(URI uri, String authorization) {
        URI ascii = URI.create(uri.toASCIIString());
        String path = ascii.getRawPath() == null || ascii.getRawPath().isEmpty() ? "/" : ascii.getRawPath();
        String authority = ascii.getRawAuthority();
        authority = authority.substring(authority.lastIndexOf('@') + 1);
        StringBuilder text = new StringBuilder("GET ").append(path);
        if (ascii.getRawQuery() != null) text.append('?').append(ascii.getRawQuery());
        text.append(" HTTP/1.1\r\nHost: ").append(authority)
                .append("\r\nAccept: application/json\r\nAccept-Encoding: identity\r\nConnection: close\r\n");
        if (authorization != null) text.append("Authorization: ").append(authorization).append("\r\n");
        return text.append("\r\n").toString().getBytes(StandardCharsets.ISO_8859_1);
    }

    // ---- The response ----

    // The socket's input with a limit on how long each read may take: what is left of the whole fetch's time.
    private static final class Wire {
        private final InputStream in;
        private final Socket socket;
        private final long deadline;
        private final byte[] buffer = new byte[8192];
        private int pos, end;

        Wire(InputStream in, Socket socket, long deadline) { this.in = in; this.socket = socket; this.deadline = deadline; }

        private boolean fill() throws IOException {
            socket.setSoTimeout(SubscriptionData.remaining(deadline));
            int n = in.read(buffer, 0, buffer.length);
            if (n < 0) return false;
            pos = 0;
            end = n;
            return true;
        }

        int read() throws IOException {
            if (pos == end && !fill()) return -1;
            return buffer[pos++] & 0xff;
        }

        int read(byte[] into, int off, int len) throws IOException {
            if (pos == end && !fill()) return -1;
            int n = Math.min(len, end - pos);
            System.arraycopy(buffer, pos, into, off, n);
            pos += n;
            return n;
        }

        // One line, without its line end (CRLF, or a bare LF), of at most MAX_LINE bytes. budget is what is left of the
        // bytes a part of the response may take in all, and is spent by what is read.
        String line(int[] budget) throws IOException {
            StringBuilder text = new StringBuilder();
            for (;;) {
                int b = read();
                if (b < 0) throw new IOException("Connection closed early");
                if (--budget[0] < 0) throw new IOException("Response head too large");
                if (b == '\n') break;
                if (text.length() >= MAX_LINE) throw new IOException("Response line too long");
                text.append((char) b);
            }
            int n = text.length();
            if (n > 0 && text.charAt(n - 1) == '\r') text.setLength(n - 1);
            return text.toString();
        }
    }

    private static SubscriptionData.Reply readReply(Wire wire) throws IOException {
        int[] budget = {MAX_HEAD};
        int status;
        List<String[]> headers;
        for (;;) {
            Matcher match = STATUS.matcher(wire.line(budget));
            if (!match.matches()) throw new IOException("Invalid response");
            status = Integer.parseInt(match.group(1));
            headers = headers(wire, budget);
            // An interim response (100 Continue, 103 Early Hints) comes before the real one, which is what is judged.
            // A switch to another protocol is no answer to this request.
            if (status == 101) throw new IOException("Invalid response");
            if (status < 100 || status >= 200) break;
        }
        // Anything but a 200 is judged on its status and, for a redirect, its Location: no body is read.
        if (status != 200) {
            List<String> location = values(headers, "location");
            return new SubscriptionData.Reply(status, location.isEmpty() ? null : location.get(0), null);
        }
        return new SubscriptionData.Reply(status, null, body(wire, headers));
    }

    private static List<String[]> headers(Wire wire, int[] budget) throws IOException {
        List<String[]> headers = new ArrayList<>();
        for (;;) {
            String line = wire.line(budget);
            if (line.isEmpty()) return headers;
            int colon = line.indexOf(':');
            // A folded line, or a name with a space in it, is not one this reads.
            if (colon <= 0 || line.charAt(0) == ' ' || line.charAt(0) == '\t' || !line.substring(0, colon).equals(line.substring(0, colon).trim()))
                throw new IOException("Invalid response");
            headers.add(new String[]{line.substring(0, colon).toLowerCase(Locale.ROOT), line.substring(colon + 1).trim()});
        }
    }

    private static List<String> values(List<String[]> headers, String name) {
        List<String> found = new ArrayList<>();
        for (String[] header : headers) if (header[0].equals(name)) found.add(header[1]);
        return found;
    }

    // The body of a 200. A transfer coding other than chunked is refused; with one, Content-Length is ignored
    // (RFC 9112 6.3). Otherwise the length given, which must be one number and no more than the cap, or what
    // comes until the connection ends, no more than the cap.
    private static byte[] body(Wire wire, List<String[]> headers) throws IOException {
        List<String> codings = values(headers, "transfer-encoding");
        if (!codings.isEmpty()) {
            String all = String.join(",", codings).toLowerCase(Locale.ROOT);
            if (!all.trim().equals("chunked")) throw new IOException("Unsupported transfer coding");
            return chunked(wire);
        }
        List<String> lengths = values(headers, "content-length");
        if (lengths.isEmpty()) return untilClosed(wire);
        for (String length : lengths)
            if (!DIGITS.matcher(length).matches() || !length.equals(lengths.get(0))) throw new IOException("Invalid response");
        String digits = lengths.get(0).replaceFirst("^0+", "");
        // Seven digits are over the cap already, and not parsed: a longer number could overflow.
        if (digits.length() > 7 || (!digits.isEmpty() && Integer.parseInt(digits) > SubscriptionData.MAX_BYTES))
            throw new IOException("Subscription too large");
        byte[] body = new byte[digits.isEmpty() ? 0 : Integer.parseInt(digits)];
        for (int got = 0; got < body.length; ) {
            int n = wire.read(body, got, body.length - got);
            if (n < 0) throw new IOException("Subscription cut short");
            got += n;
        }
        return body;
    }

    private static byte[] untilClosed(Wire wire) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] block = new byte[4096];
        for (;;) {
            int n = wire.read(block, 0, block.length);
            if (n < 0) return bytes.toByteArray();
            if (bytes.size() + n > SubscriptionData.MAX_BYTES) throw new IOException("Subscription too large");
            bytes.write(block, 0, n);
        }
    }

    // Chunks of a hex size (an extension after a semicolon is ignored), each followed by a line end, then the last,
    // empty chunk and trailers up to an empty line. Too large is known the moment the total passes the cap, before the
    // chunk that passes it is read.
    private static byte[] chunked(Wire wire) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        for (;;) {
            String line = wire.line(new int[]{MAX_LINE + 2});
            int semicolon = line.indexOf(';');
            String size = (semicolon < 0 ? line : line.substring(0, semicolon)).trim();
            if (!HEX.matcher(size).matches()) throw new IOException("Invalid response");
            size = size.replaceFirst("^0+", "");
            if (size.length() > 7) throw new IOException("Subscription too large");
            int length = size.isEmpty() ? 0 : Integer.parseInt(size, 16);
            if (length == 0) {
                int[] budget = {MAX_HEAD};
                while (!wire.line(budget).isEmpty()) { }
                return bytes.toByteArray();
            }
            if (bytes.size() + length > SubscriptionData.MAX_BYTES) throw new IOException("Subscription too large");
            byte[] chunk = new byte[length];
            for (int got = 0; got < length; ) {
                int n = wire.read(chunk, got, length - got);
                if (n < 0) throw new IOException("Subscription cut short");
                got += n;
            }
            bytes.write(chunk, 0, length);
            if (!wire.line(new int[]{MAX_LINE + 2}).isEmpty()) throw new IOException("Invalid response");
        }
    }

    // ---- Trust ----

    /**
     * What the desktop's pinning accepts (SubscriptionFetcher): the desktop gives the pins to the TLS library as its
     * only CAs, so a chain that checks out to a pin used as a CA is accepted, and, whatever the library says of a
     * chain's trust, the server's own certificate being one of the pins and in its dates is enough. In both, the
     * certificate also has to be for a server and for the name; a CA that signed it, a pin or not, never is enough for
     * a name that does not match.
     *
     * <p>No revocation is checked: neither the JVM's nor Android's default trust manager does unless asked, and the
     * pins are the config's own, not a public CA's, which have nowhere to say so.
     */
    // A trust manager of our own is the point here: the system's would trust the system's CAs, and the config's
    // certificates are the only ones this fetch may trust. It checks each chain, refuses what it does not vouch for, and
    // never accepts a client.
    @SuppressLint("CustomX509TrustManager")
    static final class PinTrust implements X509TrustManager {
        // Set when a chain was refused, so that the handshake's failure is told from a network's.
        volatile boolean refused;
        private final List<X509Certificate> pins;
        private final String verifyName;

        PinTrust(List<X509Certificate> pins, String verifyName) { this.pins = pins; this.verifyName = verifyName; }

        @Override public void checkClientTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            throw new CertificateException("Not a server");
        }

        @Override public void checkServerTrusted(X509Certificate[] chain, String authType) throws CertificateException {
            try {
                vet(chain, authType);
            } catch (CertificateException refusal) {
                refused = true;
                throw refusal;
            } catch (RuntimeException broken) {
                refused = true;
                throw new CertificateException("Certificate refused", broken);
            }
        }

        @Override public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }

        private void vet(X509Certificate[] chain, String authType) throws CertificateException {
            if (chain == null || chain.length == 0) throw new CertificateException("No certificate");
            X509Certificate leaf = chain[0];
            leaf.checkValidity(new Date());
            boolean isPin = false;
            byte[] der = leaf.getEncoded();
            for (X509Certificate pin : pins) isPin |= Arrays.equals(der, pin.getEncoded());
            if (!isPin) anchoredAtAPin(chain, authType);
            List<String> usage = leaf.getExtendedKeyUsage();
            if (usage != null && !usage.contains(SERVER_AUTH) && !usage.contains(ANY_USAGE))
                throw new CertificateException("Not a server certificate");
            if (!nameMatches(leaf, verifyName)) throw new CertificateException("Wrong name");
        }

        // The chain checked as the platform checks any, with the pins as the only trust anchors. That builds the chain
        // and checks the dates and signatures; it checks no host name, which is nameMatches's.
        private void anchoredAtAPin(X509Certificate[] chain, String authType) throws CertificateException {
            try {
                KeyStore store = KeyStore.getInstance(KeyStore.getDefaultType());
                store.load(null, null);
                for (int i = 0; i < pins.size(); i++) store.setCertificateEntry("pin" + i, pins.get(i));
                TrustManagerFactory factory = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
                factory.init(store);
                for (TrustManager manager : factory.getTrustManagers()) {
                    if (!(manager instanceof X509TrustManager)) continue;
                    ((X509TrustManager) manager).checkServerTrusted(chain, authType);
                    return;
                }
                throw new CertificateException("No trust manager");
            } catch (CertificateException refusal) {
                throw refusal;
            } catch (GeneralSecurityException | IOException broken) {
                throw new CertificateException("Pins unusable", broken);
            }
        }
    }

    // ---- Names ----

    /**
     * Whether the certificate is for the name, as the desktop's Qt reads one (isMatchingHostname). An IP number is
     * matched by the certificate's IP addresses alone, and by no wildcard. A host name is matched, after the ACE form
     * and lower case of hostKey, by every DNS name the certificate has and by every common name of its subject: Qt
     * reads the common name even when there are DNS names. A wildcard is one * at the end of the first label of a name
     * of three labels or more, so that *.com and *.co.uk are no names; it stands for one or more characters of one label
     * and is not read in an internationalized (xn--) label.
     */
    static boolean nameMatches(X509Certificate leaf, String name) {
        String peer = SubscriptionData.hostKey(name);
        Collection<List<?>> alternatives;
        try { alternatives = leaf.getSubjectAlternativeNames(); }
        catch (CertificateException unreadable) { return false; }
        if (alternatives == null) alternatives = Collections.emptyList();
        if (ConfigForm.ipLiteral(peer)) {
            try {
                byte[] wanted = InetAddress.getByName(peer).getAddress();
                for (List<?> entry : alternatives) {
                    if (!Integer.valueOf(7).equals(entry.get(0)) || !(entry.get(1) instanceof String)) continue;
                    String text = (String) entry.get(1);
                    if (ConfigForm.ipLiteral(text) && Arrays.equals(wanted, InetAddress.getByName(text).getAddress())) return true;
                }
            } catch (IOException notAnAddress) { }
            return false;
        }
        List<String> candidates = new ArrayList<>();
        for (List<?> entry : alternatives)
            if (Integer.valueOf(2).equals(entry.get(0)) && entry.get(1) instanceof String) candidates.add((String) entry.get(1));
        candidates.addAll(commonNames(leaf));
        for (String candidate : candidates) if (matches(candidate, peer)) return true;
        return false;
    }

    private static boolean matches(String candidate, String peer) {
        if (candidate.isEmpty()) return false;
        String c = SubscriptionData.hostKey(candidate);
        if (c.indexOf('*') < 0) return c.equals(peer);
        int star = c.indexOf('*'), dot = c.indexOf('.'), peerDot = peer.indexOf('.');
        if (star != c.lastIndexOf('*') || dot < 1 || star != dot - 1 || c.startsWith("xn--") || peerDot < 1) return false;
        if (c.split("\\.", -1).length < 3) return false;
        String prefix = c.substring(0, star);
        return peerDot >= prefix.length() + 1 && peer.startsWith(prefix) && peer.substring(peerDot + 1).equals(c.substring(dot + 1));
    }

    // The common names of the subject, each as its text. A name that is not text is none.
    private static List<String> commonNames(X509Certificate leaf) {
        List<String> names = new ArrayList<>();
        try {
            X500Name subject = X500Name.getInstance(leaf.getSubjectX500Principal().getEncoded());
            ASN1ObjectIdentifier commonName = BCStyle.CN;
            for (RDN rdn : subject.getRDNs()) {
                for (AttributeTypeAndValue pair : rdn.getTypesAndValues()) {
                    ASN1Encodable value = pair.getValue();
                    if (commonName.equals(pair.getType()) && value instanceof ASN1String) names.add(((ASN1String) value).getString());
                }
            }
        } catch (RuntimeException unreadable) { }
        return names;
    }
}
