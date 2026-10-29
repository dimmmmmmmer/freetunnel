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
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * A web page on the local network that takes links and TOML files from a phone's
 * browser, for a TV, which has no camera to scan a QR code and no keyboard to type a
 * link. The TV shows a QR code of url(); the page there sends the text back, pasted
 * or as a file, as many times as the phone likes. Plain JDK, so the JVM tests drive
 * it with real sockets.
 *
 * <p>Browsers give a page on a local http:// address no way to encrypt, so the text
 * crosses the local network in the clear. What limits that: it binds only to the
 * TV's private-network address, for 5 minutes from when it opens, however many texts
 * come, and stops after 3 sends with a wrong code, which add up over the whole time.
 * It takes at most MAX_WAITING texts that the TV has not answered yet, and sends
 * after that get a page that says to wait, which is no strike. The code is in the
 * URL and sent back with the text, so another page in the phone's browser cannot
 * send one without it. A GET shows the same page whatever its path, so only a send
 * tests a code. The TV then shows each text it received, one at a time, with the
 * sender's address, and asks before adding it. Nothing here is logged: the text
 * holds the VPN password.
 */
final class LinkReceiver {
    static final long LIFETIME_MS = 300_000;
    static final int FIRST_BYTE_TIMEOUT_MS = 2_000;
    static final int READ_TIMEOUT_MS = 10_000;
    static final int CONNECTION_DEADLINE_MS = 15_000;
    static final int DRAIN_MS = 2_000;
    static final int MAX_HEADER_BYTES = 8192;
    static final int MAX_BODY_BYTES = 512 * 1024;
    static final int MAX_WRONG_CODES = 3;
    // Texts taken and not yet answered on the TV, the one it shows included. At most this
    // many are held in memory, MAX_BODY_BYTES each. A send to the right code while that
    // many wait is turned away with the busy page, and is no strike.
    static final int MAX_WAITING = 10;
    static final int CODE_LENGTH = 8;
    // Crockford's base32: no I, L or O, easily taken for 1 and 0 on a TV across the
    // room, and no U. 8 of them are 40 bits.
    static final String ALPHABET = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

    enum Stop { TIMEOUT, LOCKED_OUT, FAILED }

    /** Called on the receiver's own thread. */
    interface Listener {
        /**
         * A text came in, as many times as the phone sends one while the receiver is open,
         * and never after stop() has returned, nor after it was called, but for a text already being handed over.
         * Each one holds a place in the waiting count
         * that the receiver was given, which the owner gives back when the TV has answered it.
         * fileName is the name of the file the text came in, as fileName() leaves it; null when it was pasted or has none.
         */
        void received(String text, String fileName, String sender);
        /** The receiver closed itself, once, and then it takes nothing more; not after stop(). */
        void stopped(Stop reason);
    }

    /** What handle() did with one request. BUSY is a right code with MAX_WAITING texts waiting: no text, no strike. */
    enum Kind { SERVED, REFUSED, BUSY, WRONG_CODE, RECEIVED }
    static final class Outcome {
        final Kind kind;
        final String text;
        final String fileName;
        // Answered with the request's body still unread, so serve() reads the rest
        // before it closes the connection.
        final boolean bodyUnread;
        Outcome(Kind kind, String text) { this(kind, text, null, false); }
        Outcome(Kind kind, String text, String fileName) { this(kind, text, fileName, false); }
        Outcome(Kind kind, String text, String fileName, boolean bodyUnread) {
            this.kind = kind; this.text = text; this.fileName = fileName; this.bodyUnread = bodyUnread;
        }
    }

    /** The pages the phone's browser shows, whole and static: nothing a request sends is put in them. */
    static final class Pages {
        final String form;
        final String sent;
        final String retry;
        final String tooLarge;
        final String busy;
        final String wrongCode;
        final String lockedOut;

        // The strings are the app's own, in the TV's language. The form has no action,
        // so it posts back to the address it was opened at, code and all. It takes a
        // pasted text or a file, so neither field is required: a send with neither,
        // with both, or with a file that is not text gets the form again under the
        // retry text, and one too large to take, such as a photo chosen by mistake,
        // under the too-large text. No accept attribute: a phone's picker may not
        // know a .toml file's type, and would grey it out.
        //
        // The receiver goes on after a text, so the page that says it was sent, and the
        // one that says the TV has too many waiting, are the form again under their notice:
        // the phone sends the next one from there. A wrong code and the stop after the
        // last one are a notice only: the form on them would post to the same wrong address.
        Pages(String lang, String title, String prompt, String file, String send, String note,
              String sent, String retry, String tooLarge, String busy, String wrongCode, String lockedOut) {
            String fields = "<form method=\"post\" enctype=\"multipart/form-data\" accept-charset=\"utf-8\">"
                    + "<label for=\"link\">" + escape(prompt) + "</label>"
                    + "<textarea id=\"link\" name=\"link\" autocomplete=\"off\" autocapitalize=\"off\""
                    + " autocorrect=\"off\" spellcheck=\"false\"></textarea>"
                    + "<label for=\"file\">" + escape(file) + "</label>"
                    + "<input type=\"file\" id=\"file\" name=\"file\">"
                    + "<button type=\"submit\">" + escape(send) + "</button></form>"
                    + "<p class=\"note\">" + escape(note) + "</p>";
            form = page(lang, title, fields);
            this.sent = page(lang, title, notice("ok", sent) + fields);
            this.retry = page(lang, title, notice("warn", retry) + fields);
            this.tooLarge = page(lang, title, notice("warn", tooLarge) + fields);
            this.busy = page(lang, title, notice("warn", busy) + fields);
            this.wrongCode = page(lang, title, notice("warn", wrongCode));
            this.lockedOut = page(lang, title, notice("warn", lockedOut));
        }

        // A message above the form, on the desktop's notice box: "ok" is green, "warn" amber.
        private static String notice(String kind, String text) {
            return "<p class=\"notice " + kind + "\" role=\"" + (kind.equals("ok") ? "status" : "alert") + "\">" + escape(text) + "</p>";
        }

        // No script, no image and nothing loaded from anywhere, which the CSP also
        // enforces: the page is its own style, and the browser's own font.
        private static String page(String lang, String title, String body) {
            return "<!doctype html><html lang=\"" + escape(lang) + "\"><head><meta charset=\"utf-8\">"
                    + "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    + "<meta name=\"color-scheme\" content=\"light dark\">"
                    + "<title>" + escape(title) + "</title><style>" + STYLE + "</style></head><body><main><h1>"
                    + escape(title) + "</h1>" + body + "</main></body></html>";
        }

        private static final String FONT = "system-ui,-apple-system,Segoe UI,Roboto,sans-serif";
        private static final String STYLE = style(Theme.LIGHT, Theme.DARK);

        // The app's own look, which is the desktop's (Theme.java holds its colours, and a test holds
        // those to qml/Main.qml): custom properties on :root for the light theme, the dark ones when
        // the phone is in dark mode, and the editor card's parts: a title 15 in Medium, as every card's is, a label 13 in textDim over a
        // box in inputBg with a 1 px inputBorder, radius 8, that takes an accent border where it has the
        // focus; a neutral button, 32 high, for the file's, and a primary one, as wide as its label and at the right, to send; the notice
        // on infoBg, 12; and the page's 18 margins. Nothing has a pressed or a tapped look beyond its hover, as the desktop's have none.
        // The text box is the desktop's certificate box (the only monospace one it has) in everything but its size: inset 8 on every side,
        // and 10 under it, where the desktop's form has 10 between a field and the next label. Its text is 16 px monospace where the
        // desktop's is 12: a browser on an iPhone zooms the page in on any field under 16 px when it takes the focus, and the page must
        // not jump about under a finger. The viewport is not locked with a maximum-scale instead, which would take pinch-zoom from Android's
        // Chrome. The file field's own words ("Choose File", "No file chosen") are the browser's, in its language: a custom
        // button in the app's words needs a script to say which file is chosen, and the page has none, which its CSP and its test forbid.
        private static String style(Theme light, Theme dark) {
            String fileButton = "height:32px;min-width:76px;margin:0 12px 0 0;padding:0 13px;border:0;border-radius:8px;"
                    + "background:var(--surface);color:var(--text);font:14px " + FONT + ";cursor:pointer";
            String fileHover = "background:var(--surface-hover)";
            return ":root{color-scheme:light dark;" + tokens(light) + "}"
                    + "@media (prefers-color-scheme:dark){:root{" + tokens(dark) + "}}"
                    + "*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"
                    + "body{margin:0;background:var(--bg);color:var(--text);font:16px/1.25 " + FONT + ";-webkit-text-size-adjust:100%}"
                    + "main{max-width:32em;margin:0 auto;padding:18px 18px 32px}"
                    + "h1{margin:0 0 14px;font-size:15px;font-weight:500;line-height:1.2}"
                    + ".notice{margin:0 0 16px;padding:8px 10px;border-radius:8px;background:var(--info);font-size:12px;line-height:1.2}"
                    + ".ok{color:var(--success)}"
                    + ".warn{color:var(--warn)}"
                    + "label{display:block;margin:0 0 4px;font-size:13px;line-height:1.2;color:var(--dim)}"
                    + "textarea{display:block;width:100%;min-height:150px;margin:0 0 10px;padding:8px;"
                    + "border:1px solid var(--input-border);border-radius:8px;background:var(--input-bg);color:var(--text);"
                    + "font:16px/1.4 ui-monospace,Menlo,Consolas,monospace;resize:none;outline:none;"
                    + "-webkit-appearance:none;appearance:none}"
                    + "textarea:focus{border-color:var(--accent)}"
                    + "input[type=file]{display:block;width:100%;margin:0 0 16px;font:14px " + FONT + ";color:var(--dim)}"
                    // The two names for the same button, each in a rule of its own: a browser that does
                    // not know one drops the whole rule it is in.
                    + "input[type=file]::file-selector-button{" + fileButton + "}"
                    + "input[type=file]::file-selector-button:hover{" + fileHover + "}"
                    + "input[type=file]::-webkit-file-upload-button{" + fileButton + "}"
                    + "input[type=file]::-webkit-file-upload-button:hover{" + fileHover + "}"
                    // The file field's own focus is its button's hover look, not the browser's ring around the field.
                    + "input[type=file]:focus-visible{outline:none}"
                    + "input[type=file]:focus-visible::file-selector-button{" + fileHover + "}"
                    + "input[type=file]:focus-visible::-webkit-file-upload-button{" + fileHover + "}"
                    // The desktop's primary button: 32 high, radius 8, 14 in the normal weight, as wide as its label and 88 at least, and at
                    // the right of its row.
                    + "button{display:block;margin-left:auto;min-width:88px;height:32px;padding:0 13px;border:0;border-radius:8px;"
                    + "background:var(--accent);color:var(--accent-text);font:400 14px " + FONT + ";cursor:pointer;"
                    + "-webkit-appearance:none;appearance:none}"
                    + "button:hover{background:var(--accent-hover)}"
                    // The desktop has no ring anywhere: where the focus is, a control takes the look it has under the pointer, and the
                    // text box its accent border. A rule of its own, since a browser that does not know :focus-visible drops the rule.
                    + "button:focus-visible{outline:none;background:var(--accent-hover)}"
                    + ".note{margin:14px 0 0;font-size:13px;line-height:1.25;color:var(--faint)}";
        }

        private static String tokens(Theme t) {
            return "--bg:" + hex(t.bg) + ";--surface:" + hex(t.surface) + ";--surface-hover:" + hex(t.surfaceHover)
                    + ";--input-bg:" + hex(t.inputBg) + ";--input-border:" + hex(t.inputBorder)
                    + ";--text:" + hex(t.text) + ";--dim:" + hex(t.textDim) + ";--faint:" + hex(t.textFaint)
                    + ";--accent:" + hex(t.accent) + ";--accent-hover:" + hex(t.primaryHover) + ";--accent-text:" + hex(t.accentText)
                    + ";--success:" + hex(t.success) + ";--warn:" + hex(t.warn)
                    + ";--info:rgba(" + (t.infoBg >> 16 & 0xFF) + "," + (t.infoBg >> 8 & 0xFF) + "," + (t.infoBg & 0xFF) + ","
                    + String.format(Locale.ROOT, "%.2f", (t.infoBg >>> 24) / 255.0) + ")";
        }

        private static String hex(int argb) { return String.format(Locale.ROOT, "#%06x", argb & 0xFFFFFF); }
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
    private final int firstByteTimeoutMs;
    private final int readTimeoutMs;
    private final int connectionDeadlineMs;
    private final AtomicInteger waiting;
    private final AtomicBoolean stopped = new AtomicBoolean();
    // Held while a text is handed to the listener, and by stop() while it takes effect, so that
    // no text is handed over once stop() has returned.
    private final Object delivery = new Object();
    // Set by stop() before it waits for a text that is being handed over: Java's monitors are not fair, and the serving thread, let go
    // of the lock by that text, could otherwise take the next connection and hand its text over before the waiting stop() got in.
    // From the call of stop(), no text is handed over but the one already being, and the receiver says nothing more of itself.
    private volatile boolean stopRequested;
    private volatile Socket current;
    private Thread thread;

    /**
     * Binds to address, which must be a private-network IPv4 address (see pickAddress), on a port the system picks.
     * waiting counts the texts taken and not yet answered, over this receiver and any other that shares it:
     * the receiver adds one for each text it takes, and whoever it hands the text to takes it off once the TV has answered.
     */
    LinkReceiver(InetAddress address, String code, Pages pages, AtomicInteger waiting) throws IOException {
        this(lanOnly(address), code, pages, waiting, LIFETIME_MS, FIRST_BYTE_TIMEOUT_MS, READ_TIMEOUT_MS, CONNECTION_DEADLINE_MS);
    }

    // For tests: any address, loopback too, and shorter times.
    LinkReceiver(InetAddress address, String code, Pages pages, AtomicInteger waiting, long lifetimeMs, int firstByteTimeoutMs,
                 int readTimeoutMs, int connectionDeadlineMs) throws IOException {
        this.code = normalizeCode(code);
        if (this.code.length() != CODE_LENGTH || !inAlphabet(this.code)) throw new IllegalArgumentException("Not a receiver code");
        this.pages = pages;
        this.waiting = waiting;
        this.lifetimeMs = lifetimeMs;
        this.firstByteTimeoutMs = firstByteTimeoutMs;
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

    /** Serves requests on a thread of its own until the time is up, too many wrong codes come, or stop(). */
    synchronized void start(Listener listener) {
        if (thread != null) throw new IllegalStateException("Already started");
        thread = new Thread(() -> serve(listener), "freetunnel-receiver");
        thread.setDaemon(true);
        thread.start();
    }

    /**
     * Closes the port. Idempotent, and the listener hears nothing more: a text that is being
     * handed over finishes first, and none is handed over after this is called, let alone after it returns.
     */
    void stop() {
        stopRequested = true;
        synchronized (delivery) {
            if (!stopped.compareAndSet(false, true)) return;
        }
        closeAll();
    }

    private void closeAll() {
        try { server.close(); } catch (IOException ignored) { }
        Socket client = current;
        if (client != null) try { client.close(); } catch (IOException ignored) { }
    }

    // True for the one caller that stops it, and only if stop() did not come first (nor was called and is waiting its turn).
    private boolean stopSelf() {
        if (stopRequested || !stopped.compareAndSet(false, true)) return false;
        closeAll();
        return true;
    }

    // One connection at a time: a phone sends a page request, then the text. It goes on after a
    // text, until the time is up, which is counted from here and no sends extend, or the wrong
    // codes, which are counted over all of it, reach MAX_WRONG_CODES.
    private void serve(Listener listener) {
        long end = System.nanoTime() + lifetimeMs * 1_000_000;
        int wrongCodes = 0;
        try {
            while (!stopped.get() && !stopRequested) {
                long left = (end - System.nanoTime()) / 1_000_000;
                if (left <= 0) { if (stopSelf()) listener.stopped(Stop.TIMEOUT); return; }
                server.setSoTimeout((int) Math.min(left, Integer.MAX_VALUE));
                Socket client;
                try { client = server.accept(); }
                catch (SocketTimeoutException timeUp) { continue; }
                Outcome outcome = null;
                String sender = client.getInetAddress().getHostAddress();
                current = client;
                try (Socket open = client) {
                    if (stopped.get() || stopRequested) return;
                    long deadline = System.nanoTime() + connectionDeadlineMs * 1_000_000L;
                    InputStream in = new BufferedInputStream(new DeadlineInput(open, firstByteTimeoutMs, readTimeoutMs, deadline));
                    OutputStream out = open.getOutputStream();
                    outcome = handle(in, out, code, host, pages, wrongCodes + 1 >= MAX_WRONG_CODES, waiting);
                    out.flush();
                    if (outcome.bodyUnread) drain(open, deadline);
                } catch (IOException slowOrGone) {
                    // Too slow, or gone before the end: the next connection may be the phone. A text that
                    // handle() took still counts, as its answer does, whatever became of the answer.
                    if (outcome == null) continue;
                } finally {
                    current = null;
                }
                if (outcome.kind == Kind.RECEIVED) {
                    deliver(listener, outcome, sender);
                    continue;
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

    // Hands a text taken to the listener, unless stop() came first, or was called while an earlier
    // text was being handed over, when it is not taken after all: the place it holds in the waiting
    // count is given back, and the phone, which was told it was sent, has sent it to a TV that
    // stopped listening a moment before.
    private void deliver(Listener listener, Outcome outcome, String sender) {
        synchronized (delivery) {
            if (stopped.get() || stopRequested) { waiting.decrementAndGet(); return; }
            listener.received(outcome.text, outcome.fileName, sender);
        }
    }

    // After an answer sent before the request's body was read, as to a photo too
    // large to take. Closed with the body unread, the connection would be reset, and
    // a reset can erase the answer on the phone before its browser reads it, which it
    // does once it has sent the body. So the TV ends its side after the answer, and
    // reads and drops what the phone still sends until the phone ends its side too,
    // for DRAIN_MS at most and never past the connection's deadline.
    private static void drain(Socket socket, long deadline) {
        long end = System.nanoTime() + DRAIN_MS * 1_000_000L;
        if (deadline - end < 0) end = deadline;
        try {
            socket.shutdownOutput();
            InputStream in = socket.getInputStream();
            byte[] dropped = new byte[8192];
            while (true) {
                long left = (end - System.nanoTime()) / 1_000_000;
                if (left <= 0) return;
                socket.setSoTimeout((int) left);
                if (in.read(dropped) < 0) return;
            }
        } catch (IOException doneOrGone) {
            // Its time is up, or the phone has gone: the connection closes either way.
        }
    }

    // Each read waits at most readTimeoutMs, and all of them together end at the
    // deadline, so a client that trickles bytes cannot hold the port either. The
    // first read waits only firstByteMs: a browser can open a connection ahead of
    // time and send its request on another one, which would otherwise wait behind
    // it for a whole read timeout. A request that has been sent is already there
    // when its connection is taken.
    private static final class DeadlineInput extends InputStream {
        private final Socket socket;
        private final InputStream in;
        private final int readTimeoutMs;
        private final long deadline;
        private int waitMs;

        DeadlineInput(Socket socket, int firstByteMs, int readTimeoutMs, long deadline) throws IOException {
            this.socket = socket;
            this.in = socket.getInputStream();
            this.readTimeoutMs = readTimeoutMs;
            this.deadline = deadline;
            this.waitMs = firstByteMs;
        }

        private void arm() throws IOException {
            long left = (deadline - System.nanoTime()) / 1_000_000;
            if (left <= 0) throw new SocketTimeoutException("Connection deadline");
            socket.setSoTimeout((int) Math.min(waitMs, left));
            waitMs = readTimeoutMs;
        }

        @Override public int read() throws IOException { arm(); return in.read(); }
        @Override public int read(byte[] b, int off, int len) throws IOException { arm(); return in.read(b, off, len); }
    }

    /**
     * One request: a GET gets the form, whatever its path; a POST to the code's path
     * is the text: the page's multipart form with the link field or a file in its
     * file field, or a link= form field as the page sent before it took files.
     * lastStrike shows the page that says the receiver stopped instead of the
     * wrong-code page. waiting is the count of texts taken and not yet answered: with
     * MAX_WAITING of them, a send to the right code gets the busy page and is taken
     * no further, and a text that is taken adds one to it before it is answered.
     */
    static Outcome handle(InputStream in, OutputStream out, String code, String host, Pages pages,
                          boolean lastStrike, AtomicInteger waiting) throws IOException {
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
        if (lengths.size() != 1 || !lengths.get(0).matches("[0-9]+")) return refuse(out, 400, "Bad Request");
        // A length of 8 digits or more is over the limit too, and not parsed: a photo
        // or a video chosen by mistake can be that large.
        int length = lengths.get(0).length() > 7 ? Integer.MAX_VALUE : Integer.parseInt(lengths.get(0));
        if (length > MAX_BODY_BYTES) {
            // The page is the form again under a note, which a GET shows anyone, so it
            // needs no code, and it is no strike.
            answer(out, 413, "Content Too Large", pages.tooLarge);
            return new Outcome(Kind.REFUSED, null, null, true);
        }
        List<String> types = values(headers, "content-type");
        String type = types.size() == 1 ? types.get(0).split(";", 2)[0].trim() : "";
        // Null for the urlencoded form; a multipart form without a boundary RFC 2046 allows is no form.
        String boundary = type.equalsIgnoreCase("multipart/form-data") ? boundary(types.get(0)) : null;
        if (boundary == null && !type.equalsIgnoreCase("application/x-www-form-urlencoded"))
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
        // Right code, and no room: whatever the form holds, nothing can be taken now. It is no strike,
        // and the page is the form again, for when the TV has answered what it has.
        if (waiting.get() >= MAX_WAITING) return busy(out, pages);
        if (boundary != null) return multipartSend(out, body, boundary, pages, waiting);
        String text = formField(body, "link");
        if (text == null || text.trim().isEmpty()) return refuse(out, 400, "Bad Request");
        return take(out, pages, waiting, new Outcome(Kind.RECEIVED, text.trim()));
    }

    // Takes a text: a place for it in the waiting count, then the page that says it was sent. The count is
    // checked above too, but another receiver that shares it may have filled the last place since.
    private static Outcome take(OutputStream out, Pages pages, AtomicInteger waiting, Outcome received) {
        for (int count = waiting.get(); ; count = waiting.get()) {
            if (count >= MAX_WAITING) return busy(out, pages);
            if (waiting.compareAndSet(count, count + 1)) break;
        }
        answer(out, 200, "OK", pages.sent);
        return received;
    }

    private static Outcome busy(OutputStream out, Pages pages) {
        answer(out, 503, "Service Unavailable", pages.busy);
        return new Outcome(Kind.BUSY, null);
    }

    // The page's own form, to the right code. What no browser sends is refused as
    // any such request is. A send with no text and no file, both, or a file that is
    // not UTF-8 text gets the form again under the retry text, and the receiver
    // goes on waiting: the phone can fix it.
    private static Outcome multipartSend(OutputStream out, byte[] body, String boundary, Pages pages,
                                         AtomicInteger waiting) throws IOException {
        Field[] fields = multipart(body, boundary);
        if (fields == null) return refuse(out, 400, "Bad Request");
        String text = fields[0] == null ? "" : utf8(body, fields[0].from, fields[0].to);
        if (text == null) return refuse(out, 400, "Bad Request");
        String file = fields[1] == null ? "" : utf8(body, fields[1].from, fields[1].to);
        // As a TOML file picked on the TV: a byte order mark is not part of the text.
        if (file != null && file.startsWith("\uFEFF")) file = file.substring(1);
        if (file == null || text.trim().isEmpty() == file.trim().isEmpty()) {
            answer(out, 400, "Bad Request", pages.retry);
            return new Outcome(Kind.REFUSED, null);
        }
        if (!text.trim().isEmpty()) return take(out, pages, waiting, new Outcome(Kind.RECEIVED, text.trim()));
        return take(out, pages, waiting, new Outcome(Kind.RECEIVED, file.trim(), fileName(fields[1].fileName)));
    }

    // Where a field's content is in the body, and the file name it came with: null
    // for the text field, empty for a file field with no file chosen.
    private static final class Field {
        final int from;
        final int to;
        final String fileName;
        Field(int from, int to, String fileName) { this.from = from; this.to = to; this.fileName = fileName; }
    }

    private static final byte[] CRLF = {'\r', '\n'};
    private static final byte[] CRLF_CRLF = {'\r', '\n', '\r', '\n'};
    private static final byte[] DASHES = {'-', '-'};

    // The link and file fields of a multipart/form-data body, either null when it has
    // none, or null when the body is not as a browser writes one: no preamble, each
    // part's headers, a blank line and its content, the last boundary followed by
    // nothing or a line end, link with no file name and file with one, at most one
    // of each. Fields of other names are skipped, as in the urlencoded form.
    private static Field[] multipart(byte[] body, String boundary) {
        byte[] delimiter = ("\r\n--" + boundary).getBytes(StandardCharsets.US_ASCII);
        if (!matches(body, 0, delimiter, 2)) return null;
        Field[] fields = new Field[2];
        int at = delimiter.length - 2;
        while (true) {
            if (matches(body, at, DASHES, 0))
                return at + 2 == body.length || at + 4 == body.length && matches(body, at + 2, CRLF, 0) ? fields : null;
            if (!matches(body, at, CRLF, 0)) return null;
            int start = at + 2;
            int next = indexOf(body, delimiter, start, body.length);
            int headEnd = next < 0 ? -1 : indexOf(body, CRLF_CRLF, start, next);
            if (headEnd < 0) return null;
            String[] disposition = disposition(body, start, headEnd);
            if (disposition == null) return null;
            int index = disposition[0].equals("link") ? 0 : disposition[0].equals("file") ? 1 : -1;
            if (index >= 0) {
                if (fields[index] != null || (disposition[1] == null) != (index == 0)) return null;
                fields[index] = new Field(headEnd + 4, next, disposition[1]);
            }
            at = next + delimiter.length;
        }
    }

    // The name and file name of a part's Content-Disposition: form-data header, the
    // file name null when it has none. Null when the part has not exactly one such
    // header with exactly one name and at most one file name, or when its headers are
    // not UTF-8, have a lone CR or LF or a NUL, a folded line or a name that is not a
    // token. Other control characters are let through: browsers escape only CR, LF
    // and the quote in a file name, which a file system may allow them in, and
    // fileName() drops them. Its other headers, such as a file's Content-Type, and
    // other parameters, such as a filename* no browser sends, are ignored.
    private static String[] disposition(byte[] body, int from, int to) {
        String head = utf8(body, from, to);
        if (head == null) return null;
        String[] found = null;
        for (String line : head.split("\r\n", -1)) {
            for (char c : line.toCharArray()) if (c == '\r' || c == '\n' || c == 0) return null;
            int colon = line.indexOf(':');
            if (colon <= 0 || !line.substring(0, colon).matches("[!#$%&'*+.^_`|~0-9A-Za-z-]+")) return null;
            if (!line.substring(0, colon).equalsIgnoreCase("content-disposition")) continue;
            if (found != null) return null;
            String value = line.substring(colon + 1);
            int semicolon = value.indexOf(';');
            if (semicolon < 0 || !value.substring(0, semicolon).trim().equalsIgnoreCase("form-data")) return null;
            List<String[]> parameters = parameters(value.substring(semicolon));
            if (parameters == null) return null;
            found = new String[2];
            int names = 0;
            int fileNames = 0;
            for (String[] parameter : parameters) {
                if (parameter[0].equals("name")) { found[0] = parameter[1]; names++; }
                if (parameter[0].equals("filename")) { found[1] = parameter[1]; fileNames++; }
            }
            if (names != 1 || fileNames > 1) return null;
        }
        return found;
    }

    // The parameters after a header value's type, from its first ';', as lower-case
    // names and values: ';' name=value with optional spaces around the ';', the value
    // a token or a quoted string. Null for anything else. A quoted string runs to the
    // next quote: browsers write a quote in a name as %22 and a backslash as it is,
    // so a backslash escapes nothing.
    private static List<String[]> parameters(String text) {
        List<String[]> found = new ArrayList<>();
        int i = 0;
        while (true) {
            i = skipSpaces(text, i);
            if (i == text.length()) return found;
            if (text.charAt(i) != ';') return null;
            i = skipSpaces(text, i + 1);
            int name = i;
            while (i < text.length() && isTokenChar(text.charAt(i))) i++;
            if (i == name || i == text.length() || text.charAt(i) != '=') return null;
            String key = text.substring(name, i).toLowerCase(Locale.ROOT);
            int value = ++i;
            if (i < text.length() && text.charAt(i) == '"') {
                int close = text.indexOf('"', i + 1);
                if (close < 0) return null;
                found.add(new String[]{key, text.substring(i + 1, close)});
                i = close + 1;
            } else {
                while (i < text.length() && isTokenChar(text.charAt(i))) i++;
                if (i == value) return null;
                found.add(new String[]{key, text.substring(value, i)});
            }
        }
    }

    private static int skipSpaces(String text, int i) {
        while (i < text.length() && (text.charAt(i) == ' ' || text.charAt(i) == '\t')) i++;
        return i;
    }

    private static boolean isTokenChar(char c) {
        return c >= '0' && c <= '9' || c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z' || "!#$%&'*+-.^_`|~".indexOf(c) >= 0;
    }

    // The boundary of a multipart/form-data type: its one boundary parameter, of 1 to
    // 70 of the characters RFC 2046 allows, the last not a space. Null for no
    // boundary, two, or another one.
    private static String boundary(String type) {
        int semicolon = type.indexOf(';');
        List<String[]> parameters = semicolon < 0 ? null : parameters(type.substring(semicolon));
        if (parameters == null) return null;
        String boundary = null;
        for (String[] parameter : parameters) {
            if (!parameter[0].equals("boundary")) continue;
            if (boundary != null) return null;
            boundary = parameter[1];
        }
        return boundary != null && boundary.matches("[0-9A-Za-z'()+_,./:=? -]{0,69}[0-9A-Za-z'()+_,./:=?-]") ? boundary : null;
    }

    // Longer than any file system lets a file's name be, so only a name no file has is cut.
    static final int MAX_FILE_NAME = 255;

    /**
     * A file's name as a browser sends it, to name a configuration as a file picked on
     * the TV is named: the quote and line breaks browsers write as %22, %0D and %0A
     * undone, then only what follows the last /, as for a file picked on the TV,
     * without control, line or paragraph separator characters, trimmed, and cut to
     * MAX_FILE_NAME characters. Null when the confirmation would show nothing of it
     * (ShownText), which also shows it on one line and cut, without what else could
     * reverse, hide or paint over text.
     *
     * <p>Browsers send no path, and those escapes are the only ones they write, so a
     * backslash is part of the name, as macOS, iOS and Linux allow in one.
     */
    static String fileName(String sent) {
        String name = sent.replace("%22", "\"").replace("%0D", "").replace("%0A", "");
        name = name.substring(name.lastIndexOf('/') + 1);
        StringBuilder kept = new StringBuilder(name.length());
        for (int i = 0; i < name.length(); ) {
            int c = name.codePointAt(i);
            i += Character.charCount(c);
            int type = Character.getType(c);
            if (type != Character.CONTROL && type != Character.LINE_SEPARATOR && type != Character.PARAGRAPH_SEPARATOR
                    && type != Character.SURROGATE) kept.appendCodePoint(c);
        }
        String saved = kept.toString().trim();
        if (saved.codePointCount(0, saved.length()) > MAX_FILE_NAME)
            saved = saved.substring(0, saved.offsetByCodePoints(0, MAX_FILE_NAME)).trim();
        return ShownText.oneLine(saved).trim().isEmpty() ? null : saved;
    }

    // UTF-8 text, or null when the bytes are not.
    private static String utf8(byte[] bytes, int from, int to) {
        try {
            return StandardCharsets.UTF_8.newDecoder().onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT).decode(ByteBuffer.wrap(bytes, from, to - from)).toString();
        } catch (CharacterCodingException notText) {
            return null;
        }
    }

    // Whether bytes holds part's bytes from skip on at at.
    private static boolean matches(byte[] bytes, int at, byte[] part, int skip) {
        if (at < 0 || bytes.length - at < part.length - skip) return false;
        for (int i = skip; i < part.length; i++) if (bytes[at + i - skip] != part[i]) return false;
        return true;
    }

    // Where part first lies wholly inside bytes[from, to), or -1.
    private static int indexOf(byte[] bytes, byte[] part, int from, int to) {
        for (int at = from; at <= to - part.length; at++) if (matches(bytes, at, part, 0)) return at;
        return -1;
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
        // In one write: the body written on its own would wait for the head's
        // acknowledgement, and a connection reset meanwhile would drop it.
        byte[] start = head.getBytes(StandardCharsets.US_ASCII);
        byte[] whole = Arrays.copyOf(start, start.length + content.length);
        System.arraycopy(content, 0, whole, start.length, content.length);
        out.write(whole);
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
