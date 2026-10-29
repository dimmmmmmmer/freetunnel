package org.freetunnel.android;

import android.content.Context;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * The lists the desktop and this app share, kept under data/ in the repository and copied into the APK's assets by the
 * copySharedData task of app/build.gradle: one entry to a line, in UTF-8; a blank line, and a line that begins with #, is no
 * entry. {@link RussiaList} and {@link ExcludedRoutes} read theirs through here.
 */
final class DataFile {
    private DataFile() { }

    /** The entries in a file's text, in its order. The same reading as the desktop's (cmake/SharedData.cmake), line by line. */
    static List<String> parse(String text) {
        String body = text.startsWith("\uFEFF") ? text.substring(1) : text;
        List<String> entries = new ArrayList<>();
        for (String line : body.split("\n")) {
            String entry = line.trim();
            if (!entry.isEmpty() && entry.charAt(0) != '#') entries.add(entry);
        }
        return Collections.unmodifiableList(entries);
    }

    /** The entries of an asset. */
    static List<String> read(Context context, String asset) throws IOException {
        try (InputStream in = context.getAssets().open(asset)) {
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] chunk = new byte[4096];
            int n;
            while ((n = in.read(chunk)) != -1) bytes.write(chunk, 0, n);
            return parse(new String(bytes.toByteArray(), StandardCharsets.UTF_8));
        }
    }
}
