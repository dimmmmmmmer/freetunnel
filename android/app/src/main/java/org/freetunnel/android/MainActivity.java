package org.freetunnel.android;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.BroadcastReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Configuration;
import android.animation.ArgbEvaluator;
import android.animation.ValueAnimator;
import android.database.Cursor;
import android.provider.OpenableColumns;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.text.LineBreaker;
import android.net.ConnectivityManager;
import android.net.LinkAddress;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.TrafficStats;
import android.net.Uri;
import android.net.VpnService;
import android.os.Bundle;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.TouchDelegate;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.view.animation.DecelerateInterpolator;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.core.content.ContextCompat;
import android.view.WindowManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.function.Consumer;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Android adaptation of the FreeTunnel desktop interface. */
public final class MainActivity extends Activity {
    private static final int PICK_CONFIG = 21;
    private static final int PREPARE_VPN = 22;
    private static final int EXPORT_CONFIG = 24;
    private static final int PICK_CERTIFICATE = 25;
    private static final String PREFS = "freetunnel";
    private String[] tabNames;
    // The Home tab is the logo, in its own greys: the app never tints it.
    private final int[] tabIcons = {R.drawable.ic_logo, R.drawable.ic_connection, R.drawable.ic_network,
            R.drawable.ic_settings, R.drawable.ic_log};
    private SharedPreferences prefs;
    private final List<Config> configs = new ArrayList<>();
    private final Map<Config, String> pingResults = new IdentityHashMap<>();
    // Each press of the ping button starts a new generation, and the answers of an older one are dropped. The probes
    // run side by side on a few threads, as the desktop's do.
    private int pingGeneration;
    private final ExecutorService pingWorker = Executors.newFixedThreadPool(4);
    // Writes an exported file, which a document provider may be slow to take.
    private final ExecutorService ioWorker = Executors.newSingleThreadExecutor();
    // The config an export is being asked for: the file picker answers later, when the list may have changed.
    private Config exporting;
    private int activeIndex;
    private int page;
    // The theme setting, "system" (the default), "light" or "dark", and the theme in
    // use: for "system" it follows the device's night mode (see Theme.isDark).
    private String themeMode = Theme.SYSTEM;
    private boolean dark = true;
    private Ui ui;
    // The window's one view, set once. render() rebuilds pageRoot, the nav and the page,
    // and nothing else: the overlays and the toast stay where they are.
    private FrameLayout host;
    private LinearLayout pageRoot;
    private FrameLayout overlayLayer;
    private FrameLayout toastLayer;
    // Popups and confirmations, above the toast: the desktop's toast is under its popups and its window-level confirmation, and over its cards.
    private FrameLayout popupLayer;
    // What a TV may crop at the top and the bottom, which the host is padded by (and Home's foot sits inside of: see home()).
    private int overscanY;
    // The status and navigation bars while a card or a confirmation dims the page: how many backdrops are over it (a card and a question
    // over it are two), the colour that gives the page, and where the bars are on the way to it now. Before Android 15 the bars are the
    // system's, outside the window or coloured by Window.setStatusBarColor; from there (the app targeting it) the window is edge to edge and
    // the strips under the bars are painted by barScrim, on the decor's overlay (BarScrim).
    private int barDims;
    private int barColor;
    private ValueAnimator barAnimator;
    private final BarScrim barScrim = new BarScrim();
    private boolean barScrimShown;
    // render() rebuilds every view. On a remote, the focused control's key and the
    // page's scroll position are carried over the rebuild (see restoreFocus).
    private ScrollView pageScroll;
    // The dot on the Settings tab while an update is waiting.
    private View settingsDot;
    private int renderedPage = -1;
    private int renderGeneration;
    private boolean restorePending;
    private String restoreKey;
    private int restoreScrollY;
    // Makes the next render() focus the page's own control (see onBackPressed).
    private boolean pageFocus;
    private int vpnState;
    // From the user's Disconnect until the service says it is off: the desktop's disconnecting.
    private boolean stopping;
    private final Runnable clearStopping = () -> { if (stopping) { stopping = false; if (page <= 1) render(); } };
    private String pendingConfig;
    private String pendingProfile;
    // The Split page's profiles are in the preferences (SplitStore); this is how they are reached. They are read from there
    // whenever they are wanted, and a change is a read, the change and a commit under one lock, so that the VPN service,
    // which saves there too, and this never overwrite one another.
    private SplitStore.Backing splitPrefs;
    // The "+" chip's field for the name of a new profile: open or not, what was typed in it, and the field itself while the
    // page shows it. It stays on the page, hidden, so that opening and closing it rebuilds nothing.
    private boolean profileNameOpen;
    private String profileNameDraft = "";
    private EditText profileNameInput;
    // True while render() takes the page apart: a field that loses its focus to that has not been left.
    private boolean rendering;
    // The key of what takes the focus when the next render() is done, if it is none of what had it.
    private String focusAfterRender;
    private boolean receiverRegistered;
    private boolean storageFailed;
    // What to tell the user while storage is failed: the amber banner on Configs and Settings, and a toast.
    private String storageProblem;
    // Settings' update line: see maintenance().
    private TextView updateAction;
    // Receive from phone: the open port and its card, both closed by stopReceiver().
    private LinkReceiver linkReceiver;
    // The texts it took that the TV has not answered yet: the one being checked or asked about and those
    // waiting for their turn, one at a time, so that they are asked about in the order they came. The receiver
    // counts them too, in the queue's count, and turns a send away with the busy page at LinkReceiver.MAX_WAITING.
    // They outlive the port: what was taken is the TV's to answer.
    private final ReceivedQueue receivedTexts = new ReceivedQueue(new AtomicInteger());
    // The notice that said why the last was refused is up, and the next waits until it is gone or until receivedHoldUntil.
    private boolean receivedHold;
    private long receivedHoldUntil;
    private boolean receivedPollPending;
    // Texts taken by the open receiver, for its card's line, and that line while the card is up.
    private int receivedCount;
    private TextView receivedLine;
    // Bumped when the app is left: texts the receiver's thread handed over before that, which only
    // reach the main thread after it, are dropped.
    private int receiveGeneration;
    private long validationRequest;
    private final ExecutorService validationWorker = Executors.newSingleThreadExecutor();
    // The installed applications are listed here, so that the app picker never waits on a scan.
    private final ExecutorService scanWorker = Executors.newSingleThreadExecutor();
    private final BroadcastReceiver vpnReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            // Why a connection ended in failure, while the app is on the screen: said in the window, as the desktop's errorOccurred toast is
            // (the notice in the shade is silent meanwhile, and Home shows only that the VPN is off).
            if (FreeTunnelVpnService.ACTION_FAILURE.equals(intent.getAction())) {
                String reason = intent.getStringExtra(FreeTunnelVpnService.EXTRA_REASON);
                if (reason != null && !reason.isEmpty()) toast(reason);
                return;
            }
            vpnState = FreeTunnelVpnService.state(context);
            if (vpnState == 0 && stopping) { stopping = false; host.removeCallbacks(clearStopping); }
            // Home shows the state, and so does the selected config's badge on Configs.
            if (page <= 1) render();
            QuickTileService.refresh(context);
        }
    };

    private static final class Config {
        String name;
        String toml;
        // The split profile this config uses, as it is named there: Default unless the config was given another in
        // the editor. A name that is no longer a profile counts as Default (SplitProfiles.effective).
        String profile;
        Config(String name, String toml) { this(name, toml, SplitProfiles.DEFAULT); }
        Config(String name, String toml, String profile) { this.name = name; this.toml = toml; this.profile = profile; }
    }

    // The words are in the language chosen in Settings, which Android 8 to 12 only knows from this (AppLocale).
    @Override protected void attachBaseContext(Context base) { super.attachBaseContext(AppLocale.wrap(base)); }

    // The page is kept over a recreation, which a change of language (and of the system's own theme) is: the user is
    // still on Settings afterwards. Nothing else is, not even an open overlay.
    @Override protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putInt("page", page);
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (state != null) page = Math.max(0, Math.min(4, state.getInt("page", 0)));
        tabNames = new String[] {getString(R.string.text_home), getString(R.string.text_configurations), getString(R.string.text_routing), getString(R.string.text_settings), getString(R.string.text_log)};
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        // The old "dark" boolean was written only when the user picked Light or Dark.
        themeMode = Theme.mode(prefs.getString("theme_mode", null), prefs.contains("dark") ? Boolean.valueOf(prefs.getBoolean("dark", true)) : null);
        if (!prefs.contains("theme_mode")) prefs.edit().putString("theme_mode", themeMode).remove("dark").apply();
        dark = Theme.isDark(themeMode, nightMode());
        ui = new Ui(this);
        buildHost();
        // A keyboard or a remote used on a phone: its first key leaves touch mode, and Android then gives the focus to the first control
        // in the window, the Home tab, and a rebuild keeps it there. The page has a control of its own to land on, the one a TV's window
        // lands on when it starts outside touch mode (focusTarget): the logo on Home, the tab elsewhere.
        // Only where nothing has the focus: a field that was being typed in keeps it.
        host.getViewTreeObserver().addOnTouchModeChangeListener(inTouchMode -> {
            if (inTouchMode || isDestroyed() || getCurrentFocus() != null) return;
            Overlay top = topModalOverlay();
            if (top != null) { focusOverlay(top); return; }
            View target = focusTarget(null);
            if (target != null) target.requestFocus();
        });
        activeIndex = prefs.getInt("active", 0);
        vpnState = FreeTunnelVpnService.state(this);
        // Before anything else touches the preferences: the first start of this version makes the profiles out of what
        // is there, and tells a new installation (nothing saved at all) from an update by that (SplitStore).
        splitPrefs = new SplitPrefs(prefs);
        splitModel();
        loadConfigs();
        syncMirror();
        render();
        // As the desktop checks 1.2 s after it starts (Backend.cpp): silently, saying something only for an update it finds. The
        // updater holds it to once in 12 hours, and to the official build (AndroidUpdater.checkOnLaunch).
        if (state == null) host.postDelayed(() -> { if (!isDestroyed()) AndroidUpdater.INSTANCE.checkOnLaunch(getApplicationContext()); }, 1200);
        if (state == null) handleIncomingIntent(getIntent());
        if (state == null && prefs.getBoolean("auto_connect", false) && vpnState == 0
                && !configs.isEmpty() && !Intent.ACTION_VIEW.equals(getIntent().getAction())) {
            host.post(() -> { if (FreeTunnelVpnService.state(this) == 0) connect(); });
        }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, 23);

    }

    @Override protected void onResume() {
        super.onResume();
        resumed = true;
        FreeTunnelVpnService.clearStaleAlert(this);
        if (!receiverRegistered) {
            IntentFilter filter = new IntentFilter(FreeTunnelVpnService.ACTION_STATE);
            filter.addAction(FreeTunnelVpnService.ACTION_FAILURE);
            ContextCompat.registerReceiver(this, vpnReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED);
            receiverRegistered = true;
        }
        // After the receiver: the service sends a failure's reason here only while the app is on the screen.
        FreeTunnelVpnService.setAppVisible(true);
        AndroidUpdater.INSTANCE.observe(() -> { refreshUpdateDot(); if (page == 3) refreshUpdateControls(); });
        AndroidUpdater.INSTANCE.recoverInstall(this);
        vpnState = FreeTunnelVpnService.state(this);
        if (vpnState == 0) stopping = false;
        // Settings is only refreshed, not rebuilt, so it keeps its scroll position.
        if (page <= 1) render();
        else if (page == 3) refreshUpdateControls();
        else if (page == 4) { refreshLog(false); scheduleLogTick(); }
    }

    @Override protected void onPause() {
        resumed = false;
        FreeTunnelVpnService.setAppVisible(false);
        host.removeCallbacks(homeTicker);
        host.removeCallbacks(logTicker);
        // Nothing animates while the app is not shown, and the speeds are of the time it is.
        hero.release();
        speedMeter.reset();
        // The logo does not animate across the time the app was not shown.
        homeFresh = true;
        AndroidUpdater.INSTANCE.observe(null);
        if (receiverRegistered) {
            unregisterReceiver(vpnReceiver);
            receiverRegistered = false;
        }
        super.onPause();
    }

    // Home, a screensaver or a recreation closes the port: it is open only while its
    // card is on the screen. What it took and the TV has not got to is dropped with it,
    // except the one being asked about, whose question is still there to answer.
    @Override protected void onStop() {
        stopReceiver();
        dropReceived();
        super.onStop();
    }

    // The manifest keeps the activity through a turn of the screen, a resize and a keyboard coming or going
    // (configChanges), so that an open card, and what is typed in the editor, are not lost to them. The
    // page is laid out for the new size; the overlays lay themselves out. A change of theme or of language
    // still recreates the activity, which the colours and the words need.
    @Override public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config);
        // The dropdown was sized and placed for the window it opened in: a resize closes it, as it does on the desktop.
        Overlay picker = overlay("homepicker");
        if (picker != null) closeOverlay(picker);
        render();
    }

    @Override protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIncomingIntent(intent);
    }

    private void handleIncomingIntent(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction())) return;
        Uri uri = intent.getData();
        intent.setData(null); // Do not import again if Android recreates the activity.
        if (uri != null) importDeepLink(uri.toString(), null);
    }

    private int dp(float n) { return ui.dp(n); }
    // The theme in use: the desktop's colours, in the dark or the light theme.
    private Theme t() { return Theme.of(dark); }
    private int nightMode() { return getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK; }
    private LinearLayout column() { return ui.column(); }
    private LinearLayout row() { return ui.row(); }
    private LinearLayout.LayoutParams lp(int w, int h) { return ui.lp(w, h); }
    private LinearLayout.LayoutParams weighted() { return new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1); }
    private TextView label(String value, float size, int color) { return ui.label(value, size, color); }
    private void gap(LinearLayout p, int size) { ui.gap(p, size); }
    // SectionLabel: 12 in textFaint as it is typed, 16 below the section before it.
    private void section(LinearLayout p, String title) {
        if (p.getChildCount() > 0) ui.section(p, title);
        else p.addView(ui.sectionLabel(title), lp(-1, -2));
    }
    // Every notice is the desktop's toast, in the window; the system's Toast is used only where no
    // window of ours can show one (the tile, the VPN consent screen).
    private void toast(String text) { showToast(text); }
    // Android TV, Google TV and Fire TV: driven by a remote, seen from across the room.
    private boolean isTv() {
        return (getResources().getConfiguration().uiMode & Configuration.UI_MODE_TYPE_MASK) == Configuration.UI_MODE_TYPE_TELEVISION;
    }
    // TVs often leave out screens that phones have, such as the file picker or the VPN
    // consent dialog, and starting one that is missing throws. False when it is missing.
    private boolean startForResult(Intent intent, int request) {
        try { startActivityForResult(intent, request); return true; }
        catch (ActivityNotFoundException e) { return false; }
    }

    // The window's one view, set once: render() never calls setContentView again, which
    // would take every overlay and the toast with it. The page is rebuilt in pageRoot;
    // overlays and the toast are in layers above it that render() leaves alone.
    private void buildHost() {
        host = new FrameLayout(this);
        // The overlays' layers reach the window's edges past this padding, and a layer is only drawn as far as its parent's padding by
        // default: the backdrop covers all of the window as the desktop's does, and no lighter frame is left round it where a TV's
        // overscan margin is. (Under an edge-to-edge window's bars that did not hold on a phone with Android 15, whatever the layer's
        // geometry says: BarScrim paints the strips there.)
        host.setClipToPadding(false);
        pageRoot = new LinearLayout(this);
        pageRoot.setOrientation(LinearLayout.VERTICAL);
        host.addView(pageRoot, new FrameLayout.LayoutParams(-1, -1));
        overlayLayer = new FrameLayout(this);
        host.addView(overlayLayer, new FrameLayout.LayoutParams(-1, -1));
        toastLayer = new FrameLayout(this);
        host.addView(toastLayer, new FrameLayout.LayoutParams(-1, -1));
        popupLayer = new FrameLayout(this);
        host.addView(popupLayer, new FrameLayout.LayoutParams(-1, -1));
        // A TV may crop the edges of the picture (overscan), so content keeps clear of them.
        // Set here as well as with the bars: before Android 15 the window is not edge to
        // edge, the decor takes the insets, and this listener may never run. On the TV
        // emulator (Android 14) it did not.
        int sides = isTv() ? dp(48) : 0;
        int ends = isTv() ? dp(27) : 0;
        overscanY = ends;
        setHostPadding(sides, ends, sides, ends);
        host.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                // The keyboard, when it is up, covers the bottom bar too: its inset is the larger.
                android.graphics.Insets keyboard = insets.getInsets(WindowInsets.Type.ime());
                setHostPadding(sides, bars.top + ends, sides, Math.max(bars.bottom, keyboard.bottom) + ends);
            } else {
                setHostPadding(sides, insets.getSystemWindowInsetTop() + ends, sides, insets.getSystemWindowInsetBottom() + ends);
            }
            return insets;
        });
        setContentView(host);
        // The system's own scrim behind a three-button navigation bar (80% of a colour of its choosing) is gone: it was drawn over the
        // dim of a card as a band of another grey, and what the bar shows is the window's, as on a gesture-navigation phone.
        if (Build.VERSION.SDK_INT >= 29) getWindow().setNavigationBarContrastEnforced(false);
        // The strips follow the window when it turns or resizes.
        getWindow().getDecorView().addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> { if (barScrimShown) placeBarScrim(); });
    }
    // The padding that keeps the page, the popups and the toast clear of the edges. An
    // overlay's backdrop still covers all of the window, so its layer is as large as it
    // and each overlay places its panel inside the padding.
    private void setHostPadding(int left, int top, int right, int bottom) {
        host.setPadding(left, top, right, bottom);
        for (FrameLayout layer : new FrameLayout[] {overlayLayer, popupLayer}) {
            FrameLayout.LayoutParams p = (FrameLayout.LayoutParams) layer.getLayoutParams();
            p.setMargins(-left, -top, -right, -bottom);
            layer.setLayoutParams(p);
        }
        for (Overlay overlay : overlays) overlay.layer.setSafe(left, top, right, bottom);
    }

    private void render() {
        // Until the last render()'s views are drawn, nothing in them has the focus and
        // they have not scrolled, so a render() before that, such as the last ping result
        // and the end of the ping in one frame, keeps what that one is about to restore.
        if (!restorePending) {
            View focused = getCurrentFocus();
            restoreKey = focused != null && focused.getTag() instanceof String ? (String) focused.getTag() : null;
            restoreScrollY = pageScroll != null && renderedPage == page ? pageScroll.getScrollY() : 0;
        } else if (renderedPage != page) restoreScrollY = 0;
        if (pageFocus) { restoreKey = null; pageFocus = false; }
        // The control a change asked to have the focus after it, in place of whichever had it: a field left open for the
        // next one, or the + chip a new profile's name was typed from.
        final String asked = focusAfterRender;
        if (focusAfterRender != null) { restoreKey = focusAfterRender; focusAfterRender = null; }
        int generation = ++renderGeneration;
        // The field for a new profile's name belongs to the Split page: on any other it is dropped.
        if (page != 2) { profileNameOpen = false; profileNameDraft = ""; }
        homeContinues = !homeFresh && renderedPage == 0 && page == 0;
        homeFresh = false;
        dark = Theme.isDark(themeMode, nightMode());
        ui.update(dark);
        Theme t = t();
        if (barAnimator != null) barAnimator.cancel();
        setBarColor(dimmedPage(barDims));
        getWindow().getDecorView().setSystemUiVisibility(dark ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        // The window itself too, so that nothing of the system theme's dark shows before a frame, or at an edge.
        getWindow().setBackgroundDrawable(new ColorDrawable(t.bg));
        host.setBackgroundColor(t.bg);
        // What was suggested under the application field is for a field that is going.
        appSuggestions.close();
        // A field that has the focus loses it to this, and has not been left (see closeProfileName).
        rendering = true;
        try { pageRoot.removeAllViews(); } finally { rendering = false; }
        profileNameInput = null;
        nav();
        pageScroll = null;
        if (page != 4) { logText = null; logBox = null; logPlaceholder = null; }
        else if (renderedPage != 4) { logAutoScroll = true; logStamp = -1; }
        if (page != 0) {
            // Nothing of Home is shown: its logo may not go on pulsing at views that are gone.
            hero.release();
            speedMeter.reset();
            homeStatus = null; homeDown = null; homeUp = null; homeSelectorRow = null; homeSpeedRow = null;
        }
        if (page == 0) home();
        else if (page == 4) {
            // The log fills the page, and scrolls in its own box, as the desktop's does: nothing scrolls round it. 18 on each side,
            // 6 at the top and 12 at the foot.
            LinearLayout body = column();
            body.setPadding(dp(18), dp(6), dp(18), dp(12));
            // The toggle ends at the margin, and the row round it fills a little beyond it with the focus. That row is a child of a line
            // of the page, which clips what it holds to its bounds unless its own parent, this, says it need not.
            body.setClipToPadding(false);
            body.setClipChildren(false);
            pageRoot.addView(body, new LinearLayout.LayoutParams(-1, 0, 1));
            logs(body);
        } else {
            ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setClipToPadding(false);
            scroll.setVerticalScrollBarEnabled(false);
            // The desktop's pages fill the width with 18 on each side and have no widest: at a TV's width a config's row goes from edge
            // to edge with its icons at the far right (the TV's own overscan is the host's padding).
            int side = dp(18);
            // Settings has 6 above and 14 below; Split 10 above (its first spacer) and 16 below (its last); Configs 8 and 26.
            LinearLayout body = column();
            body.setPadding(side, dp(page == 3 ? 6 : page == 2 ? 10 : 8), side, dp(page == 3 ? 14 : page == 2 ? 16 : 26));
            scroll.addView(body);
            // Links, toggles and dropdown values end at the margin, and a toggle's row fills a little beyond it with the focus.
            body.setClipToPadding(false);
            pageRoot.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
            pageScroll = scroll;
            switch (page) {
                case 1: configs(body); break;
                case 2: split(body); break;
                default: settings(body); break;
            }
        }
        renderedPage = page;
        restoreFocus(generation, restoreKey, restoreScrollY);
        refocusFields(asked);
        // Its list is the configs as they were: with another page or another list it is no longer what it was opened on.
        Overlay picker = overlay("homepicker");
        if (picker != null && (page != 0 || configs.size() != homePickerCount)) closeOverlay(picker);
        reanchorOverlays();
        scheduleHomeTick();
        scheduleLogTick();
        // Home's toast sits between the selector and the tiles, which are only where they are once laid out.
        host.post(this::placeToast);
    }

    // Puts a remote's focus back on the control that had it before render(), and the
    // page back where it was scrolled to. A phone in touch mode shows no focus and is
    // left as it was. This runs once the new views are laid out, because a ScrollView
    // cannot scroll before that, and only for the latest render(): ping results can
    // render several times in one frame, and restorePending keeps the key and the
    // scroll position the same over them. With an overlay up the focus is in it, and
    // stays there: the page behind it takes none.
    private void restoreFocus(int generation, String key, int scrollY) {
        View decor = getWindow().getDecorView();
        restorePending = !decor.isInTouchMode();
        if (!restorePending) return;
        ScrollView scroll = pageScroll;
        decor.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
            @Override public boolean onPreDraw() {
                decor.getViewTreeObserver().removeOnPreDrawListener(this);
                if (generation != renderGeneration) return true;
                restorePending = false;
                if (decor.isInTouchMode()) return true;
                if (scroll != null) scroll.scrollTo(0, scrollY);
                if (anyModalOverlay()) return true;
                View target = focusTarget(key);
                if (target != null) target.requestFocus();
                return true;
            }
        });
    }
    // In touch mode restoreFocus does nothing, which suits a page that was only drawn again (a ping result, a turn of the screen:
    // none of them brings a keyboard up), but not a field that is being typed in: the one for a new profile's name while it is open,
    // and the domains field (or Settings' routes field) after one Enter, for the next, which is what asked is: the key a change asked
    // the focus for (focusAfterRender), and null for any other render.
    private void refocusFields(String asked) {
        if ((page != 2 && page != 3) || !getWindow().getDecorView().isInTouchMode()) return;
        String wanted = page == 2 ? (profileNameOpen ? "profile:name" : "domains:input".equals(asked) || "apps:input".equals(asked) ? asked : null)
                : "routes:input".equals(asked) ? asked : null;
        if (wanted == null) return;
        View field = pageRoot.findViewWithTag(wanted);
        if (!(field instanceof EditText)) return;
        field.post(() -> {
            if (!field.isAttachedToWindow() || anyModalOverlay() || field.getVisibility() != View.VISIBLE) return;
            field.requestFocus();
            showKeyboard(field);
        });
    }
    private static final Pattern NUMBERED_KEY = Pattern.compile("([a-z]+):(\\d{1,5})(:[a-z]+)?");
    // The control with this key. When it is gone, such as after a deletion, the nearest
    // one before it: config:2:edit, then config:1:edit and config:0:edit; and where there is none of its kind, what is beside
    // where it was (see neighbours). Otherwise the page's own: the logo on Home, and the page's tab elsewhere.
    private View focusTarget(String key) {
        View found = key == null ? null : host.findViewWithTag(key);
        Matcher numbered = NUMBERED_KEY.matcher(key == null ? "" : key);
        if (found == null && numbered.matches()) {
            String rest = numbered.group(3) == null ? "" : numbered.group(3);
            for (int n = Integer.parseInt(numbered.group(2)) - 1; n >= 0 && found == null; n--)
                found = host.findViewWithTag(numbered.group(1) + ":" + n + rest);
        }
        if (found == null && key != null) {
            for (String next : neighbours(key)) {
                found = pageRoot.findViewWithTag(next);
                if (found != null) break;
            }
        }
        if (found == null) found = pageRoot.findViewWithTag(page == 0 ? "home:toggle" : "tab:" + page);
        return found;
    }
    // What is beside a control that is gone when no other of its kind is left, by key, nearest first: a rule's Clear all that has cleared
    // the rules goes to the link beside it, the last chip of a list to the link or the field above it, the last config to the words
    // that stand in for the list, the last custom profile to the chip before it. Without these a deletion would send the remote
    // to the page's tab, at the top, while the page stays scrolled to where the control was.
    private static String[] neighbours(String key) {
        Matcher numbered = NUMBERED_KEY.matcher(key);
        if (numbered.matches()) {
            int n = Integer.parseInt(numbered.group(2));
            switch (numbered.group(1)) {
                case "config": return new String[]{"configs:hint", "configs:add"};
                case "profile": return new String[]{"profile:" + Math.max(0, n - 1), "profile:add"};
                case "domain": return new String[]{"domains:russia", "domains:input"};
                case "app": return new String[]{"apps:choose"};
                case "route": return new String[]{"routes:restore", "routes:input"};
                default: return new String[0];
            }
        }
        switch (key) {
            case "domains:clear": return new String[]{"domains:russia", "domains:input"};
            case "apps:clear": return new String[]{"apps:choose"};
            case "routes:clear": return new String[]{"routes:restore", "routes:input"};
            default: return new String[0];
        }
    }

    // Back closes what is on top first: an overlay. Without tabs to tap, Back on a TV then
    // goes to Home, and leaves the app from there. Home's own control gets the focus: left
    // on the tab it was on, the next OK would go straight back to that page.
    // Still delivered at targetSdk 35; targetSdk 36 needs an OnBackInvokedCallback.
    @SuppressWarnings("deprecation")
    @Override public void onBackPressed() {
        Overlay top = topOverlay();
        if (top != null) { escape(top); return; }
        // The field for a new profile's name is what Back closes first, as Escape does on the desktop.
        if (profileNameOpen) { closeProfileName(); return; }
        if (isTv() && page != 0) { page = 0; pageFocus = true; render(); return; }
        super.onBackPressed();
    }
    // Escape is Back on a keyboard, as it is on the desktop.
    @Override public boolean onKeyUp(int keyCode, KeyEvent event) {
        Overlay top = topOverlay();
        if (keyCode == KeyEvent.KEYCODE_ESCAPE && top != null) { escape(top); return true; }
        return super.onKeyUp(keyCode, event);
    }

    // Overlays: a card (the editor, a picker), a confirmation, a popup (a dropdown, a menu).
    //
    // showOverlay is the only code that adds to overlayLayer (cards) and popupLayer (popups and
    // confirmations, which are above the toast), and every overlay in the
    // app goes through it (tests/test_android_tv_dialogs.sh). What it gives each:
    //  - a backdrop, black at 0.45 under a card or a confirmation and clear under a popup,
    //    which swallows touches, and a tap on which is that overlay's Escape;
    //  - the desktop's card: bg, a 1 px border, radius 14 (12 for a confirmation, 10 for a
    //    popup, which has a shadow), fading in over 130 ms as it slides down 8;
    //  - a place in a stack. While anything is open the page behind takes no focus and is
    //    hidden from accessibility, and so does every overlay below the top one;
    //  - while a card or a confirmation is open, the status and navigation bars dim with the page, as the desktop's backdrop dims its whole
    //    window: once for each backdrop over the page (a card and a question over it make two), as a colour of the bars where they are the
    //    system's, and as strips painted over them where the window is edge to edge (dimBars, BarScrim);
    //  - a passive overlay, the suggestions under a field being typed in, is none of that: it has no
    //    backdrop that takes a touch, the page behind keeps the focus and what it hears, and the field
    //    keeps the keyboard; only Back and Escape, which close it first, treat it as an overlay at all;
    //  - a default focus, and the focus back on what opened it, by key, when it closes;
    //  - a life that render() does not end: a popup is put back under its anchor, by key,
    //    after a render, and closes if the anchor is gone;
    //  - Back: it closes the top one (see onBackPressed).

    private final List<Overlay> overlays = new ArrayList<>();
    private boolean pageBlocked;

    private final class Overlay {
        final String name;
        final OverlayLayer.Kind kind;
        final String anchorKey;
        final Runnable onClose;
        final String openerKey;
        OverlayLayer layer;
        // Not modal: see showOverlay.
        boolean passive;
        // What Back, Escape and a tap on the backdrop do: close it, unless it asks first.
        Runnable escape;
        // The key of what takes the focus when it opens, else its first control.
        String defaultKey;
        boolean closed;
        Overlay(String name, OverlayLayer.Kind kind, String anchorKey, Runnable onClose, String openerKey) {
            this.name = name; this.kind = kind; this.anchorKey = anchorKey; this.onClose = onClose; this.openerKey = openerKey;
            this.escape = () -> closeOverlay(this);
        }
        Overlay width(int px) { layer.setPreferredWidth(px); return this; }
        Overlay height(int px) { layer.setPreferredHeight(px); return this; }
        void close() { closeOverlay(this); }
    }

    private Overlay topOverlay() { return overlays.isEmpty() ? null : overlays.get(overlays.size() - 1); }
    // The top one that takes the window: not the suggestions under a field, which have nothing to give the focus to.
    private Overlay topModalOverlay() {
        for (int i = overlays.size() - 1; i >= 0; i--) if (!overlays.get(i).passive) return overlays.get(i);
        return null;
    }
    private Overlay overlay(String name) {
        for (Overlay open : overlays) if (open.name.equals(name)) return open;
        return null;
    }
    private void escape(Overlay overlay) { if (!overlay.closed) overlay.escape.run(); }

    // content goes inside the card, which is as wide as it holds (a card's width: width()).
    // A second overlay of the same name is not opened: the one that is up is returned.
    private Overlay showOverlay(String name, OverlayLayer.Kind kind, View content, String anchorKey, Runnable onClose) {
        return showOverlay(name, kind, content, anchorKey, onClose, false);
    }
    private Overlay showOverlay(String name, OverlayLayer.Kind kind, View content, String anchorKey, Runnable onClose, boolean passive) {
        Overlay open = overlay(name);
        if (open != null) return open;
        Theme t = t();
        View focused = getCurrentFocus();
        String opener = focused != null && focused.getTag() instanceof String ? (String) focused.getTag() : null;
        // A question asked over a form puts the keyboard away: it would cover it.
        if (kind == OverlayLayer.Kind.CONFIRM && focused instanceof EditText) hideKeyboard();
        final Overlay overlay = new Overlay(name, kind, anchorKey, onClose, opener);
        overlay.passive = passive;
        FrameLayout panel = new FrameLayout(this);
        panel.setBackground(ui.outlined(t.bg, t.border, kind == OverlayLayer.Kind.CARD ? 14 : kind == OverlayLayer.Kind.CONFIRM ? 12 : 10));
        // A touch on the card is the card's: it never reaches the backdrop, whose tap is Escape.
        panel.setOnTouchListener((v, e) -> true);
        panel.addView(content, new FrameLayout.LayoutParams(-1, -2));
        View backdrop = new View(this);
        backdrop.setBackgroundColor(kind == OverlayLayer.Kind.POPUP ? Color.TRANSPARENT : t.backdrop);
        if (!passive) backdrop.setOnClickListener(v -> escape(overlay));
        backdrop.setFocusable(false);
        OverlayLayer layer = new OverlayLayer(this, kind, backdrop, panel);
        layer.setTag("overlay:" + name);
        layer.setSafe(host.getPaddingLeft(), host.getPaddingTop(), host.getPaddingRight(), host.getPaddingBottom());
        if (anchorKey != null) layer.setAnchor(l -> anchorRect(anchorKey, l), OverlayLayer.UNDER_RIGHT_EDGE, dp(4));
        // The desktop's popups cast a shadow (MultiEffect, black at 0.5 or 0.2): the layer draws one that shows, which the platform's
        // elevation shadow, at about a fifth of what it is told, does not.
        if (kind == OverlayLayer.Kind.POPUP && !passive) layer.setShadow(t.popupShadow);
        overlay.layer = layer;
        overlays.add(overlay);
        FrameLayout parent = kind == OverlayLayer.Kind.CARD ? overlayLayer : popupLayer;
        parent.addView(layer, new FrameLayout.LayoutParams(-1, -1));
        updateOverlayState();
        backdrop.setAlpha(0f);
        panel.setAlpha(0f);
        panel.setTranslationY(-dp(8));
        backdrop.animate().alpha(1f).setDuration(130).start();
        panel.animate().alpha(1f).translationY(0f).setDuration(130).setInterpolator(new DecelerateInterpolator(1.5f)).start();
        View shadow = layer.shadow();
        if (shadow != null) {
            shadow.setAlpha(0f);
            shadow.setTranslationY(-dp(8));
            shadow.animate().alpha(1f).translationY(0f).setDuration(130).setInterpolator(new DecelerateInterpolator(1.5f)).start();
        }
        // After the caller has said which control comes first. A phone in touch mode has no focus to give. A passive one gives none: the
        // field it is the suggestions of keeps it.
        if (!passive) host.post(() -> {
            if (!overlay.closed && topModalOverlay() == overlay && !getWindow().getDecorView().isInTouchMode()) focusOverlay(overlay);
        });
        return overlay;
    }
    private void focusOverlay(Overlay overlay) {
        View target = overlay.defaultKey == null ? null : overlay.layer.findViewWithTag(overlay.defaultKey);
        if (target == null) {
            ArrayList<View> controls = new ArrayList<>();
            overlay.layer.addFocusables(controls, View.FOCUS_FORWARD);
            if (!controls.isEmpty()) target = controls.get(0);
        }
        if (target != null) target.requestFocus();
    }

    // Closes it: gone from the stack at once, and its views fade out over 130 ms. Its keys
    // are no one's from then on, so that a control with the same key in the next overlay,
    // such as a queued confirmation's buttons, is the only one a search finds.
    private void closeOverlay(Overlay overlay) {
        if (overlay.closed) return;
        overlay.closed = true;
        // A field of it that had the keyboard takes it away: left up, it would cover the page.
        View focused = getCurrentFocus();
        boolean typing = focused instanceof EditText && isInside(focused, overlay.layer);
        overlays.remove(overlay);
        retag(overlay.layer);
        overlay.layer.setDescendantFocusability(ViewGroup.FOCUS_BLOCK_DESCENDANTS);
        updateOverlayState();
        // Before the focus goes back: what it does may be to show the control that is to have it, such as the empty Configs page's
        // "Add a config", which is hidden while its own menu is open, and a view that is not shown takes no focus.
        if (overlay.onClose != null) overlay.onClose.run();
        // A passive overlay never took it, so there is nothing to give back (and the keyboard stays with the field).
        if (!overlay.passive) returnFocus(overlay);
        if (typing && !overlay.passive) hideKeyboard();
        // The fade ends in taking the view away. Not left to the animation alone: with the activity stopped, as
        // Receive from phone's card is when the app is left, it may never run, and the card would be there,
        // invisible and in the way, when the app comes back.
        Runnable remove = () -> {
            if (overlay.layer.getParent() instanceof ViewGroup) ((ViewGroup) overlay.layer.getParent()).removeView(overlay.layer);
        };
        overlay.layer.animate().alpha(0f).setDuration(130).withEndAction(remove).start();
        host.postDelayed(remove, 400);
    }
    private static boolean isInside(View view, View ancestor) {
        for (View up = view; up != null; ) {
            if (up == ancestor) return true;
            up = up.getParent() instanceof View ? (View) up.getParent() : null;
        }
        return false;
    }
    private void retag(View view) {
        if (view.getTag() instanceof String) view.setTag(null);
        if (view instanceof ViewGroup) for (int i = 0; i < ((ViewGroup) view).getChildCount(); i++) retag(((ViewGroup) view).getChildAt(i));
    }
    // To what opened it, by key; failing that the overlay now on top; failing that the page's
    // own control (focusTarget). A phone in touch mode has no focus to return.
    // A control that refuses the focus (hidden, or behind the overlay that is now on top) is no more a place to put it than one
    // that is gone: left there, the focus stays in the layer that is closing, and when that goes Android hands it to the first
    // control of the window, the Home tab.
    private void returnFocus(Overlay closed) {
        if (getWindow().getDecorView().isInTouchMode()) return;
        View target = closed.openerKey == null ? null : host.findViewWithTag(closed.openerKey);
        if (target != null && target.requestFocus()) return;
        Overlay top = topModalOverlay();
        if (top != null) { focusOverlay(top); return; }
        // Gone, the nearest one to it; or there, and refusing, the page's own: the same key would only find the same control.
        View next = focusTarget(target == null ? closed.openerKey : null);
        if (next != null) next.requestFocus();
    }

    // The page behind an overlay takes no focus and no accessibility, nor does an overlay
    // under another: the top one is the only part of the window a remote can reach. A passive overlay
    // (the suggestions under a field) is not one that does this: it is not modal.
    private void updateOverlayState() {
        Overlay topModal = null;
        int dims = 0;
        for (Overlay open : overlays) {
            if (open.passive) continue;
            topModal = open;
            if (open.kind != OverlayLayer.Kind.POPUP) dims++;
        }
        boolean any = topModal != null;
        if (any != pageBlocked) {
            pageBlocked = any;
            pageRoot.setDescendantFocusability(any ? ViewGroup.FOCUS_BLOCK_DESCENDANTS : ViewGroup.FOCUS_BEFORE_DESCENDANTS);
            pageRoot.setImportantForAccessibility(any ? View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS : View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
        }
        for (Overlay open : overlays) {
            if (open.passive) continue;
            boolean topmost = open == topModal;
            OverlayLayer layer = open.layer;
            layer.setDescendantFocusability(topmost ? ViewGroup.FOCUS_BEFORE_DESCENDANTS : ViewGroup.FOCUS_BLOCK_DESCENDANTS);
            layer.setImportantForAccessibility(topmost ? View.IMPORTANT_FOR_ACCESSIBILITY_AUTO : View.IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS);
        }
        dimBars(dims);
        placeToast();
    }

    // Whether anything is open that takes the window: not the suggestions under a field.
    private boolean anyModalOverlay() {
        for (Overlay open : overlays) if (!open.passive) return true;
        return false;
    }

    // The page's colour under this many backdrops, each black at 0.45 over what is below it: the dim of the desktop's whole window, which
    // a card makes once and a question over a card makes twice.
    private int dimmedPage(int dims) {
        Theme t = t();
        int color = t.bg;
        for (int i = 0; i < dims; i++) color = Theme.over(t.backdrop, color);
        return color;
    }

    // The status and navigation bars take the dim of the backdrops with the page, over the same 130 ms. Before Android 15 they are the
    // system's, outside the window or coloured by the window's own setting, and take it as a colour. From Android 15 on, with the app
    // targeting it (the phone leg), the window is edge to edge: the bars are drawn over it, their colours cannot be set, and the
    // backdrops, which are views of the window, did not reach under them, so that a strip of the undimmed page was left above a dimmed
    // page and the navigation bar's own scrim (a band of a third grey) below it. There the strips are BarScrim's.
    private void dimBars(int dims) {
        if (dims == barDims) return;
        barDims = dims;
        int to = dimmedPage(dims);
        if (barAnimator != null) barAnimator.cancel();
        barAnimator = ValueAnimator.ofObject(new ArgbEvaluator(), barColor == 0 ? t().bg : barColor, to);
        barAnimator.setDuration(130);
        barAnimator.addUpdateListener(a -> setBarColor((Integer) a.getAnimatedValue()));
        barAnimator.start();
    }
    private void setBarColor(int color) {
        barColor = color;
        if (Build.VERSION.SDK_INT >= 35) {
            // The platform's colours are ignored here, and the navigation bar's one is the base of a scrim: nothing of them may show.
            getWindow().setStatusBarColor(Color.TRANSPARENT);
            getWindow().setNavigationBarColor(Color.TRANSPARENT);
            paintBarScrim(color);
        } else {
            getWindow().setStatusBarColor(color);
            getWindow().setNavigationBarColor(color);
        }
    }
    // The strips under the bars, as this colour: on the decor's overlay while the page is dimmed, and gone when it is not.
    private void paintBarScrim(int color) {
        boolean needed = color != t().bg;
        if (!needed) {
            if (barScrimShown) { getWindow().getDecorView().getOverlay().remove(barScrim); barScrimShown = false; }
            return;
        }
        barScrim.setColor(color);
        if (!barScrimShown) { getWindow().getDecorView().getOverlay().add(barScrim); barScrimShown = true; }
        placeBarScrim();
    }
    // Where the bars are, from the insets the window was given (not the host's padding, which the page and the keyboard also make): the
    // status bar, the navigation bar wherever it is, and a cutout.
    private void placeBarScrim() {
        View decor = getWindow().getDecorView();
        Rect bars = new Rect();
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsets given = decor.getRootWindowInsets();
            if (given != null) {
                android.graphics.Insets all = given.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                bars.set(all.left, all.top, all.right, all.bottom);
            }
        }
        barScrim.setBounds(0, 0, decor.getWidth(), decor.getHeight());
        barScrim.setInsets(bars);
        barScrim.invalidateSelf();
    }

    // The control a popup belongs to, in the layer's coordinates, or null where it is gone.
    private Rect anchorRect(String key, View layer) {
        View anchor = host.findViewWithTag(key);
        if (anchor == null) return null;
        int[] at = new int[2];
        int[] in = new int[2];
        anchor.getLocationInWindow(at);
        layer.getLocationInWindow(in);
        Rect rect = new Rect(at[0] - in[0], at[1] - in[1], at[0] - in[0] + anchor.getWidth(), at[1] - in[1] + anchor.getHeight());
        // A dropdown's value ends where its content does, at the page's margin: its own padding, past that, has no fill to hang a popup from.
        if (anchor instanceof Ui.DropdownValue) rect.right -= anchor.getPaddingRight();
        return rect;
    }
    // After render(): the popups go back under their controls, or close if a control is gone.
    private void reanchorOverlays() {
        for (Overlay overlay : new ArrayList<>(overlays)) {
            if (overlay.anchorKey == null) continue;
            if (host.findViewWithTag(overlay.anchorKey) == null) closeOverlay(overlay);
            else overlay.layer.requestLayout();
        }
    }

    // ConfirmDialog: a card with a message that wraps, and scrolls where it is taller than
    // the window leaves room for, and buttons: Cancel, an alternate beside it if there is
    // one, and the confirmation, always red and always last. Escape, Back and a tap on the
    // backdrop cancel. The confirmation ignores a click, and OK, for its first 400 ms, so
    // that an OK held or repeated on a remote cannot answer what a phone or a web page
    // asked; the alternate is as much a yes, and waits too. Confirmations queue: one asked
    // for while another is up waits its turn, and never takes its place. Cancel has the focus
    // first. Keys: confirm:message, confirm:cancel, confirm:alt and confirm:ok.
    //
    // The card is as wide as what it holds, as the desktop's is (ConfirmDialog.qml): the wider of the buttons' row and
    // the message's widest line once it wraps, each with 14 at the sides, and no wider than the window less 24. The buttons, which
    // cannot wrap, give up their padding and minimum width, and elide, when the row is wider than that.
    private static final class ConfirmRequest {
        final CharSequence message;
        final CharSequence confirm;
        final CharSequence alternate;
        final Runnable onConfirm;
        final Runnable onAlternate;
        final Runnable onCancel;
        ConfirmRequest(CharSequence message, CharSequence confirm, CharSequence alternate, Runnable onConfirm, Runnable onAlternate, Runnable onCancel) {
            this.message = message; this.confirm = confirm; this.alternate = alternate;
            this.onConfirm = onConfirm; this.onAlternate = onAlternate; this.onCancel = onCancel;
        }
    }
    private static final long CONFIRM_ARMED_MS = 400;
    private final ArrayDeque<ConfirmRequest> confirmQueue = new ArrayDeque<>();
    private boolean confirmShowing;

    private void showConfirm(CharSequence message, CharSequence confirmLabel, Runnable onConfirm) {
        showConfirm(message, confirmLabel, null, onConfirm, null, null);
    }
    private void showConfirm(CharSequence message, CharSequence confirmLabel, CharSequence alternateLabel,
                             Runnable onConfirm, Runnable onAlternate, Runnable onCancel) {
        confirmQueue.add(new ConfirmRequest(message, confirmLabel, alternateLabel, onConfirm, onAlternate, onCancel));
        nextConfirm();
    }
    private void nextConfirm() {
        if (confirmShowing || isDestroyed()) return;
        ConfirmRequest request = confirmQueue.poll();
        if (request == null) return;
        confirmShowing = true;
        Theme t = t();
        // Its size, as ConfirmDialog.qml works it out: the window is what the card is no wider than less 24; the message wraps within the
        // window less 84 to find its widest line; and the buttons, 8 apart, are label + 26 wide and 76 at least, or, where that row
        // would not fit in the window less 52, label + 16 and no more than their share of it.
        int window = pageWidthPx();
        CharSequence[] labels = request.alternate == null
                ? new CharSequence[] {getString(R.string.text_cancel), request.confirm}
                : new CharSequence[] {getString(R.string.text_cancel), request.alternate, request.confirm};
        int count = labels.length;
        int[] wanted = new int[count];
        int fullRow = dp(8) * (count - 1);
        for (int i = 0; i < count; i++) {
            wanted[i] = textWidth(labels[i].toString(), 14);
            fullRow += Math.max(dp(76), wanted[i] + dp(26));
        }
        int buttonRoom = window - dp(24) - dp(28);
        boolean compact = fullRow > buttonRoom;
        int[] widths = new int[count];
        int row = dp(8) * (count - 1);
        for (int i = 0; i < count; i++) {
            widths[i] = compact ? Math.min(wanted[i] + dp(16), (buttonRoom - dp(8) * (count - 1)) / count) : Math.max(dp(76), wanted[i] + dp(26));
            row += widths[i];
        }
        int cardWidth = Math.min(window - dp(24), Math.max(row + dp(28), widestLine(request.message, 14, window - dp(84)) + dp(28)));

        LinearLayout box = column();
        box.setPadding(dp(14), dp(12), dp(14), dp(12));
        CappedScrollView scroll = new CappedScrollView(this);
        scroll.setMaxHeight(Math.max(dp(80), getResources().getDisplayMetrics().heightPixels - dp(160)));
        TextView message = ui.label(request.message, 14, t.text);
        message.setGravity(Gravity.CENTER);
        // Wraps anywhere: a long host name has no space to break at. Lines are broken as the width of the card was worked out from.
        message.setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE);
        message.setBreakStrategy(LineBreaker.BREAK_STRATEGY_SIMPLE); // inlined (0), as in Ui.java: nothing of API 29 is loaded on 26-28
        message.setTag("confirm:message");
        scroll.addView(message, new FrameLayout.LayoutParams(-1, -2));
        box.addView(scroll, lp(-1, -2));
        LinearLayout buttons = row();
        buttons.setGravity(Gravity.CENTER);
        final Overlay[] shown = new Overlay[1];
        int minimum = compact ? 0 : 76;
        Button cancel = ui.button(labels[0], Ui.Kind.NEUTRAL, minimum, "confirm:cancel");
        cancel.setOnClickListener(v -> escape(shown[0]));
        buttons.addView(cancel, new LinearLayout.LayoutParams(widths[0], dp(32)));
        final long armedAt = SystemClock.uptimeMillis() + CONFIRM_ARMED_MS;
        if (request.alternate != null) {
            ui.gapH(buttons, 8);
            Button alternate = ui.button(request.alternate, Ui.Kind.NEUTRAL, minimum, "confirm:alt");
            alternate.setOnClickListener(v -> {
                if (SystemClock.uptimeMillis() < armedAt) return;
                closeOverlay(shown[0]);
                if (request.onAlternate != null) request.onAlternate.run();
            });
            buttons.addView(alternate, new LinearLayout.LayoutParams(widths[1], dp(32)));
        }
        ui.gapH(buttons, 8);
        Button ok = ui.button(request.confirm, Ui.Kind.DESTRUCTIVE, minimum, "confirm:ok");
        ok.setOnClickListener(v -> {
            if (SystemClock.uptimeMillis() < armedAt) return;
            closeOverlay(shown[0]);
            if (request.onConfirm != null) request.onConfirm.run();
        });
        buttons.addView(ok, new LinearLayout.LayoutParams(widths[count - 1], dp(32)));
        if (compact) for (int i = 0; i < buttons.getChildCount(); i++) {
            View child = buttons.getChildAt(i);
            if (child instanceof Button) child.setPadding(dp(4), 0, dp(4), 0);
        }
        // Every child of the box fills its width: a spacer view between them is 1 wide and not a filler, and a column that has one
        // takes the width of that, so that the card, the message and the buttons came out a few pixels across.
        LinearLayout.LayoutParams buttonsLp = lp(-1, -2);
        buttonsLp.topMargin = dp(14);
        box.addView(buttons, buttonsLp);
        Overlay overlay = showOverlay("confirm", OverlayLayer.Kind.CONFIRM, box, null, () -> {
            confirmShowing = false;
            host.post(this::nextConfirm);
        });
        shown[0] = overlay;
        overlay.width(cardWidth);
        overlay.defaultKey = "confirm:cancel";
        overlay.escape = () -> {
            closeOverlay(overlay);
            if (request.onCancel != null) request.onCancel.run();
        };
    }
    // The widest line of the text once it is wrapped to this width, as the desktop sizes a confirmation by: not the text on one line,
    // and not the width it is wrapped to, which would leave wide empty sides once a long question wrapped.
    private int widestLine(CharSequence text, float sp, int width) {
        if (text.length() == 0) return 0;
        TextPaint paint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
        // In the face the text is drawn in: the width of what is measured in another is the width of something else.
        paint.setTypeface(Fonts.regular(this));
        paint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, sp, getResources().getDisplayMetrics()));
        StaticLayout lines = StaticLayout.Builder.obtain(text, 0, text.length(), paint, Math.max(1, width))
                .setBreakStrategy(LineBreaker.BREAK_STRATEGY_SIMPLE).setHyphenationFrequency(Layout.HYPHENATION_FREQUENCY_NONE).build();
        float widest = 0;
        for (int i = 0; i < lines.getLineCount(); i++) widest = Math.max(widest, lines.getLineWidth(i));
        return (int) Math.ceil(widest);
    }

    // SelectPopup: the choices of a dropdown, under the right edge of the control they
    // belong to, 4 below it, or above it where they do not fit below, at most 8 rows with
    // the rest scrolling. The current choice is in accent with a check mark; a menu of
    // actions has none (current -1). Rows are 36 high, and focus as a menu row does.
    // Keys: select:0, select:1 and so on.
    private interface Pick { void picked(int index); }

    private Overlay showSelect(String anchorKey, String[] options, int current, Pick pick) {
        // One at a time: a choice asked for while another is up replaces it.
        Overlay previous = overlay("select");
        if (previous != null) closeOverlay(previous);
        Theme t = t();
        CappedScrollView scroll = new CappedScrollView(this);
        scroll.setMaxHeight(dp(8 * 36 + 12));
        LinearLayout list = column();
        list.setPadding(dp(6), dp(6), dp(6), dp(6));
        int widest = 0;
        final Overlay[] shown = new Overlay[1];
        for (int i = 0; i < options.length; i++) {
            final int index = i;
            widest = Math.max(widest, textWidth(options[i], 14));
            int color = i == current ? t.accent : t.text;
            LinearLayout rowView = row();
            rowView.setPadding(dp(12), 0, dp(12), 0);
            TextView name = ui.label(options[i], 14, color);
            name.setSingleLine(true);
            name.setEllipsize(TextUtils.TruncateAt.END);
            rowView.addView(name, new LinearLayout.LayoutParams(0, -1, 1));
            if (i == current) rowView.addView(ui.glyph(R.drawable.ic_check, t.accent), lp(-2, -2));
            ui.focus(rowView, "select:" + i, Ui.Look.MENU_ROW).setOnClickListener(v -> {
                closeOverlay(shown[0]);
                pick.picked(index);
            });
            list.addView(rowView, lp(-1, 36));
        }
        scroll.addView(list, new FrameLayout.LayoutParams(-1, -2));
        Overlay overlay = showOverlay("select", OverlayLayer.Kind.POPUP, scroll, anchorKey, null);
        shown[0] = overlay;
        overlay.width(Math.max(dp(140), widest + dp(56)));
        overlay.defaultKey = "select:" + Math.max(0, current);
        return overlay;
    }

    // MenuPopup: rows of actions, 40 high, with separators (a null item), under the control
    // that opened it and centred in the window: the Configs page's add menu. Keys are the
    // items'.
    private static final class MenuEntry {
        static final MenuEntry SEPARATOR = new MenuEntry(null, null, null);
        final String label;
        final String key;
        final Runnable action;
        MenuEntry(String label, String key, Runnable action) { this.label = label; this.key = key; this.action = action; }
    }

    private Overlay showMenu(String name, String anchorKey, List<MenuEntry> items) {
        return showMenu(name, anchorKey, items, null);
    }
    // onClose runs once it is closed, however it was.
    private Overlay showMenu(String name, String anchorKey, List<MenuEntry> items, Runnable onClose) {
        Overlay previous = overlay(name);
        if (previous != null) closeOverlay(previous);
        Theme t = t();
        LinearLayout list = column();
        list.setPadding(dp(5), dp(6), dp(5), dp(6));
        int widest = 0;
        String first = null;
        final Overlay[] shown = new Overlay[1];
        for (MenuEntry item : items) {
            if (item == MenuEntry.SEPARATOR) {
                // An 11 high slot holding a 1 px line, 8 in from each side.
                FrameLayout slot = new FrameLayout(this);
                View line = ui.sep();
                FrameLayout.LayoutParams lineLp = new FrameLayout.LayoutParams(-1, Math.max(1, dp(1)), Gravity.CENTER_VERTICAL);
                lineLp.setMargins(dp(8), 0, dp(8), 0);
                slot.addView(line, lineLp);
                list.addView(slot, lp(-1, 11));
                continue;
            }
            widest = Math.max(widest, textWidth(item.label, 14));
            if (first == null) first = item.key;
            TextView text = ui.label(item.label, 14, t.text);
            text.setSingleLine(true);
            text.setEllipsize(TextUtils.TruncateAt.END);
            text.setPadding(dp(9), 0, dp(9), 0);
            LinearLayout rowView = row();
            rowView.addView(text, new LinearLayout.LayoutParams(0, -1, 1));
            ui.focus(rowView, item.key, Ui.Look.MENU_ROW).setOnClickListener(v -> {
                closeOverlay(shown[0]);
                item.action.run();
            });
            LinearLayout.LayoutParams rowLp = lp(-1, 40);
            rowLp.bottomMargin = dp(1);
            list.addView(rowView, rowLp);
        }
        Overlay overlay = showOverlay(name, OverlayLayer.Kind.POPUP, list, anchorKey, onClose);
        shown[0] = overlay;
        if (anchorKey != null) overlay.layer.setAnchor(l -> anchorRect(anchorKey, l), OverlayLayer.UNDER_CENTRE_OF_WINDOW, dp(4));
        overlay.width(Math.max(dp(140), widest + dp(40)));
        overlay.defaultKey = first;
        return overlay;
    }
    private int textWidth(String text, float sp) {
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        // Regular, as every label, button and row it sizes is drawn in (see widestLine).
        paint.setTypeface(Fonts.regular(this));
        paint.setTextSize(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_SP, sp, getResources().getDisplayMetrics()));
        return (int) Math.ceil(paint.measureText(text));
    }

    // The toast: the one notice the desktop has, in the window. One at a time, a new one
    // replacing the old. surface, a 1 px border, radius 9, 13 centred up to 6 lines, as
    // wide as its longest line plus 24 (80 at least, 36 less than the window at most) and
    // at least 40 high; 26 above the bottom, or at the top, 48 down, while an overlay is
    // open, where it would be behind it. It fades in over 180 ms and stays for 55 ms a
    // character, at least 3.2 s and at most 10; a tap dismisses it. It is announced to
    // accessibility.
    private final Handler toastHandler = new Handler(Looper.getMainLooper());
    private Runnable toastHide;
    private TextView toastView;

    private void showToast(CharSequence text) {
        if (isDestroyed()) return;
        hideToastNow();
        Theme t = t();
        TextView view = ui.label(text, 13, t.text);
        view.setGravity(Gravity.CENTER);
        view.setMaxLines(6);
        view.setEllipsize(TextUtils.TruncateAt.END);
        view.setBackground(ui.outlined(t.surface, t.border, 9));
        view.setPadding(dp(12), dp(9), dp(12), dp(9));
        view.setMinHeight(dp(40));
        view.setMinWidth(dp(80));
        int window = host.getWidth() > 0 ? host.getWidth() - host.getPaddingLeft() - host.getPaddingRight() : getResources().getDisplayMetrics().widthPixels;
        view.setMaxWidth(Math.max(dp(80), window - dp(36)));
        // Not clickable: a toast is no control, and a tap on it only takes it away.
        view.setOnTouchListener((v, e) -> {
            if (e.getAction() == MotionEvent.ACTION_UP) hideToast(view);
            return true;
        });
        toastView = view;
        toastLayer.addView(view, toastParams());
        view.setAlpha(0f);
        view.animate().alpha(1f).setDuration(180).start();
        view.announceForAccessibility(text);
        toastHide = () -> hideToast(view);
        toastHandler.postDelayed(toastHide, Math.min(10_000, Math.max(3_200, 55L * text.length())));
    }
    private FrameLayout.LayoutParams toastParams() {
        boolean top = anyModalOverlay();
        FrameLayout.LayoutParams p = new FrameLayout.LayoutParams(-2, -2, (top ? Gravity.TOP : Gravity.BOTTOM) | Gravity.CENTER_HORIZONTAL);
        p.topMargin = dp(48);
        p.bottomMargin = dp(26);
        if (!top && page == 0 && toastView != null) p.bottomMargin = homeToastMargin(toastView, p.bottomMargin);
        return p;
    }
    // How far above the bottom the toast sits on Home (HomePage.qml toastMargin): in the middle of the gap between the selector and
    // the speed tiles when it fits there; a longer one covers the tiles, whole, and not the selector, the one thing here people
    // tap. Where Home is not laid out yet, or is not there, the usual 26.
    private int homeToastMargin(View toast, int otherwise) {
        View selector = homeSelectorRow;
        View tiles = homeSpeedRow;
        if (selector == null || tiles == null || !selector.isAttachedToWindow() || !tiles.isAttachedToWindow()
                || selector.getHeight() == 0 || tiles.getHeight() == 0 || toastLayer.getHeight() == 0) return otherwise;
        toast.measure(View.MeasureSpec.makeMeasureSpec(toastLayer.getWidth(), View.MeasureSpec.AT_MOST), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        int height = toast.getMeasuredHeight();
        int[] layerAt = new int[2];
        int[] selectorAt = new int[2];
        int[] tilesAt = new int[2];
        toastLayer.getLocationInWindow(layerAt);
        selector.getLocationInWindow(selectorAt);
        tiles.getLocationInWindow(tilesAt);
        int layerBottom = layerAt[1] + toastLayer.getHeight();
        int gapBottom = layerBottom - tilesAt[1];
        int gapTop = layerBottom - (selectorAt[1] + selector.getHeight());
        int room = gapTop - gapBottom;
        if (height + dp(8) <= room) return gapBottom + (room - height) / 2;
        return layerBottom - (tilesAt[1] + tiles.getHeight());
    }
    private void placeToast() {
        if (toastView != null && toastView.getParent() == toastLayer) toastView.setLayoutParams(toastParams());
    }
    private void hideToast(TextView view) {
        if (toastView != view) return;
        toastView = null;
        toastHandler.removeCallbacks(toastHide);
        view.animate().alpha(0f).setDuration(180).withEndAction(() -> toastLayer.removeView(view)).start();
    }
    private void hideToastNow() {
        if (toastView == null) return;
        toastHandler.removeCallbacks(toastHide);
        toastLayer.removeView(toastView);
        toastView = null;
    }
    // Five tiles, 46 by 38 with radius 8 and 8 between, icons only, centred. The current
    // page's tile is infoBg; the others rest on the page. The icons are 22: accent in the
    // current tab, textDim in the others, both at full strength, and the logo, which is
    // drawn in its own greys, at 1.0 in the current tab and 0.8 in the others. A remote's
    // focus is what the pointer's hover is: surface and grown to 1.08, and the current tile, which the
    // desktop leaves as it is, keeps its infoBg and only grows.
    private void nav() {
        Theme t = t();
        LinearLayout bar = row(); bar.setGravity(Gravity.CENTER);
        // 6 below the tiles, as on the desktop; the room above them is the status bar's and the overscan's.
        bar.setPadding(0, dp(9), 0, dp(6));
        // A tile that has the focus is 1.08 of its size, and the bar would cut that off at its padding.
        bar.setClipToPadding(false);
        settingsDot = null;
        for (int i = 0; i < tabNames.length; i++) {
            final int next = i;
            boolean current = i == page;
            FrameLayout item = new FrameLayout(this);
            ImageView icon = new ImageView(this); icon.setImageResource(tabIcons[i]);
            if (i == 0) icon.setAlpha(current ? 1f : 0.8f);
            else ui.tinted(icon, current ? t.accent : t.textDim);
            FrameLayout.LayoutParams iconLp = new FrameLayout.LayoutParams(dp(22), dp(22), Gravity.CENTER);
            item.addView(icon, iconLp);
            if (i == 3) {
                // Settings, while an update is waiting: a dot 7 across, 5 from the top and 8 from the right.
                View dot = new View(this);
                GradientDrawable round = new GradientDrawable();
                round.setShape(GradientDrawable.OVAL);
                round.setColor(t.warn);
                dot.setBackground(round);
                dot.setTag("update:dot");
                FrameLayout.LayoutParams dotLp = new FrameLayout.LayoutParams(dp(7), dp(7), Gravity.TOP | Gravity.END);
                dotLp.topMargin = dp(5);
                dotLp.setMarginEnd(dp(8));
                item.addView(dot, dotLp);
                settingsDot = dot;
                refreshUpdateDot();
            }
            LinearLayout.LayoutParams itemLp = lp(46, 38);
            if (i > 0) itemLp.leftMargin = dp(8);
            bar.addView(item, itemLp);
            item.setContentDescription(tabNames[i]);
            ui.focus(item, "tab:" + i, Ui.Look.TILE, current ? Ui.F_CURRENT : 0).setOnClickListener(v -> { page = next; render(); });
        }
        pageRoot.addView(bar, lp(-1, -2));
    }
    private boolean updateWaiting() {
        AndroidUpdater.State update = AndroidUpdater.INSTANCE.getState();
        return update == AndroidUpdater.State.AVAILABLE || update == AndroidUpdater.State.READY;
    }
    private void refreshUpdateDot() {
        if (settingsDot != null) settingsDot.setVisibility(updateWaiting() ? View.VISIBLE : View.GONE);
    }

    // Home, as the desktop's HomePage draws it. The logo is the control: a click connects, cancels a connection that is
    // coming up or disconnects one that is up, and with no config to connect it opens the add menu on Configs.
    // Under it, while there is a session, one line: Connecting…, Disconnecting… or the time the connection has been up. While traffic
    // is blocked the service is reconnecting by itself, which the desktop's Home shows as Connecting… (its backend is connecting then),
    // and the logo is still the control: a click disconnects. Then the selector, which opens the config dropdown right here,
    // and +. At the foot, two tiles with the speeds. Keys: home:toggle (the logo), home:config, home:add.
    // The logo's look follows the state (HeroMotion); the line under it and the speeds change once a second
    // without a render() (tickHome).
    private HeroMotion hero = new HeroMotion();
    private TextView homeStatus;
    private TextView homeDown;
    private TextView homeUp;
    private View homeSelectorRow;
    private View homeSpeedRow;
    // The last render() was of Home too, so the logo moves from where it was; otherwise it starts in its state.
    private boolean homeContinues;
    // After the app was left or came back, which the logo does not animate across.
    private boolean homeFresh = true;
    private boolean resumed;
    private final SpeedMeter speedMeter = new SpeedMeter();
    // Where the timer counts from when the service does not say (it always does, but a device test only sets the state).
    private long homeSeenUpAt;
    private long lastLogoClick;
    private int homePickerCount;

    private int pageWidthPx() {
        int width = host.getWidth() > 0 ? host.getWidth() - host.getPaddingLeft() - host.getPaddingRight() : getResources().getDisplayMetrics().widthPixels;
        return Math.max(dp(100), width);
    }

    private void home() {
        Theme t = t();
        FrameLayout stage = new FrameLayout(this);
        stage.setClipChildren(false);
        pageRoot.addView(stage, new LinearLayout.LayoutParams(-1, 0, 1));

        // The speed tiles, pinned 44 above the bottom, whatever the logo and the selector do.
        TextView[] down = new TextView[1];
        TextView[] up = new TextView[1];
        LinearLayout speeds = row();
        speeds.setGravity(Gravity.CENTER);
        speeds.addView(ui.speedTile(R.drawable.ic_arrow_down, t.success, down), lp(116, 44));
        ui.gapH(speeds, 12);
        speeds.addView(ui.speedTile(R.drawable.ic_arrow_up, t.textDim, up), lp(116, 44));
        FrameLayout.LayoutParams speedsLp = new FrameLayout.LayoutParams(-2, -2, Gravity.BOTTOM | Gravity.CENTER_HORIZONTAL);
        // 44 above the window's bottom, as the desktop's are: on a TV the host is already padded by the overscan, which the desktop's
        // 44 lies outside of, so that much less here. On a phone the window's bottom is the top of the navigation bar, and 44 is right.
        speedsLp.bottomMargin = dp(44) - overscanY;
        stage.addView(speeds, speedsLp);
        // The speeds are of the time Home has been shown: a start is taken when it is shown, and the first tick after it gives a speed.
        if (vpnState != 2) speedMeter.reset();
        else if (!homeContinues) { speedMeter.reset(); sampleSpeeds(); }
        homeDown = down[0];
        homeUp = up[0];
        homeDown.setText(speedText(speedMeter.downBytesPerSecond()));
        homeUp.setText(speedText(speedMeter.upBytesPerSecond()));
        homeSpeedRow = speeds;

        // Above them, 22 clear, the logo, its line and the selector as one column, in the middle of what is left and 6 below it. (A
        // FrameLayout puts a centred child at the middle plus its top margin less its bottom margin, the whole of each: the 6 is the margin.)
        FrameLayout area = new FrameLayout(this);
        area.setClipChildren(false);
        FrameLayout.LayoutParams areaLp = new FrameLayout.LayoutParams(-1, -1);
        areaLp.bottomMargin = dp(44 + 44 + 22) - overscanY;
        stage.addView(area, areaLp);
        LinearLayout column = column();
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        column.setClipChildren(false);
        FrameLayout.LayoutParams columnLp = new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER_VERTICAL);
        columnLp.topMargin = dp(6);
        area.addView(column, columnLp);

        // The logo: 132 across, and it is all of the control, so the focus and a tap are on it and not around it.
        FrameLayout box = new FrameLayout(this);
        box.setClipChildren(false);
        ImageView logo = new ImageView(this);
        logo.setImageResource(R.drawable.ic_logo);
        logo.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        box.addView(logo, new FrameLayout.LayoutParams(-1, -1));
        box.setContentDescription(isVpnActive() ? getString(R.string.text_disconnect_freetunnel) : getString(R.string.text_connect_freetunnel));
        ui.focus(box, "home:toggle", Ui.Look.HERO).setOnClickListener(v -> logoClicked());
        // A press, or OK on a remote, shrinks it to 0.96 and it comes back when that ends.
        box.setOnTouchListener((v, event) -> {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_DOWN) hero.press(true);
            else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) hero.press(false);
            return false;
        });
        box.setOnKeyListener((v, keyCode, event) -> {
            if (keyCode == KeyEvent.KEYCODE_DPAD_CENTER || keyCode == KeyEvent.KEYCODE_ENTER || keyCode == KeyEvent.KEYCODE_NUMPAD_ENTER) {
                if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) hero.press(true);
                else if (event.getAction() == KeyEvent.ACTION_UP) hero.press(false);
            }
            return false;
        });
        // With a remote's focus it is held at 1.05, the pulse's peak: a fill behind it would wash out its greys.
        box.setOnFocusChangeListener((v, hasFocus) -> {
            hero.focus(hasFocus);
            if (!hasFocus) hero.press(false);
        });
        LinearLayout.LayoutParams boxLp = lp(132, 132);
        boxLp.gravity = Gravity.CENTER_HORIZONTAL;
        column.addView(box, boxLp);

        // The line, 6 under the logo, which takes the room it needs when a session starts and gives it back when it ends.
        HomeState.Line kind = HomeState.line(vpnState, stopping);
        FrameLayout line = new FrameLayout(this);
        line.setClipChildren(false);
        column.addView(line, lp(-1, 0));
        LinearLayout content = column();
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        TextView status = ui.medium(statusText(kind), 15, t.textDim);
        status.setGravity(Gravity.CENTER);
        status.setMaxWidth(pageWidthPx() - dp(36));
        status.setTag("home:status");
        content.addView(status, lp(-2, -2));
        // As high as it needs whatever room the line has been given so far: while the line grows, it shows whole, as the desktop's does.
        content.measure(View.MeasureSpec.makeMeasureSpec(pageWidthPx(), View.MeasureSpec.AT_MOST), View.MeasureSpec.makeMeasureSpec(0, View.MeasureSpec.UNSPECIFIED));
        FrameLayout.LayoutParams contentLp = new FrameLayout.LayoutParams(-1, content.getMeasuredHeight());
        contentLp.topMargin = dp(6);
        line.addView(content, contentLp);
        homeStatus = kind == HomeState.Line.TIMER ? status : null;
        int lineHeight = dp(6) + content.getMeasuredHeight();
        hero.attach(box, logo, line, status, dp(12), lineHeight, HomeState.sessionActive(vpnState, stopping),
                HomeState.alpha(vpnState, stopping), HomeState.pulsing(vpnState, stopping), homeContinues);

        ui.gap(column, 22);

        // The selector and +, side by side, 6 apart, as one row in the middle.
        LinearLayout selection = row();
        selection.setGravity(Gravity.CENTER);
        boolean any = !configs.isEmpty();
        String shown = any ? configs.get(Math.max(0, Math.min(activeIndex, configs.size() - 1))).name : getString(R.string.text_add_configuration);
        // The room the name has: the page less its 18 each side, the arrow and its 6, the + and its 6.
        int room = pageWidthPx() - dp(36) - (any ? dp(Ui.DROPDOWN_ARROW_DP) + dp(Ui.SELECTOR_GAP_DP) + dp(Ui.SELECTOR_GAP_DP) + dp(22) : 0);
        LinearLayout selector = ui.selector(shown, any, room, "home:config");
        selector.setOnClickListener(v -> { if (configs.isEmpty()) addConfigOnConfigsPage(); else homePicker(); });
        selection.addView(selector, lp(-2, -2));
        // The desktop's selector is 24 high, which is little for a finger: it is hit from a little round it, by the column it is in.
        column.addOnLayoutChangeListener((v, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> {
            Rect hit = new Rect();
            selector.getDrawingRect(hit);
            column.offsetDescendantRectToMyCoords(selector, hit);
            hit.inset(-dp(8), -dp(10));
            column.setTouchDelegate(new TouchDelegate(hit, selector));
        });
        if (any) {
            FrameLayout add = ui.smallIconButton(R.drawable.ic_add_home, 14, getString(R.string.text_add_configuration), "home:add");
            add.setOnClickListener(v -> addConfigOnConfigsPage());
            LinearLayout.LayoutParams addLp = lp(22, 22);
            addLp.leftMargin = dp(6);
            selection.addView(add, addLp);
        }
        LinearLayout.LayoutParams selectionLp = lp(-2, -2);
        selectionLp.gravity = Gravity.CENTER_HORIZONTAL;
        column.addView(selection, selectionLp);
        // What the config dropdown is under and centred on, + and all, as the desktop's is (cfgSel).
        selection.setTag("home:selection");
        homeSelectorRow = selection;
    }

    private String statusText(HomeState.Line kind) {
        switch (kind) {
            case CONNECTING: return getString(R.string.text_connecting);
            case DISCONNECTING: return getString(R.string.text_disconnecting);
            case TIMER: return SessionTime.format(SystemClock.elapsedRealtime() - sessionStart());
            default: return "";
        }
    }
    private String speedText(double bytesPerSecond) {
        return SpeedMeter.megabytes(bytesPerSecond) + getString(R.string.text_mb_per_s);
    }
    // SystemClock.elapsedRealtime() when the connection came up, from the service.
    private long sessionStart() {
        long start = FreeTunnelVpnService.sessionStart();
        if (start > 0) return start;
        if (homeSeenUpAt == 0) homeSeenUpAt = SystemClock.elapsedRealtime();
        return homeSeenUpAt;
    }

    // What the logo does: with no config and no session, the way to one, the add menu; otherwise it connects, cancels or disconnects.
    // A double tap counts once: people double-tap anything that looks like an icon, and it connected and at once cancelled.
    private void logoClicked() {
        long now = SystemClock.uptimeMillis();
        if (now - lastLogoClick < ViewConfiguration.getDoubleTapTimeout()) return;
        lastLogoClick = now;
        connect();
    }
    // Home's +, its "Add a config" and the logo with nothing to connect: the Configs page, with its menu open.
    private void addConfigOnConfigsPage() {
        page = 1;
        render();
        addMenu();
    }

    // Once a second while Home shows a connected session: the time under the logo and the two speeds. Only those views are
    // changed, never the page, so that nothing the user is doing on it is touched.
    private final Runnable homeTicker = new Runnable() {
        @Override public void run() {
            if (page != 0 || !resumed || vpnState != 2 || stopping) return;
            tickHome();
            scheduleHomeTick();
        }
    };
    private void scheduleHomeTick() {
        host.removeCallbacks(homeTicker);
        if (page != 0 || !resumed || vpnState != 2 || stopping) return;
        host.postDelayed(homeTicker, SessionTime.untilNextSecond(SystemClock.elapsedRealtime() - sessionStart()));
    }
    // The counters of this app, which carries the tunnel's traffic: the VPN excludes it, and its own connection to the server is the tunnel.
    // A device that keeps none gives TrafficStats.UNSUPPORTED, and the speeds stay 0.0.
    private void sampleSpeeds() {
        int uid = android.os.Process.myUid();
        speedMeter.sample(TrafficStats.getUidRxBytes(uid), TrafficStats.getUidTxBytes(uid), SystemClock.elapsedRealtime());
    }
    private void tickHome() {
        long now = SystemClock.elapsedRealtime();
        if (homeStatus != null) {
            String time = SessionTime.format(now - sessionStart());
            if (!time.contentEquals(homeStatus.getText())) homeStatus.setText(time);
        }
        sampleSpeeds();
        if (homeDown != null) homeDown.setText(speedText(speedMeter.downBytesPerSecond()));
        if (homeUp != null) homeUp.setText(speedText(speedMeter.upBytesPerSecond()));
    }

    // The config dropdown, right on Home (qml/pages/HomePage.qml, cfgPopup): under the selector, 6 below it and centred on it, as
    // wide as the widest name at 14 plus 30 (140 at least, the page less 16 at most), a row of 40 for each config, three showing and
    // the rest scrolling, the selected one in accent. A row selects that config (the one already selected does nothing),
    // reconnecting to it if a session is up, and closes it. Back, a tap anywhere else, and the selector, close it. The
    // first focus is on the selected row. Keys: home:pick:0, home:pick:1 and so on.
    private void homePicker() {
        if (configs.isEmpty() || overlay("homepicker") != null) return;
        Theme t = t();
        int selected = Math.max(0, Math.min(activeIndex, configs.size() - 1));
        LinearLayout frame = column();
        // 5 above the rows and 7 below them: the card is 12 more than its list, and the list is 5 in (HomePage.qml).
        frame.setPadding(dp(5), dp(5), dp(5), dp(7));
        CappedScrollView scroll = new CappedScrollView(this);
        scroll.setMaxHeight(dp(3 * 40));
        LinearLayout list = column();
        final Overlay[] shown = new Overlay[1];
        int widest = 0;
        for (int i = 0; i < configs.size(); i++) {
            final int index = i;
            String name = configs.get(i).name;
            widest = Math.max(widest, textWidth(name, 14));
            int color = i == selected ? t.accent : t.text;
            TextView label = ui.label(name, 14, color);
            label.setSingleLine(true);
            label.setEllipsize(TextUtils.TruncateAt.END);
            label.setPadding(dp(10), 0, dp(10), 0);
            LinearLayout rowView = row();
            rowView.addView(label, new LinearLayout.LayoutParams(0, -1, 1));
            ui.focus(rowView, "home:pick:" + i, Ui.Look.MENU_ROW).setOnClickListener(v -> {
                closeOverlay(shown[0]);
                selectConfig(index);
            });
            list.addView(rowView, lp(-1, 40));
        }
        scroll.addView(list, new FrameLayout.LayoutParams(-1, -2));
        frame.addView(scroll, lp(-1, -2));
        Overlay overlay = showOverlay("homepicker", OverlayLayer.Kind.POPUP, frame, "home:config", null);
        homePickerCount = configs.size();
        shown[0] = overlay;
        // Under the whole selector row and centred on it, the + included, as the desktop's is; its key, home:config, is what the overlay
        // follows across a render and returns the focus to. Never turned over above it: the desktop's cfgPopup is at the selector's bottom
        // plus 6 whatever is left under it, and on a TV, with a session's line under the logo, there is less than it takes before the
        // overscan, where a popup in general would flip.
        overlay.layer.setAnchor(l -> anchorRect("home:selection", l), OverlayLayer.UNDER_CENTRE_OF_CONTROL_NO_FLIP, dp(6));
        overlay.width(Math.max(dp(140), widest + dp(30)));
        overlay.defaultKey = "home:pick:" + selected;
    }

    // What clicking a config does, here and on the Configs page: it becomes the one to connect to, and if a session is up it
    // reconnects to it. The one that is selected already is left alone.
    private void selectConfig(int index) {
        if (index < 0 || index >= configs.size() || index == activeIndex) return;
        activeIndex = index;
        saveConfigs();
        applyCurrentRules();
        render();
    }

    private void gapHorizontal(LinearLayout p, int size) { p.addView(new View(this), lp(size, 1)); }
    private void connect() {
        vpnState = FreeTunnelVpnService.state(this);
        if (isVpnActive()) {
            validationRequest++;
            // Disconnecting until the service says it is off.
            stopping = true;
            host.removeCallbacks(clearStopping);
            host.postDelayed(clearStopping, 8000);
            FreeTunnelVpnService.stop(this);
            render();
            return;
        }
        // Nothing to connect to: the way to something, as the desktop's logo is.
        if (configs.isEmpty()) { addConfigOnConfigsPage(); return; }
        Config config = activeConfig();
        String profile = activeProfileName();
        validateAsync(config.toml, profile, () -> {
            pendingConfig = config.toml;
            pendingProfile = profile;
            Intent prepare = VpnService.prepare(this);
            if (prepare == null) startVpnWithPendingConfig();
            else if (!startForResult(prepare, PREPARE_VPN)) {
                pendingConfig = null;
                toast(getString(R.string.text_vpn_consent_unavailable));
            }
        }, () -> {});
    }

    private boolean isVpnActive() {
        return vpnState == 1 || vpnState == 2 || vpnState == 3 || vpnState == 4 || vpnState == 5 || vpnState == 6;
    }

    private void startVpnWithPendingConfig() {
        if (pendingConfig == null) return;
        FreeTunnelVpnService.start(this, pendingConfig, pendingProfile);
        pendingConfig = null;
        pendingProfile = null;
        vpnState = 1;
        render();
        QuickTileService.refresh(this);
    }

    private void applyCurrentRules() {
        if (!isVpnActive()) return;
        if (configs.isEmpty()) {
            FreeTunnelVpnService.stop(this);
            vpnState = 0;
            return;
        }
        Config config = activeConfig();
        String profile = activeProfileName();
        validateAsync(config.toml, profile, () -> {
            if (FreeTunnelVpnService.state(this) == 0) return;
            FreeTunnelVpnService.start(this, config.toml, profile);
        }, () -> {});
    }

    // A config that passed: the text to save, what was left out of it, and the name
    // it gives its server, if it gives one.
    private interface Checked { void accept(RoutingConfig.Import checked); }

    private void validateAsync(String source, String profile, Runnable accepted, Runnable rejected) {
        validateAsync(source, profile, false, true, checked -> accepted.run(), rejected);
    }

    // With asImport, the text is first checked as a file to add (RoutingConfig.cleanImport),
    // which may refuse it, or take out a subscription or whatever in its certificate
    // is not a certificate. Here on the worker, since that parses the whole file once more.
    //
    // A check that is superseded is dropped, and says nothing: a newer check, or a change to the list of configs, makes what it
    // was asked for stale. That is for a check about the config in use (connecting, building the VPN again, the editor's Save),
    // which validationRequest numbers. A check of a file or a link to add is not: it is answered whatever else is going on,
    // since dropping it would lose a text the TV was to ask about, and nothing about it goes stale (the question is worked out
    // from the list as it is when the answer comes). Nor does it make another check stale: Replace builds the VPN again
    // while the next text waits behind it. No toast for the check itself: the desktop shows none, and the pulse of the logo or
    // the question that comes is the answer.
    private void validateAsync(String source, String profile, boolean asImport, boolean supersedable, Checked accepted, Runnable rejected) {
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed)); rejected.run(); return; }
        final long request = supersedable ? ++validationRequest : validationRequest;
        validationWorker.execute(() -> {
            try {
                RoutingConfig.Import checked = asImport ? RoutingConfig.cleanImport(source) : new RoutingConfig.Import(source, false);
                RoutingConfig.Plan plan = RoutingConfig.prepare(this, checked.getToml(), profile);
                NativeConfigValidator.INSTANCE.validate(getApplicationContext(), plan.getToml());
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    if (supersedable && request != validationRequest) { rejected.run(); return; }
                    // The routing warning is about the config in use. A file or a link being checked to add is not in use, and the plan made
                    // for it (with the profile that is none of its) has nothing to say: it was a toast over the question about the import.
                    if (supersedable && plan.getWarning() != null) toast(getString(plan.getWarning()));
                    accepted.accept(checked);
                });
            } catch (RoutingConfig.ImportRefused refused) {
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    rejected.run();
                    if (!supersedable || request == validationRequest) refuseImport(refused.messageResource);
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    rejected.run();
                    if (!supersedable || request == validationRequest) toast(getString(R.string.config_validation_error, error.getMessage()));
                });
            }
        });
    }

    // The Configs page, as the desktop draws it (qml/pages/ConfigsPage.qml): at the top + and the speedometer, 40 by 32 and 16 apart
    // and in the middle, the speedometer only when there are configs; under them the rows (ConfigRowView), a click on which
    // selects that config; with none, "Add a config" in the middle of the page, which opens the add menu and is hidden while it is
    // open. On a phone a row is moved by pressing and holding it and dragging; a TV has no way to. Keys: configs:add, configs:ping,
    // configs:hint, and config:N:select, config:N:export, config:N:edit and config:N:remove for each row.
    private TextView configsHint;

    private void configs(LinearLayout body) {
        storageBanner(body);
        // The header's buttons are 40 by 32, 16 apart, resting on the page, 8 from the top of it.
        LinearLayout header = row(); header.setGravity(Gravity.CENTER);
        FrameLayout add = ui.iconButton(R.drawable.ic_add, 22, getString(R.string.text_add_configuration), "configs:add");
        add.setOnClickListener(v -> addMenu()); header.addView(add, lp(40, 32));
        if (!configs.isEmpty()) {
            gapHorizontal(header, 16);
            FrameLayout ping = ui.iconButton(R.drawable.ic_ping, 22, getString(R.string.text_check_server_tcp_latency), "configs:ping");
            ping.setOnClickListener(v -> pingConfigs()); header.addView(ping, lp(40, 32));
        }
        body.addView(header, lp(-1, 32));
        gap(body, 8);
        configsHint = null;
        if (configs.isEmpty()) {
            // As wide as its words: the focus is around them, not across the page.
            TextView hint = ui.link(getString(R.string.text_add_configuration), 15, Ui.LinkKind.TEXT, "configs:hint");
            hint.setTypeface(Fonts.medium(this));
            hint.setOnClickListener(v -> addMenu());
            // Under its own menu it would be cut in half.
            hint.setVisibility(overlay("addmenu") != null ? View.INVISIBLE : View.VISIBLE);
            configsHint = hint;
            // In the middle of the whole page, the header included, as the desktop's is (anchors.centerIn: parent): the room under the header
            // starts 48 down (the page's 8, the header's 32 and the gap's 8) and ends 26 above the page's foot, so its middle is 11 below the
            // page's, and the words are lifted by that. A FrameLayout puts a centred child at the middle plus its top margin less its bottom
            // margin, the whole of each, so the margin is the 11 itself.
            FrameLayout middle = new FrameLayout(this);
            FrameLayout.LayoutParams hintLp = new FrameLayout.LayoutParams(-2, dp(40), Gravity.CENTER);
            hintLp.bottomMargin = dp(11);
            middle.addView(hint, hintLp);
            body.addView(middle, new LinearLayout.LayoutParams(-1, 0, 1));
            return;
        }
        // Answers of configs that are no more are not kept.
        pingResults.keySet().retainAll(configs);
        LinearLayout list = column();
        RowDrag drag = isTv() || storageFailed ? null
                : new RowDrag(list, dp(ConfigRowView.PITCH_DP), dp(2), this::moveConfig);
        for (int i = 0; i < configs.size(); i++) {
            final int index = i;
            final Config config = configs.get(i);
            boolean selected = i == activeIndex;
            // Worded by connected, as the desktop does; gone while it disconnects.
            CharSequence badge = null;
            if (selected && !stopping) {
                if (vpnState == 2) badge = getString(R.string.config_badge_connected);
                else if (isVpnActive()) badge = getString(R.string.config_badge_connecting);
            }
            ConfigRowView row = new ConfigRowView(this, ui, i, config.name, selected, pingResults.get(config), badge, vpnState == 2,
                    getString(R.string.text_export_config), getString(R.string.text_edit_configuration), getString(R.string.text_delete));
            row.setOnClickListener(v -> selectConfig(index));
            row.exportIcon.setOnClickListener(v -> exportMenu(config, index));
            row.editIcon.setOnClickListener(v -> editConfig(index));
            row.removeIcon.setOnClickListener(v -> confirmRemove(index));
            if (drag != null) drag.attach(row);
            list.addView(row, ConfigRowView.params(ui));
        }
        body.addView(list, lp(-1, -2));
    }
    // Moves a row to where it was dropped, and the selection stays on the config it was on.
    private void moveConfig(int from, int to) {
        if (storageFailed) return;
        activeIndex = Reorder.move(configs, from, to, activeIndex);
        saveConfigs();
        render();
    }

    // Every config at once, side by side on four threads: "…" until its probe answers, then the time or a dash.
    // Answers of an older press are dropped, as the desktop's are when its list is probed again.
    private void pingConfigs() {
        if (configs.isEmpty()) return;
        final int generation = ++pingGeneration;
        final String unit = getString(R.string.text_ms);
        List<Config> snapshot = new ArrayList<>(configs);
        for (Config config : snapshot) pingResults.put(config, PingProbe.PENDING);
        render();
        for (Config config : snapshot) {
            final String toml = config.toml;
            pingWorker.execute(() -> {
                String text = PingProbe.text(PingProbe.probe(firstAddress(toml), PingProbe.TIMEOUT_MS), unit);
                runOnUiThread(() -> {
                    if (isDestroyed() || generation != pingGeneration) return;
                    pingResults.put(config, text);
                    if (page == 1) render();
                });
            });
        }
    }
    // The config's first address, as host:port, or null where it has none.
    private static String firstAddress(String toml) {
        try {
            org.tomlj.TomlArray addresses = RoutingConfig.validate(toml).getArray("endpoint.addresses");
            return addresses == null || addresses.size() == 0 ? null : addresses.getString(0);
        } catch (Exception e) { return null; }
    }

    // The export icon's choices, under it with no check mark: the config as a .toml file, through the system's file picker,
    // and as a tt:// link on the clipboard. Both carry the password, which the notice afterwards says.
    private void exportMenu(Config config, int index) {
        String[] options = {getString(R.string.export_toml), getString(R.string.export_deep_link)};
        final String key = "config:" + index + ":export";
        Overlay menu = showSelect(key, options, -1, which -> { if (which == 0) exportToml(config); else copyDeepLink(config); });
        // The desktop anchors it to the icon's cell, which is the whole 56 high slot of the row: 4 below that. The icon's own is 40 high
        // here, in a row of 48, so the cell is 8 more above and below it.
        menu.layer.setAnchor(l -> {
            Rect cell = anchorRect(key, l);
            if (cell != null) { cell.top -= dp(8); cell.bottom += dp(8); }
            return cell;
        }, OverlayLayer.UNDER_RIGHT_EDGE, dp(4));
    }
    // A config's name may hold a "/" or a "\", which a file name cannot (ConfigsPage.exportFileName).
    private static String exportFileName(String name) {
        String flat = name.replaceAll("[/\\\\]", "-");
        return (flat.isEmpty() ? "config" : flat) + ".toml";
    }
    private void exportToml(Config config) {
        Intent save = new Intent(Intent.ACTION_CREATE_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("application/octet-stream").putExtra(Intent.EXTRA_TITLE, exportFileName(config.name));
        exporting = config;
        // Many TVs have no screen to save to.
        if (!startForResult(save, EXPORT_CONFIG)) { exporting = null; toast(getString(R.string.export_no_picker)); }
    }
    private void writeExport(Uri uri) {
        final Config config = exporting;
        exporting = null;
        if (config == null) return;
        // The list may have changed while the file picker was up: the config is found again, and if it is gone, that is said.
        if (!configs.contains(config)) { toast(getString(R.string.config_gone)); return; }
        final byte[] bytes = config.toml.getBytes(StandardCharsets.UTF_8);
        ioWorker.execute(() -> {
            boolean written;
            try (java.io.OutputStream out = getContentResolver().openOutputStream(uri, "wt")) {
                if (out == null) throw new IOException("No stream");
                out.write(bytes);
                written = true;
            } catch (Exception e) { written = false; }
            final boolean ok = written;
            runOnUiThread(() -> { if (!isDestroyed()) toast(getString(ok ? R.string.config_exported : R.string.export_failed)); });
        });
    }
    private void copyDeepLink(Config config) {
        String link = null;
        try {
            DeepLinkImport.Link described = DeepLinkImport.linkOf(config.name, RoutingConfig.validate(config.toml));
            if (described != null) link = DeepLinkImport.encode(described);
        } catch (Exception e) { link = null; }
        if (link == null) { toast(getString(R.string.deep_link_failed)); return; }
        ClipData clip = ClipData.newPlainText("FreeTunnel link", link);
        // The link holds the password: a keyboard's clipboard history and the preview Android shows are asked to leave it out.
        if (Build.VERSION.SDK_INT >= 33) {
            android.os.PersistableBundle extras = new android.os.PersistableBundle();
            extras.putBoolean(android.content.ClipDescription.EXTRA_IS_SENSITIVE, true);
            clip.getDescription().setExtras(extras);
        }
        ((ClipboardManager) getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(clip);
        toast(getString(R.string.deep_link_copied));
    }
    // The Configs page's menu under its +: no title. A TV has no camera for a link's QR code and a remote
    // is no way to type one, so there a phone's way in comes first; then the desktop's items, in its
    // order and its words, and a separator before the last.
    private void addMenu() {
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_saved_data_has_been_preserved)); return; }
        List<MenuEntry> items = new ArrayList<>();
        if (isTv()) items.add(new MenuEntry(getString(R.string.text_receive_from_phone), "add:receive", this::receiveFromPhone));
        items.add(new MenuEntry(getString(R.string.text_paste_from_clipboard), "add:paste", this::pasteFromClipboard));
        items.add(new MenuEntry(getString(R.string.add_from_file), "add:file", this::pickFile));
        items.add(MenuEntry.SEPARATOR);
        items.add(new MenuEntry(getString(R.string.add_create_new), "add:create", () -> editConfig(-1)));
        showMenu("addmenu", "configs:add", items, this::refreshConfigsHint);
        refreshConfigsHint();
    }
    // The empty page's "Add a config" is hidden while its menu is open.
    private void refreshConfigsHint() {
        if (configsHint != null) configsHint.setVisibility(overlay("addmenu") != null ? View.INVISIBLE : View.VISIBLE);
    }
    private void pasteFromClipboard() {
        ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
        ClipData clip = cm.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) { toast(getString(R.string.text_clipboard_is_empty)); return; }
        CharSequence value = clip.getItemAt(0).coerceToText(this);
        importText(getString(R.string.text_from_clipboard), value == null ? "" : value.toString().trim(), null);
    }
    // Many TVs have no document picker. Some have one for GET_CONTENT only, which
    // onActivityResult reads the same way.
    private void pickFile() {
        for (String action : new String[]{Intent.ACTION_OPEN_DOCUMENT, Intent.ACTION_GET_CONTENT}) {
            if (startForResult(new Intent(action).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), PICK_CONFIG)) return;
        }
        toast(getString(isTv() ? R.string.text_no_file_picker_receive : R.string.text_no_file_picker));
    }
    // A link, or else a TOML file named fallbackName unless it names its server.
    // origin is the address it was sent from, null when it came from this device.
    private void importText(String fallbackName, String text, String origin) {
        importText(fallbackName, text, origin, null);
    }
    // Whoever asks for an import is told when it is answered, once: it was added, replaced, or cancelled,
    // or it was refused, which is true then, and a notice says why. Receive from phone takes its next text then.
    private interface Answered { void answered(boolean refused); }
    private static void answered(Answered done, boolean refused) { if (done != null) done.answered(refused); }

    private void importText(String fallbackName, String text, String origin, Answered done) {
        if (DeepLinkImport.looksLikeLink(text)) importDeepLink(text, origin, done);
        else importToml(fallbackName, text, origin, done);
    }
    private void importDeepLink(String link, String origin) { importDeepLink(link, origin, null); }
    private void importDeepLink(String link, String origin, Answered done) {
        final DeepLinkImport.Imported imported;
        try { imported = DeepLinkImport.parse(link); }
        catch (DeepLinkImport.ParseException e) { refuseImport(e.messageResource); answered(done, true); return; }

        // The text is the app's own, and the subscription and whatever in the
        // certificate is not a certificate were left out as the link was read.
        validateAsync(imported.toml, SplitProfiles.DEFAULT, false, false, checked -> showImportConfirmation(imported.name, false,
                new RoutingConfig.Import(checked.getToml(), imported.subscriptionNotKept, imported.certificateIgnored), origin, true, done),
                () -> answered(done, true));
    }
    // Named after its file or where the text came from, unless it is an endpoint's
    // export that names its server.
    private void importToml(String name, String toml, String origin) { importToml(name, toml, origin, null); }
    private void importToml(String name, String toml, String origin, Answered done) {
        validateAsync(stripBom(toml), SplitProfiles.DEFAULT, true, false, checked -> showImportConfirmation(
                checked.getName() != null ? checked.getName() : name, checked.getName() == null, checked, origin, false, done),
                () -> answered(done, true));
    }

    // Receive from phone: a phone on the same network opens the page the QR code or
    // the address leads to, and sends links or TOML files from there, pasted or as
    // files, as many as it likes (LinkReceiver). Each one goes through the same checks and
    // confirmation as any import, one after another in the order they came, and nothing is
    // added without the TV's yes (see processReceived).
    //
    // A card like the app picker's: ←, the title, the QR code on its own white tile (black on
    // white in both themes, so that a phone can read it), the address in monospace 16 to type on a
    // laptop, a short hint, all of it centred, and, once something has come, "Received: N" in
    // textFaint under them. The card has no button: ← closes it, as it does the desktop's cards that have
    // one, and a remote's first stop is that arrow. The screen stays on while it is open: a
    // screensaver would stop the activity, and the receiver with it. The card and the port stay
    // open while the TV asks about what came, so that the phone can send the next, until Back, ←,
    // the time, three wrong codes or leaving the app. What came and was not answered yet
    // is still asked about when the port closes, except when the app is left. Keys: receive:back
    // and receive:count.
    private void receiveFromPhone() {
        stopReceiver();
        InetAddress address = lanAddress();
        if (address == null) { toast(getString(R.string.text_receive_no_network)); return; }
        final LinkReceiver r;
        try { r = new LinkReceiver(address, LinkReceiver.newCode(new SecureRandom()), receivePages(), receivedTexts.waiting()); }
        catch (IOException e) { toast(getString(R.string.text_receive_failed)); return; }
        linkReceiver = r;
        receivedCount = 0;
        final int generation = receiveGeneration;
        Theme t = t();
        final Overlay[] shown = new Overlay[1];
        LinearLayout card = column();
        card.addView(ui.cardHeader(getString(R.string.text_receive_from_phone), "receive:back", getString(R.string.text_back),
                v -> closeOverlay(shown[0])), lp(-1, -2));
        // Under the header's 48 with nothing between, as the editor's form is, and 12 to the card's foot.
        LinearLayout box = column(); box.setGravity(Gravity.CENTER_HORIZONTAL); box.setPadding(dp(18), 0, dp(18), dp(12));
        // Drawn at its own size, a whole number of pixels a module, so it is never blurred. The most
        // that fits: a TV only 540 dp high has the card's other lines to make room for. A card is as tall as the window less 40 above (or
        // the bar or the overscan) and 12 below (or the same), and 190 of it is not the code: the header 48, the gaps 30, the address 21,
        // the hint's three lines 48, the count's line 16 and the 12 under it, and some to spare (OverlayLayer).
        float density = getResources().getDisplayMetrics().density;
        int room = host.getHeight() > 0 ? Math.round(host.getHeight() / density - Math.max(40f, host.getPaddingTop() / density)
                - Math.max(12f, host.getPaddingBottom() / density)) - 190 : 220;
        LinkReceiver.Qr qr = LinkReceiver.qr(r.url(), dp(Math.max(130, Math.min(220, room))));
        ImageView code = new ImageView(this);
        code.setImageBitmap(Bitmap.createBitmap(qr.pixels, qr.side, qr.side, Bitmap.Config.ARGB_8888));
        code.setContentDescription(r.url());
        // Its own tile: the code's quiet zone is white, whatever the theme.
        FrameLayout tile = new FrameLayout(this);
        tile.setBackground(ui.shape(Color.WHITE, 10));
        tile.setClipToOutline(true);
        tile.addView(code, new FrameLayout.LayoutParams(-2, -2));
        box.addView(tile, lp(-2, -2));
        gap(box, 10);
        // For typing on a laptop.
        TextView url = ui.mono(r.url(), 16, t.text);
        url.setGravity(Gravity.CENTER); box.addView(url, lp(-1, -2));
        gap(box, 10);
        // Centred as the address above it is: two short sentences, not a paragraph (the warnings are on the phone's page and in the README).
        TextView hint = label(getString(R.string.text_receive_hint), 13, t.textDim);
        hint.setGravity(Gravity.CENTER_HORIZONTAL);
        box.addView(hint, lp(-1, -2));
        gap(box, 10);
        // How many have come, once one has: a line of its own, which is there from the start (empty), so that the card does not change
        // size, and move, when the first text comes.
        TextView counted = label("", 12, t.textFaint);
        counted.setTag("receive:count");
        counted.setAccessibilityLiveRegion(View.ACCESSIBILITY_LIVE_REGION_POLITE);
        counted.setGravity(Gravity.CENTER);
        box.addView(counted, lp(-1, -2));
        receivedLine = counted;
        ScrollView scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(false);
        // Nothing in it to take the focus: left focusable, it would be a stop with no sign of it.
        scroll.setFocusable(false);
        scroll.addView(box);
        card.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        // No screensaver while the phone is used instead of the remote.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Overlay overlay = showOverlay("receive", OverlayLayer.Kind.CARD, card, null, () -> {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            if (receivedLine == counted) receivedLine = null;
            if (linkReceiver == r) stopReceiver();
        });
        shown[0] = overlay;
        // As wide as the app picker, which it is modelled on, and no card of the desktop's is wider.
        overlay.width(dp(420));
        // A remote starts on the back arrow, which is all the card has to stop on: Back does the same as it.
        overlay.defaultKey = "receive:back";
        // These arrive on the receiver's thread. A text it took is the TV's to answer, whether or not its card is
        // still up, unless the app was left in between.
        r.start(new LinkReceiver.Listener() {
            @Override public void received(String text, String fileName, String sender) {
                runOnUiThread(() -> {
                    if (isDestroyed() || generation != receiveGeneration) { receivedTexts.giveBack(); return; }
                    receivedTexts.add(text, fileName, sender);
                    if (linkReceiver == r) {
                        receivedCount++;
                        if (receivedLine != null) receivedLine.setText(getString(R.string.text_receive_received, receivedCount));
                    }
                    processReceived();
                });
            }
            @Override public void stopped(LinkReceiver.Stop reason) {
                runOnUiThread(() -> {
                    if (isDestroyed() || linkReceiver != r) return;
                    boolean somethingCame = receivedCount > 0;
                    stopReceiver();
                    toast(getString(reason == LinkReceiver.Stop.LOCKED_OUT ? R.string.text_receive_locked_out
                            : reason == LinkReceiver.Stop.TIMEOUT ? (somethingCame ? R.string.text_receive_timed_out_after_texts : R.string.text_receive_timed_out)
                            : R.string.text_receive_failed));
                });
            }
        });
    }
    // Closes the port and its card: its card's closing finds the port gone and leaves it be. What it took
    // stays in the queue.
    private void stopReceiver() {
        LinkReceiver r = linkReceiver;
        linkReceiver = null;
        if (r != null) r.stop();
        Overlay open = overlay("receive");
        if (open != null) closeOverlay(open);
    }

    // The next text it took, checked and asked about, when none is: importText, then validateAsync, then the
    // confirmation, then the next. Nothing is added without a yes. A text holds its place in the receiver's
    // count until it is answered, whatever the answer, which is what lets the phone send another when ten
    // waited. When one is refused, its notice stays up before the next one's check, and its question, can cover it.
    private void processReceived() {
        if (isDestroyed() || receivedTexts.busy() || receivedTexts.queued() == 0) return;
        if (receivedHold) {
            if (toastView != null && SystemClock.uptimeMillis() < receivedHoldUntil) {
                if (!receivedPollPending) {
                    receivedPollPending = true;
                    toastHandler.postDelayed(() -> { receivedPollPending = false; processReceived(); }, 250);
                }
                return;
            }
            receivedHold = false;
        }
        final ReceivedQueue.Item item = receivedTexts.next();
        // A TOML file is named after its file, as one picked here is; a link names itself.
        importText(item.fileName != null ? item.fileName : getString(R.string.text_from_another_device), item.text, item.sender, refused -> {
            if (!receivedTexts.answered(item)) return;
            if (refused) { receivedHold = true; receivedHoldUntil = SystemClock.uptimeMillis() + 10_000; }
            // After what answered it is done, not inside it.
            host.post(this::processReceived);
        });
    }
    // The app is left: what is waiting for its turn is dropped, and so is what the receiver handed over
    // that this thread has not seen. The one being asked about is not: its question is still up.
    private void dropReceived() {
        receiveGeneration++;
        receivedTexts.drop();
    }
    // The address a phone on the same Wi-Fi or Ethernet reaches this device at: the
    // active network's first, and never a VPN's. FreeTunnel is left out of its own VPN,
    // so the phone's connection comes in past it.
    @SuppressWarnings("deprecation") // getAllNetworks(): a one-off look needs no network callback.
    private InetAddress lanAddress() {
        ConnectivityManager connectivity = getSystemService(ConnectivityManager.class);
        List<Network> networks = new ArrayList<>();
        Network active = connectivity.getActiveNetwork();
        if (active != null) networks.add(active);
        for (Network network : connectivity.getAllNetworks()) if (!network.equals(active)) networks.add(network);
        List<InetAddress> addresses = new ArrayList<>();
        for (Network network : networks) {
            NetworkCapabilities capabilities = connectivity.getNetworkCapabilities(network);
            LinkProperties link = connectivity.getLinkProperties(network);
            if (capabilities == null || link == null || !capabilities.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_VPN)
                    || !(capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)
                    || capabilities.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET))) continue;
            for (LinkAddress linkAddress : link.getLinkAddresses()) addresses.add(linkAddress.getAddress());
        }
        return LinkReceiver.pickAddress(addresses);
    }
    // The phone's page in this device's language.
    private LinkReceiver.Pages receivePages() {
        return new LinkReceiver.Pages(getResources().getConfiguration().getLocales().get(0).toLanguageTag(), getString(R.string.receive_page_title),
                getString(R.string.receive_page_prompt), getString(R.string.receive_page_file), getString(R.string.receive_page_send),
                getString(R.string.receive_page_note), getString(R.string.receive_page_sent), getString(R.string.receive_page_retry),
                getString(R.string.receive_page_too_large), getString(R.string.receive_page_busy),
                getString(R.string.receive_page_wrong_code), getString(R.string.receive_page_locked_out));
    }
    // A refusal is the desktop's kind of notice, a toast, however long: up to 6 lines, 10 seconds.
    private void refuseImport(int messageResource) {
        toast(getString(messageResource));
    }
    // What an import left out, a line for each.
    private String importNotes(RoutingConfig.Import checked) {
        List<String> notes = new ArrayList<>();
        if (checked.getSubscriptionNotKept()) notes.add(getString(R.string.import_subscription_note));
        if (checked.getCertificateIgnored()) notes.add(getString(R.string.import_certificate_note));
        return TextUtils.join("\n", notes);
    }
    // The question before anything is added, as the desktop asks it (Backend::askToImport): whether it
    // is a link or a file, the server it would connect to, and what is wrong with it. The lines are
    // those, with the first, the second and the third in this order whatever they say:
    //   Add a VPN server from this link?               (or, for a name already in the list: “Name” already exists…)
    //   Server: host
    //   Sent from 192.168.1.5                           (Android only: where a phone sent it from)
    // then the rest. The question does not say the name it would be added under, as the desktop's does
    // not: the name comes with the link, and is the one thing whoever made it fully controls, so that a
    // decision made on the name alone can be made on a lie; the server is what it connects to.
    // origin is the address the text was sent from, shown so that a text someone
    // else on the network sent first can be told apart; null when it came from here. The name that is
    // shown (the existing one's, in a collision) and the server are shown as ShownText shows them, so that
    // whatever sent them, the lines after them stay in sight. namedAfterFile is true when name is the one the
    // text was imported under, its file's name or the app's words for where it came from, and false when it
    // is a link's or an export's own; link is true for a tt:// link. Cancel has the focus, and Import is red
    // and waits for 400 ms. A name already in the list is the desktop's collision, and the desktop's alone:
    // Replace puts it where that one is, Add copy adds it as another. The same server under another
    // name is no collision, as on the desktop.
    private void showImportConfirmation(String name, boolean namedAfterFile, RoutingConfig.Import checked, String origin, boolean link) {
        showImportConfirmation(name, namedAfterFile, checked, origin, link, null);
    }
    private void showImportConfirmation(String name, boolean namedAfterFile, RoutingConfig.Import checked, String origin, boolean link,
                                        Answered done) {
        String source = checked.getToml();
        org.tomlj.TomlTable parsed = RoutingConfig.validate(source);
        Config clash = null;
        for (Config config : configs) {
            if (config.name.equals(name)) { clash = config; break; }
        }
        StringBuilder message = new StringBuilder(clash == null
                ? getString(link ? R.string.import_question_link : R.string.import_question_file)
                : getString(R.string.import_exists, ShownText.of(clash.name)));
        message.append('\n').append(getString(R.string.import_server, ShownText.of(parsed.getString("endpoint.hostname"))));
        if (origin != null) message.append(getString(R.string.text_nsent_from_s, origin));
        // A file's extension is not counted: a configuration named after "Дом.toml"
        // would otherwise always be said to mix alphabets. Only a file's: a link's or
        // an export's name, or the server a link is named after, such as "раураӏ.com",
        // is counted whole.
        String word = namedAfterFile && name.matches("(?s).+\\.[0-9A-Za-z]{1,5}") ? name.substring(0, name.lastIndexOf('.')) : name;
        if (word.codePoints().mapToObj(Character.UnicodeScript::of)
                .filter(script -> script != Character.UnicodeScript.COMMON && script != Character.UnicodeScript.INHERITED
                        && script != Character.UnicodeScript.UNKNOWN).distinct().count() > 1)
            message.append('\n').append(getString(R.string.import_mixed_name));
        if (Boolean.TRUE.equals(parsed.getBoolean("endpoint.skip_verification")))
            message.append('\n').append(getString(link ? R.string.import_skips_link : R.string.import_skips_file));
        String notes = importNotes(checked);
        if (!notes.isEmpty()) message.append('\n').append(notes);
        final Config existing = clash;
        // Whichever way it is answered, Cancel, Back or a tap outside included, the asker is told.
        final Runnable declined = () -> answered(done, false);
        if (existing == null) {
            showConfirm(message, getString(R.string.import_confirm), null,
                    () -> { addImported(name, source); answered(done, false); }, null, declined);
        } else {
            showConfirm(message, getString(R.string.import_replace), getString(R.string.import_add_copy),
                    () -> { replaceImported(existing, name, source); answered(done, false); },
                    () -> { addImported(name, source); answered(done, false); }, declined);
        }
    }
    // After an import the page stays as it is, and the toast says what was added, as the desktop's does.
    private void addImported(String name, String source) {
        configs.add(new Config(name, source));
        if (configs.size() == 1) activeIndex = 0;
        if (!saveConfigs()) return;
        render();
        toast(getString(R.string.config_added, ShownText.of(name)));
    }
    // Replace: the config goes where the one it replaces was, and if that one is the one the VPN runs,
    // the VPN reconnects to this. If that one is gone by now, it is added.
    private void replaceImported(Config existing, String name, String source) {
        int at = configs.indexOf(existing);
        if (at < 0) { addImported(name, source); return; }
        // The config goes where the old one was, and uses the profile it used.
        configs.set(at, new Config(name, source, existing.profile));
        if (!saveConfigs()) return;
        if (at == activeIndex) applyCurrentRules();
        render();
        toast(getString(R.string.config_added, ShownText.of(name)));
    }
    private static String stripBom(String toml) {
        return toml.startsWith("\uFEFF") ? toml.substring(1) : toml;
    }
    // The editor: the desktop's card (qml/CreateConfigOverlay.qml), with ← and the title, and the form, which scrolls with its buttons: Name;
    // Server host; Address(es); Username and Password, side by side; Protocol and DNS servers; Custom SNI and Client random; the Split
    // profile this config uses, as a select box (where a config's profile is chosen, as on the desktop); Allow IPv6, Skip certificate check
    // and Anti-DPI; the Certificate, with its "Load from file…" link; and Cancel and Save at the end. ConfigForm reads the fields from the
    // config and writes them back into it, and has the desktop's checks. It opens on the name. The config's password is in it, so while
    // the card is up the window is FLAG_SECURE. Back, ←, Cancel and a tap outside ask before they drop what was typed. Keys: editor:back,
    // editor:name, editor:host, editor:addresses, editor:username, editor:password, editor:protocol, editor:dns, editor:sni,
    // editor:random, editor:profile, editor:ipv6, editor:skip, editor:antidpi, editor:cert, editor:certfile, editor:cancel and editor:save.
    private void editConfig(int index) {
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed)); return; }
        if (overlay("editor") != null) return;
        final Config editing = index < 0 ? null : configs.get(index);
        final ConfigForm.Fields initial = editing == null ? new ConfigForm.Fields() : ConfigForm.read(editing.toml);
        final Overlay[] shown = new Overlay[1];
        EditText name = ui.lineEdit();
        name.setHint(getString(R.string.editor_name_placeholder));
        EditText host = ui.lineEdit();
        host.setHint("frankfurt.example.com");
        EditText addresses = ui.lineEdit();
        addresses.setHint("1.2.3.4:443");
        EditText username = ui.lineEdit();
        EditText password = ui.lineEdit();
        // Dots, and no suggestions: the password is a secret. The dots are the desktop's ● and none is ever shown as the character it
        // stands for (Ui.PasswordMask); set after setInputType, which replaces the transformation.
        password.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        password.setTransformationMethod(new Ui.PasswordMask());
        // setInputType makes a password field monospace, and the desktop's dots are in the font of its other fields.
        password.setTypeface(Fonts.regular(this));
        EditText dns = ui.lineEdit();
        dns.setHint("1.1.1.1, 8.8.8.8");
        EditText sni = ui.lineEdit();
        EditText random = ui.lineEdit();
        EditText certificate = new EditText(this);
        certificate.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        EditText[] texts = {name, host, addresses, username, password, dns, sni, random, certificate};
        for (EditText text : texts) {
            // In landscape a keyboard can fill the screen with its own copy of the text, which is outside FLAG_SECURE and holds the
            // password: asked not to go full screen, nor to copy the text if it does anyway, nor to learn from what is typed here.
            text.setImeOptions(text.getImeOptions() | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                    | EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING);
            // Next field, as Tab is on the desktop: the last one of a line, and a text area, have none to go on to.
            if (text != random && text != certificate) text.setImeOptions(text.getImeOptions() | EditorInfo.IME_ACTION_NEXT);
        }
        if (editing != null) name.setText(editing.name);
        host.setText(initial.hostname);
        addresses.setText(initial.addresses);
        username.setText(initial.username);
        password.setText(initial.password);
        dns.setText(initial.dns);
        sni.setText(initial.customSni);
        random.setText(initial.clientRandom);
        certificate.setText(initial.certificate);
        // A field of the desktop's holds its caret at the end of what setText gave it, so that a field reached by Tab, or by a remote's
        // arrows, appends; Android's holds it at the start, and what is typed there goes before the name, the host, the user name.
        for (EditText text : new EditText[] {name, host, addresses, username, password, dns, sni, random}) text.setSelection(text.getText().length());
        final String[] protocol = {initial.protocol};
        // The profile the config uses; a new one uses Default.
        final String[] chosenProfile = {editing == null ? SplitProfiles.DEFAULT : splitModel().effective(editing.profile)};
        ToggleView ipv6 = ui.toggle(initial.allowIpv6, false, "editor:ipv6");
        ipv6.setContentDescription(getString(R.string.editor_ipv6));
        ToggleView skip = ui.toggle(initial.skipVerification, false, "editor:skip");
        skip.setContentDescription(getString(R.string.editor_skip_verification));
        ToggleView antiDpi = ui.toggle(initial.antiDpi, false, "editor:antidpi");
        antiDpi.setContentDescription(getString(R.string.editor_anti_dpi));

        // What the form holds, as the check and the write have it.
        final java.util.function.Supplier<ConfigForm.Fields> fields = () -> {
            ConfigForm.Fields f = new ConfigForm.Fields();
            f.hostname = host.getText().toString();
            f.addresses = addresses.getText().toString();
            f.username = username.getText().toString();
            f.password = password.getText().toString();
            f.protocol = protocol[0];
            f.dns = dns.getText().toString();
            f.customSni = sni.getText().toString();
            f.clientRandom = random.getText().toString();
            f.certificate = certificate.getText().toString();
            f.allowIpv6 = ipv6.isChecked();
            f.skipVerification = skip.isChecked();
            f.antiDpi = antiDpi.isChecked();
            return f;
        };
        // Joined on a character no text holds, so that moving a boundary between two fields is a change (CreateConfigOverlay.snapshot).
        final java.util.function.Supplier<String> snapshot = () -> String.join("\u001f", name.getText(), host.getText(), addresses.getText(),
                username.getText(), password.getText(), protocol[0], dns.getText(), sni.getText(), random.getText(), certificate.getText(),
                String.valueOf(ipv6.isChecked()), String.valueOf(skip.isChecked()), String.valueOf(antiDpi.isChecked()), chosenProfile[0]);
        final String opened = snapshot.get();

        final boolean[] asking = {false};
        final Runnable tryClose = () -> {
            Overlay editor = shown[0];
            if (editor == null || editor.closed) return;
            if (snapshot.get().equals(opened)) { closeOverlay(editor); return; }
            // A second Cancel, or a Back, while the question is up or waits is no second question.
            if (asking[0]) return;
            asking[0] = true;
            showConfirm(getString(R.string.editor_discard_question), getString(R.string.editor_discard), null,
                    () -> closeOverlay(editor), null, () -> asking[0] = false);
        };

        LinearLayout card = column();
        card.addView(ui.cardHeader(getString(index < 0 ? R.string.text_new_configuration : R.string.text_edit_configuration),
                "editor:back", getString(R.string.text_back), v -> tryClose.run()), lp(-1, -2));
        LinearLayout form = column();
        form.setPadding(dp(18), 0, dp(18), 0);
        // The toggles' rows fill a little beyond the fields' edges when they have the focus, and a link ends at the margin.
        form.setClipToPadding(false);
        form.addView(ui.field(getString(R.string.editor_name), name, "editor:name"), lp(-1, -2));
        gap(form, 10);
        form.addView(ui.field(getString(R.string.editor_host), host, "editor:host"), lp(-1, -2));
        gap(form, 10);
        form.addView(ui.field(getString(R.string.editor_addresses), addresses, "editor:addresses"), lp(-1, -2));
        gap(form, 10);
        form.addView(pair(ui.field(getString(R.string.editor_username), username, "editor:username"),
                ui.field(getString(R.string.editor_password), password, "editor:password")), lp(-1, -2));
        gap(form, 10);
        TextView[] protocolValue = new TextView[1];
        LinearLayout protocolBox = ui.selectField(getString(R.string.editor_protocol), protocolLabel(protocol[0]), "editor:protocol", protocolValue, v -> {
            // The keyboard would cover the choices.
            hideKeyboard();
            showSelect("editor:protocol", new String[] {protocolLabel("http2"), protocolLabel("http3")}, "http3".equals(protocol[0]) ? 1 : 0, which -> {
                protocol[0] = which == 1 ? "http3" : "http2";
                protocolValue[0].setText(protocolLabel(protocol[0]));
            });
        });
        form.addView(pair(protocolBox, ui.field(getString(R.string.editor_dns), dns, "editor:dns")), lp(-1, -2));
        gap(form, 10);
        form.addView(pair(ui.field(getString(R.string.editor_sni), sni, "editor:sni"), ui.field(getString(R.string.editor_random), random, "editor:random")), lp(-1, -2));
        gap(form, 10);
        TextView[] profileValue = new TextView[1];
        form.addView(ui.selectField(getString(R.string.editor_split_profile), profileLabel(chosenProfile[0]), "editor:profile", profileValue, v -> {
            hideKeyboard();
            List<String> names = splitModel().names();
            String[] labels = new String[names.size()];
            for (int i = 0; i < labels.length; i++) labels[i] = profileLabel(names.get(i));
            showSelect("editor:profile", labels, names.indexOf(chosenProfile[0]), which -> {
                chosenProfile[0] = names.get(which);
                profileValue[0].setText(profileLabel(chosenProfile[0]));
            });
        }), lp(-1, -2));
        gap(form, 10);
        form.addView(ui.editorToggleRow(getString(R.string.editor_ipv6), ipv6), ui.focusRowLp());
        gap(form, 10);
        form.addView(ui.editorToggleRow(getString(R.string.editor_skip_verification), skip), ui.focusRowLp());
        gap(form, 10);
        form.addView(ui.editorToggleRow(getString(R.string.editor_anti_dpi), antiDpi), ui.focusRowLp());
        gap(form, 10);
        TextView load = ui.link(getString(R.string.editor_load_certificate), 12, Ui.LinkKind.ACCENT, "editor:certfile");
        load.setOnClickListener(v -> pickCertificate(certificate));
        form.addView(ui.fieldArea(getString(R.string.editor_certificate), load, certificate, "editor:cert", 70), lp(-1, -2));
        gap(form, 10);
        Button cancel = ui.button(getString(R.string.text_cancel), Ui.Kind.SECONDARY, 88, "editor:cancel");
        cancel.setOnClickListener(v -> tryClose.run());
        Button save = ui.button(getString(R.string.text_save), Ui.Kind.PRIMARY, 88, "editor:save");
        // The last row of the form, which scrolls with it: 6 above it and 12 below.
        LinearLayout buttons = row();
        buttons.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        buttons.setPadding(0, dp(6), 0, dp(12));
        buttons.addView(cancel, lp(-2, 32));
        ui.gapH(buttons, 8);
        buttons.addView(save, lp(-2, 32));
        form.addView(buttons, lp(-1, -2));
        ScrollView scroll = new ScrollView(this);
        scroll.setVerticalScrollBarEnabled(false);
        // The fields are its stops, not it.
        scroll.setFocusable(false);
        scroll.addView(form);
        card.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));

        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        Overlay overlay = showOverlay("editor", OverlayLayer.Kind.CARD, card, null, () -> {
            validationRequest++;
            certificateTarget = null;
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_SECURE);
        });
        shown[0] = overlay;
        overlay.width(dp(372));
        overlay.escape = tryClose;
        // A remote starts on the name: left to itself, on the first control.
        overlay.defaultKey = "editor:name";
        // One check at a time. Only a phone greys Save out meanwhile: disabling the view
        // that has a remote's focus moves the focus to the first control as well.
        boolean[] checking = {false};
        save.setOnClickListener(v -> {
            if (checking[0]) return;
            // Nothing changed, nothing to write: the file would come back in the form the editor writes, its comments gone and its keys
            // in another order (as the desktop's Save does).
            if (editing != null && snapshot.get().equals(opened)) { closeOverlay(overlay); return; }
            ConfigForm.Fields f = fields.get();
            int problem = ConfigForm.problem(f);
            if (problem != 0) { toast(getString(problem)); return; }
            // A name left empty is the host's, cut to the limit by code points as a name nobody typed is. A name that was typed is taken
            // whole or refused, as on the desktop; a config that already has a longer name (links and files could give any) keeps it.
            String typed = name.getText().toString().trim();
            if (ConfigForm.nameTooLong(typed) && (editing == null || !typed.equals(editing.name.trim()))) {
                toast(getString(R.string.editor_error_name_too_long, ConfigForm.NAME_LIMIT));
                return;
            }
            final String n = typed.isEmpty() ? ConfigForm.clippedName(f.hostname.trim()) : typed;
            final boolean certificateTrimmed = ConfigForm.certificates(f.certificate).dropped;
            final String text = ConfigForm.write(editing == null ? null : editing.toml, f);
            // The file has something the form does not show that this cannot write back unchanged: it is left as it is.
            if (text == null) { toast(getString(R.string.editor_error_unwritable)); return; }
            checking[0] = true;
            if (save.isInTouchMode()) save.setEnabled(false);
            // Checked as an import: what is written is a whole config, and the file it was made from, or a certificate pasted whole
            // with its private key, can carry what an import takes out (a subscription, an auth key, what is no certificate).
            validateAsync(text, chosenProfile[0], true, true, result -> {
                RoutingConfig.Import checked = certificateTrimmed && !result.getCertificateIgnored()
                        ? new RoutingConfig.Import(result.getToml(), result.getSubscriptionNotKept(), true) : result;
                if (overlay.closed) return;
                if (editing == null) configs.add(new Config(n, checked.getToml(), chosenProfile[0]));
                else {
                    int current = configs.indexOf(editing);
                    if (current < 0) { checking[0] = false; save.setEnabled(true); return; }
                    configs.set(current, new Config(n, checked.getToml(), chosenProfile[0]));
                }
                boolean saved = saveConfigs();
                closeOverlay(overlay);
                if (!saved) return;
                applyCurrentRules(); render();
                // What was left out of it, as a toast.
                String notes = importNotes(checked);
                if (!notes.isEmpty()) toast(notes);
            }, () -> { checking[0] = false; save.setEnabled(true); });
        });
    }

    // Two of the form's fields side by side, 10 apart and as wide as each other.
    private LinearLayout pair(View left, View right) {
        LinearLayout both = row();
        both.setGravity(Gravity.TOP);
        both.addView(left, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        ui.gapH(both, 10);
        both.addView(right, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
        return both;
    }
    private static String protocolLabel(String protocol) { return "http3".equals(protocol) ? "HTTP/3" : "HTTP/2"; }

    // "Load from file…": the system's file picker, and the text of what it gives is put in the certificate field. Many TVs have no picker.
    private EditText certificateTarget;
    private void pickCertificate(EditText target) {
        hideKeyboard();
        certificateTarget = target;
        for (String action : new String[]{Intent.ACTION_OPEN_DOCUMENT, Intent.ACTION_GET_CONTENT}) {
            if (startForResult(new Intent(action).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE), PICK_CERTIFICATE)) return;
        }
        certificateTarget = null;
        toast(getString(R.string.editor_no_file_picker));
    }
    // A file the editor cannot read changes nothing, and says so: a file that is not text, or that is empty, must not wipe what was pasted.
    // The desktop's limit (kMaxUserTextFileBytes) and words. Read on the worker that writes exports, as a document provider can keep the
    // reader waiting (a file in the cloud) and a main thread kept waiting is killed; the box is filled, or the refusal said, back on the
    // main thread, and whatever goes wrong with a file is a refusal and never a crash.
    private static final int CERTIFICATE_FILE_LIMIT = 1024 * 1024;
    private void readCertificate(Uri uri, EditText target) {
        if (isDestroyed()) return;
        try {
            ioWorker.execute(() -> {
                String pem = null;
                int refusal = 0;
                try (InputStream input = getContentResolver().openInputStream(uri); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
                    if (input == null) {
                        refusal = R.string.text_could_not_open_file;
                    } else {
                        byte[] bytes = new byte[8192];
                        int n;
                        int total = 0;
                        while (refusal == 0 && (n = input.read(bytes)) != -1) {
                            total += n;
                            if (total > CERTIFICATE_FILE_LIMIT) refusal = R.string.editor_file_too_large;
                            else out.write(bytes, 0, n);
                        }
                        if (refusal == 0) {
                            String text = out.toString(StandardCharsets.UTF_8.name());
                            if (text.trim().isEmpty()) refusal = R.string.editor_file_empty;
                            else pem = text;
                        }
                    }
                } catch (Exception | OutOfMemoryError unreadable) {
                    pem = null;
                    refusal = R.string.text_could_not_open_file;
                }
                final String text = pem;
                final int said = refusal;
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    if (said != 0) toast(getString(said));
                    else if (target.isAttachedToWindow()) target.setText(text);
                });
            });
        } catch (RejectedExecutionException closing) {
            // The window is going away.
        }
    }
    // The name is cut in the middle to 36, as the desktop cuts it, and shown as ShownText shows names.
    private void confirmRemove(int index) {
        if (storageFailed) return;
        Config config = configs.get(index);
        showConfirm(getString(R.string.delete_config_question, ShownText.elideMiddle(ShownText.oneLine(config.name), 36)),
                getString(R.string.text_delete), () -> {
                    // Where it is now: the list may have changed while this was asked.
                    int at = configs.indexOf(config);
                    if (at < 0) return;
                    boolean activeRemoved = at == activeIndex;
                    configs.remove(at);
                    if (at < activeIndex) activeIndex--;
                    activeIndex = Math.max(0, Math.min(activeIndex, configs.size() - 1));
                    saveConfigs(); if (activeRemoved) applyCurrentRules(); render();
                });
    }
    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PREPARE_VPN) {
            if (resultCode == RESULT_OK) startVpnWithPendingConfig();
            else { pendingConfig = null; toast(getString(R.string.text_vpn_permission_was_not_granted)); }
            return;
        }
        if (requestCode == PICK_CERTIFICATE) {
            EditText target = certificateTarget;
            certificateTarget = null;
            Uri picked = resultCode == RESULT_OK && data != null ? data.getData() : null;
            if (target != null && picked != null) readCertificate(picked, target);
            return;
        }
        if (requestCode == EXPORT_CONFIG) {
            Uri saved = resultCode == RESULT_OK && data != null ? data.getData() : null;
            if (saved == null) exporting = null;
            else writeExport(saved);
            return;
        }
        if (requestCode != PICK_CONFIG || resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData(); if (uri == null) return;
        try (InputStream input = getContentResolver().openInputStream(uri); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (input == null) return;
            byte[] bytes = new byte[8192]; int n; int total = 0;
            while ((n = input.read(bytes)) != -1) { total += n; if (total > 1024 * 1024) { toast(getString(R.string.text_file_is_too_large)); return; } out.write(bytes, 0, n); }
            String name = null;
            try (Cursor cursor = getContentResolver().query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
            } catch (Exception ignored) { }
            if (name == null || name.isEmpty()) name = uri.getLastPathSegment();
            if (name == null || name.isEmpty()) name = getString(R.string.text_configuration);
            int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
            importToml(name, out.toString(StandardCharsets.UTF_8.name()), null);
        } catch (Exception e) { toast(getString(R.string.text_could_not_open_file)); }
    }

    // The Split page (qml/pages/SplitPage.qml), top to bottom: the Split Tunneling switch; the Mode, as a value and its ▾; the
    // notices that apply; Profile and its chips, the edited one in accent, with a + that opens the field for a new name;
    // the rules header, with Recommended for Russia and Clear all, the domains as chips and the field to add them; and the
    // applications header, with Choose… and Clear all, the applications as chips and the field to type one's name in. The chips are the profile being edited
    // (SplitProfiles.edited), which is not the profile the selected config uses: that is chosen in the editor.
    // Keys: split:enabled, split:mode, profile:N, profile:N:remove, profile:add, profile:name, domains:russia, domains:clear,
    // domain:N, domains:input, apps:choose, apps:clear, app:N and apps:input (with the suggestions under it: AppSuggestions).
    private void split(LinearLayout body) {
        SplitProfiles model = splitModel();
        final String edited = model.edited();
        boolean enabled = prefs.getBoolean("split_enabled", false);
        boolean through = prefs.getBoolean("through", false);
        ToggleView splitSwitch = ui.toggle(enabled, false, "split:enabled");
        splitSwitch.setContentDescription(getString(R.string.text_split_tunnelling));
        splitSwitch.setListener((v, checked) -> {
            prefs.edit().putBoolean("split_enabled", checked).apply();
            applyCurrentRules();
            // The notice depends on it.
            render();
        });
        body.addView(ui.toggleRow(getString(R.string.text_split_tunnelling), null, splitSwitch), ui.focusRowLp());
        // Only the value and its ▾ are the control: the label is plain text.
        body.addView(ui.dropdown(getString(R.string.text_mode), through ? getString(R.string.text_through_vpn_62) : getString(R.string.text_bypass_vpn_61),
                "split:mode", v -> showSelect("split:mode",
                new String[]{getString(R.string.text_bypass_vpn_61), getString(R.string.text_through_vpn_62)}, through ? 1 : 0, which -> {
                    prefs.edit().putBoolean("through", which == 1).apply(); applyCurrentRules(); render();
                })), lp(-1, -2));
        for (CharSequence notice : splitNotices(model, enabled, through)) {
            gap(body, 6);
            body.addView(ui.notice(notice), lp(-1, -2));
        }
        gap(body, 6);
        body.addView(ui.sectionLabel(getString(R.string.split_profile)), lp(-1, -2));
        FlowLayout profiles = new FlowLayout(this, dp(6));
        List<String> names = model.names();
        for (int i = 0; i < names.size(); i++) {
            final String name = names.get(i);
            boolean current = name.equals(edited);
            LinearLayout chip = ui.chip(profileLabel(name), current, true, "profile:" + i, false);
            chip.setOnClickListener(v -> selectProfile(name));
            // Default is the one that cannot be deleted.
            if (!SplitProfiles.DEFAULT.equals(name)) {
                ImageView x = ui.chipX("profile:" + i + ":remove", current);
                x.setContentDescription(getString(R.string.text_delete) + " " + name);
                x.setOnClickListener(v -> confirmRemoveProfile(name));
                chip.addView(x, ui.chipXLp());
                ui.linkChipX(chip, x);
            }
            profiles.addView(chip);
        }
        TextView plus = ui.plusChip(getString(R.string.split_profile_add), "profile:add");
        plus.setOnClickListener(v -> openProfileName());
        profiles.addView(plus);
        LinearLayout.LayoutParams profilesLp = lp(-1, -2);
        profilesLp.topMargin = dp(2);
        body.addView(profiles, profilesLp);
        // The field for the name of a new profile: there all the time, and shown while it is wanted.
        // Its "profile name, then Enter" stays while it has the focus, as the desktop's does (the field is only there while it is wanted).
        EditText nameInput = ui.pageInput(getString(R.string.split_profile_name_hint), "profile:name", true);
        nameInput.setFilters(new InputFilter[]{new InputFilter.LengthFilter(SplitProfiles.MAX_NAME)});
        nameInput.setImeOptions(EditorInfo.IME_ACTION_DONE);
        nameInput.setText(profileNameDraft);
        nameInput.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence text, int start, int count, int after) { }
            @Override public void onTextChanged(CharSequence text, int start, int before, int count) { }
            @Override public void afterTextChanged(Editable text) { profileNameDraft = text.toString(); }
        });
        // Enter adds it; Escape, or going to another control, drops it.
        nameInput.setOnEditorActionListener((field, action, event) -> {
            if (event != null && event.getAction() != KeyEvent.ACTION_DOWN) return true;
            addProfile(nameInput.getText().toString());
            return true;
        });
        nameInput.setOnKeyListener((field, keyCode, event) -> {
            if (keyCode != KeyEvent.KEYCODE_ESCAPE) return false;
            if (event.getAction() == KeyEvent.ACTION_UP) closeProfileName();
            return true;
        });
        nameInput.setOnFocusChangeListener((field, hasFocus) -> { if (!hasFocus && !rendering) closeProfileName(); });
        nameInput.setVisibility(profileNameOpen ? View.VISIBLE : View.GONE);
        LinearLayout.LayoutParams nameLp = lp(-1, 36);
        nameLp.topMargin = dp(6);
        body.addView(nameInput, nameLp);
        profileNameInput = nameInput;
        gap(body, 14);

        // Rules: the domains of the profile being edited.
        List<String> domainRules = model.domains(edited);
        LinearLayout domainHeader = row();
        TextView rulesTitle = ui.sectionLabel(through ? getString(R.string.text_rules_through_vpn) : getString(R.string.text_rules_bypass_vpn));
        rulesTitle.setSingleLine(true);
        rulesTitle.setEllipsize(TextUtils.TruncateAt.END);
        domainHeader.addView(rulesTitle, weighted());
        TextView recommended = ui.link(getString(R.string.split_recommended), 12, Ui.LinkKind.ACCENT, "domains:russia");
        recommended.setOnClickListener(v -> addRecommendedRussia());
        // The links add none to the row's height, and the last ends at the page's margin.
        domainHeader.addView(recommended, ui.linkLp(domainRules.isEmpty()));
        if (!domainRules.isEmpty()) {
            TextView clearDomains = ui.link(getString(R.string.split_clear_all), 12, Ui.LinkKind.DANGER, "domains:clear");
            clearDomains.setOnClickListener(v -> showConfirm(getString(R.string.split_clear_domains_question), getString(R.string.text_clear),
                    () -> editRules(m -> m.clearDomains(m.edited()))));
            domainHeader.addView(clearDomains, ui.linkLp(true));
        }
        body.addView(domainHeader, lp(-1, -2));
        if (!domainRules.isEmpty()) {
            gap(body, 6);
            FlowLayout domains = new FlowLayout(this, dp(6));
            for (int i = 0; i < domainRules.size(); i++) {
                final int index = i;
                final String rule = domainRules.get(i);
                LinearLayout chip = ui.chip(rule, false, false, null, false);
                // A rule goes at once, without being asked about, as the desktop's does.
                ImageView x = ui.chipX("domain:" + i, false);
                x.setContentDescription(getString(R.string.text_delete) + " " + rule);
                x.setOnClickListener(v -> editRules(m -> m.removeDomain(m.edited(), index)));
                chip.addView(x, ui.chipXLp());
                domains.addView(chip);
            }
            body.addView(domains, lp(-1, -2));
        }
        EditText domainInput = ui.pageInput(getString(R.string.split_domains_hint), "domains:input");
        domainInput.setOnEditorActionListener((field, action, event) -> {
            if (event != null && event.getAction() != KeyEvent.ACTION_DOWN) return true;
            if (enterDomains(domainInput.getText().toString())) domainInput.setText("");
            return true;
        });
        LinearLayout.LayoutParams domainInputLp = lp(-1, 36);
        domainInputLp.topMargin = dp(6);
        body.addView(domainInput, domainInputLp);
        gap(body, 14);

        // Applications: those of the profile being edited, by the name the picker showed, and a field to type one's name in: what
        // matches is listed above it, as the desktop's does (the desktop's can also take a path or a dropped file, which Android has
        // none of).
        List<String> appRules = model.apps(edited);
        LinearLayout appHeader = row();
        TextView appsTitle = ui.sectionLabel(through ? getString(R.string.text_applications_through_vpn) : getString(R.string.text_applications_bypass_vpn));
        appsTitle.setSingleLine(true);
        appsTitle.setEllipsize(TextUtils.TruncateAt.END);
        appHeader.addView(appsTitle, weighted());
        TextView chooseLink = ui.link(getString(R.string.text_choose), 12, Ui.LinkKind.ACCENT, "apps:choose");
        chooseLink.setOnClickListener(v -> appPicker());
        appHeader.addView(chooseLink, ui.linkLp(appRules.isEmpty()));
        if (!appRules.isEmpty()) {
            TextView clearApps = ui.link(getString(R.string.split_clear_all), 12, Ui.LinkKind.DANGER, "apps:clear");
            clearApps.setOnClickListener(v -> showConfirm(getString(R.string.split_clear_apps_question), getString(R.string.text_clear),
                    () -> editRules(m -> m.clearApps(m.edited()))));
            appHeader.addView(clearApps, ui.linkLp(true));
        }
        body.addView(appHeader, lp(-1, -2));
        if (!appRules.isEmpty()) {
            gap(body, 6);
            FlowLayout apps = new FlowLayout(this, dp(6));
            PackageManager pm = getPackageManager();
            for (int i = 0; i < appRules.size(); i++) {
                final int index = i;
                final String label = appLabel(pm, appRules.get(i));
                LinearLayout chip = ui.chip(label, false, false, null, false);
                ImageView x = ui.chipX("app:" + i, false);
                x.setContentDescription(getString(R.string.text_delete) + " " + label);
                x.setOnClickListener(v -> editRules(m -> m.removeApp(m.edited(), index)));
                chip.addView(x, ui.chipXLp());
                apps.addView(chip);
            }
            body.addView(apps, lp(-1, -2));
        }
        EditText appInput = ui.pageInput(getString(R.string.split_apps_hint), "apps:input");
        appInput.setImeOptions(EditorInfo.IME_ACTION_DONE);
        LinearLayout.LayoutParams appInputLp = lp(-1, 36);
        appInputLp.topMargin = dp(6);
        body.addView(appInput, appInputLp);
        appSuggestions.attach(appInput);
    }

    // The name an application is listed by: the one the launcher shows, or its package where it is no longer installed, which is
    // all there is to know it by, and to take it out of the list by.
    private String appLabel(PackageManager pm, String pkg) {
        try { return pm.getApplicationInfo(pkg, 0).loadLabel(pm).toString(); }
        catch (PackageManager.NameNotFoundException gone) { return pkg; }
    }

    // The built-in profile is stored as "Default" and shown in the language of the app.
    private String profileLabel(String name) {
        return SplitProfiles.DEFAULT.equals(name) ? getString(R.string.split_profile_default) : name;
    }

    // What the page says is wrong or worth knowing, one notice each, in this order:
    //  - Through VPN would route nothing (the desktop's words, naming the config and the profile it uses), worked out by the
    //    code that builds the plan (RoutingConfig.prepare): its warning for no rules at all. The full tunnel stays on;
    //  - applications and domains both listed under Through VPN, which Android can not add together (VpnService takes the
    //    applications and the destinations as an intersection), so the full tunnel stays on then too: Android's own;
    //  - rules the selected config brings in with it, which count too; and rules an older preview let be saved that were dropped.
    private List<CharSequence> splitNotices(SplitProfiles model, boolean enabled, boolean through) {
        List<CharSequence> notices = new ArrayList<>();
        if (enabled && through) {
            if (configs.isEmpty()) {
                SplitStore.Rules rules = SplitStore.rules(splitPrefs, null, SplitProfiles.DEFAULT);
                if (rules.domains.isEmpty() && installedApps(rules.apps).isEmpty()) notices.add(getString(R.string.split_notice_nothing_no_configs));
            } else {
                Config active = activeConfig();
                String profile = model.effective(active.profile);
                SplitStore.Rules rules = SplitStore.rules(splitPrefs, null, profile);
                Integer warning = null;
                try { warning = RoutingConfig.prepare(active.toml, true, true, rules.domains, installedApps(rules.apps), true).getWarning(); }
                catch (Exception unusable) { /* a config that does not pass is said so where it is used */ }
                String config = ShownText.elideMiddle(ShownText.oneLine(active.name), 36);
                if (warning != null && warning == R.string.text_no_through_vpn_rules_full_vpn_is_enabled)
                    notices.add(getString(R.string.split_notice_nothing, config, profileLabel(profile)));
                else if (warning != null && warning == R.string.text_applications_and_addresses_are_selected_full_vpn_pro)
                    notices.add(getString(R.string.split_notice_both, config, profileLabel(profile)));
            }
        }
        if (!configs.isEmpty()) {
            try {
                org.tomlj.TomlArray imported = RoutingConfig.validate(activeConfig().toml).getArray("exclusions");
                if (imported != null && imported.size() > 0) {
                    List<String> shown = new ArrayList<>();
                    for (int i = 0; i < imported.size(); i++) shown.add(ShownText.of(imported.getString(i)));
                    notices.add(getString(R.string.imported_routing_rules, TextUtils.join(", ", shown)));
                }
            } catch (Exception ignored) { }
        }
        if (prefs.getBoolean(SplitStore.RULES_MIGRATED, false)) notices.add(getString(R.string.legacy_rules_removed));
        return notices;
    }

    // The packages of these that are installed here, other than this app, as the plan will use them.
    private List<String> installedApps(List<String> packages) {
        PackageManager pm = getPackageManager();
        List<String> installed = new ArrayList<>();
        for (String pkg : packages) {
            if (pkg.equals(getPackageName())) continue;
            try { pm.getApplicationInfo(pkg, 0); installed.add(pkg); }
            catch (PackageManager.NameNotFoundException gone) { /* left out of the plan, kept in the profile */ }
        }
        return installed;
    }

    // The profiles.

    private SplitProfiles splitModel() { return SplitStore.load(splitPrefs, RussiaList.seed(this)); }
    private boolean splitEdit(Consumer<SplitProfiles> change) {
        boolean saved = SplitStore.edit(splitPrefs, RussiaList.seed(this), change);
        if (!saved) toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed));
        return saved;
    }
    private Config activeConfig() { return configs.get(Math.max(0, Math.min(activeIndex, configs.size() - 1))); }
    // The profile the selected config uses (Default for one that names a profile that is gone, and with no configs), which
    // the VPN is built with and the rules of which "Through VPN" is about.
    private String activeProfileName() {
        return configs.isEmpty() ? SplitProfiles.DEFAULT : splitModel().effective(activeConfig().profile);
    }
    // The old global lists, which 1.0.0 reads, are a copy of the lists of the profile the selected config uses.
    private void syncMirror() {
        String profile = activeProfileName();
        splitEdit(m -> m.setMirrored(profile));
    }

    private void selectProfile(String name) {
        // Which one the page shows. It changes nothing the VPN runs on, so nothing restarts.
        final boolean[] changed = {false};
        if (!splitEdit(m -> changed[0] = m.select(name))) return;
        if (changed[0]) render();
    }

    private void confirmRemoveProfile(String name) {
        showConfirm(getString(R.string.split_profile_delete_question, ShownText.elideMiddle(ShownText.oneLine(name), 36)),
                getString(R.string.text_delete), () -> removeProfile(name));
    }

    private void removeProfile(String name) {
        boolean usedByActive = name.equals(activeProfileName());
        final boolean[] removed = {false};
        if (!splitEdit(m -> removed[0] = m.remove(name))) return;
        if (!removed[0]) return;
        // The configs that used it use Default from now on.
        for (Config config : configs) config.profile = SplitProfiles.afterRemoval(config.profile, name);
        saveConfigs();
        if (usedByActive) applyCurrentRules();
        render();
    }

    private void openProfileName() {
        if (profileNameInput == null) return;
        profileNameOpen = true;
        profileNameDraft = "";
        profileNameInput.setText("");
        profileNameInput.setVisibility(View.VISIBLE);
        EditText input = profileNameInput;
        input.requestFocus();
        // A phone types at once; a TV opens its keyboard when OK is pressed on the field.
        if (!isTv()) input.post(() -> showKeyboard(input));
    }

    // Escape, or the focus gone elsewhere, or Back: nothing is added.
    private void closeProfileName() {
        if (!profileNameOpen) return;
        profileNameOpen = false;
        profileNameDraft = "";
        EditText input = profileNameInput;
        if (input == null) return;
        // A remote's focus goes to the + chip it was opened from, and not to wherever Android finds first when a field
        // that has the focus is hidden.
        View plus = host.findViewWithTag("profile:add");
        if (input.hasFocus() && plus != null && !getWindow().getDecorView().isInTouchMode()) plus.requestFocus();
        if (input.hasFocus()) hideKeyboard();
        input.setText("");
        input.setVisibility(View.GONE);
    }

    private void addProfile(String text) {
        final SplitProfiles.NameResult[] result = new SplitProfiles.NameResult[1];
        if (!splitEdit(m -> result[0] = m.add(text))) return;
        profileNameOpen = false;
        profileNameDraft = "";
        hideKeyboard();
        focusAfterRender = "profile:add";
        // Adding a name that differs only in case from one that is there is refused, and says so.
        if (result[0] != null && result[0].status == SplitProfiles.NameStatus.CASE_CLASH)
            toast(getString(R.string.split_profile_exists, ShownText.of(result[0].existing)));
        render();
    }

    // The rules.

    // A change to the profile being edited. If it changed anything, the VPN is built again when the profile is the one the
    // selected config uses (and the split switch is on, or nothing it is built from changed), and the page is drawn again;
    // editing another profile restarts nothing. Whether it changed anything.
    private enum RulesEditResult { FAILED, UNCHANGED, CHANGED }

    private RulesEditResult editRules(Consumer<SplitProfiles> change) {
        return editRules(change, null);
    }

    // Some actions must close their overlay before a changed page is rendered, so focus restoration can find the opener.
    private RulesEditResult editRules(Consumer<SplitProfiles> change, Runnable afterSave) {
        final String[] profile = new String[1];
        final boolean[] changed = {false};
        if (!splitEdit(m -> {
            profile[0] = m.edited();
            String before = m.toJson();
            change.accept(m);
            changed[0] = !before.equals(m.toJson());
        })) return RulesEditResult.FAILED;
        if (afterSave != null) afterSave.run();
        if (changed[0]) rulesChanged(profile[0]);
        return changed[0] ? RulesEditResult.CHANGED : RulesEditResult.UNCHANGED;
    }

    private void rulesChanged(String profile) {
        if (prefs.getBoolean("split_enabled", false) && profile.equals(activeProfileName())) applyCurrentRules();
        render();
    }

    private void addRecommendedRussia() {
        final List<String> list;
        try { list = RussiaList.read(this); }
        catch (IOException unreadable) { toast(getString(R.string.text_could_not_open_file)); return; }
        editRules(m -> m.addRecommended(m.edited(), list));
    }

    // What was typed in the domains field: the words that are rules are added, and the ones that are not are named. The field is
    // cleared if something was added, and keeps what was typed if nothing was, for it to be put right.
    private boolean enterDomains(String text) {
        final SplitProfiles.Entered[] entered = new SplitProfiles.Entered[1];
        final String[] profile = new String[1];
        if (!splitEdit(m -> { profile[0] = m.edited(); entered[0] = m.addDomains(profile[0], text); })) return false;
        if (entered[0] == null) return false;
        if (!entered[0].refused.isEmpty()) toast(refusedRulesMessage(entered[0].refused));
        if (entered[0].added.isEmpty()) return false;
        // The field stays where the cursor is, for the next one.
        focusAfterRender = "domains:input";
        rulesChanged(profile[0]);
        return true;
    }

    // As the desktop says it (Backend refusedRulesMessage): which words, and, for the two reasons that deserve it, why.
    private String refusedRulesMessage(List<String> refused) {
        List<String> shown = new ArrayList<>();
        for (String word : refused) shown.add(ShownText.of(word));
        String list = TextUtils.join(", ", shown);
        switch (SplitProfiles.refusal(refused)) {
            case WILDCARD_ON_ADDRESS: return getString(R.string.split_refused_wildcard, list);
            case EVERY_ADDRESS: return getString(R.string.split_refused_every, list);
            default: return getString(R.string.split_refused, list);
        }
    }
    // The app picker (qml/AppPickerOverlay.qml): a card with ← and the title, a search box, and the installed
    // applications, a row each with the name over its package. One application to a visit: a tap adds it and
    // the card closes, as does Enter, which adds the first match. They are listed on a worker, and the card
    // says so meanwhile. The search box has the focus first, as the desktop's does, and on a phone the keyboard comes up; on a TV Down goes
    // on to the rows. No checkboxes, no icons and no Done, and no file to
    // choose instead, which Android has no such thing as. Keys: picker:back, picker:search, picker:0, ...
    private static final class AppEntry {
        final String label;
        final String pkg;
        AppEntry(String label, String pkg) { this.label = label; this.pkg = pkg; }
    }

    private void appPicker() {
        if (overlay("picker") != null) return;
        new AppPicker().open();
    }

    private final class AppPicker {
        private final List<AppEntry> apps = new ArrayList<>();
        private boolean scanning = true;
        private Overlay overlay;
        private EditText search;
        private LinearLayout list;

        void open() {
            LinearLayout card = column();
            // As tall as the desktop's card, 460, or as the window leaves room for: whatever the list holds (overlay.height, below).
            card.setMinimumHeight(dp(460));
            LinearLayout header = ui.cardHeader(getString(R.string.picker_title), "picker:back", getString(R.string.text_back),
                    v -> closeOverlay(overlay));
            // The desktop's row is 24 high under the card's 14, so its middle is 26 down; the header's own is 24. 4 more above, in the
            // same 48, and the search box under it stays where it is.
            header.setPadding(header.getPaddingLeft(), dp(4), header.getPaddingRight(), 0);
            card.addView(header, lp(-1, -2));
            // Its "Search" stays while the field has the focus (the desktop's does), and starts 10 in, as the desktop's does.
            search = ui.pageInput(getString(R.string.picker_search), "picker:search", true);
            search.setPadding(dp(10), 0, dp(10), 0);
            search.setImeOptions(EditorInfo.IME_ACTION_DONE);
            search.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence text, int start, int count, int after) { }
                @Override public void onTextChanged(CharSequence text, int start, int before, int count) { }
                @Override public void afterTextChanged(Editable text) { fill(); }
            });
            // Enter adds the first match.
            search.setOnEditorActionListener((field, action, event) -> {
                List<AppEntry> matches = matches();
                if (!matches.isEmpty()) choose(matches.get(0));
                return true;
            });
            LinearLayout.LayoutParams searchLp = lp(-1, 34);
            searchLp.setMargins(dp(14), 0, dp(14), dp(10));
            card.addView(search, searchLp);
            list = column();
            list.setPadding(dp(14), 0, dp(14), dp(14));
            ScrollView scroll = new ScrollView(MainActivity.this);
            scroll.setVerticalScrollBarEnabled(false);
            // The rows are its stops, not it: with none, it would be one with no sign of the focus.
            scroll.setFocusable(false);
            scroll.addView(list);
            card.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
            overlay = showOverlay("picker", OverlayLayer.Kind.CARD, card, null, null);
            overlay.width(dp(420)).height(dp(460));
            fill();
            // The search field has the focus first, as the desktop's does, and no row is highlighted; Down goes on to the rows. A phone's
            // keyboard comes up at once, a TV's when OK is pressed on the field.
            overlay.defaultKey = "picker:search";
            if (!isTv()) {
                search.post(() -> {
                    if (overlay.closed) return;
                    search.requestFocus();
                    showKeyboard(search);
                });
            }
            scanWorker.execute(() -> {
                List<AppEntry> found = installedApps();
                runOnUiThread(() -> {
                    if (isDestroyed() || overlay.closed) return;
                    apps.addAll(found);
                    scanning = false;
                    fill();
                });
            });
        }

        // Name or package, as it is typed.
        private List<AppEntry> matches() {
            String query = search.getText().toString().trim().toLowerCase(Locale.ROOT);
            List<AppEntry> matching = new ArrayList<>();
            for (AppEntry app : apps) {
                if (query.isEmpty() || app.label.toLowerCase(Locale.ROOT).contains(query) || app.pkg.toLowerCase(Locale.ROOT).contains(query))
                    matching.add(app);
            }
            return matching;
        }

        private void fill() {
            Theme t = t();
            list.removeAllViews();
            List<AppEntry> matching = matches();
            if (matching.isEmpty()) {
                TextView status = label(getString(scanning ? R.string.picker_scanning : apps.isEmpty() ? R.string.picker_none : R.string.picker_nothing),
                        12, t.textFaint);
                status.setGravity(Gravity.START);
                status.setTag("picker:status");
                list.addView(status, lp(-1, -2));
                return;
            }
            for (int i = 0; i < matching.size(); i++) {
                AppEntry app = matching.get(i);
                LinearLayout row = column();
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(dp(10), 0, dp(10), 0);
                TextView name = label(app.label, 13, t.text);
                name.setSingleLine(true);
                name.setEllipsize(TextUtils.TruncateAt.END);
                // The package, which tells two apps of one name apart, is cut at its left, where the end is what matters.
                TextView pkg = label(app.pkg, 11, t.textFaint);
                pkg.setSingleLine(true);
                pkg.setEllipsize(TextUtils.TruncateAt.START);
                row.addView(name, lp(-1, -2));
                // 1 between the two, as the desktop's Column (spacing: 1) has.
                LinearLayout.LayoutParams pkgLp = lp(-1, -2);
                pkgLp.topMargin = dp(1);
                row.addView(pkg, pkgLp);
                ui.focus(row, "picker:" + i, Ui.Look.ROW).setOnClickListener(v -> choose(app));
                LinearLayout.LayoutParams rowLp = lp(-1, 42);
                rowLp.bottomMargin = dp(2);
                list.addView(row, rowLp);
            }
        }

        // The application goes in the list of the profile being edited, of those the VPN does not carry, or carries alone;
        // and the card goes.
        private void choose(AppEntry app) {
            if (overlay.closed) return;
            editRules(m -> m.addApp(m.edited(), app.pkg), () -> closeOverlay(overlay));
        }
    }

    // The suggestions under the Split page's application field (qml/pages/SplitPage.qml, apSuggest): what is typed names the installed
    // applications that match it, up to 24, in a list above the field, as wide as it, three rows showing and the rest scrolling, each the
    // name over its package; the one highlighted (the first, and then the one the pointer or Up and Down are on) is what Enter or a tap
    // adds to the profile being edited. The list is a passive overlay: it takes neither the focus nor the keyboard from the field, and
    // Back and Escape close it first, as the desktop's first Escape does. Without it, Up and Down leave the field as from any field.
    private final AppSuggestions appSuggestions = new AppSuggestions();

    private final class AppSuggestions {
        private static final int MAX_ROWS = 24;
        private static final long LISTING_MS = 30_000;
        private EditText field;
        // Every installed application, null until they have been listed (on the scan worker, since loadLabel is slow on a TV), and when.
        private List<AppEntry> choices;
        private long listedAt;
        private boolean listing;
        private final List<AppEntry> rows = new ArrayList<>();
        private int highlighted;
        private Overlay open;
        private LinearLayout column;
        private CappedScrollView scroll;
        private ViewTreeObserver watched;
        private final ViewTreeObserver.OnScrollChangedListener follow = () -> { if (open != null && !open.closed) open.layer.requestLayout(); };

        void attach(EditText input) {
            field = input;
            input.addTextChangedListener(new TextWatcher() {
                @Override public void beforeTextChanged(CharSequence text, int start, int count, int after) { }
                @Override public void onTextChanged(CharSequence text, int start, int before, int count) { }
                @Override public void afterTextChanged(Editable text) { if (field == input) refresh(); }
            });
            final View.OnFocusChangeListener before = input.getOnFocusChangeListener();
            input.setOnFocusChangeListener((v, focused) -> {
                if (before != null) before.onFocusChange(v, focused);
                if (field != input) return;
                if (focused) { list(); refresh(); } else close();
            });
            input.setOnEditorActionListener((f, action, event) -> {
                if (event != null && event.getAction() != KeyEvent.ACTION_DOWN) return true;
                if (open != null && !open.closed && highlighted < rows.size()) take(highlighted);
                else if (!input.getText().toString().trim().isEmpty()) toast(getString(choices == null ? R.string.picker_scanning : R.string.picker_nothing));
                return true;
            });
            // Up and Down move the highlight while the list is up; without it they are the arrows of any field.
            input.setOnKeyListener((f, keyCode, event) -> {
                if (open == null || open.closed || (keyCode != KeyEvent.KEYCODE_DPAD_UP && keyCode != KeyEvent.KEYCODE_DPAD_DOWN)) return false;
                if (event.getAction() == KeyEvent.ACTION_DOWN) highlight(highlighted + (keyCode == KeyEvent.KEYCODE_DPAD_DOWN ? 1 : -1), true);
                return true;
            });
        }

        // The installed applications, listed again when what was listed is older than half a minute.
        private void list() {
            if (listing || isDestroyed() || (choices != null && SystemClock.elapsedRealtime() - listedAt < LISTING_MS)) return;
            listing = true;
            scanWorker.execute(() -> {
                List<AppEntry> found = installedApps();
                runOnUiThread(() -> {
                    listing = false;
                    if (isDestroyed()) return;
                    choices = found;
                    listedAt = SystemClock.elapsedRealtime();
                    refresh();
                });
            });
        }

        // What is typed, against the names and the packages, as the picker's search is; the list is put away when there is nothing
        // typed, nothing that matches, or no focus in the field.
        void refresh() {
            String query = field == null ? "" : field.getText().toString().trim().toLowerCase(Locale.ROOT);
            if (field == null || !field.hasFocus() || query.isEmpty() || choices == null) { close(); return; }
            rows.clear();
            for (AppEntry app : choices) {
                if (rows.size() >= MAX_ROWS) break;
                if (app.label.toLowerCase(Locale.ROOT).contains(query) || app.pkg.toLowerCase(Locale.ROOT).contains(query)) rows.add(app);
            }
            if (rows.isEmpty()) { close(); return; }
            highlighted = 0;
            show();
        }

        private void show() {
            if (open == null || open.closed) {
                Theme t = t();
                LinearLayout frame = column();
                frame.setPadding(dp(4), dp(4), dp(4), dp(4));
                scroll = new CappedScrollView(MainActivity.this);
                scroll.setMaxHeight(dp(3 * 42));
                column = column();
                scroll.addView(column, new FrameLayout.LayoutParams(-1, -2));
                frame.addView(scroll, lp(-1, -2));
                open = showOverlay("appsuggest", OverlayLayer.Kind.POPUP, frame, "apps:input", () -> { open = null; unwatch(); }, true);
                open.layer.setAnchor(l -> anchorRect("apps:input", l), OverlayLayer.ABOVE_CONTROL, dp(4));
                open.layer.panel().setBackground(ui.outlined(t.bg, t.border, 8));
                watched = field.getViewTreeObserver();
                watched.addOnScrollChangedListener(follow);
            }
            fill();
            open.layer.requestLayout();
        }

        private void unwatch() {
            if (watched != null && watched.isAlive()) watched.removeOnScrollChangedListener(follow);
            watched = null;
        }

        // The rows: the name, 13, over the package, 11 and cut at its left, as the picker's are, on the card's colour and on surface where
        // they are highlighted, and 42 high.
        private void fill() {
            Theme t = t();
            column.removeAllViews();
            for (int i = 0; i < rows.size(); i++) {
                final int index = i;
                AppEntry app = rows.get(i);
                LinearLayout row = column();
                row.setGravity(Gravity.CENTER_VERTICAL);
                row.setPadding(dp(8), 0, dp(8), 0);
                TextView name = label(app.label, 13, t.text);
                name.setSingleLine(true);
                name.setEllipsize(TextUtils.TruncateAt.END);
                TextView pkg = label(app.pkg, 11, t.textFaint);
                pkg.setSingleLine(true);
                pkg.setEllipsize(TextUtils.TruncateAt.START);
                row.addView(name, lp(-1, -2));
                // 1 between the two, as the desktop's Column (spacing: 1) has.
                LinearLayout.LayoutParams pkgLp = lp(-1, -2);
                pkgLp.topMargin = dp(1);
                row.addView(pkg, pkgLp);
                row.setTag("suggestion:" + i);
                row.setOnClickListener(v -> take(index));
                // Only the highlight moves, as on the desktop: a clickable view is a focus stop from API 26, and a stop here would have no key
                // and no look, and take the focus from the field, which closes the list.
                row.setFocusable(false);
                row.setOnHoverListener((v, event) -> {
                    if (event.getActionMasked() == MotionEvent.ACTION_HOVER_ENTER) highlight(index, false);
                    return false;
                });
                column.addView(row, lp(-1, 42));
            }
            paint();
        }

        private void paint() {
            Theme t = t();
            for (int i = 0; i < column.getChildCount(); i++) column.getChildAt(i).setBackground(ui.shape(i == highlighted ? t.surface : t.bg, 6));
        }

        private void highlight(int index, boolean intoView) {
            if (rows.isEmpty()) return;
            highlighted = Math.max(0, Math.min(rows.size() - 1, index));
            paint();
            if (intoView && scroll != null) {
                int top = dp(42) * highlighted;
                int shown = scroll.getHeight();
                if (top < scroll.getScrollY()) scroll.scrollTo(0, top);
                else if (top + dp(42) > scroll.getScrollY() + shown) scroll.scrollTo(0, top + dp(42) - shown);
            }
        }

        // Adds it to the profile being edited, and clears the field, which stays where the cursor is for the next.
        private void take(int index) {
            if (index < 0 || index >= rows.size()) return;
            AppEntry app = rows.get(index);
            String previousText = field.getText().toString();
            close();
            field.setText("");
            focusAfterRender = "apps:input";
            RulesEditResult result = editRules(m -> m.addApp(m.edited(), app.pkg));
            if (result != RulesEditResult.CHANGED) {
                focusAfterRender = null;
                if (result == RulesEditResult.FAILED) field.setText(previousText);
            }
        }

        void close() {
            Overlay shown = open;
            open = null;
            unwatch();
            if (shown != null) closeOverlay(shown);
        }
    }

    // The apps the launcher shows, and a TV's, named, once each, in alphabetical order. This app's own is left out.
    // On the scan worker: loadLabel() for every one of them takes long on a TV.
    private List<AppEntry> installedApps() {
        PackageManager pm = getPackageManager();
        List<ResolveInfo> found = new ArrayList<>(pm.queryIntentActivities(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER), 0));
        // Apps made for TVs may have a TV launcher entry only. "seen" drops those with both.
        found.addAll(pm.queryIntentActivities(new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LEANBACK_LAUNCHER), 0));
        List<AppEntry> entries = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (ResolveInfo app : found) {
            String pkg = app.activityInfo.packageName;
            if (pkg.equals(getPackageName()) || !seen.add(pkg)) continue;
            entries.add(new AppEntry(app.loadLabel(pm).toString(), pkg));
        }
        entries.sort((a, b) -> String.CASE_INSENSITIVE_ORDER.compare(a.label, b.label));
        return entries;
    }
    private void showKeyboard(View view) {
        InputMethodManager input = getSystemService(InputMethodManager.class);
        if (input != null) input.showSoftInput(view, InputMethodManager.SHOW_IMPLICIT);
    }
    private void hideKeyboard() {
        InputMethodManager input = getSystemService(InputMethodManager.class);
        if (input != null) input.hideSoftInputFromWindow(getWindow().getDecorView().getWindowToken(), 0);
    }

    // The Settings page, as the desktop's is (qml/pages/SettingsPage.qml), top to bottom: General (Language and Theme as Dropdowns,
    // a line between the rows, and Connect on startup), Security (the storage banner and Kill switch), Excluded routes (chips, and a
    // field), Logging, Maintenance (the update line) and the footer. Left out are what only the desktop has: Launch at system startup,
    // Hotkeys and the Windows-only ports row. Nothing is there of Android's own (Always-on VPN is explained in android/README.md).
    // Keys: settings:language, settings:theme, switch:auto_connect, switch:killswitch, routes:restore, routes:clear, route:N, routes:input, switch:logging,
    // settings:update (with update:tick, update:arrow, update:dots and update:version beside it), and in the footer settings:site
    // and settings:core.
    private void settings(LinearLayout body) {
        Theme t = t();
        // General.
        body.addView(ui.sectionLabel(getString(R.string.text_general)), lp(-1, -2));
        // English or Русский, each in itself; the one the app speaks now is the one shown, whatever was chosen.
        final String[] languageNames = {getString(R.string.language_english), getString(R.string.language_russian)};
        final int shownLanguage = AppLocale.RUSSIAN.equals(AppLocale.current(this)) ? 1 : 0;
        body.addView(ui.dropdown(getString(R.string.settings_language), languageNames[shownLanguage], "settings:language",
                v -> showSelect("settings:language", languageNames, shownLanguage, which -> {
                    if (which != shownLanguage) AppLocale.choose(this, prefs, AppLocale.LANGUAGES[which]);
                })), lp(-1, -2));
        body.addView(ui.sep(), lp(-1, 1));
        // System (the default), Light or Dark, as on the desktop.
        String[] modes = {Theme.SYSTEM, Theme.LIGHT_MODE, Theme.DARK_MODE};
        String[] names = {getString(R.string.text_theme_system), getString(R.string.text_light_78), getString(R.string.text_dark_79)};
        int shownMode = 0;
        for (int i = 0; i < modes.length; i++) if (modes[i].equals(themeMode)) shownMode = i;
        final int shownModeIndex = shownMode;
        body.addView(ui.dropdown(getString(R.string.text_theme), names[shownMode], "settings:theme",
                v -> showSelect("settings:theme", names, shownModeIndex, which -> {
                    themeMode = modes[which]; prefs.edit().putString("theme_mode", themeMode).apply(); render();
                })), lp(-1, -2));
        body.addView(ui.sep(), lp(-1, 1));
        addSwitch(body, getString(R.string.settings_connect_on_startup), "", "auto_connect", false);

        // Security.
        section(body, getString(R.string.text_security));
        storageBanner(body);
        addSwitch(body, getString(R.string.settings_kill_switch), getString(R.string.settings_kill_switch_hint), "killswitch", true);

        // Excluded routes.
        excludedRoutes(body);

        // Logging.
        section(body, getString(R.string.settings_logging));
        addSwitch(body, getString(R.string.text_enable_logging), "", "logging", true);

        // Maintenance.
        section(body, getString(R.string.settings_maintenance));
        maintenance(body);

        // The footer, centred: the app and the core it carries.
        gap(body, 14);
        LinearLayout footer = row();
        footer.setGravity(Gravity.CENTER);
        // The links add none to the footer's height, and the words round the dot are the dot's own two spaces on each side, as the
        // desktop's are: the links' own 6 at the sides are taken back.
        TextView site = ui.link("FreeTunnel " + BuildConfig.VERSION_NAME, 12, Ui.LinkKind.FAINT, "settings:site");
        site.setOnClickListener(v -> openLink("https://github.com/" + UpdatePolicy.REPOSITORY));
        LinearLayout.LayoutParams siteLp = ui.linkLp(false);
        siteLp.rightMargin = -dp(Ui.LINK_PAD_H_DP);
        footer.addView(site, siteLp);
        footer.addView(label("  ·  ", 12, t.textFaint), lp(-2, -2));
        TextView core = ui.link(getString(R.string.settings_core) + BuildConfig.CORE_VERSION, 12, Ui.LinkKind.FAINT, "settings:core");
        core.setOnClickListener(v -> openLink("https://github.com/TrustTunnel/TrustTunnelClient"));
        LinearLayout.LayoutParams coreLp = ui.linkLp(false);
        coreLp.leftMargin = -dp(Ui.LINK_PAD_H_DP);
        footer.addView(core, coreLp);
        body.addView(footer, lp(-1, -2));
    }

    // A link out of the app: the browser's, where there is one. Most TVs have none, and say so.
    private void openLink(String url) {
        try { startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url))); }
        catch (ActivityNotFoundException | SecurityException noBrowser) { toast(getString(R.string.text_no_browser)); }
    }

    // Excluded routes (Settings, qml/pages/SettingsPage.qml): subnets that bypass the tunnel at the routing level, whatever the split
    // rules say. The header has the title, which fills, "Restore defaults" (which asks first, as it replaces the list) and, with
    // routes, the danger "Clear all" (which asks as well); then the routes as chips, a ChipX takes one out at once; then a field, for
    // one or several, separated by spaces, commas or semicolons. A /0 is refused, and so is what is no subnet, by name. The
    // list is the saved one, or the defaults where none is saved (ExcludedRoutes), and a change builds the VPN again if it is up.
    // Keys: routes:restore, routes:clear, route:0, route:1, ... and routes:input.
    private List<String> excludedRoutes() {
        return ExcludedRoutes.load(prefs.getString(ExcludedRoutes.KEY, null), ExcludedRoutes.defaults(this));
    }

    private void saveExcludedRoutes(List<String> routes) {
        prefs.edit().putString(ExcludedRoutes.KEY, ExcludedRoutes.toJson(routes)).commit();
        // The plan of a VPN that is up is made again with them; one that is not up is made with them when it starts.
        applyCurrentRules();
        render();
    }

    private void excludedRoutes(LinearLayout body) {
        final List<String> routes = excludedRoutes();
        gap(body, 16);
        LinearLayout header = row();
        TextView title = ui.sectionLabel(getString(R.string.settings_excluded_routes));
        title.setSingleLine(true);
        title.setEllipsize(TextUtils.TruncateAt.END);
        header.addView(title, weighted());
        TextView restore = ui.link(getString(R.string.settings_restore_defaults), 12, Ui.LinkKind.ACCENT, "routes:restore");
        restore.setOnClickListener(v -> showConfirm(getString(R.string.settings_routes_restore_question), getString(R.string.import_replace), () -> {
            List<String> defaults = ExcludedRoutes.defaults(this);
            // Already the defaults: nothing to build again.
            if (!routes.equals(defaults)) saveExcludedRoutes(new ArrayList<>(defaults));
        }));
        // The links add none to the row's height, and the last ends where the page's margin does.
        header.addView(restore, ui.linkLp(routes.isEmpty()));
        if (!routes.isEmpty()) {
            TextView clear = ui.link(getString(R.string.split_clear_all), 12, Ui.LinkKind.DANGER, "routes:clear");
            clear.setOnClickListener(v -> showConfirm(getString(R.string.settings_routes_clear_question), getString(R.string.text_clear),
                    () -> saveExcludedRoutes(new ArrayList<>())));
            // 10 from "Restore defaults", as the desktop's row spaces them: the links' own 6 each side make 12.
            LinearLayout.LayoutParams clearLp = ui.linkLp(true);
            clearLp.leftMargin = -dp(2);
            header.addView(clear, clearLp);
        }
        body.addView(header, lp(-1, -2));
        if (!routes.isEmpty()) {
            gap(body, 6);
            FlowLayout chips = new FlowLayout(this, dp(6));
            for (int i = 0; i < routes.size(); i++) {
                final int index = i;
                final String route = routes.get(i);
                LinearLayout chip = ui.chip(route, false, false, null, false);
                // A route goes at once, without being asked about, as the desktop's does.
                ImageView x = ui.chipX("route:" + i, false);
                x.setContentDescription(getString(R.string.text_delete) + " " + route);
                x.setOnClickListener(v -> saveExcludedRoutes(ExcludedRoutes.removeAt(routes, index)));
                chip.addView(x, ui.chipXLp());
                chips.addView(chip);
            }
            body.addView(chips, lp(-1, -2));
        }
        EditText input = ui.pageInput(getString(R.string.settings_routes_hint), "routes:input");
        input.setOnEditorActionListener((field, action, event) -> {
            if (event != null && event.getAction() != KeyEvent.ACTION_DOWN) return true;
            if (enterExcludedRoutes(input.getText().toString())) input.setText("");
            return true;
        });
        LinearLayout.LayoutParams inputLp = lp(-1, 36);
        inputLp.topMargin = dp(6);
        body.addView(input, inputLp);
    }

    // What was typed in the routes field: the subnets are added and the words that are none are named, a /0 with the reason. The
    // field is cleared if something was added, and keeps what was typed if nothing was, for it to be put right.
    private boolean enterExcludedRoutes(String text) {
        ExcludedRoutes.Entered entered = ExcludedRoutes.add(excludedRoutes(), text);
        if (!entered.refused.isEmpty()) {
            List<String> shown = new ArrayList<>();
            for (String word : entered.refused) shown.add(ShownText.of(word));
            toast(getString(entered.refusedEveryAddress() ? R.string.settings_routes_refused_every : R.string.settings_routes_refused,
                    TextUtils.join(", ", shown)));
        }
        if (entered.added.isEmpty()) return false;
        // The field stays where the cursor is, for the next one.
        focusAfterRender = "routes:input";
        saveExcludedRoutes(entered.routes);
        return true;
    }

    // Maintenance (qml/pages/SettingsPage.qml): one row. At its left the update line, 14 in the text colour, which wraps (to six lines),
    // and is a link over its words alone: it is what to do next, as the icon beside it is, and a click does it. At its right the
    // picture for the state and the version (see UpdateLine, and refreshUpdateControls). Only in an official build, which is the
    // only one Android would let update itself; another says so. There is no switch for checking on launch, as the desktop has none:
    // the app checks by itself when it starts (see onCreate).
    private ImageView updateTick;
    private FrameLayout updateSlot;
    private UpdateArrowView updateArrow;
    private BusyDotsView updateDots;
    private TextView updateVersion;
    // The pointer is on the arrow: it bends (a download) or leans (a retry), as the desktop's does under the pointer. Not on the words,
    // which only take their colour and an underline there.
    private boolean updateHot;

    private void maintenance(LinearLayout body) {
        Theme t = t();
        updateAction = null; updateTick = null; updateSlot = null; updateArrow = null; updateDots = null; updateVersion = null;
        updateHot = false;
        if (!AndroidUpdater.INSTANCE.official(this)) {
            // Android would refuse an update signed with another key than this build's.
            body.addView(ui.paragraph(getString(R.string.update_unofficial), 12, t.textFaint, 0), lp(-1, -2));
            return;
        }
        RelativeLayout line = new RelativeLayout(this);
        line.setMinimumHeight(dp(42));
        // The right: ✓ when it is current, the arrow or the dots, and the version.
        LinearLayout right = row();
        right.setId(View.generateViewId());
        updateTick = ui.glyph(R.drawable.ic_check, t.success);
        updateTick.setTag("update:tick");
        updateTick.setContentDescription(getString(R.string.update_latest));
        updateTick.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_AUTO);
        right.addView(updateTick, lp(-2, -2));
        updateSlot = new FrameLayout(this);
        // The arrow does what the words do, as the desktop's icon does, and bends under the pointer: it is not a stop of its own for a
        // remote, which has the words.
        updateSlot.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        updateSlot.setOnClickListener(v -> { if (updateLine().clickable()) doUpdateAction(); });
        // A clickable view is a focus stop from API 26 unless it says it is not: this one has no key and no look to show a focus with.
        updateSlot.setFocusable(false);
        updateSlot.setOnHoverListener((v, event) -> {
            int action = event.getActionMasked();
            if (action == MotionEvent.ACTION_HOVER_ENTER) setUpdateHot(updateLine().clickable());
            else if (action == MotionEvent.ACTION_HOVER_EXIT) setUpdateHot(false);
            return false;
        });
        // The pictures are the line's, which says what they say; TalkBack has nothing to add.
        updateArrow = new UpdateArrowView(this, t.text);
        updateArrow.setTag("update:arrow");
        updateArrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        updateSlot.addView(updateArrow, new FrameLayout.LayoutParams(dp(UpdateArrowView.SIZE_DP), dp(UpdateArrowView.SIZE_DP)));
        updateDots = new BusyDotsView(this, t.text);
        updateDots.setTag("update:dots");
        updateDots.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        updateSlot.addView(updateDots, new FrameLayout.LayoutParams(dp(UpdateArrowView.SIZE_DP), dp(UpdateArrowView.SIZE_DP)));
        LinearLayout.LayoutParams slotLp = lp(UpdateArrowView.SIZE_DP, UpdateArrowView.SIZE_DP);
        slotLp.leftMargin = dp(5);
        right.addView(updateSlot, slotLp);
        updateVersion = label(BuildConfig.VERSION_NAME, 13, t.textFaint);
        updateVersion.setTag("update:version");
        LinearLayout.LayoutParams versionLp = lp(-2, -2);
        versionLp.leftMargin = dp(5);
        right.addView(updateVersion, versionLp);
        RelativeLayout.LayoutParams rightLp = new RelativeLayout.LayoutParams(-2, -2);
        rightLp.addRule(RelativeLayout.ALIGN_PARENT_RIGHT);
        rightLp.addRule(RelativeLayout.CENTER_VERTICAL);
        line.addView(right, rightLp);
        // The words, over the room the right leaves, and no wider than they are.
        updateAction = ui.link("", 14, Ui.LinkKind.TEXT, "settings:update");
        updateAction.setSingleLine(false);
        updateAction.setMaxLines(6);
        updateAction.setEllipsize(TextUtils.TruncateAt.END);
        updateAction.setOnClickListener(v -> doUpdateAction());
        // A RelativeLayout makes a child with a rule for each of its sides exactly as wide as the room between them, whatever its width
        // says: the link would be the line's whole width, and a tap or the remote's focus across empty space. So the link sits in
        // a row that takes that room, and is as wide as its words in it.
        LinearLayout room = row();
        room.addView(updateAction, lp(-2, -2));
        RelativeLayout.LayoutParams actionLp = new RelativeLayout.LayoutParams(-2, -2);
        actionLp.addRule(RelativeLayout.ALIGN_PARENT_LEFT);
        actionLp.addRule(RelativeLayout.LEFT_OF, right.getId());
        actionLp.addRule(RelativeLayout.CENTER_VERTICAL);
        // The words start where the other rows' do, with the link's own padding before them.
        actionLp.leftMargin = -dp(6);
        actionLp.rightMargin = dp(6);
        line.addView(room, actionLp);
        body.addView(line, lp(-1, -2));
        refreshUpdateControls();
    }

    private void setUpdateHot(boolean hot) {
        if (updateHot == hot) return;
        updateHot = hot;
        applyUpdateArrow(true);
    }

    // What a click or OK on the update line does now (UpdateLine): check, download, or install. A download cannot be stopped.
    private UpdateLine updateLine() {
        AndroidUpdater.State update = AndroidUpdater.INSTANCE.getState();
        // Once Android has the session, Install update starts over if its dialog was lost.
        boolean reinstall = update == AndroidUpdater.State.INSTALLING && AndroidUpdater.INSTANCE.getCommitted();
        return UpdateLine.of(UpdateLine.Phase.valueOf(update.name()), reinstall, AndroidUpdater.INSTANCE.getFailedCheck());
    }

    private void doUpdateAction() {
        switch (updateLine().action) {
            case CHECK: AndroidUpdater.INSTANCE.check(this); break;
            case DOWNLOAD: AndroidUpdater.INSTANCE.download(this); break;
            case INSTALL: installUpdate(); break;
            default: break;
        }
    }
    // Installing replaces the app, and Android stops the VPN with it, so say so
    // first. Without the install permission, install() opens its settings screen.
    private void installUpdate() {
        vpnState = FreeTunnelVpnService.state(this);
        if (getPackageManager().canRequestPackageInstalls() && isVpnActive()) {
            showConfirm(getString(R.string.update_disconnect_warning), getString(R.string.update_install),
                    () -> AndroidUpdater.INSTANCE.install(this, id -> toast(getString(id))));
        } else {
            AndroidUpdater.INSTANCE.install(this, id -> toast(getString(id)));
        }
    }
    private void refreshUpdateControls() {
        if (updateAction == null) return;
        UpdateLine line = updateLine();
        UpdatePolicy.Release release = AndroidUpdater.INSTANCE.getRelease();
        String version = release != null ? release.getVersion() : "";
        String text;
        switch (line.words) {
            case CHECKING: text = getString(R.string.update_checking); break;
            case AVAILABLE: text = getString(R.string.update_available, version); break;
            case DOWNLOADING: text = getString(R.string.update_downloading, AndroidUpdater.INSTANCE.getProgress()); break;
            case READY: text = getString(R.string.update_ready, version); break;
            // Until Android has the session there is nothing to start again, and the line does nothing: it does not say it will.
            case INSTALLING: text = getString(line.clickable() ? R.string.update_installing_again : R.string.update_installing); break;
            case CHECK_FAILED:
                text = getString(R.string.update_check_failed, getString(updateFailureText(AndroidUpdater.INSTANCE.getFailure(), true)));
                break;
            case FAILED: text = getString(updateFailureText(AndroidUpdater.INSTANCE.getFailure(), false)); break;
            default: text = getString(R.string.update_check);
        }
        updateAction.setText(text);
        // Said aloud: the words, and then what a click does with them.
        int does = line.action == UpdateLine.Action.CHECK ? R.string.update_check : line.action == UpdateLine.Action.DOWNLOAD ? R.string.update_download
                : line.action == UpdateLine.Action.INSTALL ? R.string.update_install : 0;
        updateAction.setContentDescription(does == 0 || getString(does).equals(text) ? text : text + ". " + getString(does));
        // Not setEnabled: disabling the view that has a remote's focus moves the focus to
        // the top of the window. Not clickable, it ignores taps and OK just the same.
        updateAction.setClickable(line.clickable());
        // Which has no hover look on the desktop while it does nothing: plain, in the text colour, and not underlined.
        ((Ui.LinkText) updateAction).setDormant(!line.clickable());
        updateTick.setVisibility(line.tick ? View.VISIBLE : View.GONE);
        updateVersion.setVisibility(line.version ? View.VISIBLE : View.GONE);
        applyUpdateArrow(false);
    }

    // The picture beside the line: dots while it waits on an answer, ↓ for an offered download (it bends into ↻ under the pointer), a
    // turning ↻ while one comes down, and ↻ after a failure (which leans, with the pointer on it).
    private void applyUpdateArrow(boolean animate) {
        if (updateArrow == null || updateDots == null || updateSlot == null) return;
        UpdateLine.Icon icon = updateLine().icon;
        boolean arrow = icon == UpdateLine.Icon.DOWNLOAD || icon == UpdateLine.Icon.SPINNER || icon == UpdateLine.Icon.RETRY;
        boolean dots = icon == UpdateLine.Icon.DOTS;
        updateArrow.setVisibility(arrow ? View.VISIBLE : View.GONE);
        updateDots.setVisibility(dots ? View.VISIBLE : View.GONE);
        updateSlot.setVisibility(arrow || dots ? View.VISIBLE : View.GONE);
        updateArrow.setSpinning(icon == UpdateLine.Icon.SPINNER);
        updateArrow.setBend(icon == UpdateLine.Icon.DOWNLOAD && !updateHot ? 0 : 1, animate);
        updateArrow.setTurn(icon == UpdateLine.Icon.RETRY && updateHot ? 45 : 0, animate);
    }
    // Why an update failed. Alone it is a sentence; inside "Update check failed: %1" it is the end of one, and the two that begin with a
    // word of their own (not a name) have a lower-case form for it.
    private static int updateFailureText(UpdatePolicy.Failure failure, boolean inline) {
        if (failure == UpdatePolicy.Failure.RATE_LIMITED) return R.string.update_error_rate_limited;
        if (failure == UpdatePolicy.Failure.VERIFICATION) return inline ? R.string.update_error_verification_inline : R.string.update_error_verification;
        if (failure == UpdatePolicy.Failure.INSTALLER) return R.string.update_error_installer;
        return inline ? R.string.update_error_network_inline : R.string.update_error_network;
    }
    // A row of a page: a label, with its subtitle under it, and the desktop's Toggle. Only the
    // toggle is the control.
    private void addSwitch(LinearLayout parent, String title, String subtitle, String key, boolean initial) {
        ToggleView sw = ui.toggle(prefs.getBoolean(key, initial), false, "switch:" + key);
        sw.setContentDescription(title);
        sw.setListener((v, enabled) -> {
            prefs.edit().putBoolean(key, enabled).apply();
            if (key.equals("killswitch")) applyCurrentRules();
        });
        parent.addView(ui.toggleRow(title, subtitle, sw), ui.focusRowLp());
    }
    // The Logs page (qml/pages/LogsPage.qml): at the top Clear (left) and Copy (right), links as wide as their words; with logging off,
    // "Logging is off." in warn and a "Turn on" link; the log in a box (surface, radius 8, monospace 11 in the text colour) that fills the
    // rest of the page, or, with logging on and nothing to show, a placeholder in the middle of it; and at the foot Auto-scroll with a small
    // toggle, which keeps the end of the log in view. The log is the file the VPN service writes (LogFile), each line shown as
    // "HH:mm:ss LEVEL message" (LogLines). While the page is shown the box follows the file once a second, by changing its text alone and
    // never by building the page again. On a TV the text is not selectable, so that the arrows scroll the box (a focus stop, which shows the
    // field's accent border) instead of moving a cursor in the text. There is no log path to open: the file is the app's own.
    // Keys: log:clear, log:copy, logs:enable, log:box, log:text and logs:autoscroll.
    private TextView logText;
    private ScrollView logBox;
    private TextView logPlaceholder;
    // On while the page is shown for the first time, as the desktop's is, and then as it is left.
    private boolean logAutoScroll = true;
    // The file as the page last showed it, to tell by a glance whether it changed; -1 until it has shown it.
    private long logStamp = -1;

    private File logFile() { return new File(getFilesDir(), LogFile.NAME); }

    private void logs(LinearLayout body) {
        Theme t = t();
        LinearLayout actions = row();
        actions.setMinimumHeight(dp(24));
        TextView clear = ui.link(getString(R.string.text_clear), 13, Ui.LinkKind.ACCENT, "log:clear");
        LinearLayout.LayoutParams clearLp = lp(-2, -2);
        clearLp.leftMargin = -dp(6);
        actions.addView(clear, clearLp);
        actions.addView(new View(this), new LinearLayout.LayoutParams(0, 1, 1));
        TextView copy = ui.link(getString(R.string.text_copy), 13, Ui.LinkKind.ACCENT, "log:copy");
        LinearLayout.LayoutParams copyLp = lp(-2, -2);
        copyLp.rightMargin = -dp(6);
        actions.addView(copy, copyLp);
        body.addView(actions, lp(-1, -2));
        clear.setOnClickListener(v -> {
            LogFile.clear(logFile());
            logStamp = LogFile.stamp(logFile());
            // At once, and the text alone: nothing else on the page changes.
            setLogText("");
        });
        copy.setOnClickListener(v -> copyLog());
        // The control just above the box, which Up from the box's top goes to: Copy, or "Turn on" where the page says logging is off.
        // Left to the focus search, it would take the nav tile that is straight above the middle of the box, over the links at its
        // corners (it weighs a sideways step more than a step up).
        View above = copy;
        // Logging off: nothing new will be written, so the page says so instead of showing old lines as if they were current.
        if (!prefs.getBoolean("logging", true)) {
            gap(body, 8);
            LinearLayout off = row();
            TextView message = label(getString(R.string.logs_off), 12, t.warn);
            message.setGravity(Gravity.START | Gravity.CENTER_VERTICAL);
            off.addView(message, weighted());
            TextView turnOn = ui.link(getString(R.string.logs_turn_on), 12, Ui.LinkKind.ACCENT, "logs:enable");
            turnOn.setOnClickListener(v -> { prefs.edit().putBoolean("logging", true).apply(); render(); });
            off.addView(turnOn, ui.linkLp(true));
            body.addView(off, lp(-1, -2));
            above = turnOn;
        }
        gap(body, 8);
        // The box is the scroll view: surface, radius 8, and the field's accent border with the focus. Its text is clipped
        // to 12 inside it.
        FrameLayout frame = new FrameLayout(this);
        logBox = new ScrollView(this);
        logBox.setVerticalScrollBarEnabled(false);
        logBox.setPadding(dp(12), dp(12), dp(12), dp(12));
        ui.focus(logBox, "log:box", Ui.Look.BOX);
        if (above.getId() == View.NO_ID) above.setId(View.generateViewId());
        logBox.setNextFocusUpId(above.getId());
        // A remote scrolls it with the arrows, and leaves it with Up at the top and Down at the end.
        Ui.leaveAtTheEnds(logBox);
        logText = ui.mono("", 11, t.text);
        logText.setTag("log:text");
        logText.setGravity(Gravity.TOP | Gravity.START);
        // On a TV, selectable text would take the remote's arrows as cursor moves: without it Down from Clear or Copy lands on the box,
        // and the arrows scroll it. Selected, the text is in the accent (see the desktop's selectionColor), light enough to read through.
        logText.setTextIsSelectable(!isTv());
        logText.setHighlightColor((t.accent & 0x00FFFFFF) | 0x66000000);
        logBox.addView(logText);
        frame.addView(logBox, new FrameLayout.LayoutParams(-1, -1));
        logPlaceholder = label(getString(R.string.logs_placeholder), 13, t.textFaint);
        logPlaceholder.setGravity(Gravity.CENTER);
        frame.addView(logPlaceholder, new FrameLayout.LayoutParams(-2, -2, Gravity.CENTER));
        body.addView(frame, new LinearLayout.LayoutParams(-1, 0, 1));
        gap(body, 8);
        // The foot: Auto-scroll, at the right. The label and the toggle are one small row, which fills round them with the focus and
        // is no wider than they are.
        LinearLayout foot = row();
        foot.addView(new View(this), new LinearLayout.LayoutParams(0, 1, 1));
        ToggleView auto = ui.toggle(logAutoScroll, true, "logs:autoscroll");
        auto.setContentDescription(getString(R.string.logs_auto_scroll));
        auto.setListener((v, on) -> { logAutoScroll = on; if (on) scrollLogToEnd(); });
        LinearLayout.LayoutParams autoLp = lp(-2, -2);
        // The track ends at the margin: the row's 6 of padding on its right, which the fill shows, go beyond it. The row is 28 high to fill
        // round the toggle, and the desktop's is 20: the 4 above and below that hang outside the foot.
        autoLp.rightMargin = -dp(Ui.FOCUS_ROW_RIGHT_DP);
        autoLp.topMargin = -dp(4);
        autoLp.bottomMargin = -dp(4);
        foot.addView(ui.toggleGroup(getString(R.string.logs_auto_scroll), 12, t.textDim, auto), autoLp);
        body.addView(foot, lp(-1, -2));
        refreshLog(true);
    }

    // The log as the page shows it, or null if it cannot be read.
    private String shownLog() {
        try { return LogLines.shownText(LogFile.read(logFile()), LogLines.SHOWN_LINES); }
        catch (IOException | RuntimeException unreadable) { return null; }
    }

    // Shows the file again if it changed since the page last showed it (or whether it did, with force). Not while the user holds
    // a selection in it: the new text would wipe it.
    private void refreshLog(boolean force) {
        if (logText == null) return;
        long stamp = LogFile.stamp(logFile());
        if (!force && stamp == logStamp) return;
        if (!force && logText.hasSelection()) return;
        String shown = shownLog();
        logStamp = stamp;
        setLogText(shown == null ? getString(R.string.text_could_not_read_log) : shown);
    }

    private void setLogText(String text) {
        if (logText == null) return;
        if (!text.contentEquals(logText.getText())) logText.setText(text);
        logPlaceholder.setVisibility(text.isEmpty() && prefs.getBoolean("logging", true) ? View.VISIBLE : View.GONE);
        if (logAutoScroll) scrollLogToEnd();
    }

    // The end of the log, once the text is laid out. Not ScrollView.fullScroll, which moves the focus too.
    private void scrollLogToEnd() {
        final ScrollView box = logBox;
        if (box == null) return;
        box.post(() -> {
            View child = box.getChildAt(0);
            if (child == null) return;
            int room = box.getHeight() - box.getPaddingTop() - box.getPaddingBottom();
            box.scrollTo(0, Math.max(0, child.getHeight() - room));
        });
    }

    private void copyLog() {
        String shown = shownLog();
        if (shown == null) { toast(getString(R.string.text_could_not_read_log)); return; }
        // An empty log is copied too, and said to be, as the desktop's Copy does (LogsPage.qml: copyToClipboard(logText()), "Log copied").
        ClipData clip = ClipData.newPlainText("FreeTunnel log", shown);
        // It lists where the connections went: a keyboard's clipboard history and the preview Android shows are asked to leave it out.
        if (Build.VERSION.SDK_INT >= 33) {
            android.os.PersistableBundle extras = new android.os.PersistableBundle();
            extras.putBoolean(android.content.ClipDescription.EXTRA_IS_SENSITIVE, true);
            clip.getDescription().setExtras(extras);
        }
        ((ClipboardManager) getSystemService(CLIPBOARD_SERVICE)).setPrimaryClip(clip);
        toast(getString(R.string.text_log_copied));
    }

    // Once a second while the page is shown: the box follows the file. The text alone changes, never the page.
    private final Runnable logTicker = new Runnable() {
        @Override public void run() {
            if (page != 4 || !resumed) return;
            refreshLog(false);
            host.postDelayed(this, 1000);
        }
    };
    private void scheduleLogTick() {
        host.removeCallbacks(logTicker);
        if (page == 4 && resumed) host.postDelayed(logTicker, 1000);
    }
    @Override protected void onDestroy() {
        toastHandler.removeCallbacksAndMessages(null);
        stopReceiver();
        dropReceived();
        validationRequest++;
        validationWorker.shutdown();
        scanWorker.shutdown();
        pingWorker.shutdownNow();
        ioWorker.shutdown();
        hero.release();
        super.onDestroy();
    }
    private void loadConfigs() {
        try {
            JSONArray array = new JSONArray(ConfigStore.read(prefs));
            for (int i = 0; i < array.length(); i++) {
                JSONObject o = array.getJSONObject(i);
                configs.add(new Config(o.getString("name"), o.getString("toml"), SavedProfile.profileOf(o)));
            }
        } catch (Exception error) {
            storageFailed = true;
            configs.clear();
            rootStorageWarning();
        }
        activeIndex = Math.max(0, Math.min(activeIndex, configs.size() - 1));
    }
    private void rootStorageWarning() {
        storageProblem = getString(R.string.text_configurations_are_unavailable) + ". "
                + getString(R.string.text_android_could_not_read_storage_saved_data_is_unchang);
        toast(storageProblem);
    }
    // The desktop's credential-storage banner (SettingsPage): amber, at the top of a page while storage fails.
    private void storageBanner(LinearLayout body) {
        if (!storageFailed || storageProblem == null) return;
        TextView banner = ui.banner(storageProblem);
        banner.setTag("storage:banner");
        body.addView(banner, lp(-1, -2));
        gap(body, 10);
    }
    // True when they were saved. When they could not be, nothing is changed, and the user is told.
    private boolean saveConfigs() {
        validationRequest++;
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed)); return false; }
        JSONArray array = new JSONArray();
        for (Config config : configs) {
            JSONObject o = new JSONObject();
            try {
                o.put("name", config.name); o.put("toml", config.toml);
                // Only a profile other than Default is written: a config saved before there were profiles has none, and
                // is Default, and so is one that names no profile.
                if (!SplitProfiles.DEFAULT.equals(config.profile)) o.put("profile", config.profile);
                array.put(o);
            } catch (Exception ignored) { }
        }
        if (!ConfigStore.write(prefs, array.toString())) {
            storageFailed = true;
            storageProblem = getString(R.string.text_could_not_save_configurations_data_on_disk_is_unchan);
            toast(storageProblem);
            configs.clear(); loadConfigs(); render(); return false;
        }
        prefs.edit().putInt("active", activeIndex).apply();
        syncMirror();
        return true;
    }
}
