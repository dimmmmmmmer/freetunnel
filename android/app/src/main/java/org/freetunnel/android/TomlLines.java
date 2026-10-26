package org.freetunnel.android;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Takes keys out of TOML text line by line, or gives them new values, so that
 * everything else stays exactly as it was written: comments, layout, key order and
 * multi-line certificates.
 */
final class TomlLines {
    private TomlLines() { }

    /**
     * The text without the keys at the given paths, with their values however many
     * lines those run to, and without the tables under those paths; the text itself
     * when none was there. Null when a line is in a form this does not read, such as
     * a quoted key with an escape in it. A key set inside an inline table is not
     * seen either, so RoutingConfig checks the result against a full parse.
     */
    static String without(String toml, Set<List<String>> drop) {
        return edit(toml, drop, Collections.emptyMap());
    }

    /**
     * As without(), and a key at a path in `replace` is given the value there in
     * place of its own, however many lines that ran to. The key stays as it was
     * spelled, so a dotted key still names the same table.
     */
    static String edit(String toml, Set<List<String>> drop, Map<List<String>, String> replace) {
        StringBuilder out = new StringBuilder(toml.length());
        // Comments and blank lines after the last key of a dropped table: kept if a
        // table that is kept comes next, since they are then about that table.
        StringBuilder held = new StringBuilder();
        List<String> table = new ArrayList<>();
        boolean dropTable = false;
        boolean dropValue = false;
        boolean removed = false;
        Value value = new Value();
        int start = 0;
        while (start < toml.length()) {
            int newline = toml.indexOf('\n', start);
            int next = newline < 0 ? toml.length() : newline + 1;
            String whole = toml.substring(start, next);
            String line = whole.endsWith("\n") ? whole.substring(0, whole.length() - 1) : whole;
            if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
            start = next;

            if (value.open()) {
                if (!value.read(line, 0)) return null;
                if (!dropValue) out.append(whole);
                continue;
            }
            int at = skipBlank(line, 0);
            if (at == line.length() || line.charAt(at) == '#') {
                (dropTable ? held : out).append(whole);
                continue;
            }
            if (line.charAt(at) == '[') {
                List<String> path = new ArrayList<>();
                if (!header(line, at, path)) return null;
                boolean wasDropped = dropTable;
                table = path;
                dropTable = under(path, drop);
                if (wasDropped && !dropTable) out.append(held);
                held.setLength(0);
                if (dropTable) removed = true;
                else out.append(whole);
                continue;
            }
            List<String> key = new ArrayList<>(table);
            int equals = readKey(line, at, key);
            if (equals < 0 || equals >= line.length() || line.charAt(equals) != '=') return null;
            held.setLength(0);
            String replacement = dropTable ? null : replace.get(key);
            dropValue = dropTable || replacement != null || under(key, drop);
            if (dropValue) removed = true;
            if (!value.read(line, equals + 1)) return null;
            if (replacement != null)
                out.append(line, 0, equals + 1).append(' ').append(replacement).append(whole, line.length(), whole.length());
            else if (!dropValue) out.append(whole);
        }
        if (value.open()) return null;
        return removed ? out.toString() : toml;
    }

    // Whether a key or table is at one of the paths, or inside one.
    private static boolean under(List<String> path, Set<List<String>> drop) {
        for (List<String> prefix : drop) {
            if (path.size() >= prefix.size() && path.subList(0, prefix.size()).equals(prefix)) return true;
        }
        return false;
    }

    private static int skipBlank(String line, int i) {
        while (i < line.length() && (line.charAt(i) == ' ' || line.charAt(i) == '\t')) i++;
        return i;
    }

    // A [table] or [[array of tables]] header, with nothing after it but a comment.
    private static boolean header(String line, int at, List<String> path) {
        boolean array = line.startsWith("[[", at);
        int end = readKey(line, at + (array ? 2 : 1), path);
        String close = array ? "]]" : "]";
        if (end < 0 || !line.startsWith(close, end)) return false;
        end = skipBlank(line, end + close.length());
        return end == line.length() || line.charAt(end) == '#';
    }

    // Reads a dotted key into parts and returns where it ends, past any blanks, or
    // -1. A quoted part with an escape in it is not decoded here; it gives -1.
    private static int readKey(String line, int i, List<String> parts) {
        while (true) {
            i = skipBlank(line, i);
            if (i >= line.length()) return -1;
            char c = line.charAt(i);
            if (c == '"' || c == '\'') {
                int end = line.indexOf(c, i + 1);
                if (end < 0) return -1;
                String part = line.substring(i + 1, end);
                if (c == '"' && part.indexOf('\\') >= 0) return -1;
                parts.add(part);
                i = end + 1;
            } else {
                int end = i;
                while (end < line.length() && isBare(line.charAt(end))) end++;
                if (end == i) return -1;
                parts.add(line.substring(i, end));
                i = end;
            }
            i = skipBlank(line, i);
            if (i < line.length() && line.charAt(i) == '.') { i++; continue; }
            return i;
        }
    }

    private static boolean isBare(char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    }

    // Where a value stands at the end of a line: how deep inside brackets and
    // braces, and whether inside a multi-line string.
    private static final class Value {
        private int depth;
        private String quote; // """ or ''' while a multi-line string is open

        boolean open() { return depth > 0 || quote != null; }

        // Reads the line from i on; false when it is not a value this can follow.
        boolean read(String line, int i) {
            while (i < line.length()) {
                if (quote != null) {
                    int end = closing(line, i, quote);
                    if (end < 0) return true;
                    quote = null;
                    i = end;
                    continue;
                }
                char c = line.charAt(i);
                if (c == '#') return true;
                if (c == '"' || c == '\'') {
                    String triple = c == '"' ? "\"\"\"" : "'''";
                    if (line.startsWith(triple, i)) { quote = triple; i += 3; continue; }
                    int end = c == '"' ? basicEnd(line, i + 1) : line.indexOf('\'', i + 1);
                    if (end < 0) return false;
                    i = end + 1;
                    continue;
                }
                if (c == '[' || c == '{') depth++;
                else if ((c == ']' || c == '}') && --depth < 0) return false;
                i++;
            }
            return true;
        }

        // Past the quotes that close a multi-line string, or -1 if it does not close
        // on this line. The one or two quotes TOML allows just before the closing
        // three are part of the string, so the whole run of quotes ends it.
        private static int closing(String line, int i, String quote) {
            char q = quote.charAt(0);
            while (i < line.length()) {
                if (q == '"' && line.charAt(i) == '\\') { i += 2; continue; }
                if (line.startsWith(quote, i)) {
                    int end = i;
                    while (end < line.length() && line.charAt(end) == q) end++;
                    return end;
                }
                i++;
            }
            return -1;
        }

        // The closing quote of a one-line basic string, or -1.
        private static int basicEnd(String line, int i) {
            while (i < line.length()) {
                char c = line.charAt(i);
                if (c == '\\') { i += 2; continue; }
                if (c == '"') return i;
                i++;
            }
            return -1;
        }
    }
}
