package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

// How Home's logo gets from one state to the next: the desktop's times and easings, nothing started again by a render()
// that changed nothing, views replaced while it moves, the pulse and its settling, the press and a remote's focus. The clock is a
// fake that the test advances.
public final class HeroValuesTest {
    private static final class Run implements HeroValues.Handle {
        final float from;
        final float to;
        final long millis;
        final HeroValues.Easing easing;
        final HeroValues.Apply update;
        final Runnable end;
        final boolean loop;
        boolean cancelled;
        Run(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update, Runnable end, boolean loop) {
            this.from = from; this.to = to; this.millis = millis; this.easing = easing; this.update = update; this.end = end; this.loop = loop;
        }
        @Override public void cancel() { cancelled = true; }
        /** The animation is at this value now. */
        void at(float v) { if (!cancelled) update.set(v); }
        /** The animation ran out. */
        void finish() { if (cancelled) return; update.set(to); if (end != null) end.run(); }
    }

    private static final class Clock implements HeroValues.Tween {
        final List<Run> runs = new ArrayList<>();
        @Override public HeroValues.Handle run(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update, Runnable end) {
            Run r = new Run(from, to, millis, easing, update, end, false);
            runs.add(r);
            return r;
        }
        @Override public HeroValues.Handle loop(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update) {
            Run r = new Run(from, to, millis, easing, update, null, true);
            runs.add(r);
            return r;
        }
        List<Run> live() {
            List<Run> out = new ArrayList<>();
            for (Run r : runs) if (!r.cancelled) out.add(r);
            return out;
        }
        Run last() { return runs.get(runs.size() - 1); }
    }

    /** The views: what they were last told. */
    private static final class Views {
        float alpha = -1, down = -1, room = -1, lineAlpha = -1, scale = -1;
        int calls;
    }

    private final Clock clock = new Clock();
    private final HeroValues values = new HeroValues(clock);

    private Views bind() {
        Views v = new Views();
        values.bind(x -> { v.alpha = x; v.calls++; }, x -> { v.down = x; v.calls++; }, x -> { v.room = x; v.calls++; },
            x -> { v.lineAlpha = x; v.calls++; }, x -> { v.scale = x; v.calls++; });
        return v;
    }

    private static Run find(List<Run> runs, float from, float to) {
        for (Run r : runs) if (r.from == from && r.to == to && !r.loop) return r;
        throw new AssertionError("No run from " + from + " to " + to + " in " + runs.size());
    }

    @Test public void aNewPageStartsInItsStateAtOnce() {
        Views v = bind();
        // Off: dim, sitting down, with no room under it.
        values.show(false, HomeState.OFF, 0, false, false);
        assertEquals(0.5f, v.alpha, 0);
        assertEquals("Sitting down", 1f, v.down, 0);
        assertEquals(0f, v.room, 0);
        assertEquals(0f, v.lineAlpha, 0);
        assertEquals(1f, v.scale, 0);
        assertTrue("Nothing is animated", clock.runs.isEmpty());
        // Connected, on a page just built: whole, risen, with the line's room.
        Views up = bind();
        values.show(true, HomeState.ON, 26, false, false);
        assertEquals(1f, up.alpha, 0);
        assertEquals("Risen", 0f, up.down, 0);
        assertEquals(26f, up.room, 0);
        assertEquals(1f, up.lineAlpha, 0);
        assertTrue(clock.runs.isEmpty());
    }

    @Test public void aStateThatChangesMovesFromWhereItWasOverTheDesktopsTimes() {
        values.bind(x -> { }, x -> { }, x -> { }, x -> { }, x -> { });
        values.show(false, HomeState.OFF, 0, false, false);
        values.show(true, HomeState.BUSY, 26, true, true);
        Run opacity = find(clock.runs, 0.5f, 0.7f);
        assertEquals(220, opacity.millis);
        assertEquals(HeroValues.Easing.LINEAR, opacity.easing);
        Run rise = find(clock.runs, 0f, 1f);
        assertEquals(280, rise.millis);
        assertEquals(HeroValues.Easing.OUT_CUBIC, rise.easing);
        // The line takes its room over the same 280 ms, so that the selector under it does not jump.
        Run room = find(clock.runs, 0f, 26f);
        assertEquals(280, room.millis);
        assertEquals(HeroValues.Easing.OUT_CUBIC, room.easing);
        // And fades in over 220.
        Run lineFade = clock.runs.get(3);
        assertEquals(0f, lineFade.from, 0);
        assertEquals(1f, lineFade.to, 0);
        assertEquals(220, lineFade.millis);
        // Pulsing: up from 1.0 to 1.05 in 750 ms, sine-eased.
        Run pulse = find(clock.runs, 1f, 1.05f);
        assertEquals(750, pulse.millis);
        assertEquals(HeroValues.Easing.IN_OUT_SINE, pulse.easing);
        assertTrue(values.isPulsing());
    }

    @Test public void aRenderThatChangesNothingStartsNothingAndTheNewViewsGetTheValuesAtOnce() {
        bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.show(true, HomeState.BUSY, 26, true, true);
        int started = clock.runs.size();
        // render() makes the views anew and shows the same state again.
        Views fresh = bind();
        assertEquals("The new views are given what the old had, now", 0.5f, fresh.alpha, 0);
        values.show(true, HomeState.BUSY, 26, true, true);
        assertEquals("Nothing is started again", started, clock.runs.size());
        assertEquals("and nothing is cancelled", started, clock.live().size());
    }

    @Test public void viewsReplacedWhileItMovesGetTheValueItIsAtAndEveryFrameAfter() {
        Views old = bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.show(true, HomeState.ON, 26, false, true);
        Run opacity = find(clock.runs, 0.5f, 1f);
        opacity.at(0.6f);
        assertEquals(0.6f, old.alpha, 0);
        Views fresh = bind();
        assertEquals("The value it is at", 0.6f, fresh.alpha, 0);
        opacity.at(0.8f);
        assertEquals("The next frames go to the new views", 0.8f, fresh.alpha, 0);
        assertEquals("and not to the old", 0.6f, old.alpha, 0);
        opacity.finish();
        assertEquals(1f, fresh.alpha, 0);
        assertEquals(1f, values.alpha(), 0);
    }

    @Test public void aStateThatChangesAgainMidwayGoesOnFromWhereItIs() {
        Views v = bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.show(true, HomeState.BUSY, 26, false, true);
        find(clock.runs, 0.5f, 0.7f).at(0.6f);
        // Connected before it got to 0.7: on from 0.6, and the run it was in is stopped.
        values.show(true, HomeState.ON, 26, false, true);
        Run next = find(clock.runs, 0.6f, 1f);
        assertEquals(220, next.millis);
        assertTrue(find(clock.runs, 0.5f, 0.7f).cancelled);
        assertFalse(next.cancelled);
        assertEquals(0.6f, v.alpha, 0);
    }

    @Test public void theSessionEndingSinksTheLogoAndGivesTheRoomBack() {
        values.bind(x -> { }, x -> { }, x -> { }, x -> { }, x -> { });
        values.show(true, HomeState.ON, 26, false, false);
        values.show(false, HomeState.OFF, 0, false, true);
        Run sink = find(clock.runs, 1f, 0f);
        assertEquals(280, sink.millis);
        Run room = find(clock.runs, 26f, 0f);
        assertEquals(280, room.millis);
        Run fade = find(clock.runs, 1f, 0.5f);
        assertEquals(220, fade.millis);
    }

    @Test public void thePulseGoesUpFirstAndThenSwingsBetweenTheTwoForEver() {
        bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        Run first = find(clock.runs, 1f, 1.05f);
        assertFalse(first.loop);
        assertEquals("Only the first run so far", 1, clock.live().size());
        first.at(1.02f);
        assertEquals(1.02f, values.scale(), 0.0001f);
        first.finish();
        Run swing = clock.last();
        assertTrue("then it loops", swing.loop);
        assertEquals(1.05f, swing.from, 0);
        assertEquals(0.97f, swing.to, 0);
        assertEquals(750, swing.millis);
        assertEquals(HeroValues.Easing.IN_OUT_SINE, swing.easing);
        swing.at(0.99f);
        assertEquals(0.99f, values.scale(), 0.0001f);
        assertTrue(values.isPulsing());
    }

    @Test public void stoppedAnywhereInItsSwingThePulseEasesBackToOneIn120Ms() {
        bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        clock.last().finish();
        Run swing = clock.last();
        swing.at(0.985f);
        // Connected: the pulse stops and the logo settles to 1.0 from where it is.
        values.show(true, HomeState.ON, 26, false, true);
        assertTrue("The swing is stopped", swing.cancelled);
        assertFalse(values.isPulsing());
        Run settle = find(clock.runs, 0.985f, 1f);
        assertEquals("in 120 ms", 120, settle.millis);
        assertEquals(HeroValues.Easing.OUT_CUBIC, settle.easing);
        settle.finish();
        assertEquals(1f, values.scale(), 0);
    }

    @Test public void aPulseStoppedDuringItsFirstRunNeverStartsTheLoop() {
        bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        Run first = find(clock.runs, 1f, 1.05f);
        first.at(1.03f);
        values.show(true, HomeState.ON, 26, false, true);
        assertTrue(first.cancelled);
        first.finish();
        for (Run r : clock.runs) assertFalse("No loop was started", r.loop);
        find(clock.runs, 1.03f, 1f);
    }

    @Test public void aRunThatFinishedIsNotStoppedWhenTheNextOneStarts() {
        bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.show(true, HomeState.BUSY, 26, false, true);
        Run done = find(clock.runs, 0.5f, 0.7f);
        done.finish();
        values.show(true, HomeState.ON, 26, false, true);
        assertFalse("It had ended: it is not the one that is stopped", done.cancelled);
        assertFalse(find(clock.runs, 0.7f, 1f).cancelled);
    }

    @Test public void aPulseThatStartsWhileTheLastOneIsSettlingStopsTheSettling() {
        bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        clock.last().finish();
        clock.last().at(0.985f);
        values.show(true, HomeState.ON, 26, false, true);
        Run settle = find(clock.runs, 0.985f, 1f);
        // Connecting again before it has settled: it must not go on pulling the logo to 1 under the new pulse.
        values.show(true, HomeState.BUSY, 26, true, true);
        assertTrue(settle.cancelled);
        assertEquals("The new pulse goes up from where the logo is", 0.985f, clock.last().from, 0);
    }

    @Test public void aPulseThatRestartsAfterItStoppedStartsFromOneAgain() {
        bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        clock.last().finish();
        values.show(true, HomeState.ON, 26, false, true);
        clock.last().finish();
        values.show(true, HomeState.BUSY, 26, true, true);
        Run again = clock.last();
        assertEquals(1f, again.from, 0);
        assertEquals(1.05f, again.to, 0);
        assertFalse(again.loop);
    }

    @Test public void aPressShrinksToTheDesktopsScaleAndComesBack() {
        Views v = bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.press(true);
        Run down = find(clock.runs, 1f, 0.96f);
        assertEquals(120, down.millis);
        assertEquals(HeroValues.Easing.OUT_CUBIC, down.easing);
        down.at(0.97f);
        assertEquals(0.97f, v.scale, 0.0001f);
        down.finish();
        assertEquals(0.96f, values.scale(), 0.0001f);
        values.press(false);
        Run up = find(clock.runs, 0.96f, 1f);
        assertEquals(120, up.millis);
        up.finish();
        assertEquals(1f, v.scale, 0);
        // Releasing a press that was never made does nothing.
        int runs = clock.runs.size();
        values.press(false);
        assertEquals(runs, clock.runs.size());
    }

    @Test public void theScaleIsThePulseTimesThePressSoNeitherHoldsTheOtherStill() {
        Views v = bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        clock.last().at(1.05f);
        values.press(true);
        find(clock.runs, 1f, 0.96f).finish();
        assertEquals(1.05f * 0.96f, v.scale, 0.0001f);
        // The pulse goes on under the press.
        clock.runs.get(0).at(1.0f);
        assertEquals(0.96f, v.scale, 0.0001f);
    }

    // The desktop's logo has nothing under the pointer, and a fill behind it washes its greys out, so a remote's focus is its size: held
    // at the pulse's peak, in the time a press takes.
    @Test public void aRemotesFocusHoldsTheLogoAtThePulsesPeakAndLetsGo() {
        Views v = bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.focus(true);
        Run grow = find(clock.runs, 1f, 1.05f);
        assertEquals(120, grow.millis);
        assertEquals(HeroValues.Easing.OUT_CUBIC, grow.easing);
        grow.at(1.02f);
        assertEquals(1.02f, v.scale, 0.0001f);
        grow.finish();
        assertEquals(1.05f, v.scale, 0.0001f);
        assertEquals(1.05f, values.held(), 0);
        // The same focus again, as a render() that gave it back says, starts nothing.
        int runs = clock.runs.size();
        values.focus(true);
        assertEquals(runs, clock.runs.size());
        values.focus(false);
        find(clock.runs, 1.05f, 1f).finish();
        assertEquals(1f, v.scale, 0);
        assertEquals(1f, values.held(), 0);
    }

    // The three multiply, so none of them holds another still: a focused logo still pulses, and still shrinks under OK.
    @Test public void theScaleIsThePulseTimesThePressTimesTheFocus() {
        Views v = bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        clock.last().at(1.05f);
        values.focus(true);
        clock.last().finish();
        assertEquals(1.05f * 1.05f, v.scale, 0.0001f);
        // The pulse goes on under the focus: its low is not lost to it.
        clock.runs.get(0).at(0.97f);
        assertEquals(0.97f * 1.05f, v.scale, 0.0001f);
        values.press(true);
        find(clock.runs, 1f, 0.96f).finish();
        assertEquals(0.97f * 0.96f * 1.05f, v.scale, 0.0001f);
    }

    // A press, a pulse and a focus belong to the views that saw them. Left half way by a page that went, they would stay in the
    // next views for good: nothing else sets them.
    @Test public void releaseLeavesNoScaleBehindForTheNextViews() {
        Views old = bind();
        values.show(true, HomeState.BUSY, 26, true, false);
        clock.last().at(1.03f);
        values.focus(true);
        clock.last().at(1.04f);
        values.press(true);
        float seen = old.scale;
        values.release();
        assertTrue("Everything that moved is stopped", clock.live().isEmpty());
        Views fresh = bind();
        assertEquals("The old views hear no more of it", seen, old.scale, 0);
        assertEquals("The next views start at their size", 1f, fresh.scale, 0);
        assertEquals(1f, values.held(), 0);
        assertEquals(1f, values.scale(), 0);
        // A focus that is still there is asked for again by whoever has it, and takes the time again.
        values.focus(true);
        assertEquals(1f, clock.last().from, 0);
        assertEquals(1.05f, clock.last().to, 0);
    }

    @Test public void releaseStopsEverythingKeepsTheValuesAndSilencesTheOldViews() {
        Views old = bind();
        values.show(false, HomeState.OFF, 0, false, false);
        values.show(true, HomeState.ON, 26, true, true);
        clock.last().at(1.02f);
        values.release();
        assertTrue("Everything that moved is stopped", clock.live().isEmpty());
        assertFalse(values.isPulsing());
        int calls = old.calls;
        // Even a frame that was already on its way when it was stopped.
        for (Run r : clock.runs) r.update.set(0.9f);
        assertEquals("The old views hear no more", calls, old.calls);
        // Back on a page that is built again: the logo is in its state, and goes on pulsing from one.
        Views fresh = bind();
        assertEquals("The values are kept, and given to the new views at once", values.alpha(), fresh.alpha, 0);
        values.show(true, HomeState.ON, 26, true, false);
        assertEquals(1f, fresh.alpha, 0);
        assertTrue(values.isPulsing());
        assertSame(clock.runs.get(clock.runs.size() - 1), clock.last());
        assertFalse(clock.last().cancelled);
    }

    @Test public void aTweenThatEndsBeforeItReturnsLeavesNothingRunning() {
        // A clock that has run out of time (animations off) finishes at once, from inside run().
        final int[] cancels = {0};
        final int[] loopCancels = {0};
        HeroValues instant = new HeroValues(new HeroValues.Tween() {
            @Override public HeroValues.Handle run(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update, Runnable end) {
                update.set(to);
                end.run();
                return () -> cancels[0]++;
            }
            @Override public HeroValues.Handle loop(float from, float to, long millis, HeroValues.Easing easing, HeroValues.Apply update) {
                return () -> loopCancels[0]++;
            }
        });
        float[] seen = {-1};
        instant.bind(x -> seen[0] = x, x -> { }, x -> { }, x -> { }, x -> { });
        instant.show(true, HomeState.ON, 26, false, true);
        assertEquals(1f, seen[0], 0);
        assertEquals(1f, instant.alpha(), 0);
        // The same state again starts nothing, whichever way the clock behaves.
        instant.show(true, HomeState.ON, 26, false, true);
        assertEquals(1f, instant.alpha(), 0);
        // The runs that ended are not stopped by the ones after them.
        instant.show(false, HomeState.OFF, 0, false, true);
        assertEquals(0, cancels[0]);
        // Pulsing on such a clock ends its first run at once and starts the loop, which stays, and stopping stops that.
        instant.show(true, HomeState.BUSY, 26, true, true);
        assertTrue(instant.isPulsing());
        assertEquals(0, cancels[0]);
        instant.show(true, HomeState.ON, 26, false, true);
        assertFalse(instant.isPulsing());
        assertEquals("The loop it started is the one that is stopped", 1, loopCancels[0]);
        assertEquals("and no run that had ended is", 0, cancels[0]);
    }
}
