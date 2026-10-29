package org.freetunnel.android;

import android.content.Context;
import android.view.View;
import android.widget.ScrollView;

/**
 * A scroll view that is as tall as what it holds, up to a limit, and scrolls beyond it:
 * the lists of a popup (at most 8 rows, or 3 on Home) and the message of a confirmation.
 * It is no focus stop itself; its children are.
 */
final class CappedScrollView extends ScrollView {
    private int maxHeight = Integer.MAX_VALUE;

    CappedScrollView(Context context) {
        super(context);
        setVerticalScrollBarEnabled(false);
        setOverScrollMode(OVER_SCROLL_NEVER);
        setFocusable(false);
    }

    void setMaxHeight(int px) { maxHeight = px; }

    @Override protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int mode = View.MeasureSpec.getMode(heightMeasureSpec);
        if (mode == View.MeasureSpec.EXACTLY) {
            super.onMeasure(widthMeasureSpec, heightMeasureSpec);
            return;
        }
        int limit = mode == View.MeasureSpec.AT_MOST ? Math.min(View.MeasureSpec.getSize(heightMeasureSpec), maxHeight) : maxHeight;
        super.onMeasure(widthMeasureSpec, View.MeasureSpec.makeMeasureSpec(limit, View.MeasureSpec.AT_MOST));
    }
}
