package org.freetunnel.android;

import org.bouncycastle.asn1.ASN1Encodable;
import org.bouncycastle.asn1.ASN1Encoding;
import org.bouncycastle.asn1.ASN1Integer;
import org.bouncycastle.asn1.ASN1ObjectIdentifier;
import org.bouncycastle.asn1.DERBitString;
import org.bouncycastle.asn1.DERSequence;
import org.bouncycastle.asn1.x500.X500Name;
import org.bouncycastle.asn1.x509.AlgorithmIdentifier;
import org.bouncycastle.asn1.x509.BasicConstraints;
import org.bouncycastle.asn1.x509.ExtendedKeyUsage;
import org.bouncycastle.asn1.x509.Extension;
import org.bouncycastle.asn1.x509.ExtensionsGenerator;
import org.bouncycastle.asn1.x509.GeneralName;
import org.bouncycastle.asn1.x509.GeneralNames;
import org.bouncycastle.asn1.x509.KeyPurposeId;
import org.bouncycastle.asn1.x509.SubjectPublicKeyInfo;
import org.bouncycastle.asn1.x509.TBSCertificate;
import org.bouncycastle.asn1.x509.Time;
import org.bouncycastle.asn1.x509.V3TBSCertificateGenerator;

import java.io.ByteArrayInputStream;
import java.io.Closeable;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigInteger;
import java.net.InetAddress;
import java.nio.charset.StandardCharsets;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.SecureRandom;
import java.security.Signature;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import javax.net.ssl.ExtendedSSLSession;
import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SNIHostName;
import javax.net.ssl.SNIServerName;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLServerSocket;
import javax.net.ssl.SSLSocket;

/**
 * Certificates made while the tests run, and a TLS server that answers what a test scripts, for the tests of
 * PinnedHttps. Plain keys and certificates (EC P-256), built as DER with BouncyCastle's ASN.1 and signed by hand, so
 * that nothing is stored that a secret scanner could take for a key, and a test can ask for exactly the certificate
 * it needs: out of date, for another name, for a client, signed by a CA, with no alternative names.
 */
final class TestTls {
    private TestTls() { }

    private static final long DAY = 24L * 3600 * 1000;
    private static final SecureRandom RANDOM = new SecureRandom();

    /** A certificate and its key. */
    static final class Cert {
        final KeyPair keys;
        final X509Certificate certificate;
        Cert(KeyPair keys, X509Certificate certificate) { this.keys = keys; this.certificate = certificate; }
        byte[] der() { try { return certificate.getEncoded(); } catch (Exception e) { throw new AssertionError(e); } }
        String pem() { return TestTls.pem(der()); }
    }

    /** What a certificate is to hold. The defaults are a self-signed leaf for a server, valid from yesterday for a week. */
    static final class Spec {
        String cn = "test";
        final List<String> dns = new ArrayList<>();
        final List<String> ips = new ArrayList<>();
        // Extended key usage OIDs, or null for none.
        List<String> eku;
        Date from = new Date(System.currentTimeMillis() - DAY), to = new Date(System.currentTimeMillis() + 7 * DAY);
        boolean ca;
        Cert issuer;

        Spec cn(String value) { cn = value; return this; }
        Spec dns(String... names) { Collections.addAll(dns, names); return this; }
        Spec ip(String... addresses) { Collections.addAll(ips, addresses); return this; }
        Spec eku(String... oids) { eku = new ArrayList<>(); Collections.addAll(eku, oids); return this; }
        Spec valid(Date start, Date end) { from = start; to = end; return this; }
        Spec ca() { ca = true; return this; }
        Spec signedBy(Cert by) { issuer = by; return this; }
    }

    static final String SERVER_AUTH = "1.3.6.1.5.5.7.3.1";
    static final String CLIENT_AUTH = "1.3.6.1.5.5.7.3.2";
    static final String ANY_USAGE = "2.5.29.37.0";

    static Spec spec() { return new Spec(); }

    /** A leaf for one name, which is its common name and its only alternative name. */
    static Cert leaf(String name) throws Exception { return make(spec().cn(name).dns(name)); }

    static Cert make(Spec spec) throws Exception {
        KeyPairGenerator generator = KeyPairGenerator.getInstance("EC");
        generator.initialize(256);
        KeyPair keys = generator.generateKeyPair();
        X500Name subject = new X500Name("CN=" + spec.cn);
        PrivateKey signer = spec.issuer == null ? keys.getPrivate() : spec.issuer.keys.getPrivate();
        X500Name issuer = spec.issuer == null ? subject : new X500Name(spec.issuer.certificate.getSubjectX500Principal().getName());
        AlgorithmIdentifier algorithm = new AlgorithmIdentifier(new ASN1ObjectIdentifier("1.2.840.10045.4.3.2"));
        ExtensionsGenerator extensions = new ExtensionsGenerator();
        if (spec.ca) extensions.addExtension(Extension.basicConstraints, true, new BasicConstraints(true));
        if (!spec.dns.isEmpty() || !spec.ips.isEmpty()) {
            List<GeneralName> names = new ArrayList<>();
            for (String name : spec.dns) names.add(new GeneralName(GeneralName.dNSName, name));
            for (String address : spec.ips) names.add(new GeneralName(GeneralName.iPAddress, address));
            extensions.addExtension(Extension.subjectAlternativeName, false, new GeneralNames(names.toArray(new GeneralName[0])));
        }
        if (spec.eku != null) {
            List<KeyPurposeId> purposes = new ArrayList<>();
            for (String oid : spec.eku) purposes.add(KeyPurposeId.getInstance(new ASN1ObjectIdentifier(oid)));
            extensions.addExtension(Extension.extendedKeyUsage, false, new ExtendedKeyUsage(purposes.toArray(new KeyPurposeId[0])));
        }
        V3TBSCertificateGenerator tbs = new V3TBSCertificateGenerator();
        tbs.setSerialNumber(new ASN1Integer(new BigInteger(63, RANDOM).add(BigInteger.ONE)));
        tbs.setSignature(algorithm);
        tbs.setIssuer(issuer);
        tbs.setStartDate(new Time(spec.from));
        tbs.setEndDate(new Time(spec.to));
        tbs.setSubject(subject);
        tbs.setSubjectPublicKeyInfo(SubjectPublicKeyInfo.getInstance(keys.getPublic().getEncoded()));
        if (!extensions.isEmpty()) tbs.setExtensions(extensions.generate());
        TBSCertificate body = tbs.generateTBSCertificate();
        byte[] signed = body.getEncoded(ASN1Encoding.DER);
        Signature signature = Signature.getInstance("SHA256withECDSA");
        signature.initSign(signer);
        signature.update(signed);
        byte[] der = new DERSequence(new ASN1Encodable[]{body, algorithm, new DERBitString(signature.sign())})
                .getEncoded(ASN1Encoding.DER);
        X509Certificate certificate = (X509Certificate) CertificateFactory.getInstance("X.509").generateCertificate(new ByteArrayInputStream(der));
        return new Cert(keys, certificate);
    }

    /** DER as a PEM block of 64 columns. */
    static String pem(byte[] der) {
        String b64 = Base64.getEncoder().encodeToString(der);
        StringBuilder out = new StringBuilder("-----BEGIN CERTIFICATE-----\n");
        for (int i = 0; i < b64.length(); i += 64) out.append(b64, i, Math.min(i + 64, b64.length())).append('\n');
        return out.append("-----END CERTIFICATE-----\n").toString();
    }

    /** What the server saw of one request. */
    static final class Seen {
        String requestLine;
        final Map<String, String> headers = new HashMap<>();
        String sni;
    }

    /** What the server says to a request: the whole response as bytes, or null to say nothing and keep the connection. */
    interface Script { byte[] answer(int number, Seen seen); }

    /**
     * A TLS server on 127.0.0.1 that takes one connection at a time on one thread, as a test needs it: it reads a
     * request's head, records it, and sends what the script gives, then closes. close() ends it and waits for it.
     */
    static final class Server implements Closeable {
        private final SSLServerSocket socket;
        private final Thread thread;
        private final Script script;
        private volatile boolean closed;
        private volatile SSLSocket current;
        final AtomicInteger accepted = new AtomicInteger();
        final List<Seen> requests = Collections.synchronizedList(new ArrayList<Seen>());

        /** chain is what the server presents, the first its own certificate, whose key is key. */
        Server(PrivateKey key, List<X509Certificate> chain, Script script) throws Exception {
            this.script = script;
            KeyStore store = KeyStore.getInstance("PKCS12");
            store.load(null, null);
            store.setKeyEntry("server", key, "test".toCharArray(), chain.toArray(new X509Certificate[0]));
            KeyManagerFactory keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
            keys.init(store, "test".toCharArray());
            SSLContext context = SSLContext.getInstance("TLS");
            context.init(keys.getKeyManagers(), null, null);
            socket = (SSLServerSocket) context.getServerSocketFactory().createServerSocket(0, 50, InetAddress.getByName("127.0.0.1"));
            thread = new Thread(this::serve, "TestTls server");
            thread.setDaemon(true);
            thread.start();
        }

        static Server of(Cert cert, Script script) throws Exception {
            return new Server(cert.keys.getPrivate(), Collections.singletonList(cert.certificate), script);
        }

        int port() { return socket.getLocalPort(); }

        private void serve() {
            while (!closed) {
                SSLSocket client;
                try { client = (SSLSocket) socket.accept(); }
                catch (IOException ended) { return; }
                int number = accepted.incrementAndGet();
                current = client;
                try {
                    client.setSoTimeout(10000);
                    // A script that is null says nothing at all, not even the handshake: the client waits for it.
                    byte[] reply = null;
                    if (script != null) {
                        client.startHandshake();
                        Seen seen = readRequest(client);
                        requests.add(seen);
                        reply = script.answer(number, seen);
                    }
                    if (reply == null) {
                        while (!closed) Thread.sleep(20);
                    } else {
                        OutputStream out = client.getOutputStream();
                        out.write(reply);
                        out.flush();
                    }
                } catch (IOException | InterruptedException | RuntimeException ignored) {
                } finally {
                    try { client.close(); } catch (IOException ignored) { }
                }
            }
        }

        private static Seen readRequest(SSLSocket client) throws IOException {
            Seen seen = new Seen();
            List<SNIServerName> names = ((ExtendedSSLSession) client.getSession()).getRequestedServerNames();
            if (names != null) for (SNIServerName name : names) if (name instanceof SNIHostName) seen.sni = ((SNIHostName) name).getAsciiName();
            InputStream in = client.getInputStream();
            ByteArrayOutputStream head = new ByteArrayOutputStream();
            while (true) {
                int b = in.read();
                if (b < 0) throw new IOException("closed");
                head.write(b);
                byte[] h = head.toByteArray();
                int n = h.length;
                if (n >= 4 && h[n - 4] == '\r' && h[n - 3] == '\n' && h[n - 2] == '\r' && h[n - 1] == '\n') break;
            }
            String[] lines = new String(head.toByteArray(), StandardCharsets.ISO_8859_1).split("\r\n");
            seen.requestLine = lines[0];
            for (int i = 1; i < lines.length; i++) {
                int colon = lines[i].indexOf(':');
                if (colon > 0) seen.headers.put(lines[i].substring(0, colon).toLowerCase(Locale.ROOT), lines[i].substring(colon + 1).trim());
            }
            return seen;
        }

        @Override public void close() {
            closed = true;
            try { socket.close(); } catch (IOException ignored) { }
            SSLSocket now = current;
            if (now != null) try { now.close(); } catch (IOException ignored) { }
            try { thread.join(5000); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        }
    }

    /** A complete response with a body and its length. */
    static byte[] ok(String body) { return ok(body.getBytes(StandardCharsets.UTF_8)); }

    static byte[] ok(byte[] body) {
        byte[] head = ("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + body.length + "\r\nConnection: close\r\n\r\n")
                .getBytes(StandardCharsets.ISO_8859_1);
        byte[] all = new byte[head.length + body.length];
        System.arraycopy(head, 0, all, 0, head.length);
        System.arraycopy(body, 0, all, head.length, body.length);
        return all;
    }

    static byte[] raw(String text) { return text.getBytes(StandardCharsets.ISO_8859_1); }

    static byte[] redirect(String location) { return raw("HTTP/1.1 302 Found\r\nLocation: " + location + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); }
}
