package org.freetunnel.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.View;

/**
 * Home's small graph of the tunnel's speeds over the last minute (SpeedHistory), over the speed tiles and as wide as both: what came in
 * as a line in the ↓ tile's colour over a faint fill of it, what went out as a line in the ↑ tile's colour. The newest reading is at the
 * right edge, and a minute fills the width; until then the lines start part of the way in. Drawn on the tiles' own ground. It is no
 * control and nothing to read out: the tiles under it say the numbers.
 */
final class SpeedGraphView extends View {
    private final SpeedHistory history;
    private final Paint downLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint downFill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint upLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final float pad;

    SpeedGraphView(Context context, SpeedHistory history, int downColor, int upColor) {
        super(context);
        this.history = history;
        float density = context.getResources().getDisplayMetrics().density;
        pad = 4 * density;
        for (Paint p : new Paint[] {downLine, upLine}) {
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(1.5f * density);
            p.setStrokeJoin(Paint.Join.ROUND);
            p.setStrokeCap(Paint.Cap.ROUND);
        }
        downLine.setColor(downColor);
        upLine.setColor(upColor);
        downFill.setStyle(Paint.Style.FILL);
        downFill.setColor((downColor & 0x00FFFFFF) | 0x2E000000);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
        setFocusable(false);
        setTag("home:graph");
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        int n = history.size();
        if (n < 2) return;
        float left = pad, right = getWidth() - pad, top = pad, bottom = getHeight() - pad;
        float step = (right - left) / (SpeedHistory.CAPACITY - 1);
        float x0 = right - (n - 1) * step;
        double scale = history.scale();
        // Under what came in, a fill down to the baseline.
        trace(n, x0, step, top, bottom, scale, true);
        path.lineTo(right, bottom);
        path.lineTo(x0, bottom);
        path.close();
        canvas.drawPath(path, downFill);
        trace(n, x0, step, top, bottom, scale, true);
        canvas.drawPath(path, downLine);
        trace(n, x0, step, top, bottom, scale, false);
        canvas.drawPath(path, upLine);
    }

    private void trace(int n, float x0, float step, float top, float bottom, double scale, boolean down) {
        path.reset();
        for (int i = 0; i < n; i++) {
            double v = down ? history.down(i) : history.up(i);
            float y = (float) (bottom - (bottom - top) * Math.min(1.0, v / scale));
            if (i == 0) path.moveTo(x0, y); else path.lineTo(x0 + i * step, y);
        }
    }
}
