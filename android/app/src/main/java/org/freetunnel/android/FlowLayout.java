package org.freetunnel.android;

import android.content.Context;
import android.view.View;
import android.view.ViewGroup;

/**
 * Children side by side, wrapping to a new row where the next does not fit: the desktop's
 * Flow, which the chips of Split and Settings sit in. The space between children and
 * between rows is the same, 6 on the desktop. A child wider than a row gets a row to itself,
 * as wide as the row.
 */
final class FlowLayout extends ViewGroup {
    private final int spacing;

    FlowLayout(Context context, int spacingPx) {
        super(context);
        this.spacing = spacingPx;
    }

    @Override protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int available = Math.max(0, MeasureSpec.getSize(widthMeasureSpec) - getPaddingLeft() - getPaddingRight());
        int x = 0;
        int rowHeight = 0;
        int height = 0;
        int widest = 0;
        boolean any = false;
        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            // As wide as it wants (or as its own size says), and no wider than a row.
            LayoutParams params = child.getLayoutParams();
            child.measure(getChildMeasureSpec(MeasureSpec.makeMeasureSpec(available, MeasureSpec.AT_MOST), 0, params.width),
                getChildMeasureSpec(MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED), 0, params.height));
            int w = child.getMeasuredWidth();
            int h = child.getMeasuredHeight();
            if (x > 0 && x + spacing + w > available) {
                height += rowHeight + spacing;
                x = 0;
                rowHeight = 0;
            }
            x += (x > 0 ? spacing : 0) + w;
            rowHeight = Math.max(rowHeight, h);
            widest = Math.max(widest, x);
            any = true;
        }
        if (any) height += rowHeight;
        int width = MeasureSpec.getMode(widthMeasureSpec) == MeasureSpec.EXACTLY
            ? MeasureSpec.getSize(widthMeasureSpec) : widest + getPaddingLeft() + getPaddingRight();
        setMeasuredDimension(resolveSize(width, widthMeasureSpec), resolveSize(height + getPaddingTop() + getPaddingBottom(), heightMeasureSpec));
    }

    @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        int available = Math.max(0, right - left - getPaddingLeft() - getPaddingRight());
        int x = 0;
        int y = getPaddingTop();
        int rowHeight = 0;
        for (int i = 0; i < getChildCount(); i++) {
            View child = getChildAt(i);
            if (child.getVisibility() == GONE) continue;
            int w = child.getMeasuredWidth();
            int h = child.getMeasuredHeight();
            if (x > 0 && x + spacing + w > available) {
                y += rowHeight + spacing;
                x = 0;
                rowHeight = 0;
            }
            if (x > 0) x += spacing;
            child.layout(getPaddingLeft() + x, y, getPaddingLeft() + x + w, y + h);
            x += w;
            rowHeight = Math.max(rowHeight, h);
        }
    }
}
