package org.freetunnel.android;

/**
 * The TLS ClientHello names TrustTunnel 1.3.0 accepts, one per connection.
 * Empty means chrome. {@code default} is the deprecated alias of {@code boringssl}.
 * The desktop list is {@code kTrustTunnelTlsProfiles}.
 */
final class TlsProfiles {
    private static final String[] CHOICES = {
            "chrome", "chrome_canary", "safari", "firefox", "okhttp", "openssl", "boringssl"
    };

    private TlsProfiles() { }

    /** The name to store, empty for chrome, or null when the core would refuse it. */
    static String canonical(String name) {
        if (name == null || name.isEmpty() || "chrome".equals(name)) return "";
        if ("default".equals(name)) return "boringssl";
        for (String choice : CHOICES) if (choice.equals(name)) return name;
        return null;
    }

    /** Tag 0x10, one byte, in the core's VpnTlsProfile order. Null when the byte is not one. */
    static String fromByte(int value) {
        if (value < 0 || value >= CHOICES.length) return null;
        return CHOICES[value];
    }

    /** The tag byte for a stored name. -1 for chrome, an empty value, and a name the core refuses. */
    static int byteOf(String name) {
        String stored = canonical(name);
        if (stored == null || stored.isEmpty()) return -1;
        for (int i = 0; i < CHOICES.length; i++) if (CHOICES[i].equals(stored)) return i;
        return -1;
    }
}
