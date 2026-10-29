package org.freetunnel.android;

import android.view.MotionEvent;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.widget.LinearLayout;

/**
 * Reordering the config rows on a phone: press and hold a row, then drag it up or down (the desktop drags by the
 * row body, which a touch screen cannot, because a drag is a scroll there). The dragged row follows the finger, on
 * surface and above the others, and the others slide in 130 ms, easing out, to open the gap at the slot it is over. When
 * the finger lifts, {@link Moved} is told where it went from and to. The page does not move the row itself: it
 * changes the list and builds the rows again.
 *
 * <p>A TV has no finger, and the desktop has no way to move a row with keys, so it attaches none.
 */
final class RowDrag {
    interface Moved { void moved(int from, int to); }

    private static final int SLIDE_MS = 130;

    private final LinearLayout list;
    private final int pitch;
    private final float lift;
    private final Moved moved;
    private ConfigRowView dragged;
    private int from;
    private int to;
    private float downY;

    /**
     * @param list the rows, in order, one after another, and nothing else
     * @param pitchPx the distance from one row to the next
     */
    RowDrag(LinearLayout list, int pitchPx, float liftPx, Moved moved) {
        this.list = list;
        this.pitch = pitchPx;
        this.lift = liftPx;
        this.moved = moved;
    }

    void attach(ConfigRowView row) {
        row.setOnTouchListener((v, event) -> touched(row, event));
        row.setOnLongClickListener(v -> start(row));
    }

    private boolean start(ConfigRowView row) {
        if (dragged != null) return false;
        dragged = row;
        from = row.index;
        to = from;
        // What scrolls the list is up the chain of parents, and is told not to take the touch from the row.
        list.requestDisallowInterceptTouchEvent(true);
        row.setActivated(true);
        row.setTranslationZ(lift);
        return true;
    }

    private boolean touched(ConfigRowView row, MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) { downY = event.getRawY(); return false; }
        if (dragged != row) return false;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_MOVE: {
                float dy = event.getRawY() - downY;
                row.setTranslationY(dy);
                int over = Reorder.slotOver(from, dy, pitch, list.getChildCount());
                if (over != to) {
                    to = over;
                    for (int i = 0; i < list.getChildCount(); i++) {
                        View other = list.getChildAt(i);
                        if (other == row) continue;
                        other.animate().translationY(Reorder.shift(i, from, to) * pitch).setDuration(SLIDE_MS)
                            .setInterpolator(new DecelerateInterpolator(1.5f)).start();
                    }
                }
                return true;
            }
            case MotionEvent.ACTION_UP: {
                int fromSlot = from;
                int toSlot = to;
                finish();
                if (fromSlot != toSlot) moved.moved(fromSlot, toSlot);
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                finish();
                return true;
            default:
                return true;
        }
    }

    // Everything back where the list has it, without a slide: the page is about to be built again, or the touch is over.
    private void finish() {
        ConfigRowView row = dragged;
        dragged = null;
        list.requestDisallowInterceptTouchEvent(false);
        if (row != null) {
            row.setActivated(false);
            row.setTranslationZ(0);
        }
        for (int i = 0; i < list.getChildCount(); i++) {
            View child = list.getChildAt(i);
            child.animate().cancel();
            child.setTranslationY(0);
        }
    }
}
