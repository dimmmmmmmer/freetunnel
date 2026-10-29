package org.freetunnel.android;

import android.content.Context;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.view.View;
import android.view.ViewGroup;

/**
 * One overlay in the window: a backdrop that covers all of it, and a panel on it, placed
 * as the desktop places it. MainActivity.showOverlay is what makes one and adds it.
 *
 * <ul>
 * <li>CARD (the editor, the app picker, Receive from phone): centred across, as wide as asked and 14 from the window's sides at most,
 * and as tall as it holds, or as it is asked (the picker: 460) and the window leaves room for.
 * Down the window as the desktop's cards are: centred in it, and never higher than 40 from the top (or than the status bar, or a TV's
 * overscan) nor lower than 12 from the bottom (or the bar, or the overscan), so that on a TV the card covers the nav row and a phone's
 * keeps clear of its bars.
 * <li>CONFIRM: centred both ways, 12 from the sides at most.
 * <li>POPUP (a dropdown, a menu, the select popup): under the control it belongs to,
 * 8 inside the window, flipped above when it does not fit below, and on the larger side,
 * scrolling, when it fits neither (Home's dropdown is never flipped: UNDER_CENTRE_OF_CONTROL_NO_FLIP). A popup casts the desktop's
 * shadow (MultiEffect: black at 0.5, 0.2 in the light theme, a blur of 0.7, 5 down). The suggestions of a field are a popup that is
 * above its field, as wide as it, and passive: no backdrop takes a touch.
 * </ul>
 *
 * The panel is laid out after the page behind it, so that where the control is, after the page is rebuilt, is known by then:
 * requestLayout() puts a popup back under it.
 */
final class OverlayLayer extends ViewGroup {
    enum Kind { CARD, CONFIRM, POPUP }

    /** Where a popup goes against the control it belongs to. */
    static final int UNDER_RIGHT_EDGE = 0;
    static final int UNDER_CENTRE_OF_CONTROL = 1;
    static final int UNDER_CENTRE_OF_WINDOW = 2;
    /** Above the control, as wide as it and level with its left edge: the suggestions of a text field. */
    static final int ABOVE_CONTROL = 3;
    /**
     * Under the control and centred on it, and never flipped above it: Home's config dropdown, whose desktop popup sits at the selector's
     * bottom plus 6 whatever room is left under it. Only a window's own edge limits it (it scrolls there), not the overscan or the 8 that
     * a popup keeps clear of the edges, which would turn it over on a TV with the line of a session under the logo.
     */
    static final int UNDER_CENTRE_OF_CONTROL_NO_FLIP = 4;

    /**
     * The shadow's shape, from the desktop's render of a popup: it darkens the page by a quarter of its opacity at the sides and by
     * four fifths of it at the bottom edge, which is a blur with a sigma of 5 and an offset of 5, down.
     */
    private static final float SHADOW_SIGMA_DP = 5f;
    private static final float SHADOW_OFFSET_DP = 5f;
    private static final float SHADOW_PAD_DP = 24f;
    private static final float POPUP_RADIUS_DP = 10f;

    /** The control's rectangle in this layer's own coordinates, or null where it is gone. */
    interface Anchor { Rect rect(View layer); }

    private final Kind kind;
    private final View backdrop;
    private final View panel;
    private final float density;
    private final Rect safe = new Rect();
    private ShadowView shadow;
    private int preferredWidth;
    private int preferredHeight;
    private int placement = UNDER_RIGHT_EDGE;
    private int gap;
    private Anchor anchor;

    OverlayLayer(Context context, Kind kind, View backdrop, View panel) {
        super(context);
        this.kind = kind;
        this.backdrop = backdrop;
        this.panel = panel;
        this.density = context.getResources().getDisplayMetrics().density;
        addView(backdrop);
        addView(panel);
    }

    View backdrop() { return backdrop; }
    View panel() { return panel; }
    Kind kind() { return kind; }
    /** The popup's shadow, for the one who fades the popup in to fade it too; null for anything that casts none. */
    View shadow() { return shadow; }

    /** Gives the panel a shadow of this colour (Theme.popupShadow), drawn under it and a little below. */
    void setShadow(int color) {
        if (shadow != null) removeView(shadow);
        shadow = new ShadowView(getContext(), color, density);
        addView(shadow, indexOfChild(panel));
    }

    /** What the window keeps clear of at its edges: the status bar, and a TV's overscan. */
    void setSafe(int left, int top, int right, int bottom) {
        safe.set(left, top, right, bottom);
        requestLayout();
    }
    /** A card's width in pixels, or 0 for as wide as it holds. */
    void setPreferredWidth(int px) { preferredWidth = px; requestLayout(); }
    /**
     * A card's height in pixels, or 0 for as tall as it holds: the desktop's list cards (the app picker) are min(room, 460) high however
     * much they hold, and one that holds a long list does not run the length of the window. Never more than the window leaves room for.
     */
    void setPreferredHeight(int px) { preferredHeight = px; requestLayout(); }
    void setAnchor(Anchor anchor, int placement, int gapPx) {
        this.anchor = anchor;
        this.placement = placement;
        this.gap = gapPx;
        requestLayout();
    }

    private int dp(float n) { return Math.round(n * density); }

    /** Where a card's top may not rise above: 40 from the window's top, or the bar or overscan, whichever is lower. */
    private int cardFloor() { return Math.max(dp(40), safe.top); }

    private void measurePanel(int w, int h, int forcedHeight) {
        int availableWidth = w - safe.left - safe.right;
        int availableHeight = h - safe.top - safe.bottom;
        int side = dp(kind == Kind.CARD ? 14 : kind == Kind.CONFIRM ? 12 : 8);
        int maxWidth = Math.max(0, availableWidth - 2 * side);
        int maxHeight = kind == Kind.CARD ? Math.max(0, h - cardFloor() - Math.max(dp(12), safe.bottom))
            : Math.max(0, availableHeight - dp(kind == Kind.CONFIRM ? 24 : 16));
        int width = preferredWidth;
        if (placement == ABOVE_CONTROL && anchor != null) {
            Rect a = anchor.rect(this);
            if (a != null) width = a.width();
        }
        int widthSpec = width > 0
            ? MeasureSpec.makeMeasureSpec(Math.min(width, maxWidth), MeasureSpec.EXACTLY)
            : MeasureSpec.makeMeasureSpec(maxWidth, MeasureSpec.AT_MOST);
        int heightSpec = forcedHeight >= 0
            ? MeasureSpec.makeMeasureSpec(forcedHeight, MeasureSpec.EXACTLY)
            : kind == Kind.CARD && preferredHeight > 0
            ? MeasureSpec.makeMeasureSpec(Math.min(preferredHeight, maxHeight), MeasureSpec.EXACTLY)
            : MeasureSpec.makeMeasureSpec(maxHeight, MeasureSpec.AT_MOST);
        panel.measure(widthSpec, heightSpec);
    }

    @Override protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int w = MeasureSpec.getSize(widthMeasureSpec);
        int h = MeasureSpec.getSize(heightMeasureSpec);
        setMeasuredDimension(w, h);
        backdrop.measure(MeasureSpec.makeMeasureSpec(w, MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(h, MeasureSpec.EXACTLY));
        measurePanel(w, h, -1);
    }

    @Override protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
        int w = right - left;
        int h = bottom - top;
        backdrop.layout(0, 0, w, h);
        int panelWidth = panel.getMeasuredWidth();
        int panelHeight = panel.getMeasuredHeight();
        int availableWidth = w - safe.left - safe.right;
        int availableHeight = h - safe.top - safe.bottom;
        int x = safe.left + (availableWidth - panelWidth) / 2;
        int y;
        if (kind == Kind.CARD) {
            y = Math.max(cardFloor(), safe.top + (availableHeight - panelHeight) / 2);
        } else if (kind == Kind.CONFIRM) {
            y = safe.top + (availableHeight - panelHeight) / 2;
        } else {
            Rect a = anchor == null ? null : anchor.rect(this);
            int margin = dp(8);
            if (a == null) {
                y = safe.top + (availableHeight - panelHeight) / 2;
            } else {
                if (placement == UNDER_RIGHT_EDGE) x = a.right - panelWidth;
                else if (placement == UNDER_CENTRE_OF_CONTROL || placement == UNDER_CENTRE_OF_CONTROL_NO_FLIP) x = a.centerX() - panelWidth / 2;
                else if (placement == ABOVE_CONTROL) x = a.left;
                x = Math.max(safe.left + margin, Math.min(w - safe.right - margin - panelWidth, x));
                int below = (h - safe.bottom - margin) - (a.bottom + gap);
                int above = (a.top - gap) - (safe.top + margin);
                boolean wantsAbove = placement == ABOVE_CONTROL;
                if (placement == UNDER_CENTRE_OF_CONTROL_NO_FLIP) {
                    // Always under it. Short of the window's own edge it is as tall as it is; past it, it scrolls in what is left.
                    y = a.bottom + gap;
                    int room = h - y;
                    if (panelHeight > room) {
                        panelHeight = Math.max(0, room);
                        measurePanel(w, h, panelHeight);
                        panelWidth = panel.getMeasuredWidth();
                    }
                } else if (!wantsAbove && panelHeight <= below) {
                    y = a.bottom + gap;
                } else if (panelHeight <= above) {
                    y = a.top - gap - panelHeight;
                } else if (wantsAbove && panelHeight <= below) {
                    y = a.bottom + gap;
                } else {
                    // Neither side has room for all of it: the larger one, and it scrolls.
                    boolean underneath = below >= above;
                    panelHeight = Math.max(0, underneath ? below : above);
                    measurePanel(w, h, panelHeight);
                    panelWidth = panel.getMeasuredWidth();
                    y = underneath ? a.bottom + gap : a.top - gap - panelHeight;
                }
            }
        }
        panel.layout(x, y, x + panelWidth, y + panelHeight);
        if (shadow != null) {
            int pad = dp(SHADOW_PAD_DP);
            shadow.measure(MeasureSpec.makeMeasureSpec(panelWidth + 2 * pad, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(panelHeight + 2 * pad, MeasureSpec.EXACTLY));
            shadow.layout(x - pad, y - pad, x + panelWidth + pad, y + panelHeight + pad);
        }
    }

    /**
     * The popup's shadow: the panel's outline, black at the theme's opacity, blurred and moved down. A view of its own, drawn in software
     * where a blur is the same on every release (the platform's elevation shadow, which this replaces, is drawn at a fifth of the colour
     * it is given, so that a shadow of 0.5 came out at 0.1 and a menu hardly stood off the page).
     */
    private static final class ShadowView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float density;

        ShadowView(Context context, int color, float density) {
            super(context);
            this.density = density;
            setLayerType(LAYER_TYPE_SOFTWARE, null);
            setFocusable(false);
            setClickable(false);
            setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
            paint.setColor(color);
            // Skia's sigma is 0.57735 of the radius a BlurMaskFilter is given, and a half pixel more.
            float sigma = SHADOW_SIGMA_DP * density;
            paint.setMaskFilter(new BlurMaskFilter(Math.max(1f, (sigma - 0.5f) / 0.57735f), BlurMaskFilter.Blur.NORMAL));
        }

        @Override protected void onDraw(Canvas canvas) {
            float pad = SHADOW_PAD_DP * density;
            float down = SHADOW_OFFSET_DP * density;
            float radius = POPUP_RADIUS_DP * density;
            canvas.drawRoundRect(pad, pad + down, getWidth() - pad, getHeight() - pad + down, radius, radius, paint);
        }
    }
}
