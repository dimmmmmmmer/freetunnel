package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;

// The Maintenance row's arrow and dots (UpdateMotion), which are numbers before they are a picture: the arrow's shape for a bend
// and the dots' hops at a moment. What they draw with is Android's, and is looked at on a device.
public final class UpdateMotionTest {
    private static final float EPS = 1e-3f;
    private static void near(String what, double expected, double actual) { assertEquals(what, expected, actual, EPS); }
    private static void near(double expected, double actual) { assertEquals(expected, actual, EPS); }
    private static float[] xs(float[] points) { float[] out = new float[points.length / 2]; for (int i = 0; i < out.length; i++) out[i] = points[2 * i]; return out; }
    private static float[] ys(float[] points) { float[] out = new float[points.length / 2]; for (int i = 0; i < out.length; i++) out[i] = points[2 * i + 1]; return out; }
    private static float min(float[] v) { float m = Float.POSITIVE_INFINITY; for (float f : v) m = Math.min(m, f); return m; }
    private static float max(float[] v) { float m = Float.NEGATIVE_INFINITY; for (float f : v) m = Math.max(m, f); return m; }

    private static float[] all(UpdateMotion.Arrow a) {
        float[] out = new float[a.shaft.length + a.head.length];
        System.arraycopy(a.shaft, 0, out, 0, a.shaft.length);
        System.arraycopy(a.head, 0, out, a.shaft.length, a.head.length);
        return out;
    }

    // The numbers are the desktop's: read from the QML they come from, so that a change there fails here until this follows.
    // Gradle runs unit tests in the module's directory, android/app.
    private static String qml(String name) throws Exception {
        return new String(Files.readAllBytes(new File("../../qml/components/" + name).toPath()), StandardCharsets.UTF_8);
    }
    private static void has(String text, String pattern, String what) {
        assertTrue(what + " is not as " + pattern + " in the QML any more", java.util.regex.Pattern.compile(pattern).matcher(text).find());
    }

    @Test public void theArrowsNumbersAreThoseOfUpdateArrowQml() throws Exception {
        String arrow = qml("UpdateArrow.qml");
        has(arrow, "const radius = 6\\.2, fullSweep = 290 \\* Math\\.PI / 180", "the radius and the sweep");
        has(arrow, "const length = 13 \\+ t \\* \\(radius \\* fullSweep - 13\\)", "the length of the straight arrow");
        has(arrow, "const steps = 28", "the steps of the shaft");
        has(arrow, "const barb = 4\\.5, spread = 0\\.7", "the barbs");
        has(arrow, "from: 0; to: 360; duration: 1100", "the spin");
        has(arrow, "\\* spin\\.duration \\* 2", "the settling");
        has(arrow, "strokeWidth: 1\\.4", "the stroke");
        has(arrow, "implicitWidth: 20; implicitHeight: 20", "the size");
        assertEquals(6.2f, UpdateMotion.RADIUS, 0f);
        assertEquals(290, Math.toDegrees(UpdateMotion.FULL_SWEEP), 1e-9);
        assertEquals(13f, UpdateMotion.STRAIGHT_LENGTH, 0f);
        assertEquals(28, UpdateMotion.STEPS);
        assertEquals(4.5f, UpdateMotion.BARB, 0f);
        assertEquals(0.7f, UpdateMotion.SPREAD, 0f);
        assertEquals(1100, UpdateMotion.SPIN_MS);
        assertEquals(20, UpdateArrowView.SIZE_DP);
    }

    @Test public void theDotsNumbersAreThoseOfBusyDotsQml() throws Exception {
        String dots = qml("BusyDots.qml");
        has(dots, "property real size: 2\\.5", "the size of a dot");
        has(dots, "PauseAnimation \\{ duration: index \\* 140 \\}", "the stagger");
        has(dots, "to: -dots\\.size \\* 1\\.2; duration: 220; easing\\.type: Easing\\.OutQuad", "the rise");
        has(dots, "NumberAnimation \\{ to: 0; duration: 220; easing\\.type: Easing\\.InQuad", "the fall");
        has(dots, "PauseAnimation \\{ duration: \\(2 - index\\) \\* 140 \\+ 200 \\}", "the rest");
        has(dots, "spacing: size", "the space between dots");
        assertEquals(2.5f, UpdateMotion.DOT, 0f);
        assertEquals(3f, UpdateMotion.HOP, 1e-6f);
        assertEquals(920, UpdateMotion.ROUND_MS);
    }

    // The arrow.

    @Test public void straightItIsADownArrowOnTheMiddleOfItsBox() {
        UpdateMotion.Arrow a = UpdateMotion.arrow(0, 20, 20);
        assertEquals(2 * (UpdateMotion.STEPS + 1), a.shaft.length);
        for (float x : xs(a.shaft)) near("the shaft is vertical, on the middle", 10, x);
        float[] sy = ys(a.shaft);
        for (int i = 1; i < sy.length; i++) assertTrue("the shaft runs down", sy[i] > sy[i - 1]);
        near("its length is that of the straight arrow", UpdateMotion.STRAIGHT_LENGTH, sy[sy.length - 1] - sy[0]);
        near("the pivot is the middle of the box", 10, a.pivotX);
        near("the pivot is the middle of the box", 10, a.pivotY);
    }

    @Test public void theHeadSitsOnTheTipAndPointsBackUpTheShaftOnBothSides() {
        UpdateMotion.Arrow a = UpdateMotion.arrow(0, 20, 20);
        near("the head's middle is the shaft's tip", a.shaft[a.shaft.length - 2], a.head[2]);
        near("the head's middle is the shaft's tip", a.shaft[a.shaft.length - 1], a.head[3]);
        assertTrue("the barbs are above the tip", a.head[1] < a.head[3] && a.head[5] < a.head[3]);
        near("and as far to the left as to the right", a.head[2] - a.head[0], a.head[4] - a.head[2]);
        near("each of the barb's length", UpdateMotion.BARB, Math.hypot(a.head[0] - a.head[2], a.head[1] - a.head[3]));
    }

    @Test public void atEveryBendTheWholeIsCentredInItsBox() {
        for (float bend = 0; bend <= 1.0001f; bend += 0.125f) {
            UpdateMotion.Arrow a = UpdateMotion.arrow(bend, 20, 20);
            float[] x = xs(all(a)), y = ys(all(a));
            near("centred across at a bend of " + bend, 10, (min(x) + max(x)) / 2);
            near("centred down at a bend of " + bend, 10, (min(y) + max(y)) / 2);
        }
    }

    @Test public void theBoxIsWhatItIsCentredIn() {
        UpdateMotion.Arrow a = UpdateMotion.arrow(0.5f, 30, 12);
        float[] x = xs(all(a)), y = ys(all(a));
        near("across", 15, (min(x) + max(x)) / 2);
        near("down", 6, (min(y) + max(y)) / 2);
    }

    @Test public void bentRoundItIsAnArcOfACircleThatTheSpinTurnsAbout() {
        UpdateMotion.Arrow a = UpdateMotion.arrow(1, 20, 20);
        for (int i = 0; i < a.shaft.length; i += 2)
            near("every point of the shaft is the radius from the pivot", UpdateMotion.RADIUS, Math.hypot(a.shaft[i] - a.pivotX, a.shaft[i + 1] - a.pivotY));
        // 290 degrees of it, from the tail, level with the pivot and heading down, round clockwise (on a screen) to the tip.
        double first = Math.atan2(a.shaft[1] - a.pivotY, a.shaft[0] - a.pivotX);
        double last = Math.atan2(a.shaft[a.shaft.length - 1] - a.pivotY, a.shaft[a.shaft.length - 2] - a.pivotX);
        near("it starts level with the pivot, on its right", 0, first);
        double swept = ((last - first) % (2 * Math.PI) + 2 * Math.PI) % (2 * Math.PI);
        near("and sweeps 290 degrees clockwise", Math.toRadians(290), swept);
    }

    @Test public void aBendTooSmallToTurnAboutIsStillAnArcWithItsPivotFarOff() {
        // Under a hundredth of a bend the curvature is a few ten thousandths: the pivot is thousands away, not the middle of the box.
        UpdateMotion.Arrow a = UpdateMotion.arrow(0.0005f, 20, 20);
        assertTrue("pivot at " + a.pivotX, a.pivotX < -1000);
        assertEquals(10, UpdateMotion.arrow(0, 20, 20).pivotX, EPS);
    }

    @Test public void theShaftGrowsAsItBendsAndItsTailStaysPointedDown() {
        float previous = 0;
        for (float bend = 0; bend <= 1.0001f; bend += 0.1f) {
            UpdateMotion.Arrow a = UpdateMotion.arrow(bend, 20, 20);
            double length = 0;
            for (int i = 2; i < a.shaft.length; i += 2) length += Math.hypot(a.shaft[i] - a.shaft[i - 2], a.shaft[i + 1] - a.shaft[i - 1]);
            assertTrue("longer than at the bend before: " + length, length >= previous - 1e-3);
            previous = (float) length;
            // The first segment heads straight down, whatever the bend.
            assertTrue("a tail that heads down at " + bend, a.shaft[3] > a.shaft[1] && Math.abs(a.shaft[2] - a.shaft[0]) < 0.25);
        }
        assertTrue("it ends as long as 290 degrees of the circle", Math.abs(previous - UpdateMotion.RADIUS * UpdateMotion.FULL_SWEEP) < 0.1);
    }

    @Test public void aBendBeyondTheEndsIsTheNearestEnd() {
        float[] below = all(UpdateMotion.arrow(-3, 20, 20)), zero = all(UpdateMotion.arrow(0, 20, 20));
        float[] above = all(UpdateMotion.arrow(7, 20, 20)), one = all(UpdateMotion.arrow(1, 20, 20));
        for (int i = 0; i < zero.length; i++) { near(zero[i], below[i]); near(one[i], above[i]); }
    }

    @Test public void theHeadLiesAlongTheChordOfTheLastStretchOfShaft() {
        // The barbs are as far either side of one direction, and that is the chord from the tip back along the shaft by about a barb's
        // length, not the tangent at the tip: on the full curve the two differ by about 20 degrees.
        UpdateMotion.Arrow a = UpdateMotion.arrow(1, 20, 20);
        double left = Math.atan2(a.head[1] - a.head[3], a.head[0] - a.head[2]);
        double right = Math.atan2(a.head[5] - a.head[3], a.head[4] - a.head[2]);
        double between = Math.atan2(Math.sin(left) + Math.sin(right), Math.cos(left) + Math.cos(right));
        int back = UpdateMotion.STEPS - 4; // 4 steps of a 28 step, 31 long shaft is about 4.5 back
        double chord = Math.atan2(a.shaft[2 * back + 1] - a.head[3], a.shaft[2 * back] - a.head[2]);
        assertEquals("the head points back along the chord", chord, between, 0.06);
        double tangent = Math.atan2(a.head[3] - a.pivotY, a.head[2] - a.pivotX) + Math.PI / 2 + Math.PI;
        double apart = Math.abs(Math.atan2(Math.sin(tangent - between), Math.cos(tangent - between)));
        assertTrue("and not along the tangent, which is " + apart + " away", apart > 0.2);
        near("each barb is " + UpdateMotion.SPREAD + " from it, either side", 2 * UpdateMotion.SPREAD,
            Math.abs(Math.atan2(Math.sin(left - right), Math.cos(left - right))));
    }

    @Test public void theHeadFollowsTheCurveWithoutLyingOnTheShaft() {
        // At the full bend the barbs lie along the chord of the last stretch, not the tangent: neither lies on the circle's tangent.
        UpdateMotion.Arrow a = UpdateMotion.arrow(1, 20, 20);
        double tangent = Math.atan2(a.shaft[a.shaft.length - 1] - a.pivotY, a.shaft[a.shaft.length - 2] - a.pivotX) + Math.PI / 2;
        double barb = Math.atan2(a.head[3] - a.head[1], a.head[2] - a.head[0]);
        double off = Math.abs(((barb - tangent) % Math.PI + Math.PI) % Math.PI);
        assertTrue("a barb is not along the tangent: " + off, off > 0.1 && off < Math.PI - 0.1);
    }

    // The spin.

    @Test public void aStoppedSpinComesRoundToWhereItStartedInTwiceTheTimeTheRestOfTheTurnWouldTake() {
        assertEquals(0, UpdateMotion.settleMillis(0));
        assertEquals(0, UpdateMotion.settleMillis(360));
        assertEquals(UpdateMotion.SPIN_MS, UpdateMotion.settleMillis(180));
        assertEquals("from just past the start, nearly a whole turn at twice the time", 2200, UpdateMotion.settleMillis(0.0001f));
        assertEquals(1, UpdateMotion.settleMillis(359.9999f));
        assertEquals("an angle past a turn is the same angle", UpdateMotion.settleMillis(90), UpdateMotion.settleMillis(450));
        assertEquals("and a negative one", UpdateMotion.settleMillis(270), UpdateMotion.settleMillis(-90));
    }

    // The dots.

    @Test public void theDotsHopOneAfterAnotherAndEachRoundIsTheSameLength() {
        assertEquals(920, UpdateMotion.ROUND_MS);
        // Dot 0 starts at once, dot 1 140 ms later, dot 2 after 280.
        for (int index = 0; index < 3; index++) {
            long start = index * 140L;
            if (start > 0) near("rests before its turn", 0, UpdateMotion.dotLift(index, start - 1));
            near("is at the top after rising", UpdateMotion.HOP, UpdateMotion.dotLift(index, start + 220));
            near("and is down again after falling", 0, UpdateMotion.dotLift(index, start + 440));
            near("then rests until the round ends", 0, UpdateMotion.dotLift(index, 919));
        }
    }

    @Test public void aDotRisesWithAnOutQuadAndFallsWithAnInQuad() {
        // Half way up it has done three quarters of the rise; half way down it has fallen a quarter; and at three quarters of the
        // way down it has fallen more than half.
        near(UpdateMotion.HOP * 0.75, UpdateMotion.dotLift(0, 110));
        near(UpdateMotion.HOP * 0.75, UpdateMotion.dotLift(0, 330));
        near(UpdateMotion.HOP * 0.4375, UpdateMotion.dotLift(0, 220 + 165));
    }

    @Test public void theRoundRepeatsAndNoLiftIsEverNegativeOrBeyondAHop() {
        for (long t = -2000; t < 4000; t += 7) {
            for (int index = 0; index < 3; index++) {
                float lift = UpdateMotion.dotLift(index, t);
                assertTrue(t + "/" + index + ": " + lift, lift >= -EPS && lift <= UpdateMotion.HOP + EPS);
                near("a round later it is the same", lift, UpdateMotion.dotLift(index, t + UpdateMotion.ROUND_MS));
            }
        }
    }

    @Test public void theDotsHopInTurnAndNeverAllAtOnce() {
        // Early in a round the first is up and the last has not begun; late in it the last is the only one still going.
        assertTrue(UpdateMotion.dotLift(0, 100) > 0 && UpdateMotion.dotLift(2, 100) == 0);
        assertTrue(UpdateMotion.dotLift(2, 500) > 0 && UpdateMotion.dotLift(0, 500) == 0);
        // Each of the three is at its top at a different moment.
        assertTrue(UpdateMotion.dotLift(0, 220) > UpdateMotion.dotLift(1, 220) && UpdateMotion.dotLift(1, 360) > UpdateMotion.dotLift(0, 360));
        for (long t = 0; t < UpdateMotion.ROUND_MS; t += 5) {
            boolean allUp = true;
            for (int index = 0; index < 3; index++) allUp &= UpdateMotion.dotLift(index, t) > UpdateMotion.HOP * 0.99f;
            assertFalse("all three at the top at " + t, allUp);
        }
    }
}
