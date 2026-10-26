package org.freetunnel.android;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;
import java.util.Base64;

/**
 * Certificates, and what a certificate field can hold that is not one, for the
 * tests of the rule that keeps only the certificates. Byte for byte the desktop's
 * tests/certificate_vectors.h, so that both apps are held to the same bytes; a
 * change there comes here too.
 *
 * The certificates are real and public, made for these tests with OpenSSL, and
 * their keys were not kept. No private key is here, and none is needed: the rule
 * tells a key from a certificate by its DER shape, so a few bytes in a key's shape
 * stand in for one. A key block's label is put together when the tests run, so
 * that no line of the source reads as a key to a secret scanner.
 */
final class CertificateVectors {
    private CertificateVectors() { }

    // Self-signed: O = FreeTunnel tests, CN = ca.fixture.invalid, valid to 2054.
    static final String CA_PEM = ""
            + "-----BEGIN CERTIFICATE-----\n"
            + "MIIBxjCCAW2gAwIBAgIUMggTEeksVpGHcjPlkyH1oitg5NwwCgYIKoZIzj0EAwIw\n"
            + "ODEZMBcGA1UECgwQRnJlZVR1bm5lbCB0ZXN0czEbMBkGA1UEAwwSY2EuZml4dHVy\n"
            + "ZS5pbnZhbGlkMCAXDTI2MTAyNjEzNTMxNVoYDzIwNTQwMzEzMTM1MzE1WjA4MRkw\n"
            + "FwYDVQQKDBBGcmVlVHVubmVsIHRlc3RzMRswGQYDVQQDDBJjYS5maXh0dXJlLmlu\n"
            + "dmFsaWQwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAATpBntG7ibqv0jd9BJTO2mc\n"
            + "RteaMpEPy9WesCwtGK8VO+eSJwDmZAnH8qE1BcghMBLM8KG3yy6IurbrRHoRGZk2\n"
            + "o1MwUTAdBgNVHQ4EFgQUowpTqPMvc1UujbnAJTjUyUdCFQUwHwYDVR0jBBgwFoAU\n"
            + "owpTqPMvc1UujbnAJTjUyUdCFQUwDwYDVR0TAQH/BAUwAwEB/zAKBggqhkjOPQQD\n"
            + "AgNHADBEAiAGbdQRIykqGpGXOlE7Fu1ygkC8+Bx1zYbjbKSLVRIjFwIgfMVbcUL3\n"
            + "NOi1/hJlMfPkPenNzmCFmQ3fKh6vnLqZH5I=\n"
            + "-----END CERTIFICATE-----\n";

    // Signed by CA_PEM: O = FreeTunnel tests, CN = vpn.fixture.invalid, with that
    // name as its subject alternative name, valid to 2054.
    static final String LEAF_PEM = ""
            + "-----BEGIN CERTIFICATE-----\n"
            + "MIIB1zCCAX2gAwIBAgIUR89LmrAjTDz3XjuZF76HEZe4LHgwCgYIKoZIzj0EAwIw\n"
            + "ODEZMBcGA1UECgwQRnJlZVR1bm5lbCB0ZXN0czEbMBkGA1UEAwwSY2EuZml4dHVy\n"
            + "ZS5pbnZhbGlkMCAXDTI2MTAyNjEzNTMxNVoYDzIwNTQwMzEzMTM1MzE1WjA5MRkw\n"
            + "FwYDVQQKDBBGcmVlVHVubmVsIHRlc3RzMRwwGgYDVQQDDBN2cG4uZml4dHVyZS5p\n"
            + "bnZhbGlkMFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAE2X8QzmoYE7iTMx5zPHcx\n"
            + "ao/Rpe9SkUHxxIGQj3MY6Vs+6pMjnl6m5NiecjCrWCwSJ4ApX+JNiJJ6YgBDUli0\n"
            + "gKNiMGAwHgYDVR0RBBcwFYITdnBuLmZpeHR1cmUuaW52YWxpZDAdBgNVHQ4EFgQU\n"
            + "HeFjex5n8uZHZr/wnf11A0U6JoMwHwYDVR0jBBgwFoAUowpTqPMvc1UujbnAJTjU\n"
            + "yUdCFQUwCgYIKoZIzj0EAwIDSAAwRQIgXwB9HTwKPF3zwPc2TNBnB8DVRc8CbRgn\n"
            + "KtNe8GiLyvoCIQCjUj8JIRn0w/+nwQC8iUiwBO+YsgsXlQu1BpFeFAxm/g==\n"
            + "-----END CERTIFICATE-----\n";

    // The chain a server pins: its own certificate, then the one that signed it.
    static final String CHAIN_PEM = LEAF_PEM + CA_PEM;

    static final byte[] CA_DER = derOf(CA_PEM);
    static final byte[] LEAF_DER = derOf(LEAF_PEM);
    static final byte[] CHAIN_DER = concat(LEAF_DER, CA_DER);

    // DER in the shape of each kind of key, which is all the rule looks at. Every one
    // is a SEQUENCE, as a certificate is, and none is SEQUENCE { SEQUENCE, SEQUENCE,
    // BIT STRING }.
    // PKCS#8: SEQUENCE { INTEGER 0, SEQUENCE { OID }, OCTET STRING }.
    static final byte[] PKCS8_DER = hex("300a02010030030601000400");
    // PKCS#1 (RSA): SEQUENCE { INTEGER 0, INTEGER n, INTEGER e }.
    static final byte[] PKCS1_DER = hex("3009020100020105020103");
    // SEC1 (EC): SEQUENCE { INTEGER 1, OCTET STRING }.
    static final byte[] SEC1_DER = hex("30070201010402aabb");
    // Encrypted PKCS#8: SEQUENCE { SEQUENCE { OID, NULL }, OCTET STRING }.
    static final byte[] ENCRYPTED_PKCS8_DER = hex("300b300506010005000402aabb");

    // The text an export of a PKCS#12 file writes before each block.
    static final String BAG_ATTRIBUTES = "Bag Attributes\n"
            + "    localKeyID: 01 02 03 04\n"
            + "subject=O = FreeTunnel tests, CN = vpn.fixture.invalid\n";

    // The label of a private key block, with the kind in front ("RSA ", "EC ",
    // "ENCRYPTED "), or none. In two pieces, never one literal (see the top).
    static String keyLabel(String kind) { return kind + "PRIVATE" + " KEY"; }
    static String keyLabel() { return keyLabel(""); }

    // A PKCS#8 key as a PEM block, the way a file that combines a certificate with
    // its key holds it.
    static String keyPem() { return pemBlock(keyLabel(), PKCS8_DER); }

    // A PEM block of any label around any bytes, 64 base64 characters a line.
    static String pemBlock(String label, byte[] der) {
        String b64 = Base64.getEncoder().encodeToString(der);
        StringBuilder out = new StringBuilder("-----BEGIN ").append(label).append("-----\n");
        for (int i = 0; i < b64.length(); i += 64) out.append(b64, i, Math.min(i + 64, b64.length())).append('\n');
        return out.append("-----END ").append(label).append("-----\n").toString();
    }

    // A DER element: the tag, the length in the short or the long form, the content.
    static byte[] derElement(int tag, byte[] content) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        int n = content.length;
        out.write(tag);
        if (n < 0x80) {
            out.write(n);
        } else if (n < 0x100) {
            out.write(0x81);
            out.write(n);
        } else {
            out.write(0x82);
            out.write((n >> 8) & 0xff);
            out.write(n & 0xff);
        }
        out.write(content, 0, n);
        return out.toByteArray();
    }

    // The outer shape of a certificate and nothing inside it: SEQUENCE { SEQUENCE of
    // `n` filler bytes, SEQUENCE { NULL }, BIT STRING 00 }. For what is about framing
    // and not about the certificate, such as how a chain is split.
    static byte[] certShaped(int n) {
        byte[] filler = new byte[n];
        Arrays.fill(filler, (byte) 'A');
        return derElement(0x30, concat(derElement(0x30, filler), hex("30020500"), hex("030100")));
    }

    // The DER of a PEM text that is one block, such as the certificates above.
    static byte[] derOf(String pem) {
        int begin = pem.indexOf('\n') + 1;
        int end = pem.indexOf("-----END");
        return Base64.getMimeDecoder().decode(pem.substring(begin, end));
    }

    static byte[] hex(String text) {
        byte[] out = new byte[text.length() / 2];
        for (int i = 0; i < out.length; i++) out[i] = (byte) Integer.parseInt(text.substring(2 * i, 2 * i + 2), 16);
        return out;
    }

    static byte[] concat(byte[]... parts) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        for (byte[] part : parts) out.write(part, 0, part.length);
        return out.toByteArray();
    }
}
