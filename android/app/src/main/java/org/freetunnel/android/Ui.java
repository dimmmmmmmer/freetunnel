package org.freetunnel.android;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.graphics.text.LineBreaker;
import android.os.Build;
import android.text.GetChars;
import android.text.Layout;
import android.text.TextUtils;
import android.text.method.PasswordTransformationMethod;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.TouchDelegate;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The desktop's controls, built from its measurements (qml/Main.qml, qml/Toggle.qml,
 * qml/Field.qml, qml/components/*, qml/pages/*), and how a remote's focus shows on them.
 *
 * <p>The desktop has no focus ring: where the pointer is, a control takes its hover
 * look, and a remote's focus is that same look, control by control: a nav tile and a row fill with surface, a link goes to its hover
 * colour and underlines, a chip to surfaceHover, a button to its hover colour, a dropdown's
 * value and a back arrow to the text colour, a text field's border to the accent, a box of text
 * (which rests on surface) to surfaceHover. A control
 * the desktop gives no hover (a toggle, the logo) takes the nearest thing it has: its row
 * fills as a config row does, the logo grows to the pulse's peak. What is selected stays as
 * it is, with a remote or without one: an accent chip, an accent primary button, a red
 * confirm button with a white label, and an inputBg field. Never a stroke around a control,
 * never a foreground, never a fill across empty width: a control's focus is its background
 * (or its colour), in its own shape and radius. See {@link #focus}.
 *
 * <p>The three places a remote's focus shows what the desktop's pointer does not, because the control would otherwise have none: the
 * current page's nav tile grows to 1.08 like the others (the desktop leaves it as it is under the pointer), the selected profile
 * chip goes to primaryHover (the desktop leaves it on the accent), and the update line, which has no hover look while it checks,
 * downloads or installs, goes to its hover colour and underlined with the focus (the remote's focus stays on it through the check). Each is the one
 * place a control that does nothing, or is already selected, is still somewhere a remote stops, and all are on purpose.
 *
 * <p>What else differs from the desktop on purpose, and is to stay so (android/README.md, "Accepted differences from the desktop"): a TV's
 * overscan margin (48 at the sides, 27 above and below, so its rows, its cards and the empty Configs page's words sit differently),
 * "Receive from phone" first in a TV's add menu, the application field's placeholder (an app has no path to paste and
 * nothing to drop on it), and the file field of the page a phone is served, whose words are the browser's, and its paste box, which is
 * 16 px monospace where the desktop's certificate box is 12 (an iPhone zooms in on a field under 16).
 */
final class Ui {
    /** What a control's rest, hover and focus look like. */
    enum Look { TILE, HERO, ROW, MENU_ROW, PILL, LINK, ICON, PLAIN, CHIP, BUTTON, PRIMARY, DANGER, FIELD, SELECT, BOX }
    /** The four buttons the desktop has. */
    enum Kind { PRIMARY, SECONDARY, NEUTRAL, DESTRUCTIVE }
    /** The colours of link: accent, text colour, danger, and textFaint (the footer's). */
    enum LinkKind { ACCENT, TEXT, DANGER, FAINT }

    /** The tab or chip that is the current one, which rests in its selected look. */
    static final int F_CURRENT = 1;
    /** A secondary button, which rests on the page with a border, as the editor's Cancel does. */
    static final int F_SECONDARY = 2;
    /**
     * A row that holds controls of its own, such as a config row with its icons: while one of them has
     * the focus the row is activated (setActivated), and rests on surface, as the desktop's row does
     * while the pointer is over one of its icons.
     */
    static final int F_HOLDS_FOCUS = 16;
    /** A 1 px border in the border colour, such as the + chip's, whatever the control's state. */
    static final int F_BORDER = 32;

    /** The desktop animates every hover colour for 120 ms. */
    static final int FADE_MS = 120;

    private final Context context;
    private final float density;
    private Theme t = Theme.DARK;

    Ui(Context context) {
        this.context = context;
        this.density = context.getResources().getDisplayMetrics().density;
    }

    /**
     * The theme the next controls are drawn in. A control rests the same with a remote, a finger or a pointer: the desktop's
     * looks do not change with the way the screen is driven, only what shows the focus does (see {@link #background}).
     */
    void update(boolean dark) {
        this.t = Theme.of(dark);
    }
    Theme t() { return t; }

    // Measures and containers.

    int dp(float n) { return Math.round(n * density); }
    /** Sizes as a LinearLayout wants them: dp, or -1 and -2 for match and wrap. */
    LinearLayout.LayoutParams lp(int w, int h) { return new LinearLayout.LayoutParams(w < 0 ? w : dp(w), h < 0 ? h : dp(h)); }
    LinearLayout column() { LinearLayout l = new LinearLayout(context); l.setOrientation(LinearLayout.VERTICAL); return l; }
    LinearLayout row() { LinearLayout l = new LinearLayout(context); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL); return l; }
    void gap(LinearLayout parent, int size) { parent.addView(new View(context), lp(1, size)); }
    void gapH(LinearLayout parent, int size) { parent.addView(new View(context), lp(size, 1)); }

    GradientDrawable shape(int color, float radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        return d;
    }
    GradientDrawable outlined(int fill, int stroke, float radiusDp) {
        GradientDrawable d = shape(fill, radiusDp);
        d.setStroke(Math.max(1, dp(1)), stroke);
        return d;
    }

    // Text.

    TextView label(CharSequence value, float sp, int color) {
        TextView v = new TextView(context);
        v.setText(value);
        v.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        v.setTextColor(color);
        v.setTypeface(Fonts.regular(context));
        v.setGravity(Gravity.CENTER_VERTICAL);
        // A line is as high as its font's ascent and descent, as a Qt Text is: Android adds the font's own room above and below
        // otherwise, which makes every line two or three dp taller than the desktop's.
        v.setIncludeFontPadding(false);
        return v;
    }
    /** The desktop's Medium weight, 500: Inter Medium, a file of its own, never bold. */
    TextView medium(CharSequence value, float sp, int color) {
        TextView v = label(value, sp, color);
        v.setTypeface(Fonts.medium(context));
        return v;
    }
    TextView mono(CharSequence value, float sp, int color) {
        TextView v = label(value, sp, color);
        v.setTypeface(Typeface.MONOSPACE);
        return v;
    }
    /** SectionLabel: textFaint, 12, as it is typed, with 4 under it. */
    TextView sectionLabel(CharSequence value) {
        TextView v = label(value, 12, t.textFaint);
        v.setPadding(0, 0, 0, dp(4));
        return v;
    }
    /** A line of wrapped text: it takes the height its lines need, and at least this. */
    TextView paragraph(CharSequence value, float sp, int color, int minHeightDp) {
        TextView v = label(value, sp, color);
        v.setMinHeight(dp(minHeightDp));
        return v;
    }
    /** Sep: one line in the border colour, the full width. */
    View sep() {
        View line = new View(context);
        line.setBackgroundColor(t.border);
        return line;
    }
    /** SectionLabel's own spacing: 16 between sections. */
    void section(LinearLayout parent, CharSequence title) {
        gap(parent, 16);
        parent.addView(sectionLabel(title), lp(-1, -2));
    }

    // Colours that change with the control's state.

    /** rest, while pressed or hovered, and while it has the focus. */
    static ColorStateList colors(int rest, int hot, int focused) {
        return new ColorStateList(new int[][]{
            {android.R.attr.state_focused}, {android.R.attr.state_pressed}, {android.R.attr.state_hovered}, {}},
            new int[]{focused, hot, hot, rest});
    }
    /** One colour in every state: the content of a control whose look does not change its colours. */
    static ColorStateList always(int color) { return ColorStateList.valueOf(color); }

    /** A child of a focusable control takes its states, and its text changes colour with them. */
    <T extends TextView> T follow(T child, int rest, int hot, int focused) {
        child.setTextColor(colors(rest, hot, focused));
        child.setDuplicateParentStateEnabled(true);
        return child;
    }
    /** The same for an icon, whose tint is its colour. */
    <T extends ImageView> T followTint(T child, int rest, int hot, int focused) {
        child.setImageTintList(colors(rest, hot, focused));
        child.setDuplicateParentStateEnabled(true);
        return child;
    }
    /** An icon in one colour, whatever state the control it is in is in. */
    <T extends ImageView> T tinted(T child, int color) {
        child.setImageTintList(always(color));
        return child;
    }
    /**
     * A character the desktop draws as text (▾ ✓ ↓ ↑ ←), as a shape (scripts/android-icons-from-svg.py), so that it sits on the line as
     * the desktop's does and not as whichever font a TextView would fall back to draws it. ✓ ↓ ↑ ← are the outlines of Inter's own
     * glyphs, read from the font file the desktop draws them from, at the pixel size its QML sets them in, so a drawable is the ink of
     * the character and no more: its side bearings are the margins that the layouts give it (speedTile, cardHeader). Inter has no ▾
     * (U+25BE): the desktop's system font draws it, and it is a shape of that font's size. One colour, whatever state it is in; it is
     * no thing to read out.
     */
    ImageView glyph(int drawable, int color) {
        ImageView v = new ImageView(context);
        v.setImageResource(drawable);
        v.setImageTintList(always(color));
        v.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        return v;
    }
    /**
     * The same, in the colours of the control it is in: at rest, under the pointer and with the focus. For a glyph that is a child of a
     * focusable control, which it takes the states of; a glyph that is the control itself (the ←) uses {@link #ownGlyph}.
     */
    ImageView followGlyph(int drawable, int rest, int hot, int focused) {
        return followTint(glyph(drawable, rest), rest, hot, focused);
    }
    /**
     * A glyph that is the focus stop itself (the ←): its tint changes with its own states, never its parent's, which a header or a row
     * never has focused, pressed or hovered; duplicating those would leave a remote's focus on it unseen.
     */
    ImageView ownGlyph(int drawable, int rest, int hot, int focused) {
        ImageView v = glyph(drawable, rest);
        v.setImageTintList(colors(rest, hot, focused));
        return v;
    }

    /**
     * A field of one line. Android 13 and later keep an arrow pressed on a keyboard inside an editor, so that Up and Down never left
     * a field with one (a remote's keys are not a keyboard's, but a keyboard on a TV, and the device tests' keys, are): they go on to the
     * control above or below, as they do from any other control, and stay in the field only where there is none. The cursor's own
     * moves are Left and Right.
     */
    static class LineEdit extends EditText {
        LineEdit(Context context) {
            super(context);
            setTypeface(Fonts.regular(context));
        }

        @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
            if ((keyCode == KeyEvent.KEYCODE_DPAD_UP || keyCode == KeyEvent.KEYCODE_DPAD_DOWN) && event.hasNoModifiers()) {
                int direction = keyCode == KeyEvent.KEYCODE_DPAD_UP ? FOCUS_UP : FOCUS_DOWN;
                View next = focusSearch(direction);
                if (next != null && next != this && next.requestFocus(direction)) return true;
            }
            return super.onKeyDown(keyCode, event);
        }
    }
    /**
     * The echo of a password, as the desktop's: every character a ● (U+25CF, which Qt's PasswordMaskCharacter is on Windows and Linux),
     * a larger circle than Android's own • and with room between them, and never the character just typed, which Android shows for a
     * moment wherever the system's "Show passwords" is on (it is, by default) and the desktop never does. It is a
     * PasswordTransformationMethod all the same, so that the system still treats the field as a secret: nothing in it is cut or copied.
     * Set it after setInputType, which puts the system's own in its place.
     */
    static final class PasswordMask extends PasswordTransformationMethod {
        static final char DOT = '\u25CF';

        @Override public CharSequence getTransformation(CharSequence source, View view) { return new Dots(source); }

        /** As many dots as the text has characters, following it as it changes. */
        private static final class Dots implements CharSequence, GetChars {
            private final CharSequence source;
            Dots(CharSequence source) { this.source = source; }
            @Override public int length() { return source.length(); }
            @Override public char charAt(int index) { return DOT; }
            @Override public CharSequence subSequence(int start, int end) {
                char[] dots = new char[end - start];
                getChars(start, end, dots, 0);
                return new String(dots);
            }
            @Override public String toString() { return subSequence(0, length()).toString(); }
            @Override public void getChars(int start, int end, char[] dest, int offset) {
                for (int i = start; i < end; i++) dest[offset + i - start] = DOT;
            }
        }
    }
    /** A field of one line, for a form to put in {@link #field}, with the arrows above. */
    EditText lineEdit() {
        EditText e = new LineEdit(context);
        e.setSingleLine(true);
        return e;
    }

    /**
     * A box of text that a remote scrolls with Up and Down (the log): at its top an Up, and at its end a Down, go on to the control
     * beside it, as they do from any control, where the scroll view itself would keep them. Anywhere else the arrows scroll it.
     */
    static void leaveAtTheEnds(ScrollView box) {
        box.setOnKeyListener((v, keyCode, event) -> {
            if (event.getAction() != KeyEvent.ACTION_DOWN || !event.hasNoModifiers()) return false;
            // One branch to a key, each handing the search the constant itself, so that lint can see that it is a direction.
            if (keyCode == KeyEvent.KEYCODE_DPAD_UP && !box.canScrollVertically(-1)) {
                View next = box.focusSearch(View.FOCUS_UP);
                return next != null && next != box && next.requestFocus(View.FOCUS_UP);
            }
            if (keyCode == KeyEvent.KEYCODE_DPAD_DOWN && !box.canScrollVertically(1)) {
                View next = box.focusSearch(View.FOCUS_DOWN);
                return next != null && next != box && next.requestFocus(View.FOCUS_DOWN);
            }
            return false;
        });
    }

    // Focus.

    /**
     * Makes the view a stop for a remote, with a key that names it across render(), which
     * rebuilds every view, and gives it the desktop's look for this kind of control: its
     * rest look, its hover look while pressed or hovered, and its focus look, which is the hover
     * look but for a text field's and a box of text's accent border (and the one gap, a selected
     * chip, which the desktop shows no hover of). All three are the view's background, a
     * StateListDrawable that fades in 120 ms as the desktop's colours do; the view has no
     * foreground, and Android's own highlight is off. A phone in touch mode never focuses a
     * control, so it shows none of it; text fields focus on a tap, and show the desktop's field
     * focus then too.
     */
    <T extends View> T focus(T view, String key, Look look) { return focus(view, key, look, 0); }
    <T extends View> T focus(T view, String key, Look look, int flags) {
        view.setFocusable(true);
        view.setTag(key);
        view.setDefaultFocusHighlightEnabled(false);
        view.setBackground(background(look, flags));
        if (look == Look.TILE) scaleOnFocus(view);
        return view;
    }

    /**
     * The background of a control with this look; see {@link #focus}. What the desktop shows under the pointer is what
     * is shown with the focus:
     * <ul>
     * <li>TILE (a nav tile): surface, grown to 1.08; the current tab keeps infoBg, and only grows;
     * <li>ROW and MENU_ROW (the rows of a list, a menu, a picker, a select): surface, radius 8 and 6;
     * <li>PILL (the header's + and ping) and ICON (Home's +): surface, radius 8 and 6, the icon in accent throughout;
     * <li>LINK and PLAIN: no fill. A link's colour is its hover colour (see {@link #link}) and underlined; a PLAIN control
     * (a dropdown's value, ←, a ChipX, a config row's icon, a toggle, which draws itself) changes the colour of its content;
     * <li>HERO (Home's logo): no fill, which would wash out its greys; it grows to 1.05 (see HeroValues);
     * <li>CHIP: surfaceHover. The selected chip rests on the accent, which the desktop does not change under the pointer;
     * with the focus alone it goes to primaryHover, as the primary button does;
     * <li>BUTTON: surfaceHover (a dialog's), surface (the editor's Cancel, which keeps its border); PRIMARY: primaryHover;
     * DANGER: destructiveHover;
     * <li>FIELD, SELECT: an accent border on inputBg; BOX (a box of text a remote scrolls): surfaceHover, with no border.
     * </ul>
     */
    StateListDrawable background(Look look, int flags) {
        boolean current = (flags & F_CURRENT) != 0;
        int rest = Color.TRANSPARENT;
        int hot = Color.TRANSPARENT;
        int stroke = 0;
        // The border under the pointer, when it is not the one at rest.
        int hotStroke = 0;
        boolean hotStrokeSet = false;
        // The focus is what is shown under the pointer, unless it is said to be something else.
        int focused = 0;
        int focusStroke = 0;
        boolean focusSet = false;
        float radius = 8;
        switch (look) {
            case TILE:
                // The current tab is infoBg, and hovering it changes nothing; the others rest on the page and take surface.
                rest = current ? t.infoBg : Color.TRANSPARENT;
                hot = current ? t.infoBg : t.surface;
                break;
            case ROW: hot = t.surface; break;
            case MENU_ROW: hot = t.surface; radius = 6; break;
            case PILL: hot = t.surface; break;
            case ICON: hot = t.surface; radius = 6; break;
            case CHIP:
                radius = 13;
                rest = current ? t.accent : t.surface;
                hot = current ? t.accent : t.surfaceHover;
                focused = current ? t.primaryHover : t.surfaceHover;
                focusSet = true;
                break;
            case BUTTON:
                if ((flags & F_SECONDARY) != 0) { rest = t.bg; stroke = t.border; hot = t.surface; }
                else { rest = t.surface; hot = t.surfaceHover; }
                break;
            case PRIMARY:
                rest = t.accent;
                hot = t.primaryHover;
                break;
            case DANGER:
                rest = t.danger;
                hot = t.destructiveHover;
                break;
            case FIELD:
                // The desktop's field: inputBg with an inputBorder outline that turns accent while it has the focus.
                rest = t.inputBg;
                hot = t.inputBg;
                stroke = t.inputBorder;
                focused = t.inputBg;
                focusStroke = t.accent;
                focusSet = true;
                break;
            case SELECT:
                // A select box (the editor's): a field that opens a choice. As a field, and its border turns accent under the
                // pointer, as the desktop's does, and with the focus.
                rest = t.inputBg;
                hot = t.inputBg;
                stroke = t.inputBorder;
                hotStroke = t.accent;
                hotStrokeSet = true;
                break;
            case BOX:
                // A box of text that a remote scrolls, such as the log: the desktop's surface, radius 8, which has no
                // border under the pointer or with the focus there (the log's box never draws one). As any control that rests on
                // surface, its focus is surfaceHover.
                rest = t.surface;
                hot = t.surface;
                focused = t.surfaceHover;
                focusSet = true;
                break;
            default:
                // HERO, LINK, PLAIN: no fill.
                break;
        }
        if ((flags & F_BORDER) != 0) stroke = t.border;
        if (!hotStrokeSet) hotStroke = stroke;
        if (!focusSet) { focused = hot; focusStroke = hotStroke; }
        // A look that says its own fill for the focus but no border for it keeps the border it has under the pointer: the + chip's
        // fill goes to surfaceHover and its border stays.
        else if (focusStroke == 0) focusStroke = hotStroke;
        StateListDrawable states = new StateListDrawable();
        // The focus comes first: OK held on a focused control presses it, and it keeps its focus look.
        states.addState(new int[]{android.R.attr.state_focused}, box(focused, radius, focusStroke));
        if ((flags & F_HOLDS_FOCUS) != 0) states.addState(new int[]{android.R.attr.state_activated}, box(t.surface, radius, stroke));
        states.addState(new int[]{android.R.attr.state_pressed}, box(hot, radius, hotStroke));
        states.addState(new int[]{android.R.attr.state_hovered}, box(hot, radius, hotStroke));
        states.addState(new int[]{}, box(rest, radius, stroke));
        states.setEnterFadeDuration(FADE_MS);
        states.setExitFadeDuration(FADE_MS);
        return states;
    }
    private GradientDrawable box(int fill, float radiusDp, int stroke) {
        GradientDrawable d = shape(fill, radiusDp);
        if (stroke != 0) d.setStroke(Math.max(1, dp(1)), stroke);
        return d;
    }

    // The nav tiles grow to 1.08 while they have the focus, as the desktop's tiles do under the pointer.
    private void scaleOnFocus(View view) {
        view.setOnFocusChangeListener((v, hasFocus) -> v.animate().scaleX(hasFocus ? 1.08f : 1f).scaleY(hasFocus ? 1.08f : 1f)
            .setDuration(FADE_MS).setInterpolator(new DecelerateInterpolator(1.5f)).start());
    }

    /**
     * A row round controls that have no look of their own for the focus (a toggle: the desktop gives it no hover): while the
     * control has the focus the row is activated and rests on surface, as a config row does while one of its icons has it, and the
     * focus is a fill of the row, not a ring round the control. The row is not a focus stop itself.
     */
    void holdFocus(View row, View stop) {
        row.setBackground(background(Look.ROW, F_HOLDS_FOCUS));
        stop.setOnFocusChangeListener((v, hasFocus) -> row.setActivated(hasFocus));
    }
    /**
     * Where a row that fills with the focus goes in a page whose content is 18 in from each side: its fill reaches 10 past the
     * margin on the left and 6 on the right, and the row pads its content by the same (see {@link #FOCUS_ROW_LEFT_DP}), so that the
     * words and the control stay where the other rows' are.
     */
    LinearLayout.LayoutParams focusRowLp() {
        LinearLayout.LayoutParams p = lp(-1, -2);
        p.leftMargin = -dp(FOCUS_ROW_LEFT_DP);
        p.rightMargin = -dp(FOCUS_ROW_RIGHT_DP);
        return p;
    }
    /** How far a row that fills with the focus is padded: the config row's 10 and 6. */
    static final int FOCUS_ROW_LEFT_DP = 10;
    static final int FOCUS_ROW_RIGHT_DP = 6;

    // Controls.

    /**
     * The desktop's buttons: 32 high, radius 8, a label 14 and the label's width plus
     * 26, at least minWidth (88 in the editor, 76 in a dialog). None of them shows a
     * pressed look beyond its hover. Their words keep their colour in every state: accentText
     * on the accent, white on danger, the text colour on the others.
     */
    Button button(CharSequence caption, Kind kind, int minWidthDp, String key) {
        Button b = new Button(context);
        b.setAllCaps(false);
        b.setText(caption);
        b.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        b.setTypeface(Fonts.regular(context));
        b.setLetterSpacing(0);
        b.setStateListAnimator(null);
        b.setMinWidth(dp(minWidthDp));
        b.setMinimumWidth(dp(minWidthDp));
        b.setMinHeight(dp(32));
        b.setMinimumHeight(dp(32));
        b.setPadding(dp(13), 0, dp(13), 0);
        b.setGravity(Gravity.CENTER);
        b.setSingleLine(true);
        b.setEllipsize(TextUtils.TruncateAt.END);
        Look look;
        int flags = 0;
        switch (kind) {
            case PRIMARY:
                look = Look.PRIMARY;
                b.setTextColor(t.accentText);
                break;
            case DESTRUCTIVE:
                look = Look.DANGER;
                b.setTextColor(t.onDanger);
                break;
            case SECONDARY:
                look = Look.BUTTON;
                flags = F_SECONDARY;
                b.setTextColor(t.text);
                break;
            default:
                look = Look.BUTTON;
                b.setTextColor(t.text);
                break;
        }
        return key == null ? styled(b, look, flags) : focus(b, key, look, flags);
    }
    // A control that is no focus stop: its looks without a key.
    private <T extends View> T styled(T view, Look look, int flags) {
        view.setBackground(background(look, flags));
        return view;
    }

    /**
     * A link: words, wrap_content, so that it acts over them and a little around them. Accent, which goes to the text colour
     * and underlined under the pointer; a text link the other way about, and a faint one (the footer's) too; a danger link to
     * its lighter. The focus is the same: the colour it has under the pointer, underlined, and no fill.
     */
    TextView link(CharSequence value, float sp, LinkKind kind, String key) {
        LinkText v = new LinkText(context);
        v.setText(value);
        v.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        v.setGravity(Gravity.CENTER_VERTICAL);
        v.setSingleLine(true);
        v.setEllipsize(TextUtils.TruncateAt.END);
        v.setIncludeFontPadding(false);
        v.setPadding(dp(LINK_PAD_H_DP), dp(LINK_PAD_V_DP), dp(LINK_PAD_H_DP), dp(LINK_PAD_V_DP));
        switch (kind) {
            case DANGER:
                v.setTextColor(colors(t.danger, t.dangerLinkHover, t.dangerLinkHover));
                break;
            case TEXT:
                v.setTextColor(colors(t.text, t.accent, t.accent));
                break;
            case FAINT:
                v.setTextColor(colors(t.textFaint, t.accent, t.accent));
                break;
            default:
                v.setTextColor(colors(t.accent, t.text, t.text));
                break;
        }
        return key == null ? styled(v, Look.LINK, 0) : focus(v, key, Look.LINK, 0);
    }

    /** What a link is padded by, to be hit and to take its focus by: 6 at the sides and 4 above and below. */
    static final int LINK_PAD_H_DP = 6;
    static final int LINK_PAD_V_DP = 4;
    /**
     * A link's place in a row of a page, such as a header or the footer: the desktop's link is its words, and its hit area, which reaches
     * 4 round them, adds nothing to the row's height or length, so that the padding hangs outside the row, above and below. {@code last}
     * is the last control of the row, whose words end where the page's margin does.
     */
    LinearLayout.LayoutParams linkLp(boolean last) {
        LinearLayout.LayoutParams p = lp(-2, -2);
        p.topMargin = -dp(LINK_PAD_V_DP);
        p.bottomMargin = -dp(LINK_PAD_V_DP);
        if (last) p.rightMargin = -dp(LINK_PAD_H_DP);
        return p;
    }

    /**
     * A link that underlines while it is pressed, hovered or focused: the desktop's, which has the pointer for the first two. While it is
     * dormant (the update row while it checks or installs, which cannot be clicked) it has no pointer look at all, as the desktop's has
     * none: plain, in the colour it rests in, never underlined. A remote's focus, though, stays on it (the update row keeps it through
     * the check, and a focus that shows nothing is a focus lost), so the focus look stays: the third place the focus shows what the
     * pointer does not (see {@link Ui}).
     */
    static final class LinkText extends TextView {
        private ColorStateList colors;
        private boolean dormant;

        LinkText(Context c) {
            super(c);
            setTypeface(Fonts.regular(c));
        }

        @Override public void setTextColor(ColorStateList states) {
            colors = states;
            super.setTextColor(dormant && states != null ? withoutThePointer(states) : states);
        }

        /** The colours with the pressed and hovered ones gone: the colour at rest, and the focus's own. */
        private static ColorStateList withoutThePointer(ColorStateList states) {
            int rest = states.getColorForState(new int[0], states.getDefaultColor());
            int focused = states.getColorForState(new int[]{android.R.attr.state_focused}, rest);
            return new ColorStateList(new int[][]{{android.R.attr.state_focused}, {}}, new int[]{focused, rest});
        }

        void setDormant(boolean value) {
            if (dormant == value) return;
            dormant = value;
            if (colors != null) setTextColor(colors);
            // The underline follows the state again (drawableStateChanged).
            refreshDrawableState();
        }

        @Override protected void drawableStateChanged() {
            super.drawableStateChanged();
            boolean hot = false;
            for (int state : getDrawableState()) {
                if (state == android.R.attr.state_focused) hot = true;
                else if (!dormant && (state == android.R.attr.state_pressed || state == android.R.attr.state_hovered)) hot = true;
            }
            int flags = getPaintFlags();
            int next = hot ? flags | Paint.UNDERLINE_TEXT_FLAG : flags & ~Paint.UNDERLINE_TEXT_FLAG;
            if (next != flags) setPaintFlags(next);
        }
    }

    /**
     * The desktop's Dropdown: the label does nothing; only the value and its ▾ open the choice, and they are textDim and the
     * text colour under the pointer or with the focus, with no fill. The value and ▾ are as high as the row, as the desktop's are
     * (their popup opens under the row, not under the words), and are a {@link DropdownValue}.
     */
    LinearLayout dropdown(CharSequence name, CharSequence value, String key, View.OnClickListener open) {
        LinearLayout r = row();
        r.setMinimumHeight(dp(42));
        TextView title = label(name, 14, t.text);
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.END);
        r.addView(title, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        gapH(r, 8);
        LinearLayout group = new DropdownValue(context);
        group.setOrientation(LinearLayout.HORIZONTAL);
        group.setGravity(Gravity.CENTER_VERTICAL);
        group.setPadding(dp(DROPDOWN_PADDING_DP), 0, dp(DROPDOWN_PADDING_DP), 0);
        TextView shown = follow(label(value, 14, t.textDim), t.textDim, t.text, t.text);
        shown.setSingleLine(true);
        shown.setEllipsize(TextUtils.TruncateAt.END);
        shown.setMaxWidth(dp(112));
        group.addView(shown, lp(-2, -2));
        gapH(group, 4);
        LinearLayout.LayoutParams arrowLp = lp(-2, -2);
        arrowLp.topMargin = dp(1);
        group.addView(followGlyph(R.drawable.ic_dropdown, t.textDim, t.text, t.text), arrowLp);
        focus(group, key, Look.PLAIN).setOnClickListener(open);
        // The value and its ▾ end where the page's margin does, as the desktop's do: the control's own 6 of padding, which has no
        // fill to show, go beyond it.
        LinearLayout.LayoutParams groupLp = lp(-2, -1);
        groupLp.rightMargin = -dp(DROPDOWN_PADDING_DP);
        r.addView(group, groupLp);
        return r;
    }
    /** How far a dropdown's value is padded at each side, and so how far its control reaches past the page's margin. */
    static final int DROPDOWN_PADDING_DP = 6;
    /** A dropdown's value and its ▾. A popup for its choices opens under the row and ends where its content does (see MainActivity.anchorRect). */
    static final class DropdownValue extends LinearLayout {
        DropdownValue(Context c) { super(c); }
    }

    /**
     * A page's text input: 36 high, radius 8, 13. The desktop's field look. Its placeholder goes while it has the focus, as the
     * desktop's does (the new profile's name and the picker's search keep theirs: {@link #pageInput(CharSequence, String, boolean)}).
     */
    EditText pageInput(CharSequence hint, String key) { return pageInput(hint, key, false); }

    EditText pageInput(CharSequence hint, String key, boolean keepHint) {
        EditText e = new LineEdit(context);
        e.setSingleLine(true);
        e.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        e.setTextColor(t.text);
        e.setHintTextColor(t.textFaint);
        e.setHint(hint);
        e.setPadding(dp(12), 0, dp(12), 0);
        e.setGravity(Gravity.CENTER_VERTICAL);
        caret(e);
        if (!keepHint) hideHintWhileFocused(e);
        return focus(e, key, Look.FIELD);
    }

    /** A field's text cursor is one dp wide in the text colour, as the desktop's is, and its selection is the accent: nothing of the system's teal. */
    private void caret(EditText e) {
        e.setHighlightColor(Theme.withAlpha(t.accent, 0x66));
        if (Build.VERSION.SDK_INT >= 29) {
            GradientDrawable line = new GradientDrawable();
            line.setColor(t.text);
            line.setSize(Math.max(1, dp(1)), -1);
            e.setTextCursorDrawable(line);
        }
    }

    /** The desktop shows a field's placeholder only while it is empty and has not the focus: the hint goes with the focus and comes back with it. */
    static void hideHintWhileFocused(EditText e) {
        final CharSequence hint = e.getHint();
        final View.OnFocusChangeListener before = e.getOnFocusChangeListener();
        e.setOnFocusChangeListener((v, focused) -> {
            e.setHint(focused ? null : hint);
            if (before != null) before.onFocusChange(v, focused);
        });
        if (e.hasFocus()) e.setHint(null);
    }

    /** Field: a label, 13 in textDim, 4 above a 34 high box, radius 8, 14. */
    LinearLayout field(CharSequence name, EditText input, String key) {
        LinearLayout box = column();
        box.addView(label(name, 13, t.textDim), lp(-1, -2));
        gap(box, 4);
        input.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        input.setTextColor(t.text);
        input.setHintTextColor(t.textFaint);
        input.setPadding(dp(10), 0, dp(10), 0);
        input.setGravity(Gravity.CENTER_VERTICAL);
        caret(input);
        hideHintWhileFocused(input);
        focus(input, key, Look.FIELD);
        box.addView(input, lp(-1, 34));
        return box;
    }

    /**
     * The desktop's text area (the editor's certificate): a Field's label and look, and a box of several lines in monospace 12, this high,
     * whose text scrolls in it. The label line can have a link at its right ({@code beside}, which the desktop's "Load from file…" is),
     * and the label gives way to it.
     */
    LinearLayout fieldArea(CharSequence name, EditText input, String key, int heightDp) { return fieldArea(name, null, input, key, heightDp); }

    LinearLayout fieldArea(CharSequence name, View beside, EditText input, String key, int heightDp) {
        LinearLayout box = column();
        if (beside == null) {
            box.addView(label(name, 13, t.textDim), lp(-1, -2));
        } else {
            LinearLayout line = row();
            TextView caption = label(name, 13, t.textDim);
            caption.setSingleLine(true);
            caption.setEllipsize(TextUtils.TruncateAt.END);
            line.addView(caption, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
            // The link's own padding hangs outside the line, and its words end at the margin.
            line.addView(beside, linkLp(true));
            box.addView(line, lp(-1, -2));
        }
        gap(box, 4);
        input.setTypeface(Typeface.MONOSPACE);
        input.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        // As the desktop's text area wraps (TextEdit.WrapAnywhere): greedy, and with no hyphens. Set outright, not left to the theme's style,
        // since a layout that weighs the breaks of a whole paragraph or looks for hyphens costs most where a file brings a paragraph of
        // hundreds of thousands of characters with no line break in it. LineBreaker's constant is what API 35's annotation lists; it is a
        // compile-time int (0, as Layout.BREAK_STRATEGY_SIMPLE), so javac inlines it and LineBreaker, which is API 29, is not loaded on 26-28.
        input.setBreakStrategy(LineBreaker.BREAK_STRATEGY_SIMPLE);
        input.setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE);
        input.setTextColor(t.text);
        input.setHintTextColor(t.textFaint);
        input.setPadding(dp(10), dp(8), dp(10), dp(8));
        input.setGravity(Gravity.TOP | Gravity.START);
        caret(input);
        hideHintWhileFocused(input);
        focus(input, key, Look.FIELD);
        box.addView(input, lp(-1, heightDp));
        return box;
    }

    /**
     * The editor's select box (qml/Field.qml's look, CreateConfigOverlay's Protocol and Split profile): a label, 13 in textDim, over a
     * box 34 high, radius 8, on inputBg, with the value (14, the text colour) 10 in from the left and ▾ (textDim) 10 from the right. The
     * box is the control; its border turns accent under the pointer and with the focus, as a field's does. Where the
     * value is shown is put in {@code valueOut}, for the caller to change it when a choice is made.
     */
    LinearLayout selectField(CharSequence name, CharSequence value, String key, TextView[] valueOut, View.OnClickListener open) {
        LinearLayout field = column();
        field.addView(label(name, 13, t.textDim), lp(-1, -2));
        gap(field, 4);
        LinearLayout box = row();
        box.setPadding(dp(10), 0, dp(10), 0);
        TextView shown = label(value, 14, t.text);
        shown.setSingleLine(true);
        shown.setEllipsize(TextUtils.TruncateAt.END);
        box.addView(shown, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        box.addView(glyph(R.drawable.ic_dropdown, t.textDim), lp(-2, -2));
        focus(box, key, Look.SELECT).setOnClickListener(open);
        field.addView(box, lp(-1, 34));
        if (valueOut != null) valueOut[0] = shown;
        return field;
    }

    /**
     * The editor's toggle row: its label, 13 in textDim, and the toggle, 32 high. Like a page's (see {@link #toggleRow}), it
     * fills with the toggle's focus, and goes in its parent with {@link #focusRowLp}.
     */
    LinearLayout editorToggleRow(CharSequence title, ToggleView toggle) {
        LinearLayout r = row();
        r.setMinimumHeight(dp(32));
        r.setPadding(dp(FOCUS_ROW_LEFT_DP), 0, dp(FOCUS_ROW_RIGHT_DP), 0);
        r.addView(label(title, 13, t.textDim), new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        gapH(r, 8);
        r.addView(toggle, lp(-2, -2));
        holdToggle(r, toggle);
        return r;
    }

    /**
     * A card's ←, 14.6 by 12.7 (Inter's, at the desktop's 20 px) in textDim and the text colour under the pointer and with the focus,
     * in a cell 6 larger all round (the desktop's arrow is 6 short of its hit area at every side).
     */
    ImageView backArrow(String key, CharSequence description, View.OnClickListener back) {
        ImageView arrow = ownGlyph(R.drawable.ic_back, t.textDim, t.text, t.text);
        arrow.setPadding(dp(6), dp(6), dp(6), dp(6));
        arrow.setContentDescription(description);
        arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
        arrow.setOnClickListener(back);
        return focus(arrow, key, Look.PLAIN);
    }

    /**
     * A card's header, 48 high: the ← 14 from the left, and 12 on from it the title, 15 in
     * Medium. The title is the header's second child, for a card that changes it. Those are the desktop's: the arrow's Text starts
     * 14 in and is its advance, 19.1, wide (Inter's ← at 20 px, whose ink is 2 in from the left of it), and the title starts 12 on.
     */
    LinearLayout cardHeader(CharSequence title, String backKey, CharSequence backDescription, View.OnClickListener back) {
        LinearLayout bar = row();
        bar.setMinimumHeight(dp(48));
        // The arrow's own 6 of padding and the ink's 2 in the Text make its left edge 16 in, as the desktop's is.
        bar.setPadding(dp(10), 0, dp(14), 0);
        bar.addView(backArrow(backKey, backDescription, back), lp(-2, -2));
        TextView name = medium(title, 15, t.text);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        // The Text's 19.1 end 2.5 after the ink, and the title is 12 on from there: 8.5 and the arrow's own 6 of padding.
        p.leftMargin = dp(8.5f);
        bar.addView(name, p);
        return bar;
    }

    /** A button of the header, 40 by 32, radius 8, with an icon of this size, accent in every state. */
    FrameLayout iconButton(int drawable, int sizeDp, CharSequence description, String key) {
        FrameLayout box = new FrameLayout(context);
        box.setContentDescription(description);
        ImageView icon = tinted(new ImageView(context), t.accent);
        icon.setImageResource(drawable);
        box.addView(icon, new FrameLayout.LayoutParams(dp(sizeDp), dp(sizeDp), Gravity.CENTER));
        return focus(box, key, Look.PILL);
    }

    /** A small icon button, such as Home's + (22 by 22, radius 6), with an icon of this size, accent in every state. */
    FrameLayout smallIconButton(int drawable, int iconDp, CharSequence description, String key) {
        FrameLayout box = new FrameLayout(context);
        box.setContentDescription(description);
        ImageView icon = tinted(new ImageView(context), t.accent);
        icon.setImageResource(drawable);
        box.addView(icon, new FrameLayout.LayoutParams(dp(iconDp), dp(iconDp), Gravity.CENTER));
        return focus(box, key, Look.ICON);
    }

    /**
     * A chip, 28 high, radius 13, 13, 11 in from the left; selectable ones are focus stops.
     * It is as wide as its label, or fills its row, with the label taking the room and the
     * ChipX at the end, if {@code fill}. The selected one is the accent with accentText and no
     * mark, whatever drives the screen.
     */
    LinearLayout chip(CharSequence value, boolean selected, boolean selectable, String key, boolean fill) {
        LinearLayout c = new ChipRow(context);
        c.setOrientation(LinearLayout.HORIZONTAL);
        c.setGravity(Gravity.CENTER_VERTICAL);
        c.setPadding(dp(11), 0, dp(11), 0);
        TextView name = label(value, 13, selected ? t.accentText : t.text);
        name.setSingleLine(true);
        name.setEllipsize(TextUtils.TruncateAt.END);
        if (selectable) focus(c, key, Look.CHIP, selected ? F_CURRENT : 0);
        else c.setBackground(shape(selected ? t.accent : t.surface, 13));
        c.addView(name, fill ? new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1) : lp(-2, -1));
        c.setMinimumHeight(dp(28));
        return c;
    }
    /**
     * A chip's row. Its label gives way to the ChipX at its end: a name too long for the row is cut with an ellipsis, as the
     * desktop cuts it (the label is no wider than the chip less 39), and the ChipX is still there to be pressed. A plain
     * LinearLayout gives the first child all the room it asks for and the last what is left, which may be none. The ChipX sits 8
     * into the chip's padding, which a row clips its children to unless told not to.
     */
    static final class ChipRow extends LinearLayout {
        ChipRow(Context context) {
            super(context);
            setClipToPadding(false);
        }

        @Override protected void measureChildWithMargins(View child, int parentWidthMeasureSpec, int widthUsed,
                                                         int parentHeightMeasureSpec, int heightUsed) {
            int count = getChildCount();
            if (count > 1 && child == getChildAt(0) && getChildAt(count - 1).getLayoutParams() instanceof MarginLayoutParams) {
                MarginLayoutParams x = (MarginLayoutParams) getChildAt(count - 1).getLayoutParams();
                widthUsed += Math.max(0, x.width) + x.leftMargin + x.rightMargin;
            }
            super.measureChildWithMargins(child, parentWidthMeasureSpec, widthUsed, parentHeightMeasureSpec, heightUsed);
        }
    }

    /**
     * A remote reaches a ChipX from its chip with Right, and the chip from it with Left: it is inside the chip, where Android's
     * search for the next control to the right finds none.
     */
    void linkChipX(View chip, View x) {
        if (chip.getId() == View.NO_ID) chip.setId(View.generateViewId());
        if (x.getId() == View.NO_ID) x.setId(View.generateViewId());
        chip.setNextFocusRightId(x.getId());
        x.setNextFocusLeftId(chip.getId());
    }

    /**
     * The + chip that makes a new one (Split's profiles): 34 by 28, radius 13, on surface with a border, a + of 17 in accent.
     * With the focus it is surfaceHover, as a chip is under the pointer, and keeps its border.
     */
    TextView plusChip(CharSequence description, String key) {
        TextView v = label("+", 17, t.accent);
        v.setGravity(Gravity.CENTER);
        v.setIncludeFontPadding(false);
        v.setMinWidth(dp(34));
        v.setMinimumWidth(dp(34));
        v.setMinHeight(dp(28));
        v.setMinimumHeight(dp(28));
        v.setContentDescription(description);
        return focus(v, key, Look.CHIP, F_BORDER);
    }

    /**
     * ChipX: the close icon, 14 in a cell 22 wide, textDim (accentText on an accent chip), danger under the pointer and with the
     * focus. A chip is 28 high, and its ChipX sits 3 from the label.
     */
    ImageView chipX(String key, boolean overAccent) {
        ImageView x = new ImageView(context);
        x.setImageResource(R.drawable.ic_close);
        x.setPadding(dp(4), dp(4), dp(4), dp(4));
        x.setImageTintList(colors(overAccent ? t.accentText : t.textDim, t.danger, t.danger));
        return focus(x, key, Look.PLAIN);
    }
    /** Where a ChipX sits in its chip: 3 from the label, and the chip's 11 of padding less 8 beyond it. */
    LinearLayout.LayoutParams chipXLp() {
        LinearLayout.LayoutParams p = lp(22, 22);
        p.leftMargin = dp(3);
        p.rightMargin = dp(-8);
        return p;
    }

    /** NoticeBox: infoBg, radius 8, 12 in warn, wrapped, 10 in from the sides and 8 above and below. */
    TextView notice(CharSequence value) {
        TextView v = label(value, 12, t.warn);
        v.setGravity(Gravity.START);
        v.setBackground(shape(t.infoBg, 8));
        v.setPadding(dp(10), dp(8), dp(10), dp(8));
        return v;
    }
    /** The warning that credentials cannot be stored: amber, a border, radius 8, 13, 10 round. */
    TextView banner(CharSequence value) {
        TextView v = label(value, 13, t.text);
        v.setGravity(Gravity.START);
        GradientDrawable d = shape(t.bannerFill, 8);
        d.setStroke(Math.max(1, dp(1)), t.bannerBorder);
        v.setBackground(d);
        v.setPadding(dp(10), dp(10), dp(10), dp(10));
        return v;
    }

    /**
     * The desktop's Toggle with a row's label: 42 high, more if a subtitle wraps. The toggle has no look for the focus, as the
     * desktop's has no hover; the row fills with surface while it has it, as a config row does (the desktop's idiom for a row), its
     * radius 8. Its content is padded by 10 and 6, and it goes in its parent with {@link #focusRowLp}, so that the label and
     * the track are where the other rows' are and the fill reaches a little past them.
     */
    LinearLayout toggleRow(CharSequence title, CharSequence subtitle, ToggleView toggle) {
        LinearLayout r = row();
        r.setMinimumHeight(dp(42));
        r.setPadding(dp(FOCUS_ROW_LEFT_DP), 0, dp(FOCUS_ROW_RIGHT_DP), 0);
        LinearLayout copy = column();
        copy.addView(label(title, 14, t.text), lp(-1, -2));
        if (subtitle != null && subtitle.length() > 0) copy.addView(label(subtitle, 12, t.textFaint), lp(-1, -2));
        r.addView(copy, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        gapH(r, 8);
        r.addView(toggle, lp(-2, -2));
        holdToggle(r, toggle);
        return r;
    }
    /**
     * A toggle with a label before it, as one small row at the right of a line, such as Logs' Auto-scroll: only as wide as it
     * is, with the same fill round it, so that the focus is never across empty width. Goes in its line with
     * {@link #focusRowLp}'s margin on the right alone.
     */
    LinearLayout toggleGroup(CharSequence title, float sp, int color, ToggleView toggle) {
        LinearLayout r = row();
        r.setMinimumHeight(dp(28));
        r.setPadding(dp(FOCUS_ROW_LEFT_DP), 0, dp(FOCUS_ROW_RIGHT_DP), 0);
        r.addView(label(title, sp, color), lp(-2, -2));
        gapH(r, 8);
        r.addView(toggle, lp(-2, -2));
        holdToggle(r, toggle);
        return r;
    }
    /**
     * The row fills with the toggle's focus. The track is 22 high, which is not a thing to hit with a finger: a touch over the
     * row's height, and a few dp either side of the track, is the track's, as the invisible margin round the old one made it.
     */
    private void holdToggle(LinearLayout row, ToggleView toggle) {
        holdFocus(row, toggle);
        row.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            Rect hit = new Rect();
            toggle.getHitRect(hit);
            hit.top = 0;
            hit.bottom = bottom - top;
            hit.left -= dp(8);
            hit.right += dp(FOCUS_ROW_RIGHT_DP);
            row.setTouchDelegate(new TouchDelegate(hit, toggle));
        });
    }
    /** A toggle: 38 by 22 (34 by 20 small), as a focus stop keyed {@code key}; its row shows the focus (see {@link #toggleRow}). */
    ToggleView toggle(boolean checked, boolean small, String key) {
        ToggleView v = new ToggleView(context, t, small);
        v.setChecked(checked);
        return focus(v, key, Look.PLAIN);
    }

    // Home.

    /**
     * Home's config selector: the active config's name, 15 in Medium and elided at the right to the room
     * there is, and 6 later the ▾, as one control that is as wide as they are and 24 high, as the desktop's is. Both are the text colour, and
     * accent under the pointer and with the focus, with the name underlined. Without configs it is "Add a config"
     * alone, with no ▾.
     */
    LinearLayout selector(CharSequence name, boolean arrow, int maxNameWidthPx, String key) {
        LinearLayout box = row();
        box.setMinimumHeight(dp(24));
        LinkText label = new LinkText(context);
        label.setText(name);
        label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        label.setTypeface(Fonts.medium(context));
        label.setGravity(Gravity.CENTER_VERTICAL);
        label.setIncludeFontPadding(false);
        label.setSingleLine(true);
        label.setEllipsize(TextUtils.TruncateAt.END);
        if (maxNameWidthPx > 0) label.setMaxWidth(maxNameWidthPx);
        label.setTextColor(colors(t.text, t.accent, t.accent));
        label.setDuplicateParentStateEnabled(true);
        box.addView(label, lp(-2, -2));
        if (arrow) {
            LinearLayout.LayoutParams p = lp(-2, -2);
            p.leftMargin = dp(SELECTOR_GAP_DP);
            p.topMargin = dp(1);
            box.addView(followGlyph(R.drawable.ic_dropdown, t.text, t.accent, t.accent), p);
        }
        return focus(box, key, Look.LINK);
    }
    /** The space between the selector's name and its ▾, and between the selector and the +. */
    static final int SELECTOR_GAP_DP = 6;
    /** The width of the ▾ that every dropdown has (res/drawable/ic_dropdown.xml). */
    static final int DROPDOWN_ARROW_DP = 8;

    /** One of the two speed tiles at the foot of Home: 116 by 44, radius 8, on the tile colour. Not a control. */
    LinearLayout speedTile(int arrowDrawable, int arrowColor, TextView[] valueOut) {
        LinearLayout tile = row();
        tile.setGravity(Gravity.CENTER);
        tile.setBackground(shape(t.tile, 8));
        // The desktop's Row is the arrow's Text, 5, and the number, centred. The Text is the arrow's advance wide: 11.7 at 14 px, the
        // ink of Inter's ↓ and ↑ (8.9) with 1.4 of side bearing at each end, so 1.4 before the ink and 6.4 after it.
        LinearLayout.LayoutParams a = lp(-2, -2);
        a.leftMargin = dp(1.4f);
        tile.addView(glyph(arrowDrawable, arrowColor), a);
        TextView value = medium("", 15, t.text);
        LinearLayout.LayoutParams p = lp(-2, -2);
        p.leftMargin = dp(6.4f);
        tile.addView(value, p);
        valueOut[0] = value;
        return tile;
    }

    // The Configs page's rows.

    /**
     * A pill of text on infoBg: radius 10, 20 high, 8 in from each side, 11 in Medium. A config row's
     * badge says connected (in success) or connecting… (in textDim). It is the same on a row that is filled, as the
     * desktop's is under the pointer.
     */
    TextView badge(CharSequence value, int color) {
        TextView v = medium(value, 11, color);
        v.setGravity(Gravity.CENTER);
        v.setPadding(dp(8), 0, dp(8), 0);
        v.setSingleLine(true);
        v.setBackground(shape(t.infoBg, 10));
        return v;
    }

    /**
     * A config row's icon: a cell 26 wide and as high as it is given, the icon centred in it, textDim, and the
     * text colour (danger for the close icon) under the pointer and with the focus. The cell has no fill: it is the row that
     * fills, as the desktop's does under the pointer (ConfigRowView activates it while an icon of it has the focus).
     */
    FrameLayout rowIcon(int drawable, int iconDp, CharSequence description, String key, boolean close) {
        FrameLayout cell = new FrameLayout(context);
        cell.setContentDescription(description);
        ImageView icon = new ImageView(context);
        icon.setImageResource(drawable);
        icon.setImageTintList(colors(t.textDim, close ? t.danger : t.text, close ? t.danger : t.text));
        icon.setDuplicateParentStateEnabled(true);
        cell.addView(icon, new FrameLayout.LayoutParams(dp(iconDp), dp(iconDp), Gravity.CENTER));
        return focus(cell, key, Look.PLAIN);
    }
}
