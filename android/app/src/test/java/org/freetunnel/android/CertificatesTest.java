package org.freetunnel.android;

import static org.freetunnel.android.CertificateVectors.BAG_ATTRIBUTES;
import static org.freetunnel.android.CertificateVectors.CA_DER;
import static org.freetunnel.android.CertificateVectors.CA_PEM;
import static org.freetunnel.android.CertificateVectors.CHAIN_DER;
import static org.freetunnel.android.CertificateVectors.CHAIN_PEM;
import static org.freetunnel.android.CertificateVectors.ENCRYPTED_PKCS8_DER;
import static org.freetunnel.android.CertificateVectors.LEAF_DER;
import static org.freetunnel.android.CertificateVectors.LEAF_PEM;
import static org.freetunnel.android.CertificateVectors.PKCS1_DER;
import static org.freetunnel.android.CertificateVectors.PKCS8_DER;
import static org.freetunnel.android.CertificateVectors.SEC1_DER;
import static org.freetunnel.android.CertificateVectors.certShaped;
import static org.freetunnel.android.CertificateVectors.concat;
import static org.freetunnel.android.CertificateVectors.derElement;
import static org.freetunnel.android.CertificateVectors.hex;
import static org.freetunnel.android.CertificateVectors.keyLabel;
import static org.freetunnel.android.CertificateVectors.keyPem;
import static org.freetunnel.android.CertificateVectors.pemBlock;
import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Arrays;
import java.util.Base64;
import java.util.Random;

// The desktop's rows (isCertificateDer, certificatesOnly and derElementEnd in
// tests/test_configtoml.cpp), so that both apps keep and drop the same blocks, and
// a few more on how lengths are read.
public final class CertificatesTest {
    private static boolean isCertificate(byte[] der) { return Certificates.isCertificate(der, 0, der.length); }

    private static String wrapped(byte[] der, int columns) {
        String b64 = Base64.getEncoder().encodeToString(der);
        StringBuilder out = new StringBuilder("-----BEGIN CERTIFICATE-----\n");
        for (int i = 0; i < b64.length(); i += columns) out.append(b64, i, Math.min(i + columns, b64.length())).append('\n');
        return out.append("-----END CERTIFICATE-----\n").toString();
    }

    // What holds for anything kept: no key, and a second pass over it changes
    // nothing, the DER included.
    private static void assertKeptAlone(String input, Certificates.Kept kept) {
        assertFalse(input, kept.pem.contains(keyLabel()));
        Certificates.Kept again = Certificates.fromPem(kept.pem);
        assertFalse(input, again.dropped);
        assertEquals(input, kept.pem, again.pem);
        assertArrayEquals(input, kept.der, again.der);
        Certificates.Kept asDer = Certificates.fromDer(kept.der);
        assertNotNull(input, asDer);
        assertFalse(input, asDer.dropped);
        assertArrayEquals(input, kept.der, asDer.der);
    }

    @Test public void certificatesHaveTheShape() {
        assertTrue(isCertificate(CA_DER));
        assertTrue(isCertificate(LEAF_DER));
        assertTrue(isCertificate(hex("300b3002050030020500030100"))); // the smallest shape
        // Only the BIT STRING has to hold something; the two SEQUENCEs are the shape.
        assertTrue(isCertificate(hex("300730003000030100")));
        assertTrue(isCertificate(certShaped(200)));                    // one length byte
        assertTrue(isCertificate(certShaped(300)));                    // two length bytes
        // Four length bytes, as long as the length fits.
        assertTrue(isCertificate(hex("3084" + "0000000b" + "3002050030020500030100")));
        // Only the range given is read.
        byte[] around = concat(hex("0500"), CA_DER, hex("0500"));
        assertTrue(Certificates.isCertificate(around, 2, 2 + CA_DER.length));
        assertFalse(Certificates.isCertificate(around, 2, around.length));
        assertFalse(Certificates.isCertificate(around, 0, 2 + CA_DER.length));
    }

    @Test public void keysAndOtherDerDoNot() {
        Object[][] rows = {
            // What the link tests stood in for a certificate with before: a SEQUENCE
            // holding an INTEGER, which is how a key starts.
            {"a SEQUENCE of an INTEGER", hex("3003020101")},
            {"a PKCS#8 key", PKCS8_DER},
            {"a PKCS#1 key", PKCS1_DER},
            {"a SEC1 key", SEC1_DER},
            {"an encrypted PKCS#8 key", ENCRYPTED_PKCS8_DER},
            {"a byte after it", concat(CA_DER, hex("00"))},
            {"two certificates", concat(CA_DER, LEAF_DER)},
            {"cut short", Arrays.copyOf(CA_DER, CA_DER.length - 1)},
            {"nothing", new byte[0]},
            {"a SET", hex("310b3002050030020500030100")},
            {"an indefinite length", hex("308030020500300205000301000000")},
            {"an empty BIT STRING", hex("300a30020500300205000300")},
            {"no BIT STRING", hex("30083002050030020500")},
            {"a field after the BIT STRING", hex("300d30020500300205000301000500")},
            {"the fields out of order", hex("300b0301003002050030020500")},
            {"a field past the end", hex("300b3002050030020500030200")},
            {"a field past its SEQUENCE", hex("300a3002050030020500030100")},
            {"a tag in the high form", hex("300c30020500300205001f030100")},
            // How lengths are read.
            {"an outer length short of its fields", hex("30053002050030020500030100")},
            {"an inner length past the outer one", hex("300b300c050030020500030100")},
            {"an indefinite length inside", hex("3009308030020500030100")},
            {"five length bytes", hex("3085000000000b3002050030020500030100")},
            {"the length cut short", hex("308201")},
        };
        for (Object[] row : rows) assertFalse((String) row[0], isCertificate((byte[]) row[1]));
    }

    @Test public void certificatesComeBackAsWritten() {
        String crlf = CHAIN_PEM.replace("\n", "\r\n");
        String oneLine = CA_PEM.replace("\n", "");
        String legacy = pemBlock("X509 CERTIFICATE", CA_DER);
        // The desktop's FreeTunnel 1.1.9 and earlier wrote a link's chain as one
        // block, of which OpenSSL reads the first certificate. It holds nothing but
        // certificates.
        String oneBlock = pemBlock("CERTIFICATE", CHAIN_DER);
        // White space between blocks is what the desktop's QString::trimmed() takes
        // out: no-break and other Unicode spaces and separators too, here the
        // no-break space, NEL, the line separator and the ideographic space.
        for (String pem : new String[]{CA_PEM, CHAIN_PEM, crlf, wrapped(CA_DER, 76), oneLine, legacy,
                "\n\n" + CA_PEM + " \n", "", " \r\n\t\u000b\f", pemBlock("X509 CERTIFICATE", LEAF_DER) + CA_PEM,
                LEAF_PEM + "\u00a0\u0085\u2028\u3000" + CA_PEM, pemBlock("CERTIFICATE", certShaped(4)), oneBlock}) {
            Certificates.Kept kept = Certificates.fromPem(pem);
            assertFalse(pem, kept.dropped);
            assertEquals(pem, kept.pem);
            assertKeptAlone(pem, kept);
        }
        assertArrayEquals(CA_DER, Certificates.fromPem(CA_PEM).der);
        assertArrayEquals(CA_DER, Certificates.fromPem(wrapped(CA_DER, 76)).der);
        assertArrayEquals(CA_DER, Certificates.fromPem(oneLine).der);
        assertArrayEquals(CA_DER, Certificates.fromPem(legacy).der);
        assertArrayEquals(CHAIN_DER, Certificates.fromPem(CHAIN_PEM).der);
        assertArrayEquals(CHAIN_DER, Certificates.fromPem(crlf).der);
        assertArrayEquals(CHAIN_DER, Certificates.fromPem(oneBlock).der);
        assertEquals(0, Certificates.fromPem("").der.length);
    }

    @Test public void onlyTheCertificatesAreKept() {
        String padded = LEAF_PEM.replace("MIIB1z", "MIIB=1z");
        String mismatched = LEAF_PEM.replace("END CERTIFICATE", "END X509 CERTIFICATE");
        String lower = CA_PEM.replace("CERTIFICATE", "certificate");
        String crlfLeaf = LEAF_PEM.replace("\n", "\r\n");
        String strayCharacter = pemBlock("CERTIFICATE", certShaped(4)).replace("\n-----END", "A\n-----END");
        String[][] rows = {
            // The certificates of what holds more, in order, and nothing else.
            {"a certificate and its key", LEAF_PEM + keyPem(), LEAF_PEM},
            {"the key first", keyPem() + LEAF_PEM, LEAF_PEM},
            {"a key inside a chain", LEAF_PEM + keyPem() + CA_PEM, CHAIN_PEM},
            {"an export of a PKCS#12 file", BAG_ATTRIBUTES + LEAF_PEM + BAG_ATTRIBUTES + keyPem(), LEAF_PEM},
            {"an RSA key", pemBlock(keyLabel("RSA "), PKCS1_DER) + CA_PEM, CA_PEM},
            {"an EC key", pemBlock(keyLabel("EC "), SEC1_DER) + CA_PEM, CA_PEM},
            {"an encrypted key", pemBlock(keyLabel("ENCRYPTED "), ENCRYPTED_PKCS8_DER) + CA_PEM, CA_PEM},
            {"a certificate request", pemBlock("CERTIFICATE REQUEST", certShaped(4)) + CA_PEM, CA_PEM},
            {"a key labelled as a certificate", pemBlock("CERTIFICATE", PKCS8_DER) + CA_PEM, CA_PEM},
            // One block of more than one element is kept only when each is a
            // certificate and nothing is left over.
            {"a key after a certificate in one block", pemBlock("CERTIFICATE", concat(LEAF_DER, PKCS8_DER)) + CA_PEM, CA_PEM},
            {"a key before a certificate in one block", pemBlock("CERTIFICATE", concat(PKCS8_DER, LEAF_DER)) + CA_PEM, CA_PEM},
            {"a byte after a chain in one block", pemBlock("CERTIFICATE", concat(CHAIN_DER, hex("00"))) + CA_PEM, CA_PEM},
            {"an empty block", pemBlock("CERTIFICATE", new byte[0]) + CA_PEM, CA_PEM},
            {"a key inside a certificate block",
                "-----BEGIN CERTIFICATE-----\n" + keyPem() + "-----END CERTIFICATE-----\n" + CA_PEM, CA_PEM},
            // Decoded leniently, the stray padding would be skipped and the certificate there.
            {"base64 that is not", padded + CA_PEM, CA_PEM},
            // Padding and one '=' more: not base64 to the desktop or to OpenSSL. A
            // decoder that skips what follows the padding would read it whole.
            {"one '=' after the padding", LEAF_PEM.replace("g==\n-----END", "g===\n-----END") + CA_PEM, CA_PEM},
            // One character too many after a body that needs no padding (15 bytes):
            // not whole base64 either, and OpenSSL will not read such a block. Qt's
            // decoder would drop the character and go on.
            {"a stray base64 character", strayCharacter + CA_PEM, CA_PEM},
            {"an END of another label", mismatched + CA_PEM, CA_PEM},
            {"no END", CA_PEM + LEAF_PEM.substring(0, LEAF_PEM.indexOf("-----END")), CA_PEM},
            {"a label in lower case", lower, ""},
            {"text after the certificate", CA_PEM + "see the README\n", CA_PEM},
            {"a character after the END line", CA_PEM.substring(0, CA_PEM.length() - 1) + "x", CA_PEM},
            {"text alone", "see the README", ""},
            // And nothing at all when there is no certificate.
            {"a key alone", keyPem(), ""},
            {"a trusted certificate", pemBlock("TRUSTED CERTIFICATE", CA_DER), ""},
            // More of the same: a certificate's shape under a key's label, a key under
            // the legacy label, CRLF throughout, and the made-up certificates earlier
            // tests used, which are not whole ones.
            {"a key's label around a certificate's shape", pemBlock(keyLabel(), certShaped(4)) + CA_PEM, CA_PEM},
            {"a key labelled as X509 CERTIFICATE", CA_PEM + pemBlock("X509 CERTIFICATE", SEC1_DER), CA_PEM},
            // A kept block is as written, its line breaks included, and ends a line.
            {"CRLF", crlfLeaf + keyPem().replace("\n", "\r\n"), crlfLeaf.substring(0, crlfLeaf.length() - 2) + "\n"},
            {"a truncated certificate", "-----BEGIN CERTIFICATE-----\nMIIBszCCAVmgAwIBAgIUQ0FGRQ==\n-----END CERTIFICATE-----\n", ""},
            // Controls that are not white space to the desktop either.
            {"a control character between blocks", LEAF_PEM + "\u001c" + CA_PEM, CHAIN_PEM},
            {"a NUL after the certificate", CA_PEM + "\u0000", CA_PEM},
            {"a line of text", "certificate line\n", ""},
        };
        for (String[] row : rows) {
            Certificates.Kept kept = Certificates.fromPem(row[1]);
            assertTrue(row[0], kept.dropped);
            assertEquals(row[0], row[2], kept.pem);
            assertKeptAlone(row[0], kept);
            // The DER is the kept blocks'.
            assertArrayEquals(row[0], Certificates.fromPem(row[2]).der, kept.der);
        }
        assertArrayEquals(LEAF_DER, Certificates.fromPem(LEAF_PEM + keyPem()).der);
        assertArrayEquals(CHAIN_DER, Certificates.fromPem(LEAF_PEM + keyPem() + CA_PEM).der);
        assertEquals(0, Certificates.fromPem(keyPem()).der.length);
    }

    @Test public void fromDerKeepsTheCertificatesOfALink() {
        Certificates.Kept none = Certificates.fromDer(new byte[0]);
        assertEquals("", none.pem);
        assertEquals(0, none.der.length);
        assertFalse(none.dropped);
        // In lines of 64, as OpenSSL writes them.
        assertEquals(CA_PEM, Certificates.fromDer(CA_DER).pem);
        assertEquals(CHAIN_PEM, Certificates.fromDer(CHAIN_DER).pem);
        assertEquals("-----BEGIN CERTIFICATE-----\nMAswAgUAMAIFAAMBAA==\n-----END CERTIFICATE-----\n",
                Certificates.fromDer(hex("300b3002050030020500030100")).pem);
        for (byte[] der : new byte[][]{CA_DER, CHAIN_DER, concat(certShaped(200), certShaped(300))}) {
            Certificates.Kept kept = Certificates.fromDer(der);
            assertFalse(kept.dropped);
            assertArrayEquals(der, kept.der);
        }
        // Anything else is left out, wherever it is.
        for (byte[] der : new byte[][]{concat(LEAF_DER, PKCS8_DER), concat(PKCS8_DER, LEAF_DER),
                concat(LEAF_DER, hex("3003020101")), concat(ENCRYPTED_PKCS8_DER, LEAF_DER, PKCS1_DER)}) {
            Certificates.Kept kept = Certificates.fromDer(der);
            assertTrue(kept.dropped);
            assertArrayEquals(LEAF_DER, kept.der);
            assertEquals(LEAF_PEM, kept.pem);
        }
        Certificates.Kept chain = Certificates.fromDer(concat(LEAF_DER, SEC1_DER, CA_DER));
        assertTrue(chain.dropped);
        assertEquals(CHAIN_PEM, chain.pem);
        // With nothing left.
        for (byte[] der : new byte[][]{PKCS8_DER, hex("3003020101"), concat(PKCS1_DER, SEC1_DER), hex("3000")}) {
            Certificates.Kept kept = Certificates.fromDer(der);
            assertTrue(kept.dropped);
            assertEquals(0, kept.der.length);
            assertEquals("", kept.pem);
        }
        // Bytes that are not whole SEQUENCE elements, which TrustTunnel refuses too:
        // the desktop's derElementEnd rows, then elements of other tags.
        byte[] oneShort = concat(hex("3082010041"), new byte[254]);
        for (byte[] der : new byte[][]{hex("30"), hex("3001"), hex("308005000000"), hex("3085000000000105"),
                hex("308201"), hex("30847fffffff00"), oneShort, hex("030100"), concat(CA_DER, hex("00")),
                concat(CA_DER, hex("0500")), Arrays.copyOf(CA_DER, CA_DER.length - 1), concat(CA_DER, hex("30840000"))}) {
            assertNull(Certificates.fromDer(der));
        }
        // An element whose length takes two bytes is read whole.
        assertArrayEquals(certShaped(300), Certificates.fromDer(concat(derElement(0x30, new byte[300]), certShaped(300))).der);
    }

    private static byte[] mutate(byte[] seed, Random random) {
        byte[] bytes = seed.clone();
        int edits = 1 + random.nextInt(4);
        for (int i = 0; i < edits && bytes.length > 0; i++) {
            int at = random.nextInt(bytes.length);
            switch (random.nextInt(3)) {
                case 0: bytes[at] = (byte) random.nextInt(256); break;
                case 1: bytes = Arrays.copyOf(bytes, at); break;
                default: bytes[at] ^= (byte) (1 << random.nextInt(8)); break;
            }
        }
        return bytes;
    }

    // Like the desktop's fuzz loops: damaged input never crashes, and what is kept
    // still holds to the rule. A fixed seed repeats the inputs.
    @Test public void damagedInputNeverCrashes() {
        Random random = new Random(0x7065);
        for (byte[] seed : new byte[][]{CA_DER, CHAIN_DER, certShaped(300), PKCS8_DER, concat(LEAF_DER, PKCS8_DER)}) {
            for (int round = 0; round < 2000; round++) {
                byte[] bytes = mutate(seed, random);
                boolean certificate = Certificates.isCertificate(bytes, 0, bytes.length);
                Certificates.Kept kept = Certificates.fromDer(bytes);
                if (certificate) assertArrayEquals(bytes, kept.der);
                if (kept != null) assertKeptAlone("round " + round, kept);
            }
        }
        String alphabet = "-=+/ \n\rABEGINCERTIFKYZ0189az";
        for (String seed : new String[]{LEAF_PEM + keyPem(), CHAIN_PEM, BAG_ATTRIBUTES + CHAIN_PEM.replace("\n", "\r\n"),
                pemBlock("CERTIFICATE", CHAIN_DER), pemBlock("CERTIFICATE", concat(LEAF_DER, PKCS8_DER))}) {
            for (int round = 0; round < 2000; round++) {
                StringBuilder text = new StringBuilder(seed);
                int edits = 1 + random.nextInt(4);
                for (int i = 0; i < edits && text.length() > 0; i++) {
                    int at = random.nextInt(text.length());
                    if (random.nextBoolean()) text.setCharAt(at, alphabet.charAt(random.nextInt(alphabet.length())));
                    else text.deleteCharAt(at);
                }
                assertKeptAlone(text.toString(), Certificates.fromPem(text.toString()));
            }
        }
    }
}
