package org.freetunnel.android;

import java.time.Instant;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * The log's lines, as the desktop writes and shows them (src/app/BackendLogs.cpp): a line is written
 * {@code yyyy-MM-dd HH:mm:ss<TAB>LEVEL<TAB>message} and the Logs page shows it as {@code HH:mm:ss LEVEL message}. The levels
 * are INFO, WARN and ERROR. A line written before this, {@code <epoch ms> message}, is shown as it is.
 *
 * <p>A line is a line: whatever breaks one in a message (a new line, a tab) is a space, so that a message that holds
 * several lines cannot pass for several entries, and an entry is only ever the one the app wrote. All plain Java, tested
 * on a plain JVM (LogLinesTest).
 */
final class LogLines {
    static final String INFO = "INFO";
    static final String WARN = "WARN";
    static final String ERROR = "ERROR";
    /** A message longer than this is cut: one that long is data, not a line of a log. */
    static final int MAX_MESSAGE = 2000;
    /** How many of the last lines the Logs page shows. */
    static final int SHOWN_LINES = 1000;

    private static final DateTimeFormatter STAMP = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss", Locale.ROOT);
    private static final Pattern WRITTEN = Pattern.compile("^\\d{4}-\\d{2}-\\d{2} (\\d{2}:\\d{2}:\\d{2})\t([A-Z]+)\t(.*)$");

    private LogLines() { }

    /** A line as it is written to the file, without its line break. */
    static String entry(long millis, ZoneId zone, String level, String message) {
        return STAMP.format(Instant.ofEpochMilli(millis).atZone(zone)) + "\t" + level + "\t" + oneLine(message);
    }

    /** The message on one line: every control character a space, and no longer than {@link #MAX_MESSAGE}. */
    static String oneLine(String message) {
        StringBuilder line = new StringBuilder(Math.min(message.length(), MAX_MESSAGE + 1));
        boolean broke = false;
        for (int i = 0; i < message.length() && line.length() < MAX_MESSAGE; i++) {
            char c = message.charAt(i);
            // The C0 controls, DEL, NEL and the two Unicode line separators.
            boolean control = c < 0x20 || c == 0x7f || c == 0x85 || c == 0x2028 || c == 0x2029;
            // A run of them, such as a CR and an LF, is one space.
            if (control && broke) continue;
            broke = control;
            line.append(control ? ' ' : c);
        }
        String text = line.toString().trim();
        return message.length() > MAX_MESSAGE ? text + "…" : text;
    }

    /** One line of the file as the page shows it. */
    static String shown(String line) {
        Matcher written = WRITTEN.matcher(line);
        return written.matches() ? written.group(1) + " " + written.group(2) + " " + written.group(3) : line;
    }

    /** The last {@code maxLines} lines of the file's text, each as {@link #shown}, one to a line, none of them empty. */
    static String shownText(String file, int maxLines) {
        List<String> lines = new ArrayList<>();
        for (String line : file.split("\n")) {
            String clean = line.endsWith("\r") ? line.substring(0, line.length() - 1) : line;
            if (!clean.trim().isEmpty()) lines.add(clean);
        }
        StringBuilder text = new StringBuilder();
        for (int i = Math.max(0, lines.size() - maxLines); i < lines.size(); i++) {
            if (text.length() > 0) text.append('\n');
            text.append(shown(lines.get(i)));
        }
        return text.toString();
    }
}
