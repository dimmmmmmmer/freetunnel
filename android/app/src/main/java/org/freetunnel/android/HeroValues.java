package org.freetunnel.android;

/**
 * What Home's logo shows and how it moves between states, as the desktop's does (qml/pages/HomePage.qml), kept apart from
 * the views so that a plain JVM can test it, and so that it can survive render(), which makes every view anew while an
 * animation goes on:
 *
 * <ul>
 * <li>its opacity follows the state in 220 ms: 0.5 off, 0.7 while connecting (reconnecting too) or disconnecting, 1.0 connected;
 * <li>with no session it sits 12 lower, and when one starts it rises over 280 ms, easing out, while the line under it
 * (the status) takes the room it needs over the same 280 ms, so that the selector below
 * does not jump;
 * <li>while connecting (reconnecting too) or disconnecting it pulses, to 1.05 and then between 0.97 and 1.05, 750 ms each way and
 * sine-eased, and eases back to 1.0 in 120 ms when it stops;
 * <li>a press scales it to 0.96 in 120 ms, easing out, and it comes back the same way;
 * <li>with a remote's focus it is held at 1.05, the pulse's peak, in the same 120 ms: the desktop has nothing under the pointer
 * to copy here (the logo has no hover), and a fill behind it would wash out its greys, so the focus is its size, which the pulse
 * and the press multiply as they multiply each other.
 * </ul>
 *
 * <p>Each value is held here and applied to whatever views were last bound, so a render() that replaces the views carries
 * on from where the value was, and a state that has not changed starts nothing again. Time is the {@link Tween}'s.
 *
 * <p>No Android classes: HeroMotion binds it to views and gives it Android's animators.
 */
final class HeroValues {
    interface Apply { void set(float value); }

    enum Easing { LINEAR, OUT_CUBIC, IN_OUT_SINE }

    /** Something running that can be stopped. */
    interface Handle { void cancel(); }

    /** What the values need of a clock: numbers that go from one to another over a time. */
    interface Tween {
        /** From {@code from} to {@code to}; {@code end} runs once, if it got there. Cancelled, it runs no more. */
        Handle run(float from, float to, long millis, Easing easing, Apply update, Runnable end);
        /** From {@code from} to {@code to} and back, for ever. */
        Handle loop(float from, float to, long millis, Easing easing, Apply update);
    }

    static final float PULSE_HIGH = 1.05f;
    static final float PULSE_LOW = 0.97f;
    static final float PRESSED = 0.96f;
    static final float FOCUSED = 1.05f;

    private static final Apply NOTHING = v -> { };

    /** One animated value: where it is, where it is going, and what it moves. */
    private final class Prop {
        float value;
        float target;
        Handle running;
        Apply apply = NOTHING;

        Prop(float value) { this.value = value; this.target = value; }

        void bind(Apply next) { apply = next; next.set(value); }

        void snap(float to) {
            cancel();
            value = to;
            target = to;
            apply.set(to);
        }

        void cancel() {
            if (running != null) { running.cancel(); running = null; }
        }

        void to(float to, long millis, Easing easing) {
            // Already there, or already on its way: not started again by a render() that changed nothing.
            if (to == target && (running != null || value == to)) return;
            cancel();
            target = to;
            if (millis <= 0 || value == to) { snap(to); return; }
            final Handle[] mine = new Handle[1];
            mine[0] = tween.run(value, to, millis, easing, v -> { value = v; apply.set(v); }, () -> {
                if (running == mine[0]) { running = null; value = target; apply.set(value); }
            });
            // The tween may have finished before it returned (a clock that is off): then there is nothing left running.
            if (value != target) running = mine[0];
        }
    }

    private final Tween tween;
    private final Prop alpha = new Prop(HomeState.OFF);
    private final Prop rise = new Prop(0f);
    private final Prop room = new Prop(0f);
    private final Prop statusAlpha = new Prop(0f);
    private final Prop press = new Prop(1f);
    private final Prop pulse = new Prop(1f);
    private final Prop hold = new Prop(1f);
    private Handle swing;
    private boolean pulsing;
    private Apply scale = NOTHING;

    HeroValues(Tween tween) { this.tween = tween; }

    /**
     * The views to move: their opacity, how far the logo is from its place (0 risen to 1 down), the room the line under it takes
     * in pixels, the line's opacity and the logo's scale. Each is given its value as it is now at once.
     */
    void bind(Apply logoAlpha, Apply down, Apply lineRoom, Apply lineAlpha, Apply logoScale) {
        alpha.bind(logoAlpha);
        rise.bind(v -> down.set(1f - v));
        room.bind(lineRoom);
        statusAlpha.bind(lineAlpha);
        scale = logoScale;
        press.bind(v -> scale.set(scale()));
        pulse.bind(v -> scale.set(scale()));
        hold.bind(v -> scale.set(scale()));
    }

    /**
     * Shows a state: moving there over the desktop's times if {@code animate}, else at once.
     *
     * @param lineHeightPx the room the line takes while there is a session
     */
    void show(boolean sessionActive, float alphaTarget, float lineHeightPx, boolean pulseNow, boolean animate) {
        long fade = animate ? 220 : 0;
        long glide = animate ? 280 : 0;
        alpha.to(alphaTarget, fade, Easing.LINEAR);
        rise.to(sessionActive ? 1f : 0f, glide, Easing.OUT_CUBIC);
        room.to(sessionActive ? lineHeightPx : 0f, glide, Easing.OUT_CUBIC);
        statusAlpha.to(sessionActive ? 1f : 0f, fade, Easing.LINEAR);
        pulseTo(pulseNow);
    }

    /** A press on the logo, or the OK of a remote on it: 0.96, and back to 1 when it ends. */
    void press(boolean down) {
        press.to(down ? PRESSED : 1f, 120, Easing.OUT_CUBIC);
    }

    /** The logo has a remote's focus, or has lost it: 1.05, and back to 1. */
    void focus(boolean focused) {
        hold.to(focused ? FOCUSED : 1f, 120, Easing.OUT_CUBIC);
    }

    /**
     * The views are gone: nothing may go on animating them, and what animates is stopped. The opacity, the place and the room are
     * kept, for the next views to start from. The scale is not: it is a press, a pulse and a focus that belong to the views that
     * saw them, and half way through one the next views would keep it for good.
     */
    void release() {
        for (Prop p : new Prop[]{alpha, rise, room, statusAlpha, press, pulse, hold}) { p.cancel(); p.apply = NOTHING; }
        for (Prop p : new Prop[]{press, pulse, hold}) { p.value = 1f; p.target = 1f; }
        scale = NOTHING;
        stopSwing();
    }

    // The pulse goes up to 1.05 first and then between 0.97 and 1.05 for as long as it goes on; stopped anywhere in its swing,
    // it eases back to 1 in 120 ms.
    private void pulseTo(boolean on) {
        if (on == pulsing) return;
        pulsing = on;
        if (on) {
            pulse.cancel();
            final Handle[] first = new Handle[1];
            first[0] = tween.run(pulse.value, PULSE_HIGH, 750, Easing.IN_OUT_SINE, v -> setPulse(v), () -> {
                if (pulsing && swing == first[0]) startLoop();
            });
            // A clock that ran it out before it returned has already started the loop, which stays.
            if (swing == null) swing = first[0];
        } else {
            stopSwing();
            pulse.to(1f, 120, Easing.OUT_CUBIC);
        }
    }

    private void startLoop() {
        swing = tween.loop(PULSE_HIGH, PULSE_LOW, 750, Easing.IN_OUT_SINE, v -> setPulse(v));
    }

    private void setPulse(float v) {
        pulse.value = v;
        pulse.target = v;
        pulse.apply.set(v);
    }

    private void stopSwing() {
        pulsing = false;
        if (swing != null) { swing.cancel(); swing = null; }
    }

    boolean isPulsing() { return pulsing; }
    float alpha() { return alpha.value; }
    /** 0 while the logo sits down with no session, 1 once it has risen. */
    float rise() { return rise.value; }
    float room() { return room.value; }
    float lineAlpha() { return statusAlpha.value; }
    float scale() { return pulse.value * press.value * hold.value; }
    /** 1 without the focus, 1.05 with it. */
    float held() { return hold.value; }
}
