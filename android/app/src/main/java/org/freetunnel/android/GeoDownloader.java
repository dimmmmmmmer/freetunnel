package org.freetunnel.android;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.net.Proxy;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;

/** Called on a worker, never on the UI thread. A failed update preserves the cached list. */
final class GeoDownloader {
    interface Connections { HttpURLConnection open(URI uri) throws IOException; }
    private final GeoCache cache;
    private final Connections connections;
    GeoDownloader(Path directory) {
        this(directory, uri -> (HttpURLConnection) uri.toURL().openConnection(Proxy.NO_PROXY));
    }
    GeoDownloader(Path directory, Connections connections) {
        this.cache = new GeoCache(directory);
        this.connections = connections;
    }
    GeoCache.Result fetch(GeoDefinition definition) throws IOException, GeoLists.Refused {
        GeoLists.Source source = GeoLists.parseSource(definition.source);
        if (source == null) throw new GeoLists.Refused("Invalid geo list source");
        URI uri = URI.create(source.file);
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(60);
        for (int redirects = 0; redirects <= 5; redirects++) {
            HttpURLConnection connection = connections.open(uri);
            try {
                connection.setInstanceFollowRedirects(false);
                connection.setConnectTimeout(remaining(deadline));
                connection.setReadTimeout(remaining(deadline));
                connection.setUseCaches(false);
                connection.setAllowUserInteraction(false);
                connection.setRequestProperty("User-Agent", "FreeTunnel-GeoLists");
                connection.setRequestProperty("Accept-Encoding", "identity");
                int status = connection.getResponseCode();
                if (status >= 300 && status < 400) {
                    String location = connection.getHeaderField("Location");
                    try {
                        URI target = location == null ? null : uri.resolve(location);
                        if (target == null || !"https".equalsIgnoreCase(target.getScheme())
                                || target.getHost() == null || target.getRawUserInfo() != null) {
                            throw new GeoLists.Refused("Invalid geo list redirect");
                        }
                        String next = target.toASCIIString();
                        int fragment = next.indexOf('#');
                        uri = URI.create(fragment < 0 ? next : next.substring(0, fragment));
                    } catch (IllegalArgumentException invalid) {
                        throw new GeoLists.Refused("Invalid geo list redirect");
                    }
                    continue;
                }
                if (status != 200) throw new IOException("Could not download geo list (HTTP " + status + ")");
                ByteArrayOutputStream body = new ByteArrayOutputStream();
                try (InputStream input = connection.getInputStream()) {
                    byte[] buffer = new byte[16384];
                    while (true) {
                        if (Thread.currentThread().isInterrupted()) throw new IOException("Geo list download cancelled");
                        connection.setReadTimeout(remaining(deadline));
                        int count = input.read(buffer);
                        if (count == -1) break;
                        if (count > GeoCache.MAX_BYTES - body.size()) throw new GeoLists.Refused("Geo list is too large");
                        body.write(buffer, 0, count);
                    }
                }
                return cache.store(definition.source, definition.kind, definition.bypass, body.toByteArray());
            } finally {
                connection.disconnect();
            }
        }
        throw new GeoLists.Refused("Too many geo list redirects");
    }
    private static int remaining(long deadline) throws IOException {
        long millis = TimeUnit.NANOSECONDS.toMillis(deadline - System.nanoTime());
        if (millis <= 0) throw new IOException("Geo list download timed out");
        return (int) Math.min(millis, 60000);
    }
}
