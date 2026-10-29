package org.freetunnel.android;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;

/**
 * The log file, freetunnel.log in the app's private files, written by the VPN service (and read by the Logs page, which also
 * clears it). A line is appended; when the file passes {@link #MAX_BYTES} it is cut back to its last {@link #KEEP_BYTES}, at the
 * start of a line, as the desktop keeps the tail of its own (Backend::trimLogFile). One lock serves the service's threads and
 * the page, which share a process. Every failure is quiet: the log is for the user, and must never be what stops a connection.
 */
final class LogFile {
    static final String NAME = "freetunnel.log";
    static final long MAX_BYTES = 256 * 1024;
    static final long KEEP_BYTES = 128 * 1024;
    private static final Object LOCK = new Object();

    private LogFile() { }

    /** Adds an entry (see LogLines.entry) as a line of its own. */
    static void append(File file, String entry) {
        synchronized (LOCK) {
            try {
                trimLocked(file);
                try (FileOutputStream out = new FileOutputStream(file, true)) {
                    out.write((entry + "\n").getBytes(StandardCharsets.UTF_8));
                }
            } catch (IOException | RuntimeException ignored) {
                // Nowhere to say so.
            }
        }
    }

    /** The whole file as text; none if there is no file. */
    static String read(File file) throws IOException {
        synchronized (LOCK) {
            if (!file.isFile()) return "";
            return new String(Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8);
        }
    }

    /** What the file was last changed at and how long it is, to tell whether it has changed: 0 for no file. */
    static long stamp(File file) {
        synchronized (LOCK) { return file.isFile() ? file.lastModified() * 31 + file.length() : 0; }
    }

    static void clear(File file) {
        synchronized (LOCK) {
            try { Files.deleteIfExists(file.toPath()); } catch (IOException | RuntimeException ignored) { }
        }
    }

    private static void trimLocked(File file) throws IOException {
        if (!file.isFile() || file.length() <= MAX_BYTES) return;
        byte[] all = Files.readAllBytes(file.toPath());
        int from = (int) Math.max(0, all.length - KEEP_BYTES);
        // Start at the beginning of a line, so that no half of one is left (and no half of a character: a line break is
        // never inside one in UTF-8).
        while (from > 0 && from < all.length && all[from - 1] != '\n') from++;
        Files.write(file.toPath(), from >= all.length ? new byte[0] : Arrays.copyOfRange(all, from, all.length));
    }
}
