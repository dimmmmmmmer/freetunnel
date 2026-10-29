package org.freetunnel.android;

/**
 * What the Maintenance row's two pictures are made of (qml/components/UpdateArrow.qml and BusyDots.qml), as numbers: the
 * arrow's strokes for a bend, the dots' hops at a moment, and how long a spin takes to settle. Plain Java, so that the shapes
 * are checked on a plain JVM (UpdateMotionTest); {@link UpdateArrowView} and {@link BusyDotsView} only draw them.
 */
final class UpdateMotion {
    private UpdateMotion() { }

    // The arrow.

    /** The arrow's strokes for one bend, in the box it is drawn in: the shaft and the head as points x0, y0, x1, y1, ... */
    static final class Arrow {
        final float[] shaft;
        final float[] head;
        /** What a spin or a turn goes about: the middle of the arrow's circle, which is the middle of the box while it is straight. */
        final float pivotX;
        final float pivotY;

        Arrow(float[] shaft, float[] head, float pivotX, float pivotY) {
            this.shaft = shaft;
            this.head = head;
            this.pivotX = pivotX;
            this.pivotY = pivotY;
        }
    }

    static final float RADIUS = 6.2f;
    static final double FULL_SWEEP = Math.toRadians(290);
    static final float STRAIGHT_LENGTH = 13f;
    static final int STEPS = 28;
    static final float BARB = 4.5f;
    static final float SPREAD = 0.7f;

    /**
     * The arrow for this bend, 0 being ↓ and 1 being ↻ (anything beyond is the nearest of the two), centred in a box this wide and
     * high. The shaft has one curvature along its whole length and its tail always heads straight down, so bending it swings the
     * tip clockwise; it grows as it bends, from the length of ↓ to 290 degrees of a circle. The head lies along the chord of the
     * last stretch of shaft, not the tangent at the tip, which on a curve would tip the inner barb onto the shaft.
     */
    static Arrow arrow(float bend, float width, float height) {
        double t = Math.max(0, Math.min(1, bend));
        double sweep = t * FULL_SWEEP;
        double length = STRAIGHT_LENGTH + t * (RADIUS * FULL_SWEEP - STRAIGHT_LENGTH);
        double k = sweep / length;
        double heading = Math.PI / 2; // down: y grows downward
        double[] sx = new double[STEPS + 1];
        double[] sy = new double[STEPS + 1];
        for (int i = 0; i <= STEPS; i++) {
            double s = length * i / STEPS;
            if (k < 1e-4) {
                sx[i] = 0;
                sy[i] = s;
            } else {
                sx[i] = (Math.sin(heading + k * s) - Math.sin(heading)) / k;
                sy[i] = -(Math.cos(heading + k * s) - Math.cos(heading)) / k;
            }
        }
        double tipX = sx[STEPS];
        double tipY = sy[STEPS];
        double back = heading + sweep - k * BARB / 2 + Math.PI;
        double[] hx = {tipX + BARB * Math.cos(back - SPREAD), tipX, tipX + BARB * Math.cos(back + SPREAD)};
        double[] hy = {tipY + BARB * Math.sin(back - SPREAD), tipY, tipY + BARB * Math.sin(back + SPREAD)};
        double minX = Double.POSITIVE_INFINITY, minY = Double.POSITIVE_INFINITY, maxX = Double.NEGATIVE_INFINITY, maxY = Double.NEGATIVE_INFINITY;
        for (int i = 0; i <= STEPS; i++) {
            minX = Math.min(minX, sx[i]); maxX = Math.max(maxX, sx[i]);
            minY = Math.min(minY, sy[i]); maxY = Math.max(maxY, sy[i]);
        }
        for (int i = 0; i < 3; i++) {
            minX = Math.min(minX, hx[i]); maxX = Math.max(maxX, hx[i]);
            minY = Math.min(minY, hy[i]); maxY = Math.max(maxY, hy[i]);
        }
        double dx = width / 2 - (minX + maxX) / 2;
        double dy = height / 2 - (minY + maxY) / 2;
        float[] shaft = new float[(STEPS + 1) * 2];
        for (int i = 0; i <= STEPS; i++) {
            shaft[2 * i] = (float) (sx[i] + dx);
            shaft[2 * i + 1] = (float) (sy[i] + dy);
        }
        float[] head = new float[6];
        for (int i = 0; i < 3; i++) {
            head[2 * i] = (float) (hx[i] + dx);
            head[2 * i + 1] = (float) (hy[i] + dy);
        }
        return k < 1e-4 ? new Arrow(shaft, head, width / 2, height / 2) : new Arrow(shaft, head, (float) (-1 / k + dx), (float) dy);
    }

    /** How long the spin takes to a full turn. */
    static final long SPIN_MS = 1100;

    /**
     * Once it stops, a spinning arrow comes round to where it started rather than halting at an angle, slowing as it goes: an
     * out-quad starts at twice the average speed, so it takes twice the time the rest of the turn would at the speed it was going.
     */
    static long settleMillis(float angle) {
        float a = ((angle % 360) + 360) % 360;
        if (a == 0) return 0;
        return Math.max(1, Math.round((360 - a) / 360.0 * SPIN_MS * 2));
    }

    // The dots.

    /** A dot is 2.5 and its hop 1.2 of that, with 2.5 between them. */
    static final float DOT = 2.5f;
    static final float HOP = DOT * 1.2f;
    private static final int STAGGER_MS = 140;
    private static final int RISE_MS = 220;
    private static final int FALL_MS = 220;
    private static final int REST_MS = 200;
    /** Every dot's round takes the same time, so they stay in step however long it runs. */
    static final int ROUND_MS = 3 * STAGGER_MS - STAGGER_MS + RISE_MS + FALL_MS + REST_MS;

    /**
     * How far the dot at this place (0, 1 or 2) is above where it rests, at this time into the round: it waits its turn, rises with an
     * out-quad, falls with an in-quad, and rests, so that one hops after another.
     */
    static float dotLift(int index, long millis) {
        long t = (((millis % ROUND_MS) + ROUND_MS) % ROUND_MS) - (long) index * STAGGER_MS;
        if (t < 0 || t >= RISE_MS + FALL_MS) return 0;
        if (t < RISE_MS) {
            double x = (double) t / RISE_MS;
            return (float) (HOP * (1 - (1 - x) * (1 - x)));
        }
        double x = (double) (t - RISE_MS) / FALL_MS;
        return (float) (HOP * (1 - x * x));
    }
}
