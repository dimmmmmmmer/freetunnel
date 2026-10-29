package org.freetunnel.android;

import android.content.Context;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.text.TextUtils;

/**
 * A row of the Configs page, as the desktop draws it (qml/pages/ConfigsPage.qml): 56 from one row to the next, the
 * row itself 48 high with 4 above and below, radius 8, on the page and on surface under the pointer, with the focus, or while
 * one of its icons has the focus (and then the icon alone changes its colour, as the desktop's does under the pointer). In it, 10 from the left and 6 from the right with 8 between: the logo (22, whole for the
 * config that is selected and at 0.4 for the others), its name (14, in Medium when it is the selected one, elided), the
 * ping (12 in textDim) when there is one, a badge while the selected config is connected or connecting, and the icons for
 * export (17), edit (16) and close (16), each in a cell 26 wide.
 *
 * <p>The row is the control that selects its config (key config:N:select). The icons are controls of their own:
 * config:N:export, config:N:edit and config:N:remove. A remote reaches them with Right from the row, because
 * they sit inside it, where Android's search for the next control to the right finds none; Left from the
 * export icon is back to the row, and the others find their way by their places.
 */
final class ConfigRowView extends LinearLayout {
    /** From one row to the next, and the row's own height. */
    static final int PITCH_DP = 56;
    static final int HEIGHT_DP = 48;

    final int index;
    final FrameLayout exportIcon;
    final FrameLayout editIcon;
    final FrameLayout removeIcon;

    /**
     * @param ping what the row says of the server's answer, or null for nothing
     * @param badge "connected" or "connecting…", or null for none
     * @param connected whether the badge is the connected one
     */
    ConfigRowView(Context context, Ui ui, int index, CharSequence name, boolean active, CharSequence ping,
                  CharSequence badge, boolean connected, CharSequence exportLabel, CharSequence editLabel, CharSequence removeLabel) {
        super(context);
        this.index = index;
        Theme t = ui.t();
        setOrientation(HORIZONTAL);
        setGravity(Gravity.CENTER_VERTICAL);
        setPadding(ui.dp(10), 0, ui.dp(6), 0);
        // A row of the list is no raised card: while it is dragged it is drawn above the others, and casts nothing.
        setOutlineProvider(null);
        ui.focus(this, "config:" + index + ":select", Ui.Look.ROW, Ui.F_HOLDS_FOCUS);

        ImageView logo = new ImageView(context);
        logo.setImageResource(R.drawable.ic_logo);
        logo.setAlpha(active ? 1f : 0.4f);
        logo.setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        addView(logo, ui.lp(22, 22));

        TextView label = active ? ui.medium(name, 14, t.text) : ui.label(name, 14, t.text);
        label.setSingleLine(true);
        label.setEllipsize(TextUtils.TruncateAt.END);
        LayoutParams labelLp = new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1);
        labelLp.leftMargin = ui.dp(8);
        addView(label, labelLp);

        if (ping != null && ping.length() > 0) {
            TextView answer = ui.label(ping, 12, t.textDim);
            answer.setSingleLine(true);
            LayoutParams p = ui.lp(-2, -2);
            p.leftMargin = ui.dp(8);
            addView(answer, p);
        }
        if (badge != null) {
            LayoutParams p = ui.lp(-2, 20);
            p.leftMargin = ui.dp(8);
            addView(ui.badge(badge, connected ? t.success : t.textDim), p);
        }

        exportIcon = ui.rowIcon(R.drawable.ic_export, 17, exportLabel, "config:" + index + ":export", false);
        editIcon = ui.rowIcon(R.drawable.ic_edit, 16, editLabel, "config:" + index + ":edit", false);
        removeIcon = ui.rowIcon(R.drawable.ic_close, 16, removeLabel, "config:" + index + ":remove", true);
        FrameLayout[] icons = {exportIcon, editIcon, removeIcon};
        for (int i = 0; i < icons.length; i++) {
            LayoutParams p = new LayoutParams(ui.dp(26), ui.dp(HEIGHT_DP - 8));
            if (i == 0) p.leftMargin = ui.dp(8);
            addView(icons[i], p);
            // The row rests on surface while an icon of it has the focus, as the desktop's row does under the pointer.
            icons[i].setOnFocusChangeListener((v, hasFocus) -> setActivated(hasFocus));
        }
        // Where Android's own search finds nothing: the icons are inside the row, not beside it.
        int exportId = View.generateViewId();
        exportIcon.setId(exportId);
        int rowId = View.generateViewId();
        setId(rowId);
        setNextFocusRightId(exportId);
        exportIcon.setNextFocusLeftId(rowId);
    }

    /** The row's place in a list of rows: 4 above and below it, which makes the pitch 56. */
    static LinearLayout.LayoutParams params(Ui ui) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ui.dp(HEIGHT_DP));
        p.topMargin = ui.dp(4);
        p.bottomMargin = ui.dp(4);
        return p;
    }
}
