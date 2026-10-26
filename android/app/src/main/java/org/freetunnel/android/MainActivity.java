package org.freetunnel.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.BroadcastReceiver;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.database.Cursor;
import android.provider.OpenableColumns;
import android.provider.Settings;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.net.VpnService;
import android.os.Bundle;
import android.os.Build;
import android.os.SystemClock;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import androidx.core.content.ContextCompat;
import android.text.method.PasswordTransformationMethod;
import android.view.WindowManager;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Android adaptation of the FreeTunnel desktop interface. */
public final class MainActivity extends Activity {
    private static final int PICK_CONFIG = 21;
    private static final int PREPARE_VPN = 22;
    private static final String PREFS = "freetunnel";
    private String[] tabNames;
    private final int[] tabIcons = {R.drawable.logo, R.drawable.ic_servers, R.drawable.ic_network,
            R.drawable.ic_settings, R.drawable.ic_log};
    private SharedPreferences prefs;
    private final List<Config> configs = new ArrayList<>();
    private final Map<Config, String> pingResults = new IdentityHashMap<>();
    private boolean pingRunning;
    private int activeIndex;
    private int page;
    private boolean dark = true;
    private LinearLayout root;
    private int vpnState;
    private String pendingConfig;
    private boolean receiverRegistered;
    private boolean storageFailed;
    private TextView updateStatus;
    private TextView updateAction;
    private long validationRequest;
    private final java.util.concurrent.ExecutorService validationWorker = java.util.concurrent.Executors.newSingleThreadExecutor();
    private final BroadcastReceiver vpnReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            vpnState = FreeTunnelVpnService.state(context);
            if (page == 0) render();
            QuickTileService.refresh(context);
        }
    };

    private static final class Config {
        String name;
        String toml;
        Config(String name, String toml) { this.name = name; this.toml = toml; }
    }

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        tabNames = new String[] {getString(R.string.text_home), getString(R.string.text_configurations), getString(R.string.text_routing), getString(R.string.text_settings), getString(R.string.text_log)};
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        dark = prefs.getBoolean("dark", true);
        activeIndex = prefs.getInt("active", 0);
        vpnState = FreeTunnelVpnService.state(this);
        loadConfigs();
        render();
        if (state == null && prefs.getBoolean("check_updates_on_launch", false)) AndroidUpdater.INSTANCE.check();
        if (state == null) handleIncomingIntent(getIntent());
        if (state == null && prefs.getBoolean("auto_connect", false) && vpnState == 0
                && !configs.isEmpty() && !Intent.ACTION_VIEW.equals(getIntent().getAction())) {
            root.post(() -> { if (FreeTunnelVpnService.state(this) == 0) connect(); });
        }
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) requestPermissions(new String[]{android.Manifest.permission.POST_NOTIFICATIONS}, 23);

    }

    @Override protected void onResume() {
        super.onResume();
        if (!receiverRegistered) {
            IntentFilter filter = new IntentFilter(FreeTunnelVpnService.ACTION_STATE);
            ContextCompat.registerReceiver(this, vpnReceiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED);
            receiverRegistered = true;
        }
        AndroidUpdater.INSTANCE.observe(() -> { if (page == 3) refreshUpdateControls(); });
        vpnState = FreeTunnelVpnService.state(this);
        if (page == 0 || page == 3) render();
    }

    @Override protected void onPause() {
        AndroidUpdater.INSTANCE.observe(null);
        if (receiverRegistered) {
            unregisterReceiver(vpnReceiver);
            receiverRegistered = false;
        }
        super.onPause();
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
        if (uri != null) importDeepLink(uri.toString());
    }

    private int dp(float n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    private int bg() { return Color.parseColor(dark ? "#181818" : "#ececec"); }
    private int surface() { return Color.parseColor(dark ? "#262626" : "#e2e2e2"); }
    private int tile() { return Color.parseColor(dark ? "#202020" : "#d8d8d8"); }
    private int textColor() { return Color.parseColor(dark ? "#eaeaea" : "#1b1b1b"); }
    private int dim() { return Color.parseColor(dark ? "#9a9a9a" : "#6b6b6b"); }
    private int faint() { return Color.parseColor(dark ? "#6a6a6a" : "#9a9a9a"); }
    private int success() { return Color.parseColor(dark ? "#3fbf93" : "#1d9e75"); }
    private int input() { return Color.parseColor(dark ? "#101010" : "#d6d6d6"); }
    private int accent() { return Color.parseColor(dark ? "#b0b0b0" : "#4f4f4f"); }
    private GradientDrawable shape(int color, int radius) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radius));
        return d;
    }
    private LinearLayout column() { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.VERTICAL); return l; }
    private LinearLayout row() { LinearLayout l = new LinearLayout(this); l.setOrientation(LinearLayout.HORIZONTAL); l.setGravity(Gravity.CENTER_VERTICAL); return l; }
    private LinearLayout.LayoutParams lp(int w, int h) { return new LinearLayout.LayoutParams(w < 0 ? w : dp(w), h < 0 ? h : dp(h)); }
    private TextView label(String value, int size, int color, boolean bold) {
        TextView t = new TextView(this);
        t.setText(value);
        t.setTextSize(size);
        t.setTextColor(color);
        t.setGravity(Gravity.CENTER_VERTICAL);
        if (bold) t.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        return t;
    }
    private void gap(LinearLayout p, int size) { p.addView(new View(this), lp(1, size)); }
    private LinearLayout card() {
        LinearLayout c = column(); c.setPadding(dp(16), dp(14), dp(16), dp(14));
        c.setBackground(shape(surface(), 14)); return c;
    }
    private void section(LinearLayout p, String title) {
        gap(p, 18); TextView t = label(title.toUpperCase(java.util.Locale.getDefault()), 11, faint(), true);
        p.addView(t, lp(-1, 28));
    }
    private void toast(String text) { Toast.makeText(this, text, Toast.LENGTH_LONG).show(); }
    private Button button(String caption, boolean filled) {
        Button b = new Button(this);
        b.setAllCaps(false); b.setText(caption); b.setTextSize(14);
        b.setTextColor(filled ? (dark ? bg() : Color.WHITE) : textColor());
        b.setBackground(shape(filled ? accent() : tile(), 10));
        b.setPadding(dp(12), 0, dp(12), 0);
        return b;
    }

    private void render() {
        getWindow().setStatusBarColor(bg()); getWindow().setNavigationBarColor(bg());
        getWindow().getDecorView().setSystemUiVisibility(dark ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        root = column(); root.setBackgroundColor(bg());
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                root.setPadding(0, bars.top, 0, bars.bottom);
            } else {
                root.setPadding(0, insets.getSystemWindowInsetTop(), 0, insets.getSystemWindowInsetBottom());
            }
            return insets;
        });
        setContentView(root);
        nav();
        if (page == 0) { home(); return; }
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setClipToPadding(false);
        scroll.setVerticalScrollBarEnabled(false);
        LinearLayout body = column(); body.setPadding(dp(18), dp(8), dp(18), dp(26)); scroll.addView(body);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        switch (page) {
            case 1: configs(body); break;
            case 2: split(body); break;
            case 3: settings(body); break;
            default: logs(body); break;
        }
    }

    private void nav() {
        LinearLayout bar = row(); bar.setGravity(Gravity.CENTER);
        bar.setPadding(0, dp(9), 0, dp(4));
        for (int i = 0; i < tabNames.length; i++) {
            final int next = i;
            FrameLayout item = new FrameLayout(this);
            if (i == page) item.setBackground(shape(dark ? Color.parseColor("#343434") : Color.parseColor("#d7d7d7"), 8));
            ImageView icon = new ImageView(this); icon.setImageResource(tabIcons[i]);
            icon.setAlpha(i == page ? 1f : 0.78f);
            if (i != 0) icon.setColorFilter(i == page ? accent() : dim());
            FrameLayout.LayoutParams iconLp = new FrameLayout.LayoutParams(dp(22), dp(22), Gravity.CENTER);
            item.addView(icon, iconLp);
            LinearLayout.LayoutParams itemLp = lp(46, 38);
            if (i > 0) itemLp.leftMargin = dp(8);
            bar.addView(item, itemLp);
            item.setContentDescription(tabNames[i]);
            item.setOnClickListener(v -> { page = next; render(); });
        }
        root.addView(bar, lp(-1, 56));
    }

    private void home() {
        FrameLayout stage = new FrameLayout(this);
        root.addView(stage, new LinearLayout.LayoutParams(-1, 0, 1));
        LinearLayout hero = column(); hero.setGravity(Gravity.CENTER_HORIZONTAL);
        ImageView logo = new ImageView(this); logo.setImageResource(R.drawable.logo); logo.setAlpha(0.5f);
        hero.addView(logo, lp(132, 132));
        logo.setContentDescription(isVpnActive() ? getString(R.string.text_disconnect_freetunnel) : getString(R.string.text_connect_freetunnel));
        logo.setOnClickListener(v -> connect());
        gap(hero, 22);
        String name = configs.isEmpty() ? getString(R.string.text_add_configuration) : configs.get(Math.min(activeIndex, configs.size() - 1)).name + "  ▾";
        TextView selected = label(name, 15, textColor(), true); selected.setGravity(Gravity.CENTER);
        hero.addView(selected, lp(-1, 28));
        selected.setOnClickListener(v -> {
            page = 1; render(); if (configs.isEmpty()) addMenu();
        });
        TextView status = label(connectionCaption(), 13, vpnState == 2 ? success() : dim(), false);
        status.setGravity(Gravity.CENTER);
        hero.addView(status, lp(-1, 30));
        Button toggle = button(isVpnActive() ? getString(R.string.text_disconnect) : getString(R.string.text_connect), isVpnActive());
        toggle.setContentDescription(isVpnActive() ? getString(R.string.text_disconnect_vpn) : getString(R.string.text_connect_vpn));
        toggle.setOnClickListener(v -> connect());
        hero.addView(toggle, lp(178, 44));
        FrameLayout.LayoutParams heroLp = new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER);
        heroLp.bottomMargin = dp(34);
        stage.addView(hero, heroLp);
    }
    private void gapHorizontal(LinearLayout p, int size) { p.addView(new View(this), lp(size, 1)); }
    private void connect() {
        vpnState = FreeTunnelVpnService.state(this);
        if (isVpnActive()) {
            validationRequest++;
            FreeTunnelVpnService.stop(this);
            return;
        }
        if (configs.isEmpty()) { page = 1; render(); toast(getString(R.string.text_add_a_configuration_first)); return; }
        Config config = configs.get(Math.max(0, Math.min(activeIndex, configs.size() - 1)));
        validateAsync(config.toml, () -> {
            pendingConfig = config.toml;
            Intent prepare = VpnService.prepare(this);
            if (prepare != null) startActivityForResult(prepare, PREPARE_VPN);
            else startVpnWithPendingConfig();
        }, () -> {});
    }

    private boolean isVpnActive() {
        return vpnState == 1 || vpnState == 2 || vpnState == 3 || vpnState == 4 || vpnState == 5 || vpnState == 6;
    }

    private String connectionCaption() {
        switch (vpnState) {
            case 1: return getString(R.string.text_connecting);
            case 2: return getString(R.string.text_connected);
            case 6: return getString(R.string.vpn_traffic_blocked);
            case 3: case 4: case 5: return getString(R.string.text_recovering_connection);
            default: return getString(R.string.text_disconnected);
        }
    }

    private void startVpnWithPendingConfig() {
        if (pendingConfig == null) return;
        FreeTunnelVpnService.start(this, pendingConfig);
        pendingConfig = null;
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
        Config config = configs.get(Math.max(0, Math.min(activeIndex, configs.size() - 1)));
        validateAsync(config.toml, () -> {
            if (FreeTunnelVpnService.state(this) == 0) return;
            FreeTunnelVpnService.start(this, config.toml);
            toast(getString(R.string.text_applying_routing_settings));
        }, () -> {});
    }

    // A config that passed: the text to save, and whether a subscription was left out of it.
    private interface Checked { void accept(String toml, boolean subscriptionNotKept); }

    private void validateAsync(String source, Runnable accepted, Runnable rejected) {
        validateAsync(source, false, (toml, dropped) -> accepted.run(), rejected);
    }

    // With asImport, the text is first checked as a file to add (RoutingConfig.cleanImport),
    // which may refuse it or take a subscription out of it. Here on the worker, since
    // that parses the whole file once more.
    private void validateAsync(String source, boolean asImport, Checked accepted, Runnable rejected) {
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed)); rejected.run(); return; }
        final long request = ++validationRequest;
        toast(getString(R.string.validating_config));
        validationWorker.execute(() -> {
            try {
                RoutingConfig.Import checked = asImport ? RoutingConfig.cleanImport(source) : new RoutingConfig.Import(source, false);
                RoutingConfig.Plan plan = RoutingConfig.prepare(this, checked.getToml());
                NativeConfigValidator.INSTANCE.validate(getApplicationContext(), plan.getToml());
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    if (request != validationRequest) { rejected.run(); return; }
                    if (plan.getWarning() != null) toast(getString(plan.getWarning()));
                    accepted.accept(checked.getToml(), checked.getSubscriptionNotKept());
                });
            } catch (RoutingConfig.ImportRefused refused) {
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    rejected.run();
                    if (request == validationRequest) refuseImport(refused.messageResource);
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    if (isDestroyed()) return;
                    rejected.run();
                    if (request == validationRequest) toast(getString(R.string.config_validation_error, error.getMessage()));
                });
            }
        });
    }

    private void configs(LinearLayout body) {
        LinearLayout header = row(); header.setGravity(Gravity.CENTER);
        FrameLayout add = configHeaderButton(R.drawable.ic_add, getString(R.string.text_add_configuration));
        add.setOnClickListener(v -> addMenu()); header.addView(add, lp(40, 32));
        gapHorizontal(header, 12);
        FrameLayout ping = configHeaderButton(R.drawable.ic_ping, getString(R.string.text_check_server_tcp_latency));
        ping.setOnClickListener(v -> pingConfigs()); header.addView(ping, lp(40, 32));
        body.addView(header, lp(-1, 44));
        if (configs.isEmpty()) {
            body.addView(new View(this), new LinearLayout.LayoutParams(1, 0, 1));
            TextView hint = label(getString(R.string.text_add_configuration), 15, textColor(), true); hint.setGravity(Gravity.CENTER);
            hint.setOnClickListener(v -> addMenu()); body.addView(hint, lp(-1, 40));
            body.addView(new View(this), new LinearLayout.LayoutParams(1, 0, 1));
        }
        for (int i = 0; i < configs.size(); i++) {
            final int index = i; Config config = configs.get(i); gap(body, 12);
            LinearLayout c = card(); LinearLayout top = row();
            TextView name = label(config.name, 16, textColor(), true); top.addView(name, new LinearLayout.LayoutParams(0, dp(35), 1));
            if (i == activeIndex) top.addView(label(getString(R.string.text_selected), 12, success(), false), lp(-2, 35));
            c.addView(top, lp(-1, 36));
            LinearLayout meta = row();
            meta.addView(label("TrustTunnel  ·  TOML", 12, dim(), false), new LinearLayout.LayoutParams(0, dp(25), 1));
            String pingText = pingResults.get(config);
            if (pingText != null) meta.addView(label(pingText, 12, dim(), false), lp(-2, 25));
            c.addView(meta, lp(-1, 25));
            LinearLayout actions = row();
            Button select = button(getString(R.string.text_select), false); select.setOnClickListener(v -> { activeIndex = index; saveConfigs(); applyCurrentRules(); render(); });
            actions.addView(select, new LinearLayout.LayoutParams(0, dp(38), 1)); gapHorizontal(actions, 8);
            Button edit = button(getString(R.string.text_edit), false); edit.setOnClickListener(v -> editConfig(index));
            actions.addView(edit, new LinearLayout.LayoutParams(0, dp(38), 1)); gapHorizontal(actions, 8);
            Button remove = button("✕", false); remove.setOnClickListener(v -> confirmRemove(index));
            actions.addView(remove, lp(42, 38)); c.addView(actions, lp(-1, 38));
            body.addView(c, lp(-1, -2));
        }
    }
    private FrameLayout configHeaderButton(int iconId, String description) {
        FrameLayout box = new FrameLayout(this);
        box.setBackground(shape(surface(), 8));
        box.setContentDescription(description);
        ImageView icon = new ImageView(this);
        icon.setImageResource(iconId);
        icon.setColorFilter(accent());
        box.addView(icon, new FrameLayout.LayoutParams(dp(20), dp(20), Gravity.CENTER));
        return box;
    }
    private void pingConfigs() {
        if (configs.isEmpty()) { toast(getString(R.string.text_add_a_configuration_first)); return; }
        if (pingRunning) return;
        pingRunning = true;
        List<Config> snapshot = new ArrayList<>(configs);
        for (Config config : snapshot) pingResults.put(config, "…");
        render();
        new Thread(() -> {
            for (Config config : snapshot) {
                String result = tcpPing(config.toml);
                runOnUiThread(() -> {
                    pingResults.put(config, result);
                    if (page == 1) render();
                });
            }
            runOnUiThread(() -> { pingRunning = false; if (page == 1) render(); });
        }, "freetunnel-ping").start();
    }
    private String tcpPing(String toml) {
        String target;
        try { target = RoutingConfig.validate(toml).getArray("endpoint.addresses").getString(0); }
        catch (Exception e) { return getString(R.string.text_no_address); }
        int separator = target.lastIndexOf(':');
        if (separator <= 0) return getString(R.string.text_no_port);
        String host = target.substring(0, separator);
        if (host.startsWith("[") && host.endsWith("]")) host = host.substring(1, host.length() - 1);
        try {
            int port = Integer.parseInt(target.substring(separator + 1));
            if (port < 1 || port > 65535) return getString(R.string.text_no_port);
            long start = SystemClock.elapsedRealtime();
            // Times a TCP handshake to the server and sends nothing over it, so
            // there is nothing to encrypt.
            try (Socket socket = new Socket()) { socket.connect(new InetSocketAddress(host, port), 2500); } // nosemgrep: java.lang.security.audit.crypto.unencrypted-socket.unencrypted-socket
            return (SystemClock.elapsedRealtime() - start) + getString(R.string.text_ms);
        } catch (Exception e) { return getString(R.string.text_unreachable); }
    }
    private void addMenu() {
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_saved_data_has_been_preserved)); return; }
        new AlertDialog.Builder(this).setTitle(getString(R.string.text_add_configuration))
                .setItems(new String[]{getString(R.string.text_import_toml_file), getString(R.string.text_paste_from_clipboard), getString(R.string.text_create_manually)}, (d, which) -> {
                    if (which == 0) {
                        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
                        startActivityForResult(pick, PICK_CONFIG);
                    } else if (which == 1) {
                        ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                        ClipData clip = cm.getPrimaryClip();
                        if (clip == null || clip.getItemCount() == 0) { toast(getString(R.string.text_clipboard_is_empty)); return; }
                        CharSequence value = clip.getItemAt(0).coerceToText(this);
                        String text = value == null ? "" : value.toString().trim();
                        if (DeepLinkImport.looksLikeLink(text)) importDeepLink(text);
                        else importToml(getString(R.string.text_from_clipboard), text);
                    } else editConfig(-1);
                }).show();
    }
    private void importDeepLink(String link) {
        final DeepLinkImport.Imported imported;
        try { imported = DeepLinkImport.parse(link); }
        catch (DeepLinkImport.ParseException e) { refuseImport(e.messageResource); return; }

        // The text is the app's own, and any subscription was left out as it was read.
        validateAsync(imported.toml, false, (toml, dropped) ->
                showImportConfirmation(imported.name, toml, imported.subscriptionNotKept), () -> {});
    }
    private void importToml(String name, String toml) {
        validateAsync(stripBom(toml), true, (cleaned, dropped) -> showImportConfirmation(name, cleaned, dropped), () -> {});
    }
    // These two refusals are about what this version cannot do yet, and say what to
    // do instead. That is more than the two lines a toast shows, so they get a dialog.
    private void refuseImport(int messageResource) {
        if (messageResource == R.string.text_subscription_links_are_not_supported_yet
                || messageResource == R.string.text_this_server_needs_a_newer_trusttunnel_core)
            new AlertDialog.Builder(this).setTitle(getString(R.string.text_import_trusttunnel))
                    .setMessage(getString(messageResource)).setPositiveButton(getString(R.string.text_ok), null).show();
        else toast(getString(messageResource));
    }
    private void showImportConfirmation(String name, String source, boolean subscriptionNotKept) {
        org.tomlj.TomlTable parsed = RoutingConfig.validate(source);
        String message = getString(R.string.text_add_configuration_36) + name + getString(R.string.text_nserver) + parsed.getString("endpoint.hostname");
        if (Boolean.TRUE.equals(parsed.getBoolean("endpoint.skip_verification")))
            message += getString(R.string.text_nserver_certificate_verification_is_disabled);
        if (subscriptionNotKept) message += getString(R.string.text_nsubscriptions_are_not_supported_yet_the_server_wont);
        for (Config config : configs) {
            if (config.name.equals(name) || config.toml.equals(source)) {
                message += getString(R.string.text_nthis_configuration_or_name_already_exists_a_separat); break;
            }
        }
        if (name.codePoints().mapToObj(Character.UnicodeScript::of)
                .filter(script -> script != Character.UnicodeScript.COMMON && script != Character.UnicodeScript.INHERITED
                        && script != Character.UnicodeScript.UNKNOWN).distinct().count() > 1)
            message += getString(R.string.text_nthe_name_mixes_similar_characters_from_different_al);
        new AlertDialog.Builder(this).setTitle(getString(R.string.text_import_trusttunnel)).setMessage(message)
                .setNegativeButton(getString(R.string.text_cancel), null).setPositiveButton(getString(R.string.text_add), (dialog, which) -> {
                    configs.add(new Config(name, source));
                    if (configs.size() == 1) activeIndex = 0;
                    saveConfigs(); page = 1; render();
                }).show();
    }
    private static String stripBom(String toml) {
        return toml.startsWith("\uFEFF") ? toml.substring(1) : toml;
    }
    private void editConfig(int index) {
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed)); return; }
        final Config editing = index < 0 ? null : configs.get(index);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        LinearLayout form = column(); form.setPadding(dp(20), dp(5), dp(20), 0);
        EditText name = new EditText(this); name.setSingleLine(true); name.setTextColor(textColor());
        name.setHint(getString(R.string.text_configuration_name)); name.setHintTextColor(faint());
        EditText raw = new EditText(this); raw.setTextColor(textColor()); raw.setHintTextColor(faint());
        raw.setHint(getString(R.string.text_paste_complete_trusttunnel_toml)); raw.setMinLines(8); raw.setGravity(Gravity.TOP);
        raw.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        raw.setTransformationMethod(PasswordTransformationMethod.getInstance());
        CheckBox show = new CheckBox(this); show.setText(getString(R.string.text_show_toml_and_password)); show.setTextColor(textColor());
        show.setOnCheckedChangeListener((b, checked) -> raw.setTransformationMethod(checked ? null : PasswordTransformationMethod.getInstance()));
        form.addView(show, lp(-1, 48));
        if (index >= 0) { name.setText(configs.get(index).name); raw.setText(configs.get(index).toml); }
        form.addView(name, lp(-1, 54)); form.addView(raw, lp(-1, 230));
        ScrollView scroll = new ScrollView(this); scroll.addView(form);
        AlertDialog editor = new AlertDialog.Builder(this).setTitle(index < 0 ? getString(R.string.text_new_configuration) : getString(R.string.text_edit_configuration))
                .setView(scroll).setNegativeButton(getString(R.string.text_cancel), null)
                .setPositiveButton(getString(R.string.text_save), null).create();
        editor.setOnDismissListener(d -> {
            validationRequest++;
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_SECURE);
        });
        editor.show();
        editor.getWindow().addFlags(WindowManager.LayoutParams.FLAG_SECURE);
        editor.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
                    String n = name.getText().toString().trim();
                    String t = stripBom(raw.getText().toString().trim());
                    if (n.isEmpty()) { toast(getString(R.string.text_enter_a_configuration_name)); return; }
                    editor.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(false);
                    // Checked as an import: the hint asks for a whole TrustTunnel file, and
                    // one pasted here, or saved by an earlier version, can carry a
                    // subscription address or an auth key just as an imported one can.
                    validateAsync(t, true, (saved, dropped) -> {
                        if (!editor.isShowing()) return;
                        if (editing == null) configs.add(new Config(n, saved));
                        else {
                            int current = configs.indexOf(editing);
                            if (current < 0) return;
                            configs.set(current, new Config(n, saved));
                        }
                        saveConfigs(); editor.dismiss(); applyCurrentRules(); render();
                        if (dropped) new AlertDialog.Builder(this)
                                .setMessage(getString(R.string.text_nsubscriptions_are_not_supported_yet_the_server_wont).trim())
                                .setPositiveButton(getString(R.string.text_ok), null).show();
                    }, () -> editor.getButton(AlertDialog.BUTTON_POSITIVE).setEnabled(true));
                });
    }
    private void confirmRemove(int index) {
        if (storageFailed) return;
        new AlertDialog.Builder(this).setTitle(getString(R.string.text_delete_configuration)).setMessage(configs.get(index).name)
                .setNegativeButton(getString(R.string.text_cancel), null).setPositiveButton(getString(R.string.text_delete), (d, w) -> {
                    boolean activeRemoved = index == activeIndex;
                    configs.remove(index);
                    if (index < activeIndex) activeIndex--;
                    activeIndex = Math.max(0, Math.min(activeIndex, configs.size() - 1));
                    saveConfigs(); if (activeRemoved) applyCurrentRules(); render();
                }).show();
    }
    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PREPARE_VPN) {
            if (resultCode == RESULT_OK) startVpnWithPendingConfig();
            else { pendingConfig = null; toast(getString(R.string.text_vpn_permission_was_not_granted)); }
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
            importToml(name, out.toString(StandardCharsets.UTF_8.name()));
        } catch (Exception e) { toast(getString(R.string.text_could_not_open_file)); }
    }

    private void split(LinearLayout body) {
        LinearLayout enabled = row();
        enabled.addView(label(getString(R.string.text_split_tunnelling), 14, textColor(), false),
                new LinearLayout.LayoutParams(0, dp(42), 1));
        Switch splitSwitch = new Switch(this); splitSwitch.setChecked(prefs.getBoolean("split_enabled", false));
        styleSwitch(splitSwitch);
        splitSwitch.setOnCheckedChangeListener((v, checked) -> {
            prefs.edit().putBoolean("split_enabled", checked).apply();
            applyCurrentRules();
        });
        enabled.addView(splitSwitch); body.addView(enabled, lp(-1, 42));
        boolean through = prefs.getBoolean("through", false);
        LinearLayout mode = row(); mode.addView(label(getString(R.string.text_mode), 14, textColor(), false),
                new LinearLayout.LayoutParams(0, dp(42), 1));
        TextView modeValue = label(through ? getString(R.string.text_through_vpn) : getString(R.string.text_bypass_vpn), 14, dim(), false);
        mode.addView(modeValue, lp(-2, 42));
        mode.setOnClickListener(v -> new AlertDialog.Builder(this).setTitle(getString(R.string.text_mode))
                .setItems(new String[]{getString(R.string.text_bypass_vpn_61), getString(R.string.text_through_vpn_62)}, (dialog, which) -> {
                    prefs.edit().putBoolean("through", which == 1).apply(); applyCurrentRules(); render();
                }).show());
        body.addView(mode, lp(-1, 42));
        TextView explanation = label(getString(R.string.text_for_through_vpn_choose_applications_or_addresses_sel), 12, dim(), false);
        body.addView(explanation, lp(-1, 66));
        for (Config config : configs) {
            if (configs.indexOf(config) != activeIndex) continue;
            try {
                org.tomlj.TomlArray imported = RoutingConfig.validate(config.toml).getArray("exclusions");
                if (imported != null && imported.size() > 0) body.addView(label(getString(R.string.imported_routing_rules, imported.toList().toString()),
                        12, dim(), false), lp(-1, -2));
            } catch (Exception ignored) { }
        }
        if (prefs.getBoolean("rules_migrated", false)) {
            body.addView(label(getString(R.string.legacy_rules_removed), 12, dim(), false), lp(-1, -2));
        }
        gap(body, 14);
        LinearLayout domainHeader = row();
        domainHeader.addView(label(through ? getString(R.string.text_rules_through_vpn) : getString(R.string.text_rules_bypass_vpn), 11, faint(), true),
                new LinearLayout.LayoutParams(0, dp(29), 1));
        TextView clearDomains = label(getString(R.string.text_clear), 12, accent(), false);
        clearDomains.setOnClickListener(v -> { prefs.edit().remove("domains").apply(); applyCurrentRules(); render(); });
        domainHeader.addView(clearDomains, lp(-2, 29)); body.addView(domainHeader, lp(-1, 29));
        Set<String> rules = new HashSet<>(prefs.getStringSet("domains", Collections.emptySet()));
        LinearLayout domains = column();
        for (String rule : sorted(rules)) {
            LinearLayout r = row(); TextView text = label(rule, 14, textColor(), false);
            r.setBackground(shape(surface(), 13)); r.setPadding(dp(11), 0, dp(4), 0);
            r.addView(text, new LinearLayout.LayoutParams(0, dp(28), 1));
            TextView del = label("✕", 16, dim(), false); del.setGravity(Gravity.CENTER); r.addView(del, lp(28, 28));
            del.setOnClickListener(v -> { rules.remove(rule); prefs.edit().putStringSet("domains", rules).apply(); applyCurrentRules(); render(); });
            domains.addView(r, lp(-1, 28)); gap(domains, 6);
        }
        body.addView(domains, lp(-1, -2));
        EditText domainInput = new EditText(this); domainInput.setSingleLine(true); domainInput.setTextSize(13);
        domainInput.setTextColor(textColor()); domainInput.setHintTextColor(faint());
        domainInput.setHint(getString(R.string.text_domains_or_ips_space_comma));
        domainInput.setPadding(dp(12), 0, dp(12), 0);
        domainInput.setBackground(shape(input(), 8));
        domainInput.setOnEditorActionListener((field, action, event) -> {
            addDomains(domainInput.getText().toString()); domainInput.setText(""); return true;
        });
        body.addView(domainInput, lp(-1, 36));
        gap(body, 15);
        LinearLayout appHeader = row();
        appHeader.addView(label(through ? getString(R.string.text_applications_through_vpn) : getString(R.string.text_applications_bypass_vpn), 11, faint(), true),
                new LinearLayout.LayoutParams(0, dp(29), 1));
        TextView chooseLink = label(getString(R.string.text_choose), 12, accent(), false);
        chooseLink.setOnClickListener(v -> appPicker());
        appHeader.addView(chooseLink, lp(-2, 29)); body.addView(appHeader, lp(-1, 29));
        LinearLayout apps = column();
        Set<String> selected = prefs.getStringSet("apps", Collections.emptySet());
        PackageManager pm = getPackageManager();
        for (String pkg : sorted(selected)) {
            try {
                android.content.pm.ApplicationInfo appInfo = pm.getApplicationInfo(pkg, 0);
                LinearLayout appRow = row();
                appRow.setBackground(shape(surface(), 13)); appRow.setPadding(dp(10), 0, dp(4), 0);
                ImageView appIcon = new ImageView(this); appIcon.setImageDrawable(appInfo.loadIcon(pm));
                appRow.addView(appIcon, lp(20, 20)); gapHorizontal(appRow, 8);
                appRow.addView(label(appInfo.loadLabel(pm).toString(), 13, textColor(), false),
                        new LinearLayout.LayoutParams(0, dp(28), 1));
                TextView remove = label("✕", 16, dim(), false); remove.setGravity(Gravity.CENTER);
                remove.setOnClickListener(v -> {
                    Set<String> next = new HashSet<>(prefs.getStringSet("apps", Collections.emptySet()));
                    next.remove(pkg); prefs.edit().putStringSet("apps", next).apply(); applyCurrentRules(); render();
                });
                appRow.addView(remove, lp(28, 28)); apps.addView(appRow, lp(-1, 28)); gap(apps, 6);
            } catch (PackageManager.NameNotFoundException ignored) { }
        }
        body.addView(apps, lp(-1, -2));
        TextView addApp = label(getString(R.string.text_choose_installed_applications), 13, faint(), false);
        addApp.setPadding(dp(12), 0, dp(12), 0); addApp.setBackground(shape(input(), 8));
        addApp.setOnClickListener(v -> appPicker()); body.addView(addApp, lp(-1, 36));
    }
    private void addDomains(String inputText) {
        Set<String> rules = new HashSet<>(prefs.getStringSet("domains", Collections.emptySet()));
        try {
            for (String rule : SplitRules.INSTANCE.normalize(inputText, false)) rules.add(rule);
        } catch (IllegalArgumentException e) { toast(getString(R.string.invalid_routing_rule)); return; }
        prefs.edit().putStringSet("domains", rules).apply(); applyCurrentRules(); render();
    }
    private List<String> sorted(Set<String> set) { List<String> list = new ArrayList<>(set); Collections.sort(list); return list; }
    private void appPicker() {
        PackageManager pm = getPackageManager();
        Intent launcher = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> found = pm.queryIntentActivities(launcher, 0);
        found.sort((a, b) -> String.CASE_INSENSITIVE_ORDER.compare(a.loadLabel(pm).toString(), b.loadLabel(pm).toString()));
        Set<String> chosen = new HashSet<>(prefs.getStringSet("apps", Collections.emptySet()));
        LinearLayout list = column(); list.setPadding(dp(16), 0, dp(16), 0);
        Set<String> seen = new HashSet<>();
        for (ResolveInfo app : found) {
            String pkg = app.activityInfo.packageName;
            if (pkg.equals(getPackageName()) || !seen.add(pkg)) continue;
            CheckBox box = new CheckBox(this); box.setText(app.loadLabel(pm)); box.setTextColor(textColor());
            box.setButtonTintList(android.content.res.ColorStateList.valueOf(success()));
            box.setChecked(chosen.contains(pkg)); box.setPadding(dp(4), dp(6), dp(4), dp(6));
            box.setOnCheckedChangeListener((b, checked) -> { if (checked) chosen.add(pkg); else chosen.remove(pkg); });
            list.addView(box, lp(-1, 52));
        }
        ScrollView scroll = new ScrollView(this); scroll.addView(list);
        new AlertDialog.Builder(this).setTitle(getString(R.string.text_choose_applications)).setView(scroll)
                .setNegativeButton(getString(R.string.text_cancel), null).setPositiveButton(getString(R.string.text_done), (d, w) -> {
                    prefs.edit().putStringSet("apps", chosen).apply(); applyCurrentRules(); render();
                }).show();
    }

    private void settings(LinearLayout body) {
        section(body, getString(R.string.text_general));
        LinearLayout appearance = row();
        appearance.addView(label(getString(R.string.text_theme), 14, textColor(), false), new LinearLayout.LayoutParams(0, dp(42), 1));
        appearance.addView(label(dark ? getString(R.string.text_dark) : getString(R.string.text_light), 14, dim(), false), lp(-2, 42));
        appearance.setOnClickListener(v -> new AlertDialog.Builder(this).setTitle(getString(R.string.text_theme))
                .setItems(new String[]{getString(R.string.text_light_78), getString(R.string.text_dark_79)}, (d, which) -> {
                    dark = which == 1; prefs.edit().putBoolean("dark", dark).apply(); render();
                }).show());
        body.addView(appearance, lp(-1, 42)); separator(body);
        addSwitch(body, getString(R.string.text_connect_on_launch), "", "auto_connect", false);
        section(body, getString(R.string.text_security));
        addSwitch(body, "Kill switch", getString(R.string.text_blocks_traffic_while_reconnecting), "killswitch", true);
        LinearLayout systemVpn = row();
        systemVpn.addView(label(getString(R.string.text_android_vpn_settings), 14, textColor(), false),
                new LinearLayout.LayoutParams(0, dp(48), 1));
        systemVpn.addView(label(getString(R.string.text_open), 13, accent(), false), lp(-2, 48));
        systemVpn.setOnClickListener(v -> {
            try { startActivity(new Intent(Settings.ACTION_VPN_SETTINGS)); }
            catch (Exception e) { toast(getString(R.string.text_vpn_settings_are_unavailable)); }
        });
        body.addView(systemVpn, lp(-1, 48));
        body.addView(label(getString(R.string.text_enable_always_on_vpn_and_block_connections_without_v),
                12, dim(), false), lp(-1, 46));
        body.addView(label(getString(R.string.lockdown_app_warning), 12, dim(), false), lp(-1, -2));
        section(body, getString(R.string.text_log));
        addSwitch(body, getString(R.string.text_enable_logging), "", "logging", true);
        section(body, getString(R.string.update_title));
        addSwitch(body, getString(R.string.update_on_launch), "", "check_updates_on_launch", false);
        updateStatus = label("", 12, dim(), false);
        body.addView(updateStatus, lp(-1, -2));
        updateAction = label("", 14, accent(), false);
        updateAction.setOnClickListener(v -> {
            AndroidUpdater.State update = AndroidUpdater.INSTANCE.getState();
            if (update == AndroidUpdater.State.AVAILABLE) AndroidUpdater.INSTANCE.download(this);
            else if (update == AndroidUpdater.State.READY) AndroidUpdater.INSTANCE.install(this);
            else AndroidUpdater.INSTANCE.check();
        });
        body.addView(updateAction, lp(-1, 48));
        refreshUpdateControls();
        section(body, getString(R.string.text_about));
        body.addView(label(getString(R.string.text_freetunnel_for_android), 14, textColor(), false), lp(-1, 42));
        body.addView(label(getString(R.string.text_version) + BuildConfig.VERSION_NAME, 12, dim(), false), lp(-1, 28));
    }
    private void refreshUpdateControls() {
        if (updateStatus == null || updateAction == null) return;
        AndroidUpdater.State update = AndroidUpdater.INSTANCE.getState();
        int status;
        switch (update) {
            case CHECKING: status = R.string.update_checking; break;
            case CURRENT: status = R.string.update_current; break;
            case AVAILABLE: status = R.string.update_available; break;
            case DOWNLOADING: status = R.string.update_downloading; break;
            case READY: status = R.string.update_ready; break;
            case INSTALLING: status = R.string.update_installing; break;
            case ERROR: status = R.string.update_error; break;
            default: status = R.string.update_hint;
        }
        String updateText = getString(status);
        UpdatePolicy.Release release = AndroidUpdater.INSTANCE.getRelease();
        if (release != null) updateText += " " + release.getVersion();
        if (update == AndroidUpdater.State.DOWNLOADING) updateText += " · " + AndroidUpdater.INSTANCE.getProgress() + "%";
        updateStatus.setText(updateText);
        updateAction.setText(getString(update == AndroidUpdater.State.AVAILABLE ? R.string.update_download :
                update == AndroidUpdater.State.READY ? R.string.update_install : R.string.update_check));
        updateAction.setEnabled(update != AndroidUpdater.State.CHECKING && update != AndroidUpdater.State.DOWNLOADING
                && update != AndroidUpdater.State.INSTALLING);
    }
    private void separator(LinearLayout parent) {
        View line = new View(this); line.setBackgroundColor(dark ? Color.parseColor("#2e2e2e") : Color.parseColor("#d0d0d0"));
        parent.addView(line, lp(-1, 1));
    }
    private void addSwitch(LinearLayout parent, String title, String subtitle, String key, boolean initial) {
        LinearLayout r = row(); LinearLayout copy = column();
        copy.addView(label(title, 14, textColor(), false), lp(-1, 23));
        if (!subtitle.isEmpty()) copy.addView(label(subtitle, 12, dim(), false), lp(-1, 22));
        r.addView(copy, new LinearLayout.LayoutParams(0, -2, 1));
        Switch sw = new Switch(this); sw.setChecked(prefs.getBoolean(key, initial));
        styleSwitch(sw);
        sw.setOnCheckedChangeListener((v, enabled) -> {
            prefs.edit().putBoolean(key, enabled).apply();
            if (key.equals("killswitch")) applyCurrentRules();
        });
        r.addView(sw); parent.addView(r, lp(-1, subtitle.isEmpty() ? 42 : 56));
    }
    private void styleSwitch(Switch sw) {
        int[][] states = {new int[]{android.R.attr.state_checked}, new int[]{}};
        int[] thumbs = {accent(), dark ? Color.parseColor("#9a9a9a") : Color.parseColor("#ffffff")};
        int[] tracks = {dark ? Color.parseColor("#575757") : Color.parseColor("#a8a8a8"),
                dark ? Color.parseColor("#3a3a3a") : Color.parseColor("#c4c4c4")};
        sw.setThumbTintList(new android.content.res.ColorStateList(states, thumbs));
        sw.setTrackTintList(new android.content.res.ColorStateList(states, tracks));
    }
    private void logs(LinearLayout body) {
        LinearLayout actions = row();
        TextView clear = label(getString(R.string.text_clear), 13, accent(), false);
        actions.addView(clear, new LinearLayout.LayoutParams(0, dp(34), 1));
        TextView copy = label(getString(R.string.text_copy), 13, accent(), false); actions.addView(copy, lp(-2, 34));
        clear.setOnClickListener(v -> {
            try { new java.io.File(getFilesDir(), "freetunnel.log").delete(); } catch (Exception ignored) { }
            render();
        });
        copy.setOnClickListener(v -> {
            try {
                java.io.File file = new java.io.File(getFilesDir(), "freetunnel.log");
                String contents = file.exists() ? new String(java.nio.file.Files.readAllBytes(file.toPath()), StandardCharsets.UTF_8) : "";
                ClipboardManager manager = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                manager.setPrimaryClip(ClipData.newPlainText("FreeTunnel log", contents));
                toast(contents.isEmpty() ? getString(R.string.text_log_is_empty) : getString(R.string.text_log_copied));
            } catch (Exception e) { toast(getString(R.string.text_could_not_read_log)); }
        });
        body.addView(actions, lp(-1, 34)); gap(body, 8);
        FrameLayout panel = new FrameLayout(this); panel.setBackground(shape(surface(), 8));
        String logText = getString(R.string.text_connect_vpn_to_see_the_log);
        java.io.File logFile = new java.io.File(getFilesDir(), "freetunnel.log");
        if (logFile.exists()) {
            try { logText = new String(java.nio.file.Files.readAllBytes(logFile.toPath()), StandardCharsets.UTF_8).trim(); }
            catch (Exception ignored) { logText = getString(R.string.text_could_not_read_log); }
        }
        TextView placeholder = label(logText, 12, logFile.exists() ? dim() : faint(), false);
        placeholder.setGravity(Gravity.TOP | Gravity.START);
        placeholder.setPadding(dp(12), dp(12), dp(12), dp(12));
        placeholder.setTextIsSelectable(true);
        panel.addView(placeholder, new FrameLayout.LayoutParams(-1, -1));
        body.addView(panel, new LinearLayout.LayoutParams(-1, 0, 1));
    }
    @Override protected void onDestroy() {
        validationRequest++;
        validationWorker.shutdown();
        super.onDestroy();
    }
    private void loadConfigs() {
        try {
            JSONArray array = new JSONArray(ConfigStore.read(prefs));
            for (int i = 0; i < array.length(); i++) {
                JSONObject o = array.getJSONObject(i); configs.add(new Config(o.getString("name"), o.getString("toml")));
            }
        } catch (Exception error) {
            storageFailed = true;
            configs.clear();
            rootStorageWarning();
        }
        activeIndex = Math.max(0, Math.min(activeIndex, configs.size() - 1));
    }
    private void rootStorageWarning() {
        new AlertDialog.Builder(this).setTitle(getString(R.string.text_configurations_are_unavailable))
                .setMessage(getString(R.string.text_android_could_not_read_storage_saved_data_is_unchang))
                .setPositiveButton(getString(R.string.text_ok), null).show();
    }
    private void saveConfigs() {
        validationRequest++;
        if (storageFailed) { toast(getString(R.string.text_storage_is_unavailable_data_has_not_been_changed)); return; }
        JSONArray array = new JSONArray();
        for (Config config : configs) {
            JSONObject o = new JSONObject();
            try { o.put("name", config.name); o.put("toml", config.toml); array.put(o); } catch (Exception ignored) { }
        }
        if (!ConfigStore.write(prefs, array.toString())) {
            storageFailed = true; toast(getString(R.string.text_could_not_save_configurations_data_on_disk_is_unchan));
            configs.clear(); loadConfigs(); render(); return;
        }
        prefs.edit().putInt("active", activeIndex).apply();
    }
}
