package org.freetunnel.android;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;
import android.view.animation.LinearInterpolator;

/**
 * Three dots that hop one after another (qml/components/BusyDots.qml): waiting on an answer, with nothing yet to show for it. The
 * Maintenance row shows them while it checks; the turning arrow is for a download under way. They sit in a box 20 by 20 as the
 * arrow does, 2 below its middle. {@link UpdateMotion#dotLift} says how high each is at a time; this draws them and keeps time,
 * while it is on the screen.
 */
final class BusyDotsView extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final float density;
    private ValueAnimator clock;
    private long millis;

    BusyDotsView(Context context, int color) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        paint.setColor(color);
        setFocusable(false);
    }

    void setColor(int color) { paint.setColor(color); invalidate(); }

    /** How far into its round the dots are, for a test or a screenshot to hold them still. */
    long millis() { return millis; }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        update();
    }

    @Override protected void onDetachedFromWindow() {
        if (clock != null) { clock.cancel(); clock = null; }
        super.onDetachedFromWindow();
    }

    @Override protected void onVisibilityChanged(View changed, int visibility) {
        super.onVisibilityChanged(changed, visibility);
        if (isAttachedToWindow()) update();
    }

    private void update() {
        boolean run = isShown();
        if (run && clock == null) {
            clock = ValueAnimator.ofInt(0, UpdateMotion.ROUND_MS);
            clock.setDuration(UpdateMotion.ROUND_MS);
            clock.setInterpolator(new LinearInterpolator());
            clock.setRepeatCount(ValueAnimator.INFINITE);
            clock.addUpdateListener(a -> { millis = (Integer) a.getAnimatedValue(); invalidate(); });
            clock.start();
        } else if (!run && clock != null) {
            clock.cancel();
            clock = null;
        }
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth() / density;
        float h = getHeight() / density;
        float row = 3 * UpdateMotion.DOT + 2 * UpdateMotion.DOT;
        float left = (w - row) / 2;
        // The row's middle is 2 below the box's.
        float centerY = h / 2 + 2;
        canvas.save();
        canvas.scale(density, density);
        for (int i = 0; i < 3; i++) {
            float cx = left + UpdateMotion.DOT / 2 + i * 2 * UpdateMotion.DOT;
            canvas.drawCircle(cx, centerY - UpdateMotion.dotLift(i, millis), UpdateMotion.DOT / 2, paint);
        }
        canvas.restore();
    }
}
