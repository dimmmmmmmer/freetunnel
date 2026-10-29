package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import io.nayuki.qrcodegen.QrCode;

import org.junit.After;
import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.ConnectException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

// The receiver a TV opens for Receive from phone: what one request gets (handle(),
// driven through byte streams), the code and address rules, the pages and the QR
// code, and then the whole receiver on real loopback sockets.
public final class LinkReceiverTest {
    private static final String CODE = "K7QM4XHD";
    private static final String HOST = "192.168.1.23:41234";
    private static final String LINK = "tt://?AAECAQ92cG4uZXhhbXBsZS5jb20FBWFsaWNlBgZzZWNyZXQCCzEuMi4zLjQ6NDQz";
    private static final LinkReceiver.Pages PAGES = new LinkReceiver.Pages("en", "Send to the TV", "Paste a link",
            "Or choose a file", "Send", "The TV asks first", "Sent", "Paste or choose one", "Too large", "Wait", "Wrong code", "Stopped");
    private static final String CSP =
            "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'";

    private final List<LinkReceiver> opened = new ArrayList<>();
    // The TV's count of texts it has taken and not answered, which the receivers in a test share.
    private final AtomicInteger waiting = new AtomicInteger();

    @After public void stopReceivers() {
        for (LinkReceiver receiver : opened) receiver.stop();
    }

    // What handle() made of one request, what it answered, and how much of the
    // request it left unread.
    private static final class Exchange {
        final LinkReceiver.Outcome outcome;
        final String response;
        final int unread;
        Exchange(LinkReceiver.Outcome outcome, String response, int unread) {
            this.outcome = outcome; this.response = response; this.unread = unread;
        }
        String status() { return response.isEmpty() ? "" : response.substring(0, response.indexOf("\r\n")); }
        String body() { return response.substring(response.indexOf("\r\n\r\n") + 4); }
    }

    private static Exchange exchange(String request, boolean lastStrike) throws IOException {
        return exchange(request.getBytes(StandardCharsets.UTF_8), lastStrike);
    }
    private static Exchange exchange(byte[] request, boolean lastStrike) throws IOException {
        return exchange(request, lastStrike, new AtomicInteger());
    }
    private static Exchange exchange(byte[] request, boolean lastStrike, AtomicInteger waiting) throws IOException {
        ByteArrayInputStream in = new ByteArrayInputStream(request);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        LinkReceiver.Outcome outcome = LinkReceiver.handle(in, out, CODE, HOST, PAGES, lastStrike, waiting);
        return new Exchange(outcome, new String(out.toByteArray(), StandardCharsets.UTF_8), in.available());
    }
    private static Exchange exchange(String request, AtomicInteger waiting) throws IOException {
        return exchange(request.getBytes(StandardCharsets.UTF_8), false, waiting);
    }
    private static Exchange exchange(String request) throws IOException { return exchange(request, false); }

    private static String get(String path) {
        return "GET " + path + " HTTP/1.1\r\nHost: " + HOST + "\r\nAccept: text/html\r\n\r\n";
    }
    // A form post as a browser sends one.
    private static String post(String path, String host, String headers, String body) {
        return "POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + headers
                + "Content-Length: " + body.getBytes(StandardCharsets.UTF_8).length + "\r\n\r\n" + body;
    }
    private static String post(String path, String body) {
        return post(path, HOST, "Content-Type: application/x-www-form-urlencoded\r\n", body);
    }
    private static String form(String text) throws IOException { return "link=" + URLEncoder.encode(text, "UTF-8"); }

    // The page's own form, multipart as Chrome and Safari send it, with their kind
    // of boundary: the text field with no file name and no type, then the file field
    // with both. With no file chosen, that has an empty name and no content.
    private static final String BOUNDARY = "----WebKitFormBoundary7MA4YWxkTrZu0gW";
    private static byte[] utf8(String text) { return text.getBytes(StandardCharsets.UTF_8); }
    private static byte[] concat(byte[]... parts) {
        ByteArrayOutputStream all = new ByteArrayOutputStream();
        for (byte[] part : parts) all.write(part, 0, part.length);
        return all.toByteArray();
    }
    private static byte[] textField(String name, String text) {
        return utf8("Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + text);
    }
    // The name as the browser writes it, with %22 for a quote and %0D%0A for a line break.
    private static byte[] fileField(String fileName, byte[] content) {
        return concat(utf8("Content-Disposition: form-data; name=\"file\"; filename=\"" + fileName
                + "\"\r\nContent-Type: application/octet-stream\r\n\r\n"), content);
    }
    private static byte[] noFile() { return fileField("", new byte[0]); }
    private static byte[] parts(String boundary, byte[]... parts) {
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        for (byte[] part : parts) {
            byte[] start = utf8("--" + boundary + "\r\n");
            body.write(start, 0, start.length);
            body.write(part, 0, part.length);
            body.write('\r');
            body.write('\n');
        }
        byte[] end = utf8("--" + boundary + "--\r\n");
        body.write(end, 0, end.length);
        return body.toByteArray();
    }
    private static byte[] pageForm(String text, String fileName, byte[] file) {
        return parts(BOUNDARY, textField("link", text), fileField(fileName, file));
    }
    private static byte[] multipartPost(String path, String host, String type, byte[] body) {
        return concat(utf8("POST " + path + " HTTP/1.1\r\nHost: " + host + "\r\nContent-Type: " + type
                + "\r\nContent-Length: " + body.length + "\r\n\r\n"), body);
    }
    private static byte[] multipartPost(String path, byte[] body) {
        return multipartPost(path, HOST, "multipart/form-data; boundary=" + BOUNDARY, body);
    }
    private static final String TOML = "[endpoint]\r\nhostname = \"сервер.example\"\r\npassword = \"a+b&c=d %41\"\r\n";
    private static final byte[] BOM = {(byte) 0xef, (byte) 0xbb, (byte) 0xbf};

    private static void assertRefused(String why, String status, Exchange exchange) {
        assertEquals(why, status, exchange.status());
        assertSame(why, LinkReceiver.Kind.REFUSED, exchange.outcome.kind);
    }
    // Refused as what no browser sends is: the status in plain text, not a page.
    private static void assertRefusedPlainly(String why, String status, Exchange exchange) {
        assertRefused(why, status, exchange);
        assertTrue(why, exchange.response.contains("\r\nContent-Type: text/plain; charset=utf-8\r\n"));
        assertEquals(why, status.substring("HTTP/1.1 ".length()), exchange.body());
    }

    @Test public void aGetShowsTheSameFormWhateverItsPath() throws IOException {
        // So a GET cannot tell a right code from a wrong one, and a browser's own
        // requests, such as for an icon, are never counted as wrong codes.
        for (String path : new String[]{"/K7QM-4XHD", "/favicon.ico", "/WRONGCOD?link=x", "/"}) {
            Exchange exchange = exchange(get(path));
            assertEquals(path, "HTTP/1.1 200 OK", exchange.status());
            assertEquals(path, PAGES.form, exchange.body());
            assertSame(path, LinkReceiver.Kind.SERVED, exchange.outcome.kind);
        }
    }

    @Test public void aPostToTheCodeWithALinkIsReceived() throws IOException {
        Exchange exchange = exchange(post("/K7QM-4XHD", form(LINK)));
        assertEquals("HTTP/1.1 200 OK", exchange.status());
        assertEquals(PAGES.sent, exchange.body());
        assertSame(LinkReceiver.Kind.RECEIVED, exchange.outcome.kind);
        assertEquals(LINK, exchange.outcome.text);
        assertNull("Pasted, it has no file name", exchange.outcome.fileName);
        assertFalse("Read whole, it leaves nothing to drain", exchange.outcome.bodyUnread);
        assertEquals(0, exchange.unread);
    }

    @Test public void theTextRoundTripsAsABrowserEncodesIt() throws IOException {
        // A TOML file with a Russian name, a plus, an ampersand, an equals sign and
        // CRLF line ends, as a textarea sends them; the ends of the text are trimmed.
        String toml = "[endpoint]\r\nhostname = \"сервер.example\"\r\npassword = \"a+b&c=d %41\"\r\n";
        Exchange exchange = exchange(post("/K7QM4XHD", "other=1&" + form("  \n" + toml + "\n ")));
        assertSame(LinkReceiver.Kind.RECEIVED, exchange.outcome.kind);
        assertEquals(toml.trim(), exchange.outcome.text);
        // A browser sends a space in a form as a plus, and the first link counts.
        assertEquals("a b", exchange(post("/K7QM4XHD", "link=a+b&link=c")).outcome.text);
        // A charset parameter is the browser's to add, with or without a space before it.
        for (String type : new String[]{"Application/X-WWW-Form-Urlencoded; charset=UTF-8", "application/x-www-form-urlencoded ;charset=utf-8"})
            assertEquals(type, LINK, exchange(post("/K7QM4XHD", HOST, "Content-Type: " + type + "\r\n", form(LINK))).outcome.text);
    }

    @Test public void theCodeMayBeTypedInAnyCaseWithOrWithoutTheHyphen() throws IOException {
        // A query is not part of the code.
        for (String path : new String[]{"/k7qm-4xhd", "/K7QM4XHD", "/K7qm-4XhD", "/K7QM-4XHD?from=qr"})
            assertSame(path, LinkReceiver.Kind.RECEIVED, exchange(post(path, form(LINK))).outcome.kind);
        // O, I and L for 0, 1 and 1.
        assertEquals("K7QM4XHD", LinkReceiver.normalizeCode("k7qm-4xhd"));
        assertEquals("0111ABCD", LinkReceiver.normalizeCode("oIl1-abcd"));
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        LinkReceiver.Outcome outcome = LinkReceiver.handle(new ByteArrayInputStream(
                post("/o1il-abcd", form(LINK)).getBytes(StandardCharsets.UTF_8)), out, "0111ABCD", HOST, PAGES, false, new AtomicInteger());
        assertSame(LinkReceiver.Kind.RECEIVED, outcome.kind);
    }

    @Test public void aWrongCodeIsAStrikeWithAPageThatSaysSo() throws IOException {
        for (String path : new String[]{"/K7QM-4XHE", "/K7QM-4XH", "/K7QM-4XHDD", "/", "/favicon.ico", "/K7QM-4XHD/"}) {
            Exchange exchange = exchange(post(path, form(LINK)));
            assertEquals(path, "HTTP/1.1 404 Not Found", exchange.status());
            assertEquals(path, PAGES.wrongCode, exchange.body());
            assertSame(path, LinkReceiver.Kind.WRONG_CODE, exchange.outcome.kind);
            assertNull(path, exchange.outcome.text);
            assertEquals("The text is read first, so the browser gets the page", 0, exchange.unread);
        }
        // Whatever the text: a wrong code with no link is still a strike.
        assertSame(LinkReceiver.Kind.WRONG_CODE, exchange(post("/K7QM-4XHE", "link=")).outcome.kind);
        // The last strike says that the TV stopped.
        Exchange last = exchange(post("/K7QM-4XHE", form(LINK)), true);
        assertEquals(PAGES.lockedOut, last.body());
        assertSame(LinkReceiver.Kind.WRONG_CODE, last.outcome.kind);
        // And it takes a wrong code: the last strike's right code is received.
        assertSame(LinkReceiver.Kind.RECEIVED, exchange(post("/K7QM-4XHD", form(LINK)), true).outcome.kind);
    }

    @Test public void theHostMustBeTheTvsAddress() throws IOException {
        // Another name for the TV's address, as a page that rebinds its own name to
        // it would send, is refused, and so is a request without exactly one.
        String body = form(LINK);
        String type = "Content-Type: application/x-www-form-urlencoded\r\n";
        for (String host : new String[]{"evil.example:41234", "192.168.1.23", "192.168.1.23:41235", "192.168.1.23:41234 x"}) {
            assertRefused(host, "HTTP/1.1 400 Bad Request", exchange(post("/K7QM4XHD", host, type, body)));
            assertRefused(host, "HTTP/1.1 400 Bad Request", exchange(get("/").replace(HOST, host)));
        }
        assertRefused("two hosts", "HTTP/1.1 400 Bad Request", exchange(post("/K7QM4XHD", HOST, "Host: " + HOST + "\r\n" + type, body)));
        assertRefused("no host", "HTTP/1.1 400 Bad Request", exchange("GET / HTTP/1.1\r\n\r\n"));
        // Its case and the spaces around it are the header's own.
        assertSame(LinkReceiver.Kind.SERVED, exchange("GET / HTTP/1.1\r\nhOsT:   " + HOST + "  \r\n\r\n").outcome.kind);
    }

    @Test public void aBodyNeedsOneLengthAndNoChunks() throws IOException {
        String head = "POST /K7QM4XHD HTTP/1.1\r\nHost: " + HOST + "\r\nContent-Type: application/x-www-form-urlencoded\r\n";
        String body = form(LINK);
        assertRefused("no length", "HTTP/1.1 411 Length Required", exchange(head + "\r\n" + body));
        assertRefused("chunks", "HTTP/1.1 501 Not Implemented", exchange(head + "Transfer-Encoding: chunked\r\nContent-Length: "
                + body.length() + "\r\n\r\n" + body));
        String length = "Content-Length: " + body.length() + "\r\n";
        assertRefused("two lengths", "HTTP/1.1 400 Bad Request", exchange(head + length + length + "\r\n" + body));
        for (String bad : new String[]{"+5", "-1", "1e3", "", "5 5", "0x10", "1234567x"})
            assertRefused(bad, "HTTP/1.1 400 Bad Request", exchange(head + "Content-Length: " + bad + "\r\n\r\n" + body));
        // Over the limit, the text is not even read, and serve() is told to read the
        // rest before it closes. The page says so, with the form.
        String large = head + "Content-Length: " + (LinkReceiver.MAX_BODY_BYTES + 1) + "\r\n\r\n";
        Exchange tooLarge = exchange(large + "x".repeat(LinkReceiver.MAX_BODY_BYTES + 1));
        assertRefused("too large", "HTTP/1.1 413 Content Too Large", tooLarge);
        assertEquals(PAGES.tooLarge, tooLarge.body());
        assertTrue(tooLarge.outcome.bodyUnread);
        assertEquals(LinkReceiver.MAX_BODY_BYTES + 1, tooLarge.unread);
        // So is a length of 8 digits or more, as a photo or a video can have.
        for (String huge : new String[]{"12345678", "10000000", "99999999999999999999"}) {
            Exchange photo = exchange(head + "Content-Length: " + huge + "\r\n\r\n" + body);
            assertRefused(huge, "HTTP/1.1 413 Content Too Large", photo);
            assertEquals(huge, PAGES.tooLarge, photo.body());
            assertTrue(huge, photo.outcome.bodyUnread);
            assertEquals(huge, body.length(), photo.unread);
        }
        // Answered before the code is checked, it is no strike with a wrong code
        // either, not even the last: a mistyped address and a photo picked by mistake
        // three times must not stop the TV.
        Exchange mistyped = exchange(head.replace("/K7QM4XHD", "/K7QM-4XHE") + "Content-Length: 12345678\r\n\r\n" + body, true);
        assertRefused("too large to a wrong code", "HTTP/1.1 413 Content Too Large", mistyped);
        assertEquals(PAGES.tooLarge, mistyped.body());
        assertTrue(mistyped.outcome.bodyUnread);
        // At the limit it is.
        String padded = form(LINK) + "&pad=" + "x".repeat(LinkReceiver.MAX_BODY_BYTES - form(LINK).length() - 5);
        assertEquals(LinkReceiver.MAX_BODY_BYTES, padded.length());
        assertSame(LinkReceiver.Kind.RECEIVED, exchange(post("/K7QM4XHD", padded)).outcome.kind);
        // A client that closes before the end of what it announced gets nothing.
        Exchange cut = exchange(head + "Content-Length: 100\r\n\r\nlink=x");
        assertSame(LinkReceiver.Kind.REFUSED, cut.outcome.kind);
        assertEquals("", cut.response);
    }

    @Test public void onlyAFormPostCarriesTheText() throws IOException {
        String body = form(LINK);
        for (String type : new String[]{"text/plain", "multipart/mixed; boundary=x", "application/json",
                "application/x-www-form-urlencodedx", ""}) {
            String headers = type.isEmpty() ? "" : "Content-Type: " + type + "\r\n";
            assertRefused(type, "HTTP/1.1 415 Unsupported Media Type", exchange(post("/K7QM4XHD", HOST, headers, body)));
        }
        String two = "Content-Type: application/x-www-form-urlencoded\r\nContent-Type: application/x-www-form-urlencoded\r\n";
        assertRefused("two types", "HTTP/1.1 415 Unsupported Media Type", exchange(post("/K7QM4XHD", HOST, two, body)));
        // Other methods get nothing, not even the form, and a query is no way in.
        for (String method : new String[]{"PUT", "HEAD", "OPTIONS", "DELETE", "GETX"}) {
            Exchange exchange = exchange(post("/K7QM4XHD", body).replaceFirst("POST", method));
            assertRefused(method, "HTTP/1.1 405 Method Not Allowed", exchange);
            assertTrue(exchange.response.contains("\r\nAllow: GET, POST\r\n"));
        }
        assertSame(LinkReceiver.Kind.SERVED, exchange(get("/K7QM4XHD?link=" + LINK)).outcome.kind);
    }

    @Test public void headersOver8KiBAreRefused() throws IOException {
        String start = "GET / HTTP/1.1\r\nHost: " + HOST + "\r\nX-Pad: ";
        int pad = LinkReceiver.MAX_HEADER_BYTES - start.length() - 4;
        assertSame("At the limit", LinkReceiver.Kind.SERVED, exchange(start + "x".repeat(pad) + "\r\n\r\n").outcome.kind);
        assertRefused("Over it", "HTTP/1.1 431 Request Header Fields Too Large",
                exchange(start + "x".repeat(pad + 1) + "\r\n\r\n"));
        // A client that closes before the blank line gets nothing.
        Exchange cut = exchange("GET / HTTP/1.1\r\nHost: " + HOST + "\r\n");
        assertSame(LinkReceiver.Kind.REFUSED, cut.outcome.kind);
        assertEquals("", cut.response);
    }

    @Test public void aTlsHandshakeIsDroppedAtItsFirstByte() throws IOException {
        // A browser that tries https first sends a TLS ClientHello, which starts with
        // 0x16. Dropped at once, it falls back to http instead of waiting.
        byte[] hello = {0x16, 0x03, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, (byte) 0xfc, 0x03, 0x03};
        Exchange tls = exchange(hello, false);
        assertSame(LinkReceiver.Kind.REFUSED, tls.outcome.kind);
        assertEquals("", tls.response);
        assertEquals("Nothing after the first byte is read", hello.length - 1, tls.unread);
        for (String start : new String[]{"\r\nGET / HTTP/1.1", " GET / HTTP/1.1", "get / HTTP/1.1"}) {
            Exchange other = exchange(start + "\r\nHost: " + HOST + "\r\n\r\n");
            assertEquals(start, "", other.response);
            assertSame(start, LinkReceiver.Kind.REFUSED, other.outcome.kind);
        }
    }

    @Test public void aTlsHandshakeIsDroppedWithoutWaiting() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 5_000, 10_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket tls = connect(port)) {
            tls.setSoTimeout(5_000);
            long started = System.nanoTime();
            tls.getOutputStream().write(new byte[]{0x16, 0x03, 0x01, 0x02, 0x00});
            int read;
            try { read = tls.getInputStream().read(); }
            catch (java.net.SocketException reset) { read = -1; }
            assertEquals(-1, read);
            assertTrue("Dropped at once, not at its read timeout", System.nanoTime() - started < 2_000_000_000L);
        }
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
    }

    @Test public void aBrokenOrEmptyTextIsRefusedWithoutAStrike() throws IOException {
        for (String body : new String[]{"link=%zz", "link=%", "link=%4", "link=", "link", "link=%20%0A%09", "other=x", "",
                "%zz=1&link=x"})
            assertRefused(body, "HTTP/1.1 400 Bad Request", exchange(post("/K7QM4XHD", body)));
        // Bytes a browser never puts in a form: not ASCII, or a control character.
        for (byte bad : new byte[]{(byte) 0xd0, 0x00, 0x0a, 0x7f}) {
            byte[] start = post("/K7QM4XHD", "link=ab").getBytes(StandardCharsets.UTF_8);
            start[start.length - 1] = bad;
            assertRefused("byte " + bad, "HTTP/1.1 400 Bad Request", exchange(start, false));
        }
    }

    @Test public void aFileIsReceivedWithItsName() throws IOException {
        // A TOML file with a Russian server name and CRLF line ends, the text field left empty.
        Exchange exchange = exchange(multipartPost("/K7QM-4XHD", pageForm("", "home.toml", utf8(TOML))), false);
        assertEquals("HTTP/1.1 200 OK", exchange.status());
        assertEquals(PAGES.sent, exchange.body());
        assertSame(LinkReceiver.Kind.RECEIVED, exchange.outcome.kind);
        assertEquals(TOML.trim(), exchange.outcome.text);
        assertEquals("home.toml", exchange.outcome.fileName);
        assertEquals(0, exchange.unread);
        // A text file with a link, a byte order mark and a CRLF after it, as Notepad saves one.
        LinkReceiver.Outcome link = exchange(multipartPost("/K7QM4XHD",
                pageForm(" \r\n", "link.txt", concat(BOM, utf8(LINK + "\r\n")))), false).outcome;
        assertSame(LinkReceiver.Kind.RECEIVED, link.kind);
        assertEquals(LINK, link.text);
        assertEquals("link.txt", link.fileName);
        // Firefox's boundary, and the type it gives a file.
        String gecko = "----geckoformboundary1f6c0a9e2b3d4c5e8a7b6c5d4e3f2a1b";
        byte[] firefox = parts(gecko, textField("link", ""), concat(utf8("Content-Disposition: form-data; name=\"file\"; "
                + "filename=\"home.toml\"\r\nContent-Type: application/toml\r\n\r\n"), utf8(TOML)));
        LinkReceiver.Outcome fromFirefox = exchange(multipartPost("/K7QM4XHD", HOST, "multipart/form-data; boundary=" + gecko,
                firefox), false).outcome;
        assertEquals(TOML.trim(), fromFirefox.text);
        assertEquals("home.toml", fromFirefox.fileName);
    }

    @Test public void aPastedTextStillWorksInTheMultipartForm() throws IOException {
        for (String text : new String[]{LINK, " " + LINK + "\r\n", TOML}) {
            Exchange exchange = exchange(multipartPost("/K7QM4XHD", pageForm(text, "", new byte[0])), false);
            assertSame(LinkReceiver.Kind.RECEIVED, exchange.outcome.kind);
            assertEquals(text.trim(), exchange.outcome.text);
            assertNull(exchange.outcome.fileName);
        }
        // In any order, with other fields skipped, a name as a token, any case where a
        // header's is free, other headers, and without the last line end.
        byte[] plain = pageForm(LINK, "", new byte[0]);
        byte[][] forms = {
            parts(BOUNDARY, noFile(), textField("other", "x"), textField("link", LINK)),
            parts(BOUNDARY, utf8("content-DISPOSITION: Form-Data;NAME=link\r\nX-Other: 1\r\n\r\n" + LINK), noFile()),
            Arrays.copyOf(plain, plain.length - 2)};
        for (byte[] form : forms)
            assertEquals(new String(form, StandardCharsets.UTF_8), LINK, exchange(multipartPost("/K7QM4XHD", form), false).outcome.text);
        // A file of nothing but white space counts as none, as an empty text does.
        assertEquals(LINK, exchange(multipartPost("/K7QM4XHD", pageForm(LINK, "blank.txt", utf8(" \r\n"))), false).outcome.text);
    }

    @Test public void nothingOrBothGetsTheFormAgainWithoutAStrike() throws IOException {
        // Neither, both, or a file that is not UTF-8 text: the phone can fix any of them.
        byte[] png = {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0x0d};
        byte[] utf16 = {(byte) 0xff, (byte) 0xfe, '[', 0, 'e', 0};
        byte[][] forms = {pageForm("", "", new byte[0]), pageForm(" \r\n\t", "", new byte[0]),
                pageForm(LINK, "home.toml", utf8(TOML)), pageForm("", "photo.png", png), pageForm(LINK, "photo.png", png),
                pageForm("", "home.toml", utf16), parts(BOUNDARY, textField("Link", LINK), noFile()),
                parts(BOUNDARY, textField("other", LINK)), utf8("--" + BOUNDARY + "--\r\n")};
        for (byte[] form : forms) {
            Exchange exchange = exchange(multipartPost("/K7QM4XHD", form), false);
            String what = new String(form, StandardCharsets.UTF_8);
            assertEquals(what, "HTTP/1.1 400 Bad Request", exchange.status());
            assertEquals(what, PAGES.retry, exchange.body());
            assertSame(what, LinkReceiver.Kind.REFUSED, exchange.outcome.kind);
            assertEquals(what, 0, exchange.unread);
        }
    }

    @Test public void aMultipartFormMustBeAsABrowserWritesIt() throws IOException {
        byte[] good = pageForm(LINK, "", new byte[0]);
        String d = "--" + BOUNDARY;
        String link = "Content-Disposition: form-data; name=\"link\"";
        byte[] badText = pageForm("ab", "", new byte[0]);
        badText[new String(badText, StandardCharsets.ISO_8859_1).indexOf("ab")] = (byte) 0xff;
        byte[] badName = pageForm("", "ab.toml", utf8(TOML));
        badName[new String(badName, StandardCharsets.ISO_8859_1).indexOf("ab.toml")] = (byte) 0xc0;
        byte[][] bodies = {
            // A preamble, an epilogue, no last boundary, padding after a boundary, a
            // boundary in the content, bare LFs, a part with no headers or no blank line.
            concat(utf8("preamble\r\n"), good), concat(good, utf8("epilogue")), Arrays.copyOf(good, good.length - 10),
            utf8(d + " \r\n" + link + "\r\n\r\n" + LINK + "\r\n" + d + "--\r\n"),
            pageForm(LINK + "\r\n" + d + "x", "", new byte[0]),
            utf8(d + "\n" + link + "\n\n" + LINK + "\n" + d + "--\n"),
            utf8(d + "\r\n\r\n" + LINK + "\r\n" + d + "--\r\n"),
            utf8(d + "\r\n" + link + "\r\n" + d + "--\r\n"),
            // Not exactly one Content-Disposition: form-data with one name, at most
            // one file name, link without one, file with one, each field once.
            parts(BOUNDARY, utf8("Content-Type: text/plain\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8(link + "\r\n" + link + "\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: attachment; name=\"link\"\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: form-data\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: form-data; filename=\"a.txt\"\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: form-data; name=\"link\"; name=\"file\"\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: form-data; name=\"file\"; filename=\"a\"; filename=\"b\"\r\n\r\n" + LINK)),
            parts(BOUNDARY, textField("link", LINK), textField("link", ""), noFile()),
            parts(BOUNDARY, textField("link", LINK), noFile(), noFile()),
            parts(BOUNDARY, utf8(link + "; filename=\"a.txt\"\r\n\r\n" + LINK), noFile()),
            parts(BOUNDARY, textField("link", ""), textField("file", TOML)),
            // Header syntax no browser writes.
            parts(BOUNDARY, utf8("Content-Disposition: form-data; name=\"link\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: form-data; name=li nk\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8(link + ";\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content-Disposition: form-data;\r\n name=\"link\"\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8("Content Disposition: form-data; name=\"link\"\r\n\r\n" + LINK)),
            // A NUL, or a CR or LF on its own, which browsers write as %0D and %0A.
            parts(BOUNDARY, utf8(link + "\r\nX: a\0b\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8(link + "\r\nX: a\rb\r\n\r\n" + LINK)),
            parts(BOUNDARY, utf8(link + "\r\nX: a\nb\r\n\r\n" + LINK)),
            parts(BOUNDARY, textField("link", ""), fileField("a\nb.toml", utf8(TOML))),
            // A backslash escapes nothing, so old Firefox's \" for a quote breaks the name.
            pageForm("", "a\\\"b.toml", utf8(TOML)),
            // A text or a file name that is not UTF-8.
            badText, badName};
        // Not the form again: what no browser writes is refused as any such request is.
        for (byte[] body : bodies)
            assertRefusedPlainly(new String(body, StandardCharsets.UTF_8), "HTTP/1.1 400 Bad Request",
                    exchange(multipartPost("/K7QM4XHD", body), false));
        // A boundary is matched as it was given, case and all.
        assertRefusedPlainly("case", "HTTP/1.1 400 Bad Request", exchange(multipartPost("/K7QM4XHD", HOST,
                "multipart/form-data; boundary=" + BOUNDARY.toLowerCase(java.util.Locale.ROOT), good), false));
    }

    @Test public void aMultipartTypeNeedsOneBoundaryRfc2046Allows() throws IOException {
        byte[] body = pageForm(LINK, "", new byte[0]);
        for (String type : new String[]{"multipart/form-data", "multipart/form-data;", "multipart/form-data; boundary=",
                "multipart/form-data; boundary=\"\"", "multipart/form-data; boundary=" + "a".repeat(71),
                "multipart/form-data; boundary=x; boundary=x", "multipart/form-data; boundary=a/b",
                "multipart/form-data; boundary=\"abc \"", "multipart/form-data; boundary=\"a\\b\"",
                "multipart/form-data; boundary=x;", "multipart/form-data boundary=x", "multipart/form-data; boundary =x",
                "multipart/form-datax; boundary=x", "multipart/mixed; boundary=" + BOUNDARY}) {
            Exchange exchange = exchange(multipartPost("/K7QM4XHD", HOST, type, body), false);
            assertRefusedPlainly(type, "HTTP/1.1 415 Unsupported Media Type", exchange);
            assertEquals("Refused before the text is read", body.length, exchange.unread);
        }
        // Any case and spacing where they are free, other parameters, quoted or not,
        // and every character RFC 2046 allows in one, up to 70. The last is old Firefox's.
        for (String boundary : new String[]{"a".repeat(70), "\"gc0p4Jq0M2Yt08j34c0p (x)\"", "\"'()+_,-./:=?\"", "x",
                "---------------------------9051914041544843365972754266"}) {
            String bare = boundary.startsWith("\"") ? boundary.substring(1, boundary.length() - 1) : boundary;
            String type = "Multipart/Form-Data ;\tcharset=utf-8; BOUNDARY=" + boundary;
            Exchange exchange = exchange(multipartPost("/K7QM4XHD", HOST, type, parts(bare, textField("link", LINK), noFile())), false);
            assertSame(type, LinkReceiver.Kind.RECEIVED, exchange.outcome.kind);
        }
    }

    @Test public void aFileNameIsKeptAsTheFileHasIt() throws IOException {
        // It names the configuration, as a file picked on the TV does. The confirmation
        // shows it as ShownText shows any name, without what only it leaves out.
        String[][] names = {
            // As browsers write a quote and line breaks, and a path, which none sends now.
            {"My %22home%22 VPN.toml", "My \"home\" VPN.toml"}, {"Мой сервер.toml", "Мой сервер.toml"},
            {"a%0D%0Ab.toml", "ab.toml"}, {"dir/home.toml", "home.toml"},
            // A backslash is sent as it is, as part of the name: macOS, iOS and Linux
            // allow one, and Chrome, Safari and Firefox send no path.
            {"Home\\Office.toml", "Home\\Office.toml"}, {"C:\\Users\\me\\home.toml", "C:\\Users\\me\\home.toml"},
            {"x\\", "x\\"}, {"\\", "\\"},
            // Nothing that breaks the line, and trimmed.
            {"a\u2028b\u2029c\u0085d\u0007", "abcd"}, {" home.toml ", "home.toml"},
            // What only the confirmation leaves out is kept: what reverses text or joins
            // characters, other spaces, and every mark a script stacks on a character.
            // Tibetan and Hebrew with cantillation stack more than two.
            {"\u202Elmot.exe", "\u202Elmot.exe"}, {"\u2066x\u2069.toml", "\u2066x\u2069.toml"},
            {"\uD83D\uDC69\u200D\uD83D\uDCBB.toml", "\uD83D\uDC69\u200D\uD83D\uDCBB.toml"},
            {"My\u3000VPN\u00A0.toml", "My\u3000VPN\u00A0.toml"},
            {"\u0F66\u0F92\u0FB2\u0F74\u0F56.toml", "\u0F66\u0F92\u0FB2\u0F74\u0F56.toml"},
            {"\u05D1\u05BC\u05B0\u0591\u05E8.toml", "\u05D1\u05BC\u05B0\u0591\u05E8.toml"},
            {"a" + "\u0336".repeat(20) + "b", "a" + "\u0336".repeat(20) + "b"},
            // 255 characters at most, more than a file system lets a name have, an
            // emoji counted once.
            {"x".repeat(300), "x".repeat(255)}, {"\uD83D\uDD12".repeat(300), "\uD83D\uDD12".repeat(255)},
            {"x".repeat(254) + " y", "x".repeat(254)}};
        for (String[] name : names) assertEquals(name[0], name[1], LinkReceiver.fileName(name[0]));
        assertEquals(255, LinkReceiver.MAX_FILE_NAME);
        // Null when the confirmation would show nothing of it.
        for (String nothing : new String[]{"", " ", "dir/", "\\/ ", "\u200F\u061C\u00AD", "%0D%0A", "\u3000\u00A0",
                "\u115F\u1160\u3164\uFFA0", "\u2800\u2800", "\u0301\u0302", " \uFE0F", "\u20DD"})
            assertNull(nothing, LinkReceiver.fileName(nothing));
        // Browsers send other control characters in a file name as they are.
        for (String sent : new String[]{"home\u0001.toml", "\u001bhome.toml", "home\u007f.toml", "home\t.toml"})
            assertEquals(sent, "home.toml", exchange(multipartPost("/K7QM4XHD", pageForm("", sent, utf8(TOML))), false).outcome.fileName);
        // Through the form, as Chrome, Safari and Firefox send "Home\Office q"u.toml",
        // and a file with no name is received without one.
        assertEquals("Home\\Office q\"u.toml", exchange(multipartPost("/K7QM4XHD",
                pageForm("", "Home\\Office q%22u.toml", utf8(TOML))), false).outcome.fileName);
        assertEquals("\u202Elmot.exe", exchange(multipartPost("/K7QM4XHD", pageForm("", "\u202Elmot.exe", utf8(TOML))), false)
                .outcome.fileName);
        for (String none : new String[]{"", "\u202E"}) {
            LinkReceiver.Outcome nameless = exchange(multipartPost("/K7QM4XHD", pageForm("", none, utf8(TOML))), false).outcome;
            assertSame(none, LinkReceiver.Kind.RECEIVED, nameless.kind);
            assertNull(none, nameless.fileName);
        }
    }

    @Test public void aMultipartFormHasTheSameSizeLimit() throws IOException {
        // The limit is on the whole body, boundaries and headers included.
        int room = LinkReceiver.MAX_BODY_BYTES - pageForm("", "big.toml", new byte[0]).length;
        byte[] content = new byte[room + 1];
        Arrays.fill(content, (byte) '#');
        byte[] full = pageForm("", "big.toml", Arrays.copyOf(content, room));
        assertEquals(LinkReceiver.MAX_BODY_BYTES, full.length);
        assertSame(LinkReceiver.Kind.RECEIVED, exchange(multipartPost("/K7QM4XHD", full), false).outcome.kind);
        Exchange tooLarge = exchange(multipartPost("/K7QM4XHD", pageForm("", "big.toml", content)), false);
        assertRefused("too large", "HTTP/1.1 413 Content Too Large", tooLarge);
        assertEquals(PAGES.tooLarge, tooLarge.body());
        assertEquals(LinkReceiver.MAX_BODY_BYTES + 1, tooLarge.unread);
    }

    @Test public void aWrongCodeIsAStrikeWhateverTheForm() throws IOException {
        // The code is checked before the form is read, as for the urlencoded one.
        for (byte[] body : new byte[][]{pageForm("", "home.toml", utf8(TOML)), utf8("not a form")}) {
            Exchange exchange = exchange(multipartPost("/K7QM-4XHE", body), false);
            assertEquals("HTTP/1.1 404 Not Found", exchange.status());
            assertEquals(PAGES.wrongCode, exchange.body());
            assertSame(LinkReceiver.Kind.WRONG_CODE, exchange.outcome.kind);
            assertNull(exchange.outcome.fileName);
            assertEquals(PAGES.lockedOut, exchange(multipartPost("/K7QM-4XHE", body), true).body());
        }
    }

    @Test public void requestsNoBrowserSendsAreRefused() throws IOException {
        String host = "\r\nHost: " + HOST;
        for (String request : new String[]{
                "GET / HTTP/2.0" + host, "GET / HTTP/1.1 x" + host, "GET  / HTTP/1.1" + host, "GET /" + host,
                "GET http://" + HOST + "/ HTTP/1.1" + host, "GET * HTTP/1.1" + host, "GET / http/1.1" + host,
                "GET / HTTP/1.1" + host + "\r\n Folded: x", "GET / HTTP/1.1" + host + "\r\nNo colon",
                "GET / HTTP/1.1" + host + "\r\nBad Name: x", "GET / HTTP/1.1" + host + "\r\n: x",
                "GET / HTTP/1.1" + host + "\r\nX: a\nb", "GET / HTTP/1.1" + host + "\r\nX: a\rb",
                "GET / HTTP/1.1" + host + "\r\nX: \u0001", "GET / HTTP/1.1" + host + "\r\nX: \u007f",
                "GET /é HTTP/1.1" + host, "GET / HTTP/1.1" + host + "\r\n\r"})
            assertRefused(request, "HTTP/1.1 400 Bad Request", exchange(request + "\r\n\r\n"));
        assertSame("HTTP/1.0 is a browser's too", LinkReceiver.Kind.SERVED,
                exchange("GET / HTTP/1.0" + host + "\r\n\r\n").outcome.kind);
        assertSame("A tab in a value is allowed", LinkReceiver.Kind.SERVED,
                exchange("GET / HTTP/1.1" + host + "\r\nX: a\tb\r\n\r\n").outcome.kind);
    }

    @Test public void everyAnswerIsLockedDown() throws IOException {
        String type = "Content-Type: application/x-www-form-urlencoded\r\n";
        String[] requests = {get("/"), post("/K7QM4XHD", form(LINK)), post("/K7QM4XHE", form(LINK)),
                post("/K7QM4XHD", "link="), post("/K7QM4XHD", HOST, type, "").replace("Content-Length: 0\r\n", ""),
                post("/K7QM4XHD", HOST, type + "Transfer-Encoding: chunked\r\n", "x"),
                post("/K7QM4XHD", HOST, "", "x"), "PUT / HTTP/1.1\r\nHost: " + HOST + "\r\n\r\n",
                "GET / HTTP/1.1\r\nHost: " + HOST + "\r\nX: " + "x".repeat(LinkReceiver.MAX_HEADER_BYTES) + "\r\n\r\n",
                new String(multipartPost("/K7QM4XHD", pageForm("", "home.toml", utf8(TOML))), StandardCharsets.UTF_8),
                new String(multipartPost("/K7QM4XHD", pageForm("", "", new byte[0])), StandardCharsets.UTF_8),
                post("/K7QM4XHD", HOST, type, "x".repeat(LinkReceiver.MAX_BODY_BYTES + 1)),
                // The right code with no room.
                post("/K7QM4XHD", form(LINK))};
        List<String> statuses = new ArrayList<>();
        List<String> types = new ArrayList<>();
        for (String request : requests) {
            // The last one finds MAX_WAITING texts waiting, the others none.
            Exchange exchange = exchange(request, new AtomicInteger(request == requests[requests.length - 1] ? LinkReceiver.MAX_WAITING : 0));
            statuses.add(exchange.status().substring(9, 12));
            List<String> headers = Arrays.asList(exchange.response.substring(0, exchange.response.indexOf("\r\n\r\n")).split("\r\n"));
            for (String expected : new String[]{"Connection: close", "Cache-Control: no-store", "Referrer-Policy: no-referrer",
                    "X-Content-Type-Options: nosniff", "X-Frame-Options: DENY", "Content-Security-Policy: " + CSP,
                    "Content-Length: " + exchange.body().getBytes(StandardCharsets.UTF_8).length})
                assertTrue(exchange.status() + " lacks " + expected, headers.contains(expected));
            types.add(headers.get(1));
        }
        assertEquals(Arrays.asList("200", "200", "404", "400", "411", "501", "415", "405", "431", "200", "400", "413", "503"), statuses);
        // The pages are HTML, which nosniff makes the browser take at its word; the refusals are plain text.
        String html = "Content-Type: text/html; charset=utf-8";
        String plain = "Content-Type: text/plain; charset=utf-8";
        assertEquals(Arrays.asList(html, html, html, plain, plain, plain, plain, plain, plain, html, html, html, html), types);
    }

    @Test public void thePagesLoadNothingAndEscapeTheirText() {
        String hostile = "<script>alert(\"x\")</script> & 'y'";
        LinkReceiver.Pages pages = new LinkReceiver.Pages("en\"><script>", hostile, hostile, hostile, hostile, hostile, hostile,
                hostile, hostile, hostile, hostile, hostile);
        String escaped = "&lt;script&gt;alert(&quot;x&quot;)&lt;/script&gt; &amp; &#39;y&#39;";
        for (String page : new String[]{pages.form, pages.sent, pages.retry, pages.tooLarge, pages.busy, pages.wrongCode, pages.lockedOut}) {
            assertFalse(page, page.contains("<script"));
            assertTrue(page, page.contains(escaped));
            assertTrue(page, page.startsWith("<!doctype html><html lang=\"en&quot;&gt;&lt;script&gt;\">"));
            // Nothing loaded from anywhere, and no script: the page is its own style, which the CSP allows
            // and nothing else.
            for (String outside : new String[]{"src=", "href=", "http:", "https:", "url(", "@import", "action=", "<script", "<link",
                    "<img", "<iframe", "onload", "onclick", "javascript:", "data:"})
                assertFalse(outside + " in " + page, page.toLowerCase(java.util.Locale.ROOT).contains(outside));
            assertEquals(page, 1, page.split("<style>", -1).length - 1);
        }
        // It posts back to its own address, which carries the code, a pasted text or a
        // file, so neither field is required.
        assertTrue(PAGES.form.contains("<form method=\"post\" enctype=\"multipart/form-data\" accept-charset=\"utf-8\">"));
        assertTrue(PAGES.form.contains("<textarea id=\"link\" name=\"link\" autocomplete=\"off\""));
        assertTrue(PAGES.form.contains("<label for=\"file\">Or choose a file</label><input type=\"file\" id=\"file\" name=\"file\">"));
        assertFalse(PAGES.form.contains("required"));
        // Any file: a browser may not know a .toml file's type and grey it out.
        assertFalse(PAGES.form.contains("accept="));
        // The phone sends the next one from the page that says the last was sent, and from the
        // pages that say what to fix or wait for: each is the form again, with its note, under its own notice.
        String form = PAGES.form.substring(PAGES.form.indexOf("<form"), PAGES.form.indexOf("</main>"));
        assertTrue(form.endsWith("<p class=\"note\">The TV asks first</p>"));
        assertTrue(PAGES.sent.contains("<p class=\"notice ok\" role=\"status\">Sent</p>" + form));
        assertTrue(PAGES.retry.contains("<p class=\"notice warn\" role=\"alert\">Paste or choose one</p>" + form));
        assertTrue(PAGES.tooLarge.contains("<p class=\"notice warn\" role=\"alert\">Too large</p>" + form));
        assertTrue(PAGES.busy.contains("<p class=\"notice warn\" role=\"alert\">Wait</p>" + form));
        // A wrong code and the stop after the last one are a notice and no form, which would post to the same wrong address.
        for (String page : new String[]{PAGES.wrongCode, PAGES.lockedOut}) {
            assertTrue(page, page.contains("<p class=\"notice warn\" role=\"alert\">"));
            assertFalse(page, page.contains("<form"));
        }
        assertFalse(PAGES.form.contains("class=\"notice"));
        assertTrue(PAGES.form.contains("<h1>Send to the TV</h1>"));
        assertEquals("a&amp;b", LinkReceiver.escape("a&b"));
    }

    @Test public void thePagesAreInTheAppsDesign() {
        String page = PAGES.form;
        String css = page.substring(page.indexOf("<style>") + 7, page.indexOf("</style>"));
        // Light on :root, the desktop's dark tokens when the phone is in dark mode, both from Theme.java,
        // which a test holds to qml/Main.qml.
        assertTrue(css.startsWith(":root{color-scheme:light dark;--bg:#ececec;"));
        int dark = css.indexOf("@media (prefers-color-scheme:dark){:root{");
        assertTrue(dark > 0);
        String light = css.substring(0, dark);
        String night = css.substring(dark, css.indexOf("}}", dark));
        for (Theme[] pair : new Theme[][]{{Theme.LIGHT}, {Theme.DARK}}) {
            Theme t = pair[0];
            String scope = t.dark ? night : light;
            String[][] tokens = {{"--bg", hex(t.bg)}, {"--surface", hex(t.surface)}, {"--surface-hover", hex(t.surfaceHover)},
                    {"--input-bg", hex(t.inputBg)}, {"--input-border", hex(t.inputBorder)}, {"--text", hex(t.text)},
                    {"--dim", hex(t.textDim)}, {"--faint", hex(t.textFaint)}, {"--accent", hex(t.accent)},
                    {"--accent-hover", hex(t.primaryHover)}, {"--accent-text", hex(t.accentText)},
                    {"--success", hex(t.success)}, {"--warn", hex(t.warn)}};
            for (String[] token : tokens) assertTrue(token[0] + " " + token[1], scope.contains(token[0] + ":" + token[1] + ";") || scope.contains(token[0] + ":" + token[1] + "}"));
        }
        assertTrue(light.contains("--info:rgba(79,79,79,0.12)"));
        assertTrue(night.contains("--info:rgba(176,176,176,0.16)"));
        // The desktop's neutral greys: nothing else is a colour in the rules.
        assertFalse("A colour is a token", css.substring(dark + night.length()).matches("(?s).*#[0-9a-f]{3,6}.*"));
        // Type: the system's, the title 15 at weight 500 as every card's is, labels 13 in textDim, set as tight as a Qt Text is.
        assertTrue(css.contains("font:16px/1.25 system-ui,-apple-system,Segoe UI,Roboto,sans-serif"));
        assertTrue(css.contains("h1{margin:0 0 14px;font-size:15px;font-weight:500;line-height:1.2}"));
        assertTrue(css.contains("label{display:block;margin:0 0 4px;font-size:13px;line-height:1.2;color:var(--dim)}"));
        // The page's margins are the pages' 18, and a tap leaves no highlight, which the desktop's controls have none of either.
        assertTrue(css.contains("main{max-width:32em;margin:0 auto;padding:18px 18px 32px}"));
        assertTrue(css.contains("*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}"));
        // The text box is the desktop's certificate box but for its size: inputBg, a 1 px inputBorder, radius 8, 150 high, inset 8 on every
        // side (its Flickable's margins) with 10 under it (the form's spacing between a field and the next label), no grip to resize it
        // by, and an accent border, no outline, where it has the focus. Its text is monospace 16, where the desktop's is 12, so that an
        // iPhone does not zoom in on it (Safari does on any field under 16 px). The file field under it keeps its 16: 10 and the button row's 6.
        assertTrue(css.contains("textarea{display:block;width:100%;min-height:150px;margin:0 0 10px;padding:8px;"
                + "border:1px solid var(--input-border);border-radius:8px;background:var(--input-bg);color:var(--text);"
                + "font:16px/1.4 ui-monospace,Menlo,Consolas,monospace;resize:none;"));
        assertTrue(css.contains("outline:none"));
        assertTrue(css.contains("textarea:focus{border-color:var(--accent)}"));
        // The file field, 14 px in the page's font and in textDim, with the browser's button as the neutral one: surface, radius 8, 32 high
        // and 13 padded as the desktop's buttons are, in a rule of its own for each of its two names.
        assertTrue(css.contains("input[type=file]{display:block;width:100%;margin:0 0 16px;font:14px system-ui,-apple-system,Segoe UI,Roboto,sans-serif;color:var(--dim)}"));
        for (String name : new String[]{"::file-selector-button", "::-webkit-file-upload-button"})
            assertTrue(name, css.contains("input[type=file]" + name + "{height:32px;min-width:76px;margin:0 12px 0 0;padding:0 13px;border:0;"
                    + "border-radius:8px;background:var(--surface);color:var(--text);font:14px "));
        assertFalse("One rule for each name: a browser drops a rule it half knows", css.contains("::file-selector-button,"));
        // The button: the desktop's primary one, accent, accentText, radius 8, 32 high, 14 in the normal weight, as wide as its label
        // and 88 at least, at the right of its row, and no border.
        assertTrue(css.contains("button{display:block;margin-left:auto;min-width:88px;height:32px;padding:0 13px;border:0;border-radius:8px;"
                + "background:var(--accent);color:var(--accent-text);font:400 14px system-ui"));
        assertFalse("Not the width of the page", css.contains("button{display:block;width:100%"));
        // The desktop has no focus ring: where the focus is, the button takes its hover look and the file field's button its own,
        // each in a rule of its own, and no rule draws an outline but to take the browser's away.
        assertTrue(css.contains("button:hover{background:var(--accent-hover)}"));
        assertTrue(css.contains("button:focus-visible{outline:none;background:var(--accent-hover)}"));
        assertTrue(css.contains("input[type=file]:focus-visible{outline:none}"));
        for (String name : new String[]{"::file-selector-button", "::-webkit-file-upload-button"})
            assertTrue(name, css.contains("input[type=file]:focus-visible" + name + "{background:var(--surface-hover)}"));
        assertFalse("No ring", css.matches("(?s).*outline:\\s*[0-9].*"));
        assertFalse("No ring", css.contains("outline-offset"));
        // Notices on infoBg, radius 8: warn and success. Notes 13 in textFaint.
        assertTrue(css.contains(".notice{margin:0 0 16px;padding:8px 10px;border-radius:8px;background:var(--info);font-size:12px;line-height:1.2}"));
        assertTrue(css.contains(".ok{color:var(--success)}"));
        assertTrue(css.contains(".warn{color:var(--warn)}"));
        assertTrue(css.contains(".note{margin:14px 0 0;font-size:13px;line-height:1.25;color:var(--faint)}"));
        // And the browser is told both themes are the page's.
        assertTrue(page.contains("<meta name=\"color-scheme\" content=\"light dark\">"));
    }
    private static String hex(int argb) { return String.format(java.util.Locale.ROOT, "#%06x", argb & 0xFFFFFF); }

    // A SecureRandom that answers 0, 1, 2 ... and records the bounds it was asked for.
    private static final class Counting extends SecureRandom {
        int next;
        final List<Integer> bounds = new ArrayList<>();
        @Override public int nextInt(int bound) { bounds.add(bound); return next++ % bound; }
    }

    @Test public void aCodeIsEightCharactersOfTheAlphabet() {
        Counting random = new Counting();
        assertEquals("01234567", LinkReceiver.newCode(random));
        assertEquals("89ABCDEF", LinkReceiver.newCode(random));
        random.next = 24;
        assertEquals("RSTVWXYZ", LinkReceiver.newCode(random));
        for (int bound : random.bounds) assertEquals(32, bound);
        assertEquals(32, LinkReceiver.ALPHABET.length());
        SecureRandom real = new SecureRandom();
        for (int i = 0; i < 1000; i++)
            assertTrue(LinkReceiver.newCode(real).matches("[0-9A-HJKMNP-TV-Z]{8}"));
    }

    @Test public void onlyAPrivateNetworkIpv4AddressIsPicked() throws IOException {
        assertEquals(address("192.168.1.23"), LinkReceiver.pickAddress(addresses(
                "127.0.0.1", "169.254.1.1", "100.64.0.5", "8.8.8.8", "fe80::1", "fd00::1", "192.168.1.23", "10.0.0.2")));
        for (String lan : new String[]{"10.0.2.15", "10.255.255.255", "172.16.0.1", "172.31.255.1", "192.168.0.1"})
            assertEquals(lan, address(lan), LinkReceiver.pickAddress(addresses(lan)));
        for (String other : new String[]{"172.15.255.255", "172.32.0.1", "192.169.0.1", "11.0.0.1", "100.127.255.254",
                "0.0.0.0", "255.255.255.255", "224.0.0.1"})
            assertNull(other, LinkReceiver.pickAddress(addresses(other)));
        // fec0::/10 is IPv6's old site-local range, which Java still calls site-local.
        assertNull("IPv6 only", LinkReceiver.pickAddress(addresses("fe80::1", "fd00::1", "fec0::1", "2001:db8::1", "::ffff:8.8.8.8")));
        assertNull(LinkReceiver.pickAddress(new ArrayList<>()));
        // The receiver itself refuses any other address, and a code it never makes.
        assertThrows(IllegalArgumentException.class, () -> new LinkReceiver(address("127.0.0.1"), CODE, PAGES, new AtomicInteger()));
        assertThrows(IllegalArgumentException.class, () -> new LinkReceiver(address("8.8.8.8"), CODE, PAGES, new AtomicInteger()));
        for (String code : new String[]{"K7QM4XH", "K7QM4XHDD", "K7QM4XHU", "K7QM 4XH"})
            assertThrows(code, IllegalArgumentException.class, () -> loopback(code, 60_000, 1_000, 2_000));
    }

    private static InetAddress address(String literal) throws IOException { return InetAddress.getByName(literal); }
    private static List<InetAddress> addresses(String... literals) throws IOException {
        List<InetAddress> list = new ArrayList<>();
        for (String literal : literals) list.add(address(literal));
        return list;
    }

    @Test public void theQrCodeHasModulesBigEnoughToScanAcrossARoom() {
        // The longest address the receiver can show.
        String longest = "http://192.168.255.255:65535/ZZZZ-ZZZZ";
        QrCode symbol = QrCode.encodeText(longest, QrCode.Ecc.MEDIUM);
        assertTrue("Version " + symbol.version + ", 29 modules a side at most", symbol.version <= 3);
        LinkReceiver.Qr qr = LinkReceiver.qr(longest, 440);
        int modules = symbol.size + 8;
        int scale = qr.side / modules;
        assertEquals("A whole number of pixels a module", modules * scale, qr.side);
        assertTrue(qr.side <= 440 && qr.side > 440 - modules);
        assertEquals(qr.side * qr.side, qr.pixels.length);
        for (int y = 0; y < modules; y++)
            for (int x = 0; x < modules; x++) {
                boolean dark = symbol.getModule(x - 4, y - 4);
                if (x < 4 || y < 4 || x >= modules - 4 || y >= modules - 4) assertFalse("quiet zone", dark);
                for (int py = y * scale; py < (y + 1) * scale; py++)
                    for (int px = x * scale; px < (x + 1) * scale; px++)
                        assertEquals(dark ? 0xFF000000 : 0xFFFFFFFF, qr.pixels[py * qr.side + px]);
            }
        // Smaller than the symbol itself, it still draws one pixel a module.
        assertEquals(modules, LinkReceiver.qr(longest, 10).side);
    }

    // Real sockets: the receiver on loopback, a raw socket as the phone's browser.

    // The first byte gets as long as every other read, unless a test says otherwise.
    private LinkReceiver loopback(String code, long lifetimeMs, int readTimeoutMs, int deadlineMs) throws IOException {
        return loopback(code, lifetimeMs, readTimeoutMs, readTimeoutMs, deadlineMs);
    }
    private LinkReceiver loopback(String code, long lifetimeMs, int firstByteMs, int readTimeoutMs, int deadlineMs) throws IOException {
        return loopback(code, waiting, lifetimeMs, firstByteMs, readTimeoutMs, deadlineMs);
    }
    private LinkReceiver loopback(String code, AtomicInteger waiting, long lifetimeMs, int firstByteMs, int readTimeoutMs,
                                  int deadlineMs) throws IOException {
        LinkReceiver receiver = new LinkReceiver(InetAddress.getLoopbackAddress(), code, PAGES, waiting, lifetimeMs, firstByteMs,
                readTimeoutMs, deadlineMs);
        opened.add(receiver);
        return receiver;
    }
    private static BlockingQueue<String> start(LinkReceiver receiver) {
        BlockingQueue<String> events = new LinkedBlockingQueue<>();
        receiver.start(new LinkReceiver.Listener() {
            @Override public void received(String text, String fileName, String sender) {
                events.add("received " + text + (fileName == null ? "" : " as " + fileName) + " from " + sender);
            }
            @Override public void stopped(LinkReceiver.Stop reason) { events.add("stopped " + reason); }
        });
        return events;
    }
    private static int port(LinkReceiver receiver) {
        String url = receiver.url();
        return Integer.parseInt(url.substring(url.lastIndexOf(':') + 1, url.lastIndexOf('/')));
    }
    // The test's own end of a loopback connection, as a browser would open it: plain
    // HTTP, which is all the receiver speaks. With a timeout: a port that no one
    // takes connections on any more fills its queue, and a connect would then wait
    // for minutes.
    private static Socket connect(int port, int timeoutMs) throws IOException {
        Socket socket = new Socket(); // nosemgrep: java.lang.security.audit.crypto.unencrypted-socket.unencrypted-socket
        try {
            socket.connect(new InetSocketAddress(InetAddress.getLoopbackAddress(), port), timeoutMs);
            return socket;
        } catch (IOException failed) {
            socket.close();
            throw failed;
        }
    }
    private static Socket connect(int port) throws IOException { return connect(port, 5_000); }
    private static String send(int port, String request) throws IOException { return send(port, utf8(request)); }
    private static String send(int port, byte[] request) throws IOException {
        try (Socket socket = connect(port)) {
            socket.setSoTimeout(5_000);
            socket.getOutputStream().write(request);
            socket.getOutputStream().flush();
            return new String(socket.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        }
    }
    private static String loopbackPost(int port, String path, String body) {
        return post(path, "127.0.0.1:" + port, "Content-Type: application/x-www-form-urlencoded\r\n", body);
    }
    // stop() from another thread closes the port, but the receiver's thread, if it
    // waits in accept(), lets go of it only once it wakes, a moment later on a busy
    // machine. Until then a connection is taken or reset rather than refused.
    private static void assertClosed(int port) throws InterruptedException {
        long end = System.nanoTime() + 2_000_000_000L;
        while (System.nanoTime() < end) {
            try { connect(port, 200).close(); }
            catch (ConnectException refused) { return; }
            catch (IOException closing) { /* not yet */ }
            Thread.sleep(20);
        }
        fail("The port must be closed");
    }

    // The page's own multipart send of a file, to the right code, over a real socket.
    private static String sendFile(int port, String fileName, String content) throws IOException {
        return send(port, multipartPost("/K7QM-4XHD", "127.0.0.1:" + port, "multipart/form-data; boundary=" + BOUNDARY,
                pageForm("", fileName, utf8(content))));
    }
    private static String get(int port, String path) throws IOException {
        return send(port, "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + port + "\r\n\r\n");
    }

    @Test public void severalTextsAreReceivedAndThePortStaysOpen() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        assertEquals("http://127.0.0.1:" + port + "/K7QM-4XHD", receiver.url());
        BlockingQueue<String> events = start(receiver);
        assertTrue(get(port, "/K7QM-4XHD").endsWith(PAGES.form));
        // A pasted link, a file, and another pasted text, in turn: each answered with the page that says it
        // was sent, which is the form again for the next, and each handed over in the order it came.
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertEquals(1, waiting.get());
        assertTrue(sendFile(port, "home.toml", TOML).endsWith(PAGES.sent));
        assertEquals("received " + TOML.trim() + " as home.toml from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertTrue(send(port, loopbackPost(port, "/K7QM4XHD", form("a b"))).endsWith(PAGES.sent));
        assertEquals("received a b from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertEquals("Each text holds a place until the TV has answered it", 3, waiting.get());
        // Still open: the page is served, and nothing was reported but the texts.
        assertTrue(get(port, "/").endsWith(PAGES.form));
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
        // A text that is turned away, or a wrong code, takes no place.
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", "link=")).startsWith("HTTP/1.1 400 "));
        assertEquals(3, waiting.get());
        // Closed by the owner: the port, and the callbacks, stop.
        receiver.stop();
        assertClosed(port);
        assertNull("Exactly the three callbacks", events.poll(300, TimeUnit.MILLISECONDS));
        receiver.stop();
        assertNull(events.poll(100, TimeUnit.MILLISECONDS));
    }

    @Test public void theBusyPageIsNoStrikeAndAFreedPlaceLetsTheNextSendIn() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        for (int i = 0; i < LinkReceiver.MAX_WAITING; i++) {
            assertTrue("send " + i, send(port, loopbackPost(port, "/K7QM-4XHD", form("text " + i))).endsWith(PAGES.sent));
            assertEquals("received text " + i + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        }
        assertEquals(LinkReceiver.MAX_WAITING, waiting.get());
        // Ten wait, and the TV answers none: the next sends, as many as the phone likes, are told to wait,
        // as a link, a file or a form with nothing in it. Not a one of them is a strike, and none is handed over.
        for (int i = 0; i < 2 * LinkReceiver.MAX_WRONG_CODES; i++) {
            String busy = i % 3 == 0 ? send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK)))
                    : i % 3 == 1 ? sendFile(port, "home.toml", TOML) : send(port, loopbackPost(port, "/K7QM-4XHD", "link="));
            assertTrue(busy, busy.startsWith("HTTP/1.1 503 Service Unavailable\r\n") && busy.endsWith(PAGES.busy));
        }
        assertEquals(LinkReceiver.MAX_WAITING, waiting.get());
        assertNull("No callback, and not locked out", events.poll(300, TimeUnit.MILLISECONDS));
        // A wrong code is a strike all the same: it gets the wrong-code page, and the busy page no way to tell it.
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHE", form(LINK))).endsWith(PAGES.wrongCode));
        // Someone who just looks at the page, and a photo, which is turned away before the code is known, are no different.
        assertTrue(get(port, "/K7QM-4XHD").endsWith(PAGES.form));
        // The TV answers one: that place is the next text's.
        waiting.decrementAndGet();
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("text 10"))).endsWith(PAGES.sent));
        assertEquals("received text 10 from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertEquals(LinkReceiver.MAX_WAITING, waiting.get());
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("text 11"))).endsWith(PAGES.busy));
        // Two answered, two places, the third send is turned away again.
        waiting.addAndGet(-2);
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("text 12"))).endsWith(PAGES.sent));
        assertTrue(sendFile(port, "home.toml", TOML).endsWith(PAGES.sent));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("text 14"))).endsWith(PAGES.busy));
        assertEquals("received text 12 from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertEquals("received " + TOML.trim() + " as home.toml from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
    }

    @Test public void theLimitIsTheCountThatIsSharedNotTheReceivers() throws Exception {
        // Texts still waiting from an earlier session count in the next one's: the places are the TV's.
        AtomicInteger shared = new AtomicInteger(LinkReceiver.MAX_WAITING - 1);
        LinkReceiver receiver = loopback(CODE, shared, 60_000, 2_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertEquals(LinkReceiver.MAX_WAITING, shared.get());
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.busy));
        assertEquals(LinkReceiver.MAX_WAITING, shared.get());
        assertEquals("The test's own count was never given", 0, waiting.get());
    }

    @Test public void theBusyAnswerIsMadeBeforeAnyTextIsTakenAndATextIsTakenOnlyOnce() throws IOException {
        // At the level of one request. Nine wait: this one takes the last place.
        AtomicInteger count = new AtomicInteger(LinkReceiver.MAX_WAITING - 1);
        Exchange last = exchange(post("/K7QM4XHD", form(LINK)), count);
        assertEquals("HTTP/1.1 200 OK", last.status());
        assertEquals(PAGES.sent, last.body());
        assertSame(LinkReceiver.Kind.RECEIVED, last.outcome.kind);
        assertEquals(LinkReceiver.MAX_WAITING, count.get());
        // Ten wait: every send to the right code is told so, whatever it holds, and takes nothing.
        for (byte[] request : new byte[][]{utf8(post("/K7QM4XHD", form(LINK))), utf8(post("/K7QM4XHD", "link=")),
                utf8(post("/K7QM4XHD", "garbage")), multipartPost("/K7QM4XHD", pageForm("", "home.toml", utf8(TOML))),
                multipartPost("/K7QM4XHD", pageForm("", "", new byte[0])), multipartPost("/K7QM4XHD", pageForm(LINK, "home.toml", utf8(TOML)))}) {
            String what = new String(request, StandardCharsets.UTF_8);
            Exchange busy = exchange(request, false, count);
            assertEquals(what, "HTTP/1.1 503 Service Unavailable", busy.status());
            assertEquals(what, PAGES.busy, busy.body());
            assertSame(what, LinkReceiver.Kind.BUSY, busy.outcome.kind);
            assertNull(what, busy.outcome.text);
            assertFalse(what, busy.outcome.bodyUnread);
            assertEquals("The body is read first, so the browser gets the page", 0, busy.unread);
            assertEquals(what, LinkReceiver.MAX_WAITING, count.get());
        }
        // A wrong code is a wrong code, full or not, and it is the last strike's page on the last strike.
        Exchange wrong = exchange(post("/K7QM-4XHE", form(LINK)), count);
        assertSame(LinkReceiver.Kind.WRONG_CODE, wrong.outcome.kind);
        assertEquals(PAGES.wrongCode, wrong.body());
        assertEquals(PAGES.lockedOut, exchange(post("/K7QM-4XHE", form(LINK)).getBytes(StandardCharsets.UTF_8), true, count).body());
        // A GET shows the form, and a photo too large to take gets its own page: neither asks for a code or a place.
        assertEquals(PAGES.form, exchange(get("/"), count).body());
        Exchange photo = exchange(post("/K7QM4XHD", HOST, "Content-Type: application/x-www-form-urlencoded\r\n", "x").replace(
                "Content-Length: 1", "Content-Length: 12345678"), count);
        assertEquals(PAGES.tooLarge, photo.body());
        assertSame(LinkReceiver.Kind.REFUSED, photo.outcome.kind);
        assertEquals(LinkReceiver.MAX_WAITING, count.get());
        // A text the TV answered gives its place to the next, and a request that is turned away for another reason takes none.
        count.decrementAndGet();
        assertSame(LinkReceiver.Kind.REFUSED, exchange(post("/K7QM4XHD", "link="), count).outcome.kind);
        assertEquals(LinkReceiver.MAX_WAITING - 1, count.get());
        assertSame(LinkReceiver.Kind.RECEIVED, exchange(multipartPost("/K7QM4XHD", pageForm("", "home.toml", utf8(TOML))), false, count).outcome.kind);
        assertEquals(LinkReceiver.MAX_WAITING, count.get());
    }

    @Test public void theLimitIsTenAndTheBusyTextSaysSo() throws Exception {
        // The phone is told the number in words, in each language, and nothing but this test ties the words to
        // the constant: a change to one has to be a change to the other.
        assertEquals(10, LinkReceiver.MAX_WAITING);
        java.io.File res = null;
        for (String dir : new String[]{"src/main/res", "app/src/main/res", "android/app/src/main/res"})
            if (new java.io.File(dir, "values/strings.xml").isFile()) res = new java.io.File(dir);
        assertNotNull("The resources are found from the module's directory", res);
        for (String values : new String[]{"values", "values-ru"}) {
            String xml = new String(java.nio.file.Files.readAllBytes(new java.io.File(res, values + "/strings.xml").toPath()), StandardCharsets.UTF_8);
            java.util.regex.Matcher busy = java.util.regex.Pattern.compile("<string name=\"receive_page_busy\">([^<]*)</string>").matcher(xml);
            assertTrue(values, busy.find());
            assertTrue(values + ": " + busy.group(1), busy.group(1).matches(".*\\b" + LinkReceiver.MAX_WAITING + "\\b.*"));
        }
    }

    // The time and the strikes are said in words too (the timeout notices, the lockout notice, the README and the
    // release notes), and every other test counts with the constants, so that a change to one of them alone passes them all.
    // (The TV's card says neither: it has two short sentences, and the warnings are on the phone's page, which the next test holds.)
    @Test public void theTimeIsFiveMinutesAndThreeWrongCodesStopItAsTheTextsSay() throws Exception {
        assertEquals(5 * 60 * 1000L, LinkReceiver.LIFETIME_MS);
        assertEquals(3, LinkReceiver.MAX_WRONG_CODES);
        java.io.File res = null;
        for (String dir : new String[]{"src/main/res", "app/src/main/res", "android/app/src/main/res"})
            if (new java.io.File(dir, "values/strings.xml").isFile()) res = new java.io.File(dir);
        assertNotNull("The resources are found from the module's directory", res);
        for (String values : new String[]{"values", "values-ru"}) {
            String xml = new String(java.nio.file.Files.readAllBytes(new java.io.File(res, values + "/strings.xml").toPath()), StandardCharsets.UTF_8);
            // "5 minutes" and "5 минут", "3 times" and "3 раза".
            String minutes = values.equals("values") ? "5 minutes" : "5 \u043C\u0438\u043D\u0443\u0442";
            String times = values.equals("values") ? "3 times" : "3 \u0440\u0430\u0437\u0430";
            for (String name : new String[]{"text_receive_timed_out", "text_receive_timed_out_after_texts"}) {
                java.util.regex.Matcher text = java.util.regex.Pattern.compile("<string name=\"" + name + "\">([^<]*)</string>").matcher(xml);
                assertTrue(values + " " + name, text.find());
                assertTrue(values + " " + name + ": " + text.group(1), text.group(1).contains(minutes));
            }
            java.util.regex.Matcher locked = java.util.regex.Pattern.compile("<string name=\"text_receive_locked_out\">([^<]*)</string>").matcher(xml);
            assertTrue(values, locked.find());
            assertTrue(values + ": " + locked.group(1), locked.group(1).contains(times));
        }
    }

    // The card on the TV is two short sentences and keeps the warning about the network out of it: the page the phone opens says that
    // the text crosses the network unencrypted, and to send it only over one that is trusted, in each language.
    @Test public void thePhonesPageSaysTheTextCrossesTheNetworkUnencrypted() throws Exception {
        java.io.File res = null;
        for (String dir : new String[]{"src/main/res", "app/src/main/res", "android/app/src/main/res"})
            if (new java.io.File(dir, "values/strings.xml").isFile()) res = new java.io.File(dir);
        assertNotNull("The resources are found from the module's directory", res);
        for (String values : new String[]{"values", "values-ru"}) {
            String xml = new String(java.nio.file.Files.readAllBytes(new java.io.File(res, values + "/strings.xml").toPath()), StandardCharsets.UTF_8);
            java.util.regex.Matcher note = java.util.regex.Pattern.compile("<string name=\"receive_page_note\">([^<]*)</string>").matcher(xml);
            assertTrue(values, note.find());
            String unencrypted = values.equals("values") ? "unencrypted" : "\u0431\u0435\u0437 \u0448\u0438\u0444\u0440\u043E\u0432\u0430\u043D\u0438\u044F";
            String trusted = values.equals("values") ? "network you trust" : "\u0434\u043E\u0432\u0435\u0440\u0435\u043D\u043D\u043E\u0439 \u0441\u0435\u0442\u0438";
            assertTrue(values + ": " + note.group(1), note.group(1).contains(unencrypted) && note.group(1).contains(trusted));
            // And the card's own hint is no paragraph: two sentences, with neither the warning nor the tip about the phone's VPN.
            java.util.regex.Matcher hint = java.util.regex.Pattern.compile("<string name=\"text_receive_hint\">([^<]*)</string>").matcher(xml);
            assertTrue(values, hint.find());
            assertTrue(values + ": " + hint.group(1), hint.group(1).split("(?<=[.!?])\\s+").length <= 2 && hint.group(1).length() <= 160);
        }
    }

    @Test public void aTextTakesItsPlaceAtomicallyAndNeverTheEleventh() throws Exception {
        // Another receiver that shares the count may fill the last place after this one looked: taking it is a
        // compare-and-set, which the last place goes to one of them by. The step is private; called as handle() calls it.
        java.lang.reflect.Method take = LinkReceiver.class.getDeclaredMethod("take", java.io.OutputStream.class, LinkReceiver.Pages.class,
                AtomicInteger.class, LinkReceiver.Outcome.class);
        take.setAccessible(true);
        LinkReceiver.Outcome text = new LinkReceiver.Outcome(LinkReceiver.Kind.RECEIVED, LINK);
        AtomicInteger count = new AtomicInteger(LinkReceiver.MAX_WAITING - 1);
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        assertSame(text, take.invoke(null, out, PAGES, count, text));
        assertTrue(new String(out.toByteArray(), StandardCharsets.UTF_8).startsWith("HTTP/1.1 200 OK"));
        assertEquals(LinkReceiver.MAX_WAITING, count.get());
        out.reset();
        LinkReceiver.Outcome turnedAway = (LinkReceiver.Outcome) take.invoke(null, out, PAGES, count, text);
        assertSame(LinkReceiver.Kind.BUSY, turnedAway.kind);
        assertTrue(new String(out.toByteArray(), StandardCharsets.UTF_8).startsWith("HTTP/1.1 503 Service Unavailable"));
        assertEquals(LinkReceiver.MAX_WAITING, count.get());
    }

    @Test public void aRefusedSendTakesNoPlace() throws IOException {
        AtomicInteger count = new AtomicInteger();
        byte[] png = {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0x0d};
        for (byte[] form : new byte[][]{pageForm("", "", new byte[0]), pageForm(LINK, "home.toml", utf8(TOML)),
                pageForm("", "photo.png", png)})
            assertSame(LinkReceiver.Kind.REFUSED, exchange(multipartPost("/K7QM4XHD", form), false, count).outcome.kind);
        assertSame(LinkReceiver.Kind.WRONG_CODE, exchange(multipartPost("/K7QM-4XHE", pageForm("", "home.toml", utf8(TOML))), false, count).outcome.kind);
        assertSame(LinkReceiver.Kind.REFUSED, exchange(post("/K7QM4XHD", "link=%zz"), count).outcome.kind);
        assertEquals(0, count.get());
        // And a taken text takes exactly one, a pasted one and a file alike.
        exchange(post("/K7QM4XHD", form(LINK)), count);
        assertEquals(1, count.get());
        exchange(multipartPost("/K7QM4XHD", pageForm("", "home.toml", utf8(TOML))), false, count);
        assertEquals(2, count.get());
        exchange(multipartPost("/K7QM4XHD", pageForm(LINK, "", new byte[0])), false, count);
        assertEquals(3, count.get());
    }

    @Test public void threeWrongCodesStopIt() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        for (int strike = 1; strike < LinkReceiver.MAX_WRONG_CODES; strike++) {
            assertTrue(send(port, loopbackPost(port, "/K7QM-4XHE", form(LINK))).endsWith(PAGES.wrongCode));
            assertNull("Still open after strike " + strike, events.poll(100, TimeUnit.MILLISECONDS));
        }
        // Requests that are not a send with a code do not count.
        assertTrue(send(port, "GET /K7QM-4XHE HTTP/1.1\r\nHost: 127.0.0.1:" + port + "\r\n\r\n").endsWith(PAGES.form));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", "link=")).startsWith("HTTP/1.1 400 "));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHE", form(LINK))).endsWith(PAGES.lockedOut));
        assertEquals("stopped LOCKED_OUT", events.poll(5, TimeUnit.SECONDS));
        assertClosed(port);
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
    }

    @Test public void wrongCodesAddUpOverTheSendsBetweenThem() throws Exception {
        // Three wrong codes stop it however many texts come between them: a good send does not wipe the slate.
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHE", form(LINK))).endsWith(PAGES.wrongCode));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("one"))).endsWith(PAGES.sent));
        assertEquals("received one from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHF", form(LINK))).endsWith(PAGES.wrongCode));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("two"))).endsWith(PAGES.sent));
        assertEquals("received two from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertNull("Two strikes: still open", events.poll(200, TimeUnit.MILLISECONDS));
        // The third is the last: it says the TV stopped, and then it has.
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHG", form(LINK))).endsWith(PAGES.lockedOut));
        assertEquals("stopped LOCKED_OUT", events.poll(5, TimeUnit.SECONDS));
        assertClosed(port);
        assertNull("Nothing after it", events.poll(300, TimeUnit.MILLISECONDS));
        // The texts that came before it were taken, and are the TV's to answer.
        assertEquals(2, waiting.get());
    }

    @Test public void theTimeIsCountedFromTheOpeningAndNoSendExtendsIt() throws Exception {
        LinkReceiver receiver = loopback(CODE, 1_500, 2_000, 5_000);
        int port = port(receiver);
        long opened = System.nanoTime();
        BlockingQueue<String> events = start(receiver);
        // A text every 300 ms, well inside the time: each is taken, and the end comes when it was always going to.
        int sent = 0;
        String stopped = null;
        while (stopped == null) {
            long elapsed = (System.nanoTime() - opened) / 1_000_000;
            if (elapsed < 1_000) {
                assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("text " + sent))).endsWith(PAGES.sent));
                assertEquals("received text " + sent + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
                sent++;
                Thread.sleep(300);
            } else {
                stopped = events.poll(5, TimeUnit.SECONDS);
            }
        }
        long ms = (System.nanoTime() - opened) / 1_000_000;
        assertTrue("Sent " + sent, sent >= 3);
        assertEquals("stopped TIMEOUT", stopped);
        assertTrue("Stopped after " + ms + " ms, at its 1.5 s and not later for the sends", ms >= 1_400 && ms < 2_100);
        assertClosed(port);
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
    }

    @Test public void aTextTakenJustBeforeStopIsNotHandedOverAndItsPlaceIsGivenBack() throws Exception {
        // stop() comes after a text is taken and before it is handed over, which no socket can be timed to
        // do: the phone has had its answer, but the listener hears nothing, and the place the text took is
        // given back, as no one will answer it. The step is private, so it is called as serve() does.
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        List<String> heard = new ArrayList<>();
        LinkReceiver.Listener listener = new LinkReceiver.Listener() {
            @Override public void received(String text, String fileName, String sender) { heard.add(text); }
            @Override public void stopped(LinkReceiver.Stop reason) { heard.add("stopped " + reason); }
        };
        java.lang.reflect.Method deliver = LinkReceiver.class.getDeclaredMethod("deliver", LinkReceiver.Listener.class,
                LinkReceiver.Outcome.class, String.class);
        deliver.setAccessible(true);
        LinkReceiver.Outcome taken = new LinkReceiver.Outcome(LinkReceiver.Kind.RECEIVED, LINK);
        waiting.set(1);
        deliver.invoke(receiver, listener, taken, "127.0.0.1");
        assertEquals(Arrays.asList(LINK), heard);
        assertEquals("Handed over, the place is the TV's until it answers", 1, waiting.get());
        receiver.stop();
        waiting.set(2);
        deliver.invoke(receiver, listener, taken, "127.0.0.1");
        assertEquals("Nothing after stop()", Arrays.asList(LINK), heard);
        assertEquals("and the place is given back", 1, waiting.get());
    }

    @Test public void stopWaitsForATextThatIsBeingHandedOverAndThenNoMoreAreHandedOver() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        List<String> texts = new java.util.concurrent.CopyOnWriteArrayList<>();
        CountDownLatch inside = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        receiver.start(new LinkReceiver.Listener() {
            @Override public void received(String text, String fileName, String sender) {
                texts.add(text);
                inside.countDown();
                try { release.await(5, TimeUnit.SECONDS); } catch (InterruptedException ignored) { }
            }
            @Override public void stopped(LinkReceiver.Stop reason) { texts.add("stopped " + reason); }
        });
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form("one"))).endsWith(PAGES.sent));
        assertTrue(inside.await(5, TimeUnit.SECONDS));
        // A second text is on its way while the first is being handed over, and the owner stops the receiver.
        Thread phone = new Thread(() -> {
            try { send(port, loopbackPost(port, "/K7QM-4XHD", form("two"))); } catch (IOException gone) { /* stopped */ }
        });
        phone.start();
        Thread owner = new Thread(receiver::stop);
        owner.start();
        owner.join(300);
        assertTrue("stop() waits for the text it would otherwise overtake", owner.isAlive());
        release.countDown();
        owner.join(5_000);
        assertFalse(owner.isAlive());
        phone.join(5_000);
        assertClosed(port);
        Thread.sleep(300);
        assertEquals("Only the one that was being handed over", Arrays.asList("one"), texts);
    }

    // A whole send made by a client that leaves at once, without reading the answer:
    // with a reset, as SO_LINGER 0 gives, or with an ordinary close.
    private static void sendAndLeave(int port, String request, boolean reset) throws IOException {
        sendAndLeave(port, utf8(request), reset);
    }
    private static void sendAndLeave(int port, byte[] request, boolean reset) throws IOException {
        Socket socket = connect(port);
        socket.getOutputStream().write(request);
        if (reset) socket.setSoLinger(true, 0);
        socket.close();
    }

    // Each send below waits behind a connection that holds the receiver, so that it
    // was made and its sender gone before the receiver reads it, and the answer
    // cannot be written.
    @Test public void wrongCodesCountThoughTheirSendersLeaveBeforeTheAnswer() throws Exception {
        // Or a guesser that never reads the answers would never use up its tries.
        LinkReceiver receiver = loopback(CODE, 60_000, 5_000, 10_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket holder = connect(port)) {
            for (int strike = 0; strike < LinkReceiver.MAX_WRONG_CODES; strike++)
                sendAndLeave(port, loopbackPost(port, "/K7QM-4XHE", form(LINK)), strike % 2 == 0);
            Thread.sleep(100);
            // Lets go: the receiver reads its end, and goes on to the sends.
            holder.shutdownOutput();
            assertEquals("stopped LOCKED_OUT", events.poll(5, TimeUnit.SECONDS));
        }
        assertClosed(port);
    }

    @Test public void aTextIsReceivedThoughItsSenderLeavesBeforeTheAnswer() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 5_000, 10_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket holder = connect(port)) {
            sendAndLeave(port, loopbackPost(port, "/K7QM-4XHD", form(LINK)), true);
            Thread.sleep(100);
            holder.shutdownOutput();
            assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        }
        // The text is taken, and the receiver goes on.
        assertTrue(get(port, "/").endsWith(PAGES.form));
        receiver.stop();
        assertClosed(port);
    }

    @Test public void aFileIsReceivedOnceTheFormIsRight() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        String type = "multipart/form-data; boundary=" + BOUNDARY;
        String host = "127.0.0.1:" + port;
        // Sent with nothing in it, the form comes back, and the TV goes on waiting.
        assertTrue(send(port, multipartPost("/K7QM-4XHD", host, type, pageForm("", "", new byte[0]))).endsWith(PAGES.retry));
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
        assertTrue(send(port, multipartPost("/K7QM-4XHD", host, type, pageForm("", "home.toml", concat(BOM, utf8(TOML)))))
                .endsWith(PAGES.sent));
        assertEquals("received " + TOML.trim() + " as home.toml from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        // And the form comes again, for the next file.
        assertTrue(send(port, multipartPost("/K7QM-4XHD", host, type, pageForm("", "link.txt", utf8(LINK + "\n")))).endsWith(PAGES.sent));
        assertEquals("received " + LINK + " as link.txt from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
        receiver.stop();
        assertClosed(port);
    }

    @Test public void aFileIsReceivedThoughItsSenderLeavesBeforeTheAnswer() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 5_000, 10_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket holder = connect(port)) {
            sendAndLeave(port, multipartPost("/K7QM-4XHD", "127.0.0.1:" + port, "multipart/form-data; boundary=" + BOUNDARY,
                    pageForm("", "link.txt", utf8(LINK + "\n"))), true);
            Thread.sleep(100);
            holder.shutdownOutput();
            assertEquals("received " + LINK + " as link.txt from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
        }
        assertTrue(get(port, "/").endsWith(PAGES.form));
        receiver.stop();
        assertClosed(port);
    }

    // A photo chosen by mistake: the TV answers before it reads the body, and a
    // browser reads that answer only once it has sent the body. Closed with the body
    // unread, the connection would be reset instead, which fails the browser's
    // writes and can erase the answer it has not read yet.
    private static String sendAsABrowser(int port, byte[] request) throws Exception {
        AtomicReference<IOException> failed = new AtomicReference<>();
        String answer;
        try (Socket browser = connect(port)) {
            browser.setSoTimeout(5_000);
            // As a browser, it goes on sending while the answer waits.
            Thread writer = new Thread(() -> {
                try {
                    browser.getOutputStream().write(request);
                    browser.shutdownOutput();
                } catch (IOException reset) {
                    failed.set(reset);
                }
            });
            writer.start();
            answer = new String(browser.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
            writer.join(5_000);
            assertFalse("The whole body must be taken", writer.isAlive());
        }
        assertNull("The body must be read, not reset: " + failed.get(), failed.get());
        return answer;
    }

    @Test public void aSendTooLargeToTakeGetsItsPageAndTheTvGoesOnWaiting() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        // To the right code, then to a mistyped one as many times as would stop the
        // TV: the page is the form again, which anyone gets, so it is no strike.
        List<String> paths = new ArrayList<>(Arrays.asList("/K7QM-4XHD"));
        for (int i = 0; i < LinkReceiver.MAX_WRONG_CODES; i++) paths.add("/K7QM-4XHE");
        for (String path : paths) {
            String answer = sendAsABrowser(port, multipartPost(path, "127.0.0.1:" + port, "multipart/form-data; boundary=" + BOUNDARY,
                    pageForm("", "photo.jpg", new byte[2 * LinkReceiver.MAX_BODY_BYTES])));
            assertTrue(path + ": " + answer, answer.startsWith("HTTP/1.1 413 Content Too Large\r\n"));
            assertTrue(path, answer.endsWith(PAGES.tooLarge));
        }
        // The right file from the same page goes through.
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
    }

    // A too-large send that announces a body it never sends.
    private static void sendTooLargeHead(Socket socket, int port) throws IOException {
        socket.getOutputStream().write(utf8("POST /K7QM-4XHD HTTP/1.1\r\nHost: 127.0.0.1:" + port
                + "\r\nContent-Type: multipart/form-data; boundary=" + BOUNDARY + "\r\nContent-Length: 99999999\r\n\r\n"));
    }

    @Test public void aTooLargeSendThatNeverEndsHoldsTheTvForTheDrainOnly() throws Exception {
        // Its answer comes at once, and the TV waits for the rest for DRAIN_MS, not
        // for its read timeout or deadline.
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000, 20_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket stalled = connect(port)) {
            stalled.setSoTimeout(5_000);
            sendTooLargeHead(stalled, port);
            assertTrue(new String(stalled.getInputStream().readAllBytes(), StandardCharsets.UTF_8).endsWith(PAGES.tooLarge));
            long started = System.nanoTime();
            // Waits in the queue behind the stalled one, until its drain is over.
            assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
            long heldMs = (System.nanoTime() - started) / 1_000_000;
            assertTrue("Held " + heldMs + " ms: for the drain, not the read timeout", heldMs >= LinkReceiver.DRAIN_MS - 500 && heldMs < 4_500);
        }
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
    }

    @Test public void aTooLargeSendNeverOutlivesItsDeadline() throws Exception {
        // Its deadline comes before DRAIN_MS is up, as for a send that starts late in
        // it, and the drain ends there: no connection holds the TV past its deadline.
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000, 1_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        long started = System.nanoTime();
        try (Socket stalled = connect(port)) {
            stalled.setSoTimeout(5_000);
            sendTooLargeHead(stalled, port);
            assertTrue(new String(stalled.getInputStream().readAllBytes(), StandardCharsets.UTF_8).endsWith(PAGES.tooLarge));
            // Waits in the queue behind the stalled one, until its deadline.
            assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
            long heldMs = (System.nanoTime() - started) / 1_000_000;
            assertTrue("Held " + heldMs + " ms: to its 1 s deadline, not for DRAIN_MS",
                    heldMs >= 900 && heldMs < LinkReceiver.DRAIN_MS - 400);
        }
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
    }

    @Test public void itStopsWhenTheTimeIsUp() throws Exception {
        LinkReceiver receiver = loopback(CODE, 200, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        assertEquals("stopped TIMEOUT", events.poll(5, TimeUnit.SECONDS));
        assertClosed(port);
    }

    @Test public void aSilentClientIsDroppedAndThePhoneStillGetsThrough() throws Exception {
        // As a browser's connection opened ahead of time, while it sends the request
        // on another one.
        LinkReceiver receiver = loopback(CODE, 60_000, 300, 4_000, 8_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket silent = connect(port)) {
            silent.setSoTimeout(5_000);
            long started = System.nanoTime();
            // Waits in the queue behind the silent one, until its first byte's wait is up.
            assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
            long heldMs = (System.nanoTime() - started) / 1_000_000;
            assertTrue("Held " + heldMs + " ms: for its first byte, not its read timeout", heldMs >= 250 && heldMs < 2_000);
            assertEquals(-1, silent.getInputStream().read());
        }
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
    }

    @Test public void aRequestThatHasStartedWaitsForItsReadTimeout() throws Exception {
        // Only the first byte has the short wait. After it, a pause shorter than the
        // read timeout is waited out, and a longer one ends the connection, long
        // before its deadline.
        LinkReceiver receiver = loopback(CODE, 60_000, 200, 1_500, 8_000);
        int port = port(receiver);
        start(receiver);
        byte[] begun = "GET / HTTP/1.1\r\n".getBytes(StandardCharsets.US_ASCII);
        try (Socket slow = connect(port)) {
            slow.setSoTimeout(5_000);
            slow.getOutputStream().write(begun);
            Thread.sleep(700);
            slow.getOutputStream().write(("Host: 127.0.0.1:" + port + "\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            assertTrue(new String(slow.getInputStream().readAllBytes(), StandardCharsets.UTF_8).endsWith(PAGES.form));
        }
        try (Socket stalled = connect(port)) {
            stalled.setSoTimeout(5_000);
            long started = System.nanoTime();
            stalled.getOutputStream().write(begun);
            assertEquals(-1, stalled.getInputStream().read());
            long heldMs = (System.nanoTime() - started) / 1_000_000;
            assertTrue("Held " + heldMs + " ms: for its read timeout, not to its deadline", heldMs >= 1_400 && heldMs < 4_000);
        }
    }

    @Test public void aTricklingClientIsCutOffAtItsDeadline() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 400, 1_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        long cutAfterMs;
        try (Socket trickling = connect(port)) {
            trickling.setSoTimeout(5_000);
            // A byte every 100 ms, well inside each read's timeout, and never a whole
            // request, though it starts as one.
            Thread writer = new Thread(() -> {
                try {
                    for (int i = 0; i < 50; i++) { trickling.getOutputStream().write(i == 0 ? 'G' : 'x'); Thread.sleep(100); }
                } catch (IOException | InterruptedException done) {
                    // Cut off, or the test is over.
                }
            });
            long started = System.nanoTime();
            writer.start();
            InputStream in = trickling.getInputStream();
            try {
                assertEquals(-1, in.read());
            } catch (java.net.SocketException reset) {
                // Closed with bytes it never read, the receiver's end resets instead.
            }
            cutAfterMs = (System.nanoTime() - started) / 1_000_000;
            writer.interrupt();
            writer.join();
        }
        assertTrue("Cut off after " + cutAfterMs + " ms, at its 1 s deadline", cutAfterMs >= 900 && cutAfterMs < 3_000);
        assertTrue(send(port, loopbackPost(port, "/K7QM-4XHD", form(LINK))).endsWith(PAGES.sent));
        assertEquals("received " + LINK + " from 127.0.0.1", events.poll(5, TimeUnit.SECONDS));
    }

    @Test public void stopClosesThePortSilentlyAndTwiceIsFine() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        assertTrue(send(port, "GET / HTTP/1.1\r\nHost: 127.0.0.1:" + port + "\r\n\r\n").startsWith("HTTP/1.1 200 "));
        receiver.stop();
        receiver.stop();
        assertClosed(port);
        assertNull("stop() is the owner's, so it hears nothing", events.poll(300, TimeUnit.MILLISECONDS));
        assertThrows(IllegalStateException.class, () -> start(receiver));
        // Stopped before it starts, it never serves.
        LinkReceiver early = loopback(CODE, 60_000, 2_000, 5_000);
        early.stop();
        BlockingQueue<String> none = start(early);
        assertClosed(port(early));
        assertNull(none.poll(300, TimeUnit.MILLISECONDS));
    }

    @Test public void aPortThatBreaksIsReportedAsFailed() throws Exception {
        // As when the network goes away under it: the port closes, but not by stop().
        LinkReceiver receiver = loopback(CODE, 60_000, 2_000, 5_000);
        BlockingQueue<String> events = start(receiver);
        java.lang.reflect.Field server = LinkReceiver.class.getDeclaredField("server");
        server.setAccessible(true);
        ((java.net.ServerSocket) server.get(receiver)).close();
        assertEquals("stopped FAILED", events.poll(5, TimeUnit.SECONDS));
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
    }

    @Test public void stopWhileAClientIsConnectedDropsIt() throws Exception {
        LinkReceiver receiver = loopback(CODE, 60_000, 5_000, 10_000);
        int port = port(receiver);
        BlockingQueue<String> events = start(receiver);
        try (Socket waiting = connect(port)) {
            waiting.setSoTimeout(5_000);
            waiting.getOutputStream().write("GET / HTTP/1.1\r\n".getBytes(StandardCharsets.US_ASCII));
            Thread.sleep(200);
            long started = System.nanoTime();
            receiver.stop();
            assertEquals(-1, waiting.getInputStream().read());
            assertTrue("Dropped at once, not at its read timeout", System.nanoTime() - started < 2_000_000_000L);
        }
        assertNull(events.poll(300, TimeUnit.MILLISECONDS));
    }
}
