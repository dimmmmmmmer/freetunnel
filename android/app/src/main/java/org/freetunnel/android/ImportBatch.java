package org.freetunnel.android;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** Clipboard/text-file batches. TOML stays a single document, including multiline strings. */
final class ImportBatch {
    static final int MAX_ITEMS = 100;
    static final int MAX_BYTES = 1024 * 1024;

    private ImportBatch() { }

    static List<String> split(String text) {
        if (text.length() > MAX_BYTES || text.getBytes(StandardCharsets.UTF_8).length > MAX_BYTES)
            throw new IllegalArgumentException("Import is too large");
        String trimmed = text.trim();
        if (trimmed.startsWith("\uFEFF")) trimmed = trimmed.substring(1).trim();
        if (!DeepLinkImport.looksLikeLink(trimmed)) return Collections.singletonList(trimmed);
        String[] links = trimmed.split("\\s+");
        int starts = 0;
        for (String link : links) if (DeepLinkImport.looksLikeLink(link)) starts++;
        // Existing single-link import accepts a payload wrapped onto several lines.
        if (starts <= 1) return Collections.singletonList(trimmed);
        if (links.length > MAX_ITEMS) throw new IllegalArgumentException("Too many links");
        // Keep invalid entries too: each receives the normal validation error, and
        // does not silently disappear or prevent the next valid entry being offered.
        return Arrays.asList(links);
    }
}
