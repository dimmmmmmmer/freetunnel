package org.freetunnel.android;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;
import java.util.Base64;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Keeps only the certificates in a server's certificate, by the desktop's rule
 * (certificatesOnly in ConfigTomlValues.cpp). The file a server's certificate is
 * copied from can hold other PEM data too, a private key above all, which a client
 * has no use for and should not keep.
 */
final class Certificates {
    // A PEM block: a label, then a body of base64 and whitespace only, so a BEGIN
    // line inside another block starts a block of its own.
    private static final Pattern BLOCK =
            Pattern.compile("-----BEGIN ([A-Z0-9 ]+)-----([A-Za-z0-9+/=\\s]*)-----END \\1-----");
    private static final Pattern SPACE = Pattern.compile("\\s+");

    /** The certificates kept, as DER and as PEM, and whether anything else was left out. */
    static final class Kept {
        final byte[] der;
        final String pem;
        final boolean dropped;

        Kept(byte[] der, String pem, boolean dropped) {
            this.der = der;
            this.pem = pem;
            this.dropped = dropped;
        }
    }

    private Certificates() { }

    /**
     * Whether der[start, end) is one DER element with the outer shape of an X.509
     * certificate: a SEQUENCE of the signed part (a SEQUENCE), the signature
     * algorithm (a SEQUENCE) and the signature (a BIT STRING), and nothing else.
     * Private keys in their usual forms start with an INTEGER or end with an OCTET
     * STRING, so none of them has this shape.
     */
    static boolean isCertificate(byte[] der, int start, int end) {
        int[] outer = element(der, start, end, 0x30);
        if (outer == null || outer[1] != end) return false;
        int[] signed = element(der, outer[0], end, 0x30);
        int[] algorithm = signed == null ? null : element(der, signed[1], end, 0x30);
        int[] signature = algorithm == null ? null : element(der, algorithm[1], end, 0x03);
        return signature != null && signature[1] == end && signature[1] > signature[0];
    }

    /**
     * The certificates among the DER elements of a link's tag 0x08, each as a PEM
     * block of 64 columns. Null when the bytes are not whole SEQUENCE elements,
     * which TrustTunnel's own decoder refuses too.
     */
    static Kept fromDer(byte[] der) {
        ByteArrayOutputStream kept = new ByteArrayOutputStream();
        StringBuilder pem = new StringBuilder();
        boolean dropped = false;
        int pos = 0;
        while (pos < der.length) {
            int[] item = element(der, pos, der.length, 0x30);
            if (item == null) return null;
            if (isCertificate(der, pos, item[1])) {
                kept.write(der, pos, item[1] - pos);
                pem.append(block(Arrays.copyOfRange(der, pos, item[1])));
            } else {
                dropped = true;
            }
            pos = item[1];
        }
        return new Kept(kept.toByteArray(), pem.toString(), dropped);
    }

    /**
     * The certificate blocks of PEM text, as they were written. A block is kept when
     * its label is CERTIFICATE, or X509 CERTIFICATE, which OpenSSL reads as well, and
     * its body is strict base64 of one certificate. Anything else is dropped: other
     * labels, broken blocks and text between blocks. With nothing dropped, the text
     * comes back as it was; otherwise the kept blocks, each ending a line, or
     * nothing when none is left.
     */
    static Kept fromPem(String text) {
        ByteArrayOutputStream der = new ByteArrayOutputStream();
        StringBuilder pem = new StringBuilder();
        boolean dropped = false;
        int last = 0;
        Matcher block = BLOCK.matcher(text);
        while (block.find()) {
            dropped |= !blank(text, last, block.start());
            last = block.end();
            byte[] certificate = certificate(block.group(1), block.group(2));
            if (certificate == null) { dropped = true; continue; }
            der.write(certificate, 0, certificate.length);
            pem.append(block.group()).append('\n');
        }
        dropped |= !blank(text, last, text.length());
        return new Kept(der.toByteArray(), dropped ? pem.toString() : text, dropped);
    }

    // Whether text[start, end) is only white space as the desktop's QString::trimmed()
    // sees it: tab to carriage return, NEL, and every Unicode space and separator.
    private static boolean blank(String text, int start, int end) {
        for (int i = start; i < end; i++) {
            char c = text.charAt(i);
            if (!Character.isSpaceChar(c) && !(c >= 0x09 && c <= 0x0d) && c != 0x85) return false;
        }
        return true;
    }

    // The DER of a block that holds one certificate, or null.
    private static byte[] certificate(String label, String body) {
        if (!label.equals("CERTIFICATE") && !label.equals("X509 CERTIFICATE")) return null;
        byte[] der;
        try { der = Base64.getDecoder().decode(SPACE.matcher(body).replaceAll("")); }
        catch (IllegalArgumentException e) { return null; }
        return isCertificate(der, 0, der.length) ? der : null;
    }

    private static String block(byte[] der) {
        String b64 = Base64.getEncoder().encodeToString(der);
        StringBuilder out = new StringBuilder("-----BEGIN CERTIFICATE-----\n");
        for (int i = 0; i < b64.length(); i += 64)
            out.append(b64, i, Math.min(i + 64, b64.length())).append('\n');
        return out.append("-----END CERTIFICATE-----\n").toString();
    }

    // The element at pos with the given one-byte tag, as {where its value starts,
    // where it ends}, or null when the bytes before limit hold no such element. The
    // length is one short byte, or a count of one to four bytes and then the length,
    // as the desktop reads it. A count of zero is the indefinite form, which DER
    // forbids.
    private static int[] element(byte[] der, int pos, int limit, int tag) {
        if (limit - pos < 2 || (der[pos] & 0xff) != tag) return null;
        int first = der[pos + 1] & 0xff;
        int start = pos + 2;
        long length = first;
        if ((first & 0x80) != 0) {
            int count = first & 0x7f;
            if (count == 0 || count > 4 || count > limit - start) return null;
            length = 0;
            for (int i = 0; i < count; i++) length = (length << 8) | (der[start + i] & 0xff);
            start += count;
        }
        if (length > limit - start) return null;
        return new int[]{start, start + (int) length};
    }
}
