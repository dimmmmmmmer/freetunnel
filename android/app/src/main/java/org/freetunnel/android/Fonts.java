package org.freetunnel.android;

import android.content.Context;
import android.graphics.Typeface;
import android.util.Log;

/**
 * Inter, the typeface of the whole app, as it is of the desktop's (assets/fonts/inter, SIL OFL 1.1): the one place the code gets
 * it from, and the only typefaces it sets on text besides monospace ({@link Typeface#MONOSPACE}, for the log, the certificate and the
 * TV's address, which the desktop sets in its monospace font too).
 *
 * <p>Two weights, as the desktop uses two: Regular (400) for all text, and Medium (500) wherever the desktop sets
 * {@code font.weight: Font.Medium}. Each is a file of its own (res/font/inter_regular.ttf and inter_medium.ttf, which the build copies
 * from assets/fonts/inter, the files the desktop compiles in), so nothing is made up from another weight: no bold, no italic, no
 * synthesized weight. The theme names Regular as {@code android:fontFamily} (res/values/styles.xml) for any text that nothing sets.
 *
 * <p>Read once and kept, from the application's resources: a Typeface is the same whichever activity asks for it.
 *
 * <p>The font is looks, not function, as it is on the desktop: if a file cannot be read the app runs in the system's font, and says
 * so in the log. Whatever is measured to size a control has to be measured in the face that is drawn: give a Paint
 * {@code regular(context)}, or {@code medium(context)}, as the view it stands for has.
 */
final class Fonts {
    private static Typeface regular;
    private static Typeface medium;

    private Fonts() {}

    /** Regular (400): all text, unless the desktop sets it in Medium. */
    static synchronized Typeface regular(Context context) {
        if (regular == null) regular = load(context, R.font.inter_regular, "inter_regular", "sans-serif");
        return regular;
    }

    /** Medium (500): where the desktop's text has {@code font.weight: Font.Medium}. Never bold. */
    static synchronized Typeface medium(Context context) {
        if (medium == null) medium = load(context, R.font.inter_medium, "inter_medium", "sans-serif-medium");
        return medium;
    }

    private static Typeface load(Context context, int font, String name, String systemFamily) {
        try {
            Context application = context.getApplicationContext();
            return (application != null ? application : context).getResources().getFont(font);
        } catch (RuntimeException e) {
            Log.w("FreeTunnel", "Could not load the Inter font " + name + "; using the system font", e);
            return Typeface.create(systemFamily, Typeface.NORMAL);
        }
    }
}
