package org.freetunnel.android;

import android.content.Context;

import java.io.IOException;
import java.util.List;

/**
 * "Recommended for Russia", the list the desktop and this app share: data/split/recommended-russia.txt, which the build
 * copies into the assets (the copySharedData task of app/build.gradle) and the desktop has compiled in
 * (cmake/SharedData.cmake). One rule to a line, in UTF-8; a blank line, and a line that begins with #, is no rule.
 */
final class RussiaList {
    static final String ASSET = "split/recommended-russia.txt";
    private static volatile List<String> cached;

    private RussiaList() { }

    /** The rules in a file's text, in its order. The same reading as the desktop's, line by line. */
    static List<String> parse(String text) { return DataFile.parse(text); }

    /** The list from the assets; it is read once. */
    static List<String> read(Context context) throws IOException {
        List<String> list = cached;
        if (list != null) return list;
        list = DataFile.read(context, ASSET);
        cached = list;
        return list;
    }

}
