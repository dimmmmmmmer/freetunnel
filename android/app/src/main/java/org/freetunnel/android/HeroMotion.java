package org.freetunnel.android;

import android.animation.Animator;
import android.animation.AnimatorListenerAdapter;
import android.animation.TimeInterpolator;
import android.animation.ValueAnimator;
import android.view.View;
import android.view.ViewGroup;
import android.view.animation.AccelerateDecelerateInterpolator;
import android.view.animation.DecelerateInterpolator;
import android.view.animation.LinearInterpolator;
import android.widget.ImageView;

/**
 * Home's logo moving as the desktop's does: HeroValues holds what it shows and how it gets from one state to the next, and
 * this binds it to the views of the page as it is built, with Android's animators for its time. render() makes every view
 * anew, and what was moving carries on in the new ones.
 */
final class HeroMotion {
    /** HeroValues' clock, from ValueAnimator. */
    private static final class Animators implements HeroValues.Tween {
        private static TimeInterpolator of(HeroValues.Easing easing) {
            switch (easing) {
                case OUT_CUBIC: return new DecelerateInterpolator(1.5f);
                // The desktop's InOutSine: half a cosine, which is what AccelerateDecelerateInterpolator is.
                case IN_OUT_SINE: return new AccelerateDecelerateInterpolator();
                default: return new LinearInterpolator();
            }
        }

        @Override public HeroValues.Handle run(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update, Runnable end) {
            ValueAnimator a = ValueAnimator.ofFloat(from, to);
            a.setDuration(millis);
            a.setInterpolator(of(easing));
            a.addUpdateListener(x -> update.set((Float) x.getAnimatedValue()));
            a.addListener(new AnimatorListenerAdapter() {
                private boolean cancelled;
                @Override public void onAnimationCancel(Animator animation) { cancelled = true; }
                @Override public void onAnimationEnd(Animator animation) { if (!cancelled) end.run(); }
            });
            a.start();
            return () -> a.cancel();
        }

        @Override public HeroValues.Handle loop(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update) {
            ValueAnimator a = ValueAnimator.ofFloat(from, to);
            a.setDuration(millis);
            a.setInterpolator(of(easing));
            a.setRepeatCount(ValueAnimator.INFINITE);
            a.setRepeatMode(ValueAnimator.REVERSE);
            a.addUpdateListener(x -> update.set((Float) x.getAnimatedValue()));
            a.start();
            return () -> a.cancel();
        }
    }

    private final HeroValues values = new HeroValues(new Animators());

    /**
     * Shows Home's logo for a state, in these views.
     *
     * @param box the 132 by 132 control, which rises, pulses and shrinks under a press
     * @param logo the picture in it, whose opacity is the state's
     * @param line the room under the logo for the status, as a view whose height is set here
     * @param status the status line, which fades in
     * @param riseOffsetPx how far down the logo sits with no session
     * @param lineHeightPx the room the line takes while there is a session
     * @param animate false for a page that has just been built, which starts in its state; true for a render() of the page
     * that is already showing, which moves from where it was
     */
    void attach(View box, ImageView logo, View line, View status, float riseOffsetPx, int lineHeightPx,
                boolean sessionActive, float alphaTarget, boolean pulseNow, boolean animate) {
        values.bind(v -> logo.setAlpha(v),
            down -> box.setTranslationY(down * riseOffsetPx),
            height -> {
                ViewGroup.LayoutParams lp = line.getLayoutParams();
                int h = Math.round(height);
                if (lp.height != h) { lp.height = h; line.setLayoutParams(lp); }
            },
            v -> status.setAlpha(v),
            s -> { box.setScaleX(s); box.setScaleY(s); });
        values.show(sessionActive, alphaTarget, lineHeightPx, pulseNow, animate);
    }

    /** A press on the logo, or the OK of a remote on it. */
    void press(boolean down) { values.press(down); }

    /** The logo has a remote's focus, or has lost it. */
    void focus(boolean focused) { values.focus(focused); }

    /** The views are gone (another page, or the window): nothing may go on animating them. */
    void release() { values.release(); }

    boolean isPulsing() { return values.isPulsing(); }
    float alphaNow() { return values.alpha(); }
    float riseNow() { return values.rise(); }
    int roomNow() { return Math.round(values.room()); }
    float scaleNow() { return values.scale(); }
    float heldNow() { return values.held(); }
}
