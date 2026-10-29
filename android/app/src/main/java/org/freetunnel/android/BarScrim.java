package org.freetunnel.android;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;

/**
 * What the status and navigation bars show while a card or a question dims the window: the page's colour as the backdrops give it, in
 * the strips the bars are over. The desktop dims the whole of its window, and from Android 15 on (the app targeting it) so is the
 * phone's: the window is edge to edge, the bars are drawn over it, and their colours cannot be set. The backdrops are the window's
 * layers, and a strip under a bar stayed the page's colour and not the dim of it, a bright band across the top of a dimmed screen. So
 * the strips are painted here, opaque and in the colour a page has under that many backdrops (MainActivity.dimmedPage), on the decor's
 * overlay, which is drawn after every view of the window, the system's colour views for the bars included: whatever else covers the
 * strip, or leaves it alone, it is this colour, once and not twice.
 */
final class BarScrim extends Drawable {
    private final Paint paint = new Paint();
    private final Rect insets = new Rect();

    /** The colour of the strips, opaque. */
    void setColor(int color) {
        if (paint.getColor() == color) return;
        paint.setColor(color);
        invalidateSelf();
    }

    /** How deep the bars are at each edge of the window, which the drawable's bounds are. */
    void setInsets(Rect bars) {
        if (insets.equals(bars)) return;
        insets.set(bars);
        invalidateSelf();
    }

    /** How deep the strips are now, for a test to read. */
    Rect insets() { return new Rect(insets); }

    @Override public void draw(Canvas canvas) {
        Rect b = getBounds();
        if (insets.top > 0) canvas.drawRect(b.left, b.top, b.right, Math.min(b.bottom, b.top + insets.top), paint);
        if (insets.bottom > 0) canvas.drawRect(b.left, Math.max(b.top, b.bottom - insets.bottom), b.right, b.bottom, paint);
        if (insets.left > 0) canvas.drawRect(b.left, b.top, Math.min(b.right, b.left + insets.left), b.bottom, paint);
        if (insets.right > 0) canvas.drawRect(Math.max(b.left, b.right - insets.right), b.top, b.right, b.bottom, paint);
    }

    @Override public void setAlpha(int alpha) { }
    @Override public void setColorFilter(ColorFilter filter) { }
    @SuppressWarnings("deprecation") // abstract on every release the app runs on; deprecated from 29.
    @Override public int getOpacity() { return PixelFormat.OPAQUE; }
}
