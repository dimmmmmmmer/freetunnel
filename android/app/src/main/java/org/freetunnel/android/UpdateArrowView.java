package org.freetunnel.android;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.LinearInterpolator;

/**
 * The Maintenance row's arrow (qml/components/UpdateArrow.qml), 20 by 20, drawn so that it can change shape. Straight it is ↓,
 * "download"; bent round clockwise it is ↻, the arrow of the work that follows, and while that work runs the ↻ turns. In between,
 * the shaft bends and its head sweeps down, left, up and round: the arrow turns into the other one rather than being swapped
 * for it. {@link UpdateMotion#arrow} has the shape; this draws it, 1.4 wide with round ends, in the colour it is given, and
 * moves it: the bend over 320 ms, a lean (the turn) over 160, and the spin, one turn in 1100 ms, which on stopping comes round to
 * where it started.
 *
 * <p>The desktop bends the arrow under the pointer; here a remote's focus on the row does the same (the row calls
 * {@link #setBend}), and a retry arrow leans 45 degrees the way it goes. Nothing runs while the view is off the screen.
 */
final class UpdateArrowView extends View {
    static final int SIZE_DP = 20;
    private static final float STROKE_DP = 1.4f;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path shaft = new Path();
    private final Path head = new Path();
    private final float density;
    private float bend;
    private float turn;
    private float spinAngle;
    private boolean spinning;
    private ValueAnimator bendAnimator;
    private ValueAnimator turnAnimator;
    private ValueAnimator spin;
    private ValueAnimator settle;
    private UpdateMotion.Arrow arrow;

    UpdateArrowView(Context context, int color) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeCap(Paint.Cap.ROUND);
        paint.setStrokeJoin(Paint.Join.ROUND);
        // In dp: onDraw scales the canvas by the density, and a width in pixels would be scaled again.
        paint.setStrokeWidth(STROKE_DP);
        paint.setColor(color);
        setFocusable(false);
        rebuild();
    }

    void setColor(int color) { paint.setColor(color); invalidate(); }

    float bend() { return bend; }
    float turn() { return turn; }
    float spinAngle() { return spinAngle; }
    boolean spinning() { return spinning; }

    /** 0 is ↓ and 1 is ↻; with {@code animate} it goes there over 320 ms, as the desktop's arrow does. */
    void setBend(float value, boolean animate) {
        if (bendAnimator != null) bendAnimator.cancel();
        if (!animate || !isShown()) { bend = value; rebuild(); invalidate(); return; }
        bendAnimator = ValueAnimator.ofFloat(bend, value);
        bendAnimator.setDuration(320);
        bendAnimator.setInterpolator(new DecelerateInterpolator(1.5f));
        bendAnimator.addUpdateListener(a -> { bend = (Float) a.getAnimatedValue(); rebuild(); invalidate(); });
        bendAnimator.start();
    }

    /** Leans the arrow this many degrees clockwise, over 160 ms. */
    void setTurn(float degrees, boolean animate) {
        if (turnAnimator != null) turnAnimator.cancel();
        if (!animate || !isShown()) { turn = degrees; invalidate(); return; }
        turnAnimator = ValueAnimator.ofFloat(turn, degrees);
        turnAnimator.setDuration(160);
        turnAnimator.setInterpolator(new DecelerateInterpolator(1.5f));
        turnAnimator.addUpdateListener(a -> { turn = (Float) a.getAnimatedValue(); invalidate(); });
        turnAnimator.start();
    }

    /** Turns it clockwise, a turn in 1100 ms, until it is told to stop, and then it comes round to where it started. */
    void setSpinning(boolean on) {
        if (on == spinning) return;
        spinning = on;
        if (isAttachedToWindow()) updateSpin();
    }

    @Override protected void onAttachedToWindow() {
        super.onAttachedToWindow();
        updateSpin();
    }

    @Override protected void onDetachedFromWindow() {
        stopAnimators();
        super.onDetachedFromWindow();
    }

    @Override protected void onVisibilityChanged(View changed, int visibility) {
        super.onVisibilityChanged(changed, visibility);
        if (isAttachedToWindow()) updateSpin();
    }

    private void updateSpin() {
        boolean run = spinning && isShown();
        if (run) {
            if (settle != null) settle.cancel();
            if (spin == null) {
                spin = ValueAnimator.ofFloat(spinAngle, 360);
                spin.setDuration(UpdateMotion.SPIN_MS);
                spin.setInterpolator(new LinearInterpolator());
                spin.setRepeatCount(ValueAnimator.INFINITE);
                spin.addUpdateListener(a -> { spinAngle = (Float) a.getAnimatedValue(); invalidate(); });
                spin.start();
            }
        } else if (spin != null) {
            spin.cancel();
            spin = null;
            long ms = UpdateMotion.settleMillis(spinAngle);
            if (ms > 0 && isAttachedToWindow()) {
                settle = ValueAnimator.ofFloat(spinAngle, 360);
                settle.setDuration(ms);
                settle.setInterpolator(new DecelerateInterpolator(1f));
                settle.addUpdateListener(a -> { spinAngle = (Float) a.getAnimatedValue(); invalidate(); });
                settle.addListener(new android.animation.AnimatorListenerAdapter() {
                    @Override public void onAnimationEnd(android.animation.Animator animation) { spinAngle = 0; invalidate(); }
                });
                settle.start();
            } else {
                spinAngle = 0;
                invalidate();
            }
        }
    }

    private void stopAnimators() {
        for (ValueAnimator a : new ValueAnimator[] {bendAnimator, turnAnimator, spin, settle}) if (a != null) a.cancel();
        spin = null;
        settle = null;
        spinAngle = 0;
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        rebuild();
    }

    // The strokes for the bend, in dp: the canvas is scaled to them when it is drawn.
    private void rebuild() {
        float w = getWidth() > 0 ? getWidth() / density : SIZE_DP;
        float h = getHeight() > 0 ? getHeight() / density : SIZE_DP;
        arrow = UpdateMotion.arrow(bend, w, h);
        line(shaft, arrow.shaft);
        line(head, arrow.head);
    }

    private static void line(Path path, float[] points) {
        path.reset();
        path.moveTo(points[0], points[1]);
        for (int i = 2; i < points.length; i += 2) path.lineTo(points[i], points[i + 1]);
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.save();
        canvas.scale(density, density);
        canvas.rotate(spinAngle + turn, arrow.pivotX, arrow.pivotY);
        canvas.drawPath(shaft, paint);
        canvas.drawPath(head, paint);
        canvas.restore();
    }
}
