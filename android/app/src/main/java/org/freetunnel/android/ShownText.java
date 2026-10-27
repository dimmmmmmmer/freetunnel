package org.freetunnel.android;

/**
 * A name or a server as the import confirmation shows it: on one line, and cut to 80
 * characters. A file or a link sets both, whoever sent it, and a remote cannot scroll
 * a dialog's message, so what they hold must not push the lines after them, where the
 * text was sent from and that certificate verification is off, out of sight, or paint
 * over them. What is saved stays as it was. Plain JDK, so the JVM tests check it.
 */
final class ShownText {
    static final int MAX_CHARACTERS = 80;

    // Letters that draw nothing, though they are not spaces: the Hangul fillers and
    // the blank braille pattern. A name of them alone would look like no name.
    private static final String BLANK_LETTERS = "\u115F\u1160\u3164\uFFA0\u2800";

    private ShownText() { }

    /** oneLine(text), cut to 80 characters, the last of them an ellipsis when it is cut. */
    static String of(String text) {
        String line = oneLine(text);
        if (line.codePointCount(0, line.length()) <= MAX_CHARACTERS) return line;
        return line.substring(0, line.offsetByCodePoints(0, MAX_CHARACTERS - 1)) + "…";
    }

    /**
     * text without control, format (such as the ones that reverse or hide text), line
     * or paragraph separator characters, with any other space as an ordinary one, and
     * without letters that draw nothing. A character keeps at most two combining marks,
     * none is kept with nothing to sit on, and marks that enclose it are dropped: drawn
     * above and below it, a long stack of them would paint over the lines after it.
     * Two are as many as Vietnamese stacks; a Tibetan stack or Hebrew with cantillation
     * can have more, and is shown with its first two.
     */
    static String oneLine(String text) {
        StringBuilder kept = new StringBuilder(text.length());
        // Combining marks kept on the last character; -1 at the start or after a
        // space, where a mark has nothing to sit on.
        int marks = -1;
        for (int i = 0; i < text.length(); ) {
            int c = text.codePointAt(i);
            i += Character.charCount(c);
            int type = Character.getType(c);
            if (type == Character.CONTROL || type == Character.FORMAT || type == Character.LINE_SEPARATOR
                    || type == Character.PARAGRAPH_SEPARATOR || type == Character.SURROGATE
                    || type == Character.ENCLOSING_MARK || BLANK_LETTERS.indexOf(c) >= 0) continue;
            if (type == Character.NON_SPACING_MARK) {
                if (marks < 0 || marks >= 2) continue;
                marks++;
            } else if (Character.isSpaceChar(c)) {
                c = ' ';
                marks = -1;
            } else {
                marks = 0;
            }
            kept.appendCodePoint(c);
        }
        return kept.toString();
    }
}
