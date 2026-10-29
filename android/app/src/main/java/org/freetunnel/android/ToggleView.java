package org.freetunnel.android;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.View;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.animation.DecelerateInterpolator;

/**
 * The desktop's Toggle (qml/Toggle.qml): a track 38 by 22, fully round, accent while on
 * with no border, toggleOff with a 1 px border while off (the fill stops inside the border); a knob 16 across, 3 in from
 * the track's end, white while on and a step lighter than the track while off, which
 * slides across in 120 ms, easing out. Dimmed to 0.45 while disabled. A tap anywhere on
 * it flips it. The one on Logs is 34 by 20.
 *
 * <p>The view is its track and nothing around it: the desktop's toggle has no hover and no focus look of its own, and a fill round a
 * track reads as the ring the desktop never draws. A remote's focus is shown by the row the toggle is in (Ui.toggleRow).
 */
final class ToggleView extends View {
    interface Listener { void onToggled(ToggleView toggle, boolean checked); }

    private final Theme theme;
    private final float density;
    private final int trackWidth;
    private final int trackHeight;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private boolean checked;
    /** 0 off to 1 on: the knob's place and the track's colour. */
    private float progress;
    private ValueAnimator animator;
    private Listener listener;

    ToggleView(Context context, Theme theme, boolean small) {
        super(context);
        this.theme = theme;
        this.density = context.getResources().getDisplayMetrics().density;
        this.trackWidth = small ? 34 : 38;
        this.trackHeight = small ? 20 : 22;
        setClickable(true);
    }

    void setListener(Listener listener) { this.listener = listener; }
    boolean isChecked() { return checked; }

    /** Shows this state at once, with no animation. */
    void setChecked(boolean value) {
        checked = value;
        if (animator != null) animator.cancel();
        progress = value ? 1f : 0f;
        invalidate();
    }

    private int px(float dp) { return Math.round(dp * density); }

    @Override protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        setMeasuredDimension(px(trackWidth), px(trackHeight));
    }

    @Override public void setEnabled(boolean enabled) {
        super.setEnabled(enabled);
        setAlpha(enabled ? 1f : 0.45f);
    }

    /**
     * A tap, or OK, flips it and says so: the animation runs, and then the listener. True when it did: a click that was acted on is
     * performClick's true, as for a view with an OnClickListener, and what a test, an accessibility service or a caller that asks
     * "was it clickable" is told. A disabled toggle does nothing, and says so.
     */
    @Override public boolean performClick() {
        if (!isEnabled()) return false;
        flip();
        // The click sound and the accessibility event, and a listener someone set on the view itself, as any view's click has.
        super.performClick();
        return true;
    }

    private void flip() {
        checked = !checked;
        float target = checked ? 1f : 0f;
        if (animator != null) animator.cancel();
        if (isLaidOut() && isShown()) {
            animator = ValueAnimator.ofFloat(progress, target);
            animator.setDuration(Ui.FADE_MS);
            animator.setInterpolator(new DecelerateInterpolator(1.5f));
            animator.addUpdateListener(a -> { progress = (Float) a.getAnimatedValue(); invalidate(); });
            animator.start();
        } else {
            progress = target;
            invalidate();
        }
        if (listener != null) listener.onToggled(this, checked);
    }

    private static int mix(int from, int to, float amount) {
        int a = Math.round((from >>> 24) + ((to >>> 24) - (from >>> 24)) * amount);
        int r = Math.round((from >> 16 & 0xFF) + ((to >> 16 & 0xFF) - (from >> 16 & 0xFF)) * amount);
        int g = Math.round((from >> 8 & 0xFF) + ((to >> 8 & 0xFF) - (from >> 8 & 0xFF)) * amount);
        int b = Math.round((from & 0xFF) + ((to & 0xFF) - (from & 0xFF)) * amount);
        return a << 24 | r << 16 | g << 8 | b;
    }

    @Override protected void onDraw(Canvas canvas) {
        float width = px(trackWidth);
        float height = px(trackHeight);
        float radius = height / 2;
        // Off, the track's fill stops inside its 1 px border, as a Qt Rectangle's does, and the border is the translucent black of the desktop's
        // over whatever is behind the toggle: a rim a little darker than the page, in both themes. Stroked over the fill it was a little
        // lighter than the page on the dark theme and much darker than it on the light. Both go as the track turns on.
        float border = Math.max(1f, px(1));
        float fillInset = border * (1f - progress);
        rect.set(fillInset, fillInset, width - fillInset, height - fillInset);
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(mix(theme.toggleOff, theme.accent, progress));
        canvas.drawRoundRect(rect, radius - fillInset, radius - fillInset, paint);
        if (progress < 1f) {
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(border);
            paint.setColor(Theme.withAlpha(theme.toggleOffBorder, Math.round((theme.toggleOffBorder >>> 24) * (1f - progress))));
            rect.set(border / 2, border / 2, width - border / 2, height - border / 2);
            canvas.drawRoundRect(rect, radius - border / 2, radius - border / 2, paint);
        }
        float knob = px(16);
        float inset = px(3);
        float x = inset + (width - knob - 2 * inset) * progress;
        float y = (height - knob) / 2;
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(mix(theme.offKnob, theme.onKnob, progress));
        canvas.drawCircle(x + knob / 2, y + knob / 2, knob / 2, paint);
    }

    @Override public CharSequence getAccessibilityClassName() { return "android.widget.Switch"; }

    @Override public void onInitializeAccessibilityNodeInfo(AccessibilityNodeInfo info) {
        super.onInitializeAccessibilityNodeInfo(info);
        info.setCheckable(true);
        info.setChecked(checked);
    }

    @Override public void onInitializeAccessibilityEvent(AccessibilityEvent event) {
        super.onInitializeAccessibilityEvent(event);
        event.setChecked(checked);
    }
}
