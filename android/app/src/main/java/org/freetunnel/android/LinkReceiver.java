package org.freetunnel.android;

import io.nayuki.qrcodegen.QrCode;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UnsupportedEncodingException;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * A one-time web page on the local network that takes a link or a TOML file from a
 * phone's browser, for a TV, which has no camera to scan a QR code and no keyboard
 * to type a link. The TV shows a QR code of url(); the page there sends the text
 * back. Plain JDK, so the JVM tests drive it with real sockets.
 *
 * <p>Browsers give a page on a local http:// address no way to encrypt, so the text
 * crosses the local network in the clear, once. What limits that: it binds only to
 * the TV's private-network address, for 5 minutes, takes one text and stops, and
 * stops after 3 sends with a wrong code. The code is in the URL and sent back with
 * the text, so another page in the phone's browser cannot send one without it. A
 * GET shows the same page whatever its path, so only a send tests a code. The TV
 * then shows what it received, with the sender's address, and asks before adding it.
 * Nothing here is logged: the text holds the VPN password.
 */
final class LinkReceiver {
    static final long LIFETIME_MS = 300_000;
    static final int READ_TIMEOUT_MS = 10_000;
    static final int CONNECTION_DEADLINE_MS = 15_000;
    static final int MAX_HEADER_BYTES = 8192;
    static final int MAX_BODY_BYTES = 512 * 1024;
    static final int MAX_WRONG_CODES = 3;
    static final int CODE_LENGTH = 8;
    // Crockford's base32: no I, L or O, easily taken for 1 and 0 on a TV across the
    // room, and no U. 8 of them are 40 bits.
    static final String ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    enum Stop { TIMEOUT, LOCKED_OUT, FAILED }

    /** Called on the receiver's own thread, at most once, and never after stop(). */
    interface Listener {
        void received(String text, String sender);
        void stopped(Stop reason);
    }

    /** What handle() did with one request. */
    enum Kind { SERVED, REFUSED, WRONG_CODE, RECEIVED }
    static final class Outcome {
        final Kind kind;
        final String text;
        Outcome(Kind kind, String text) { this.kind = kind; this.text = text; }
    }

    /** The pages the phone's browser shows, whole and static: nothing a request sends is put in them. */
    static final class Pages {
        final String form;
        final String sent;
        final String wrongCode;
        final String lockedOut;

        // The strings are the app's own, in the TV's language. The form has no action,
        // so it posts back to the address it was opened at, code and all.
        Pages(String lang, String title, String prompt, String send, String note,
              String sent, String wrongCode, String lockedOut) {
            form = page(lang, title, "<form method=\"post\" accept-charset=\"utf-8\">"
                    + "<label for=\"link\">" + escape(prompt) + "</label>"
                    + "<textarea id=\"link\" name=\"link\" required autocomplete=\"off\" autocapitalize=\"off\""
                    + " autocorrect=\"off\" spellcheck=\"false\"></textarea>"
                    + "<button type=\"submit\">" + escape(send) + "</button></form>"
                    + "<p>" + escape(note) + "</p>");
            this.sent = page(lang, title, "<p>" + escape(sent) + "</p>");
            this.wrongCode = page(lang, title, "<p>" + escape(wrongCode) + "</p>");
            this.lockedOut = page(lang, title, "<p>" + escape(lockedOut) + "</p>");
        }

        // No script, no image and nothing loaded from anywhere, which the CSP also
        // enforces. The browser picks light or dark colours itself.
        private static String page(String lang, String title, String body) {
            return "<!doctype html><html lang=\"" + escape(lang) + "\"><head><meta charset=\"utf-8\">"
                    + "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    + "<title>" + escape(title) + "</title><style>"
                    + ":root{color-scheme:light dark}body{font:16px/1.4 system-ui,sans-serif;margin:0 auto;padding:16px;max-width:40em}"
                    + "label,textarea,button{display:block;width:100%;box-sizing:border-box}"
                    + "textarea{min-height:12em;margin:8px 0 12px;font:14px monospace}"
                    + "button{padding:12px;font-size:16px}"
                    + "</style></head><body><h1>" + escape(title) + "</h1>" + body + "</body></html>";
        }
    }

    /** A QR code as pixels, black on white: side by side of them, row by row. */
    static final class Qr {
        final int side;
        final int[] pixels;
        Qr(int side, int[] pixels) { this.side = side; this.pixels = pixels; }
    }

    private final ServerSocket server;
    private final String code;
    private final String host;
    private final Pages pages;
    private final long lifetimeMs;
    private final int readTimeoutMs;
    private final int connectionDeadlineMs;
    private final AtomicBoolean stopped = new AtomicBoolean();
    private volatile Socket current;
    private Thread thread;

    /** Binds to address, which must be a private-network IPv4 address (see pickAddress), on a port the system picks. */
    LinkReceiver(InetAddress address, String code, Pages pages) throws IOException {
        this(lanOnly(address), code, pages, LIFETIME_MS, READ_TIMEOUT_MS, CONNECTION_DEADLINE_MS);
    }

    // For tests: any address, loopback too, and shorter times.
    LinkReceiver(InetAddress address, String code, Pages pages, long lifetimeMs, int readTimeoutMs,
                 int connectionDeadlineMs) throws IOException {
        this.code = normalizeCode(code);
        if (this.code.length() != CODE_LENGTH || !inAlphabet(this.code)) throw new IllegalArgumentException("Not a receiver code");
        this.pages = pages;
        this.lifetimeMs = lifetimeMs;
        this.readTimeoutMs = readTimeoutMs;
        this.connectionDeadlineMs = connectionDeadlineMs;
        // Plain HTTP on purpose: it serves a phone's browser on the local network,
        // which has no way to check a certificate for a local address, so there is
        // nothing TLS could be set up with.
        server = new ServerSocket(0, 4, address); // nosemgrep: java.lang.security.audit.crypto.unencrypted-socket.unencrypted-socket
        host = address.getHostAddress() + ":" + server.getLocalPort();
    }

    private static InetAddress lanOnly(InetAddress address) {
        if (pickAddress(Collections.singletonList(address)) == null)
            throw new IllegalArgumentException("Not a private-network IPv4 address");
        return address;
    }

    /** The first private-network IPv4 address: 10/8, 172.16/12 or 192.168/16. Not a public, CGNAT, link-local or IPv6 one. */
    static InetAddress pickAddress(List<InetAddress> addresses) {
        for (InetAddress address : addresses)
            if (address instanceof Inet4Address && address.isSiteLocalAddress()) return address;
        return null;
    }

    static String newCode(SecureRandom random) {
        StringBuilder code = new StringBuilder(CODE_LENGTH);
        for (int i = 0; i < CODE_LENGTH; i++) code.append(ALPHABET.charAt(random.nextInt(ALPHABET.length())));
        return code.toString();
    }

    /** A code as typed: any case, with or without the hyphen, and O, I or L for 0, 1 or 1. */
    static String normalizeCode(String typed) {
        StringBuilder code = new StringBuilder(typed.length());
        for (char c : typed.toUpperCase(Locale.ROOT).toCharArray()) {
            if (c == '-') continue;
            code.append(c == 'O' ? '0' : c == 'I' || c == 'L' ? '1' : c);
        }
        return code.toString();
    }

    private static boolean inAlphabet(String code) {
        for (char c : code.toCharArray()) if (ALPHABET.indexOf(c) < 0) return false;
        return true;
    }

    /** The page's address, with the code split in two for typing. */
    String url() {
        return "http://" + host + "/" + code.substring(0, CODE_LENGTH / 2) + "-" + code.substring(CODE_LENGTH / 2);
    }

    /**
     * text's QR code with the 4-module quiet zone scanners need, at the largest whole
     * number of pixels a module that fits in maxSide, so that it is never blurred.
     */
    static Qr qr(String text, int maxSide) {
        QrCode symbol = QrCode.encodeText(text, QrCode.Ecc.MEDIUM);
        int modules = symbol.size + 8;
        int scale = Math.max(1, maxSide / modules);
        int side = modules * scale;
        int[] pixels = new int[side * side];
        for (int y = 0; y < side; y++)
            for (int x = 0; x < side; x++)
                // Outside the symbol, getModule() is light: that is the quiet zone.
                pixels[y * side + x] = symbol.getModule(x / scale - 4, y / scale - 4) ? 0xFF000000 : 0xFFFFFFFF;
        return new Qr(side, pixels);
    }

    /** Serves requests on a thread of its own until a text arrives, the time is up, or stop(). */
    synchronized void start(Listener listener) {
        if (thread != null) throw new IllegalStateException("Already started");
        thread = new Thread(() -> serve(listener), "freetunnel-receiver");
        thread.setDaemon(true);
        thread.start();
    }

    /** Closes the port. Idempotent, and the listener hears nothing more. */
    void stop() {
        if (stopped.compareAndSet(false, true)) closeAll();
    }

    private void closeAll() {
        try { server.close(); } catch (IOException ignored) { }
        Socket client = current;
        if (client != null) try { client.close(); } catch (IOException ignored) { }
    }

    // True for the one caller that stops it, and only if stop() did not come first.
    private boolean stopSelf() {
        if (!stopped.compareAndSet(false, true)) return false;
        closeAll();
        return true;
    }

    // One connection at a time: a phone sends a page request, then the text.
    private void serve(Listener listener) {
        long end = System.nanoTime() + lifetimeMs * 1_000_000;
        int wrongCodes = 0;
        try {
            while (!stopped.get()) {
                long left = (end - System.nanoTime()) / 1_000_000;
                if (left <= 0) { if (stopSelf()) listener.stopped(Stop.TIMEOUT); return; }
                server.setSoTimeout((int) Math.min(left, Integer.MAX_VALUE));
                Socket client;
                try { client = server.accept(); }
                catch (SocketTimeoutException timeUp) { continue; }
                Outcome outcome;
                String sender = client.getInetAddress().getHostAddress();
                current = client;
                try (Socket open = client) {
                    if (stopped.get()) return;
                    long deadline = System.nanoTime() + connectionDeadlineMs * 1_000_000L;
                    InputStream in = new BufferedInputStream(new DeadlineInput(open, readTimeoutMs, deadline));
                    OutputStream out = open.getOutputStream();
                    outcome = handle(in, out, code, host, pages, wrongCodes + 1 >= MAX_WRONG_CODES);
                    out.flush();
                } catch (IOException slowOrGone) {
                    // Too slow, or gone before the end: the next connection may be the phone.
                    continue;
                } finally {
                    current = null;
                }
                if (outcome.kind == Kind.RECEIVED) {
                    if (stopSelf()) listener.received(outcome.text, sender);
                    return;
                }
                if (outcome.kind == Kind.WRONG_CODE && ++wrongCodes >= MAX_WRONG_CODES) {
                    if (stopSelf()) listener.stopped(Stop.LOCKED_OUT);
                    return;
                }
            }
        } catch (IOException failed) {
            // A close by stop() lands here too; stopSelf() tells the two apart.
            if (stopSelf()) listener.stopped(Stop.FAILED);
        }
    }

    // Each read waits at most readTimeoutMs, and all of them together end at the
    // deadline, so a client that trickles bytes cannot hold the port either.
    private static final class DeadlineInput extends InputStream {
        private final Socket socket;
        private final InputStream in;
        private final int readTimeoutMs;
        private final long deadline;

        DeadlineInput(Socket socket, int readTimeoutMs, long deadline) throws IOException {
            this.socket = socket;
            this.in = socket.getInputStream();
            this.readTimeoutMs = readTimeoutMs;
            this.deadline = deadline;
        }

        private void arm() throws IOException {
            long left = (deadline - System.nanoTime()) / 1_000_000;
            if (left <= 0) throw new SocketTimeoutException("Connection deadline");
            socket.setSoTimeout((int) Math.min(readTimeoutMs, left));
        }

        @Override public int read() throws IOException { arm(); return in.read(); }
        @Override public int read(byte[] b, int off, int len) throws IOException { arm(); return in.read(b, off, len); }
    }

    /**
     * One request: a GET gets the form, whatever its path; a POST to the code's path
     * with a link= form field is the text. lastStrike shows the page that says the
     * receiver stopped instead of the wrong-code page.
     */
    static Outcome handle(InputStream in, OutputStream out, String code, String host, Pages pages,
                          boolean lastStrike) throws IOException {
        byte[] head = readHead(in);
        if (head == null) return new Outcome(Kind.REFUSED, null);
        if (head.length == 0) return refuse(out, 431, "Request Header Fields Too Large");
        List<String[]> headers = new ArrayList<>();
        String[] request = parseHead(head, headers);
        if (request == null || values(headers, "host").size() != 1 || !host.equals(values(headers, "host").get(0)))
            return refuse(out, 400, "Bad Request");
        if (request[0].equals("GET")) {
            respond(out, 200, "OK", "text/html", pages.form);
            return new Outcome(Kind.SERVED, null);
        }
        if (!request[0].equals("POST")) return refuse(out, 405, "Method Not Allowed");
        if (!values(headers, "transfer-encoding").isEmpty()) return refuse(out, 501, "Not Implemented");
        List<String> lengths = values(headers, "content-length");
        if (lengths.isEmpty()) return refuse(out, 411, "Length Required");
        if (lengths.size() != 1 || !lengths.get(0).matches("[0-9]{1,7}")) return refuse(out, 400, "Bad Request");
        int length = Integer.parseInt(lengths.get(0));
        if (length > MAX_BODY_BYTES) return refuse(out, 413, "Content Too Large");
        List<String> types = values(headers, "content-type");
        if (types.size() != 1 || !types.get(0).split(";", 2)[0].trim().equalsIgnoreCase("application/x-www-form-urlencoded"))
            return refuse(out, 415, "Unsupported Media Type");
        byte[] body = new byte[length];
        for (int read = 0; read < length; ) {
            int n = in.read(body, read, length - read);
            if (n < 0) return new Outcome(Kind.REFUSED, null);
            read += n;
        }
        // Read before the code is checked, so that the browser gets the page: closing
        // with the text unread would reset the connection instead.
        String path = request[1];
        byte[] sentCode = normalizeCode(path.substring(1)).getBytes(StandardCharsets.UTF_8);
        if (!MessageDigest.isEqual(sentCode, code.getBytes(StandardCharsets.UTF_8))) {
            answer(out, 404, "Not Found", lastStrike ? pages.lockedOut : pages.wrongCode);
            return new Outcome(Kind.WRONG_CODE, null);
        }
        String text = formField(body, "link");
        if (text == null || text.trim().isEmpty()) return refuse(out, 400, "Bad Request");
        answer(out, 200, "OK", pages.sent);
        return new Outcome(Kind.RECEIVED, text.trim());
    }

    // The answer to a whole send, which counts whether or not the answer gets
    // through: a guesser that leaves without reading it must still use up its
    // tries, and a phone that leaves early has still sent its text.
    private static void answer(OutputStream out, int status, String reason, String page) {
        try {
            respond(out, status, reason, "text/html", page);
            out.flush();
        } catch (IOException ignored) {
            // The sender is gone.
        }
    }

    // The request line and headers, up to the blank line after them. Null when the
    // client closes before that, or sends what cannot start a method, such as the
    // TLS handshake of a browser that tries https first: dropped at once, it falls
    // back to http instead of waiting out a read timeout. Empty when they are longer
    // than MAX_HEADER_BYTES.
    private static byte[] readHead(InputStream in) throws IOException {
        ByteArrayOutputStream head = new ByteArrayOutputStream();
        int matched = 0;
        while (head.size() < MAX_HEADER_BYTES) {
            int b = in.read();
            if (b < 0 || head.size() == 0 && (b < 'A' || b > 'Z')) return null;
            head.write(b);
            matched = b == "\r\n\r\n".charAt(matched) ? matched + 1 : b == '\r' ? 1 : 0;
            if (matched == 4) return head.toByteArray();
        }
        return new byte[0];
    }

    // Method and path, with the headers' lower-case names and values added to headers.
    // Null for anything a browser does not send: other bytes than printable ASCII and
    // tabs between the line ends, a request line not of three parts, a path not
    // starting with /, another version than HTTP/1.0 or 1.1, a folded header, or a
    // header name that is not a token.
    private static String[] parseHead(byte[] head, List<String[]> headers) {
        String[] lines = new String(head, 0, head.length - 4, StandardCharsets.ISO_8859_1).split("\r\n", -1);
        for (String line : lines)
            for (char c : line.toCharArray()) if ((c < 0x20 || c > 0x7e) && c != '\t') return null;
        String[] request = lines[0].split(" ", -1);
        if (request.length != 3 || !request[1].startsWith("/")
                || !(request[2].equals("HTTP/1.1") || request[2].equals("HTTP/1.0"))) return null;
        for (int i = 1; i < lines.length; i++) {
            int colon = lines[i].indexOf(':');
            if (colon <= 0 || !lines[i].substring(0, colon).matches("[!#$%&'*+.^_`|~0-9A-Za-z-]+")) return null;
            headers.add(new String[]{lines[i].substring(0, colon).toLowerCase(Locale.ROOT), lines[i].substring(colon + 1).trim()});
        }
        // A query string is dropped: the code is the path.
        int query = request[1].indexOf('?');
        return new String[]{request[0], query < 0 ? request[1] : request[1].substring(0, query)};
    }

    private static List<String> values(List<String[]> headers, String name) {
        List<String> found = new ArrayList<>();
        for (String[] header : headers) if (header[0].equals(name)) found.add(header[1]);
        return found;
    }

    // The first field of that name in a form the browser encoded, null when there is
    // none, or when the body is not ASCII or an escape is broken.
    private static String formField(byte[] body, String name) {
        for (byte b : body) if (b < 0x20 || b > 0x7e) return null;
        try {
            for (String pair : new String(body, StandardCharsets.US_ASCII).split("&")) {
                int equals = pair.indexOf('=');
                if (URLDecoder.decode(equals < 0 ? pair : pair.substring(0, equals), "UTF-8").equals(name))
                    return equals < 0 ? "" : URLDecoder.decode(pair.substring(equals + 1), "UTF-8");
            }
        } catch (IllegalArgumentException broken) {
            return null;
        } catch (UnsupportedEncodingException impossible) {
            throw new AssertionError(impossible);
        }
        return null;
    }

    private static Outcome refuse(OutputStream out, int status, String reason) throws IOException {
        respond(out, status, reason, "text/plain", status + " " + reason);
        return new Outcome(Kind.REFUSED, null);
    }

    // Every answer closes the connection, is never cached or framed, and lets the
    // page load nothing and post only back to the TV.
    private static void respond(OutputStream out, int status, String reason, String type, String body) throws IOException {
        byte[] content = body.getBytes(StandardCharsets.UTF_8);
        String head = "HTTP/1.1 " + status + " " + reason + "\r\n"
                + "Content-Type: " + type + "; charset=utf-8\r\n"
                + "Content-Length: " + content.length + "\r\n"
                + "Connection: close\r\n"
                + "Cache-Control: no-store\r\n"
                + "Referrer-Policy: no-referrer\r\n"
                + "X-Content-Type-Options: nosniff\r\n"
                + "X-Frame-Options: DENY\r\n"
                + "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\n"
                + (status == 405 ? "Allow: GET, POST\r\n" : "")
                + "\r\n";
        out.write(head.getBytes(StandardCharsets.US_ASCII));
        out.write(content);
    }

    static String escape(String text) {
        StringBuilder escaped = new StringBuilder(text.length());
        for (char c : text.toCharArray()) {
            switch (c) {
                case '&': escaped.append("&amp;"); break;
                case '<': escaped.append("&lt;"); break;
                case '>': escaped.append("&gt;"); break;
                case '"': escaped.append("&quot;"); break;
                case '\'': escaped.append("&#39;"); break;
                default: escaped.append(c);
            }
        }
        return escaped.toString();
    }
}
