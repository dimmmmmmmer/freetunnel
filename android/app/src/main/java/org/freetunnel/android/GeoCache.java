package org.freetunnel.android;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Only successfully parsed downloads can replace the last working list. */
final class GeoCache {
    static final int MAX_BYTES = 128 * 1024 * 1024;
    private final Path directory;

    static final class Result {
        final List<String> entries;
        final GeoLists.Stats stats;
        final long updatedMillis;
        final long routingBytes;
        Result(List<String> entries, GeoLists.Stats stats, long updatedMillis) {
            this.entries = Collections.unmodifiableList(new ArrayList<>(entries));
            this.stats = stats;
            this.updatedMillis = updatedMillis;
            long bytes = 0;
            for (String entry : entries) bytes += entry.length() + 6L;
            this.routingBytes = bytes;
        }
    }

    GeoCache(Path directory) { this.directory = directory; }

    private Path path(String source, GeoLists.Kind kind, boolean bypass) {
        String identity = source + '\0' + kind.ordinal() + ':' + (bypass ? 1 : 0);
        try {
            byte[] hash = MessageDigest.getInstance("SHA-256").digest(identity.getBytes(StandardCharsets.UTF_8));
            StringBuilder name = new StringBuilder();
            for (byte value : hash) {
                name.append(Character.forDigit((value & 0xff) >>> 4, 16));
                name.append(Character.forDigit(value & 0xf, 16));
            }
            return directory.resolve(name + ".list");
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("SHA-256 unavailable", impossible);
        }
    }

    private Result parse(String source, GeoLists.Kind kind, boolean bypass, byte[] data, long updated)
            throws GeoLists.Refused {
        if (data.length > MAX_BYTES) throw new GeoLists.Refused("Geo list is too large");
        GeoLists.Source spec = GeoLists.parseSource(source);
        if (spec == null) throw new GeoLists.Refused("Invalid geo list source");
        List<String> entries = new ArrayList<>();
        GeoLists.Stats stats = GeoLists.append(data, spec, kind, bypass, entries);
        return new Result(entries, stats, updated);
    }

    Result load(String source, GeoLists.Kind kind, boolean bypass) throws IOException, GeoLists.Refused {
        Path file = path(source, kind, bypass);
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (InputStream input = Files.newInputStream(file)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (count > MAX_BYTES - bytes.size()) throw new GeoLists.Refused("Geo list is too large");
                bytes.write(buffer, 0, count);
            }
        }
        return parse(source, kind, bypass, bytes.toByteArray(), Files.getLastModifiedTime(file).toMillis());
    }

    Result store(String source, GeoLists.Kind kind, boolean bypass, byte[] data)
            throws IOException, GeoLists.Refused {
        Result parsed = parse(source, kind, bypass, data, 0);
        Files.createDirectories(directory);
        Path destination = path(source, kind, bypass);
        Path temporary = Files.createTempFile(directory, ".geo-", ".tmp");
        try {
            Files.write(temporary, data);
            Files.move(temporary, destination, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
            return new Result(parsed.entries, parsed.stats, Files.getLastModifiedTime(destination).toMillis());
        } finally {
            Files.deleteIfExists(temporary);
        }
    }
}
