package org.freetunnel.android;

import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * The time a TCP handshake with a server takes, which the Configs page shows beside each
 * config, measured as the desktop measures it (Backend::pingConfigAtIndex): the first
 * address of the config, looked up first and not counted, then the time to connect, with
 * 3 seconds to do it in. Nothing is sent over the connection. Any failure, whatever it
 * was, is the same: no result.
 *
 * <p>No Android classes: a plain JVM can load and test it.
 */
final class PingProbe {
    static final int TIMEOUT_MS = 3000;
    /** What a probe that failed shows. */
    static final String FAILED = "—";
    /** What a probe that has not answered yet shows. */
    static final String PENDING = "…";

    /** A host and a port. */
    static final class Target {
        final String host;
        final int port;
        Target(String host, int port) { this.host = host; this.port = port; }
    }

    private PingProbe() { }

    /**
     * "host:port" or "[v6 address]:port", as the desktop splits it. Null where there is no
     * port, or it is not 1 to 65535, or there is no host.
     */
    static Target target(String address) {
        if (address == null) return null;
        String text = address.trim();
        String host;
        String port;
        if (text.startsWith("[")) {
            int close = text.indexOf("]:");
            if (close < 0) return null;
            host = text.substring(1, close);
            port = text.substring(close + 2);
        } else {
            int colon = text.lastIndexOf(':');
            if (colon < 0) return null;
            host = text.substring(0, colon);
            port = text.substring(colon + 1);
        }
        if (host.isEmpty() || port.isEmpty() || port.length() > 5) return null;
        for (int i = 0; i < port.length(); i++) if (port.charAt(i) < '0' || port.charAt(i) > '9') return null;
        int number = Integer.parseInt(port);
        return number < 1 || number > 65535 ? null : new Target(host, number);
    }

    /** The milliseconds to connect, or -1 for a failure of any kind. Blocks; run it off the main thread. */
    static long probe(String address, int timeoutMillis) {
        Target target = target(address);
        if (target == null) return -1;
        try {
            InetAddress where = InetAddress.getByName(target.host);
            long start = System.nanoTime();
            // Times a TCP handshake to the server and sends nothing over it, so
            // there is nothing to encrypt.
            try (Socket socket = new Socket()) { socket.connect(new InetSocketAddress(where, target.port), timeoutMillis); } // nosemgrep: java.lang.security.audit.crypto.unencrypted-socket.unencrypted-socket
            return Math.max(0, (System.nanoTime() - start) / 1_000_000);
        } catch (Exception failure) {
            return -1;
        }
    }

    /** What the row shows for a result: the time and the unit's word, or the dash. */
    static String text(long millis, String unit) {
        return millis < 0 ? FAILED : millis + unit;
    }
}
