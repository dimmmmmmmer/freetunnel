package org.freetunnel.android;

import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.TemporaryFolder;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import static org.junit.Assert.*;

public class GeoCacheTest {
    @Rule public TemporaryFolder temporary = new TemporaryFolder();
    private static final String SOURCE = "https://example.com/sites.txt";
    private static byte[] bytes(String text) { return text.getBytes(StandardCharsets.UTF_8); }

    @Test public void failedDownloadKeepsLastGoodList() throws Exception {
        GeoCache cache = new GeoCache(temporary.newFolder().toPath());
        GeoCache.Result good = cache.store(SOURCE, GeoLists.Kind.SITES, true, bytes("domain:example.com\n"));
        assertTrue(good.updatedMillis > 0);
        try {
            cache.store(SOURCE, GeoLists.Kind.SITES, true, bytes("# empty update\n"));
            fail("An empty update must be refused");
        } catch (GeoLists.Refused expected) {
            assertEquals(good.entries, cache.load(SOURCE, GeoLists.Kind.SITES, true).entries);
        }
        try {
            cache.load(SOURCE, GeoLists.Kind.SITES, false);
            fail("Changing direction must not reuse another policy's cache");
        } catch (IOException expected) {
            // No cache for this policy yet.
        }
    }

    @Test public void corruptedCacheIsRejected() throws Exception {
        Path directory = temporary.newFolder().toPath();
        GeoCache cache = new GeoCache(directory);
        cache.store(SOURCE, GeoLists.Kind.SITES, false, bytes("domain:example.com\n"));
        try (java.util.stream.Stream<Path> files = Files.list(directory)) {
            Files.write(files.findFirst().orElseThrow(), bytes("<html>Unavailable</html>"));
        }
        try {
            cache.load(SOURCE, GeoLists.Kind.SITES, false);
            fail("Corrupted cache must not be used");
        } catch (GeoLists.Refused expected) {
            assertNotNull(expected.getMessage());
        }
    }
}
