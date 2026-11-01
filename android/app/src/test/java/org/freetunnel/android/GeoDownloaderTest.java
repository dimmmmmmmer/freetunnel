package org.freetunnel.android;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import java.io.ByteArrayInputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import static org.junit.Assert.*;

public class GeoDownloaderTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private static final GeoDefinition LIST = new GeoDefinition("a", "Example", "https://example.org/list.txt#group",
            GeoLists.Kind.SITES, false, true);
    private static final class Reply extends HttpURLConnection {
        final byte[] body; final int status; final String location;
        boolean disconnected;
        Reply(URI uri, String body, int status, String location) throws java.io.IOException {
            super(uri.toURL()); this.body = body.getBytes(StandardCharsets.UTF_8); this.status = status; this.location = location;
        }
        @Override public int getResponseCode() { return status; }
        @Override public String getHeaderField(String name) { return location; }
        @Override public InputStream getInputStream() { return new ByteArrayInputStream(body); }
        @Override public void connect() {}
        @Override public void disconnect() { disconnected = true; }
        @Override public boolean usingProxy() { return false; }
    }
    @Test public void followsAssetRedirectWithoutCategoryAndStoresParsedList() throws Exception {
        List<URI> requests = new ArrayList<>(); List<Reply> replies = new ArrayList<>();
        GeoDownloader downloader = new GeoDownloader(temporary.getRoot().toPath(), uri -> {
            requests.add(uri);
            Reply reply = new Reply(uri, "domain:example.org", requests.size() == 1 ? 302 : 200,
                    "https://assets.example.net/list.txt");
            replies.add(reply); return reply;
        });
        assertEquals(2, downloader.fetch(LIST).entries.size());
        assertEquals(2, requests.size());
        assertNull(requests.get(0).getFragment());
        assertEquals("assets.example.net", requests.get(1).getHost());
        for (Reply reply : replies) assertTrue(reply.disconnected);
    }
    @Test public void refusesDowngradeAndCredentials() throws Exception {
        for (String address : new String[]{"http://example.org/list", "https://user:pass@example.org/list"}) {
            List<URI> requests = new ArrayList<>();
            GeoDownloader downloader = new GeoDownloader(temporary.getRoot().toPath(), uri -> {
                requests.add(uri); return new Reply(uri, "", 302, address);
            });
            assertThrows(GeoLists.Refused.class, () -> downloader.fetch(LIST));
            assertEquals(1, requests.size());
        }
    }
    @Test public void aFailedRefreshKeepsWorkingRules() throws Exception {
        Path path = temporary.getRoot().toPath();
        GeoCache cache = new GeoCache(path);
        List<String> original = cache.store(LIST.source, LIST.kind, LIST.bypass,
                "domain:example.org".getBytes(StandardCharsets.UTF_8)).entries;
        GeoDownloader downloader = new GeoDownloader(path, uri -> new Reply(uri, "<html>Error</html>", 200, null));
        assertThrows(GeoLists.Refused.class, () -> downloader.fetch(LIST));
        assertEquals(original, cache.load(LIST.source, LIST.kind, LIST.bypass).entries);
    }
    @Test public void redirectLoopIsBounded() throws Exception {
        List<URI> requests = new ArrayList<>();
        GeoDownloader downloader = new GeoDownloader(temporary.getRoot().toPath(), uri -> {
            requests.add(uri); return new Reply(uri, "", 302, uri.toString());
        });
        assertThrows(GeoLists.Refused.class, () -> downloader.fetch(LIST));
        assertEquals(6, requests.size());
    }
}
