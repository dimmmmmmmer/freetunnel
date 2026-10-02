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
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Android adaptation of the FreeTunnel desktop interface. */
public final class MainActivity extends Activity {
    private static final int PICK_CONFIG = 21;
    private static final int PREPARE_VPN = 22;
    private static final String PREFS = "freetunnel";
    private final String[] tabNames = {"Главная", "Конфиги", "Маршруты", "Настройки", "Журнал"};
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
    private final BroadcastReceiver vpnReceiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            vpnState = intent.getIntExtra(FreeTunnelVpnService.EXTRA_STATE, 0);
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
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        dark = prefs.getBoolean("dark", true);
        activeIndex = prefs.getInt("active", 0);
        vpnState = FreeTunnelVpnService.state(this);
        loadConfigs();
        render();
        handleIncomingIntent(getIntent());
        if (getIntent() != null && getIntent().getBooleanExtra("tile_connect", false)) {
            getIntent().removeExtra("tile_connect");
            root.post(this::connect);
        } else if (prefs.getBoolean("auto_connect", false) && vpnState == 0 && !configs.isEmpty()) {
            root.post(this::connect);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        if (!receiverRegistered) {
            IntentFilter filter = new IntentFilter(FreeTunnelVpnService.ACTION_STATE);
            if (Build.VERSION.SDK_INT >= 33) registerReceiver(vpnReceiver, filter, Context.RECEIVER_NOT_EXPORTED);
            else registerReceiver(vpnReceiver, filter);
            receiverRegistered = true;
        }
        vpnState = FreeTunnelVpnService.state(this);
    }

    @Override protected void onPause() {
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
    private LinearLayout column() { LinearLayout l = new LinearLayout(this); l.setOrientation(1); return l; }
    private LinearLayout row() { LinearLayout l = new LinearLayout(this); l.setOrientation(0); l.setGravity(Gravity.CENTER_VERTICAL); return l; }
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
        gap(p, 18); TextView t = label(title.toUpperCase(), 11, faint(), true);
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
                android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars());
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
        logo.setContentDescription("Подключить FreeTunnel");
        logo.setOnClickListener(v -> connect());
        gap(hero, 22);
        String name = configs.isEmpty() ? "Добавить конфиг" : configs.get(Math.min(activeIndex, configs.size() - 1)).name + "  ▾";
        TextView selected = label(name, 15, textColor(), true); selected.setGravity(Gravity.CENTER);
        hero.addView(selected, lp(-1, 28));
        selected.setOnClickListener(v -> {
            page = 1; render(); if (configs.isEmpty()) addMenu();
        });
        TextView status = label(connectionCaption(), 13, vpnState == 2 ? success() : dim(), false);
        status.setGravity(Gravity.CENTER);
        hero.addView(status, lp(-1, 30));
        Button toggle = button(isVpnActive() ? "Отключить" : "Подключить", isVpnActive());
        toggle.setContentDescription(isVpnActive() ? "Отключить VPN" : "Подключить VPN");
        toggle.setOnClickListener(v -> connect());
        hero.addView(toggle, lp(178, 44));
        FrameLayout.LayoutParams heroLp = new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER);
        heroLp.bottomMargin = dp(34);
        stage.addView(hero, heroLp);
        LinearLayout speeds = row(); speeds.setGravity(Gravity.CENTER);
        speeds.addView(speedCard("↓", "— MB/s", success()), lp(116, 44));
        gapHorizontal(speeds, 12);
        speeds.addView(speedCard("↑", "— MB/s", dim()), lp(116, 44));
        FrameLayout.LayoutParams speedLp = new FrameLayout.LayoutParams(-1, dp(44), Gravity.BOTTOM);
        speedLp.bottomMargin = dp(44);
        stage.addView(speeds, speedLp);
    }
    private void gapHorizontal(LinearLayout p, int size) { p.addView(new View(this), lp(size, 1)); }
    private LinearLayout speedCard(String arrow, String speed, int color) {
        LinearLayout c = row(); c.setGravity(Gravity.CENTER); c.setBackground(shape(tile(), 8));
        TextView a = label(arrow, 14, color, false); c.addView(a, lp(20, -1));
        c.addView(label(speed, 14, textColor(), true), lp(-2, -1)); return c;
    }
    private void connect() {
        if (isVpnActive()) {
            FreeTunnelVpnService.stop(this);
            return;
        }
        if (configs.isEmpty()) { page = 1; render(); toast("Сначала добавьте конфиг"); return; }
        Config config = configs.get(Math.max(0, Math.min(activeIndex, configs.size() - 1)));
        pendingConfig = effectiveConfig(config.toml);
        Intent prepare = VpnService.prepare(this);
        if (prepare != null) startActivityForResult(prepare, PREPARE_VPN);
        else startVpnWithPendingConfig();
    }

    private boolean isVpnActive() {
        return vpnState == 1 || vpnState == 2 || vpnState == 3 || vpnState == 4 || vpnState == 5;
    }

    private String connectionCaption() {
        switch (vpnState) {
            case 1: return "Подключение…";
            case 2: return "Подключено";
            case 3: case 4: case 5: return "Восстановление соединения…";
            default: return "Отключено";
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
        FreeTunnelVpnService.restart(this, effectiveConfig(config.toml));
        vpnState = 1;
        toast("Применяю настройки маршрутизации");
        QuickTileService.refresh(this);
    }

    private String effectiveConfig(String source) {
        boolean splitEnabled = prefs.getBoolean("split_enabled", false);
        boolean selective = splitEnabled && prefs.getBoolean("through", false);
        Set<String> domains = splitEnabled ? prefs.getStringSet("domains", Collections.emptySet()) : Collections.emptySet();
        StringBuilder out = new StringBuilder();
        boolean rootSectionEnded = false;
        boolean settingsWritten = false;
        String[] lines = source.replace("\r\n", "\n").split("\n", -1);
        for (String line : lines) {
            String trimmed = line.trim();
            boolean section = trimmed.startsWith("[") && trimmed.endsWith("]");
            if (section && !settingsWritten) {
                appendRoutingSettings(out, selective, domains);
                settingsWritten = true;
            }
            if (!rootSectionEnded && section) rootSectionEnded = true;
            if (!rootSectionEnded && trimmed.matches("(?i)^(vpn_mode|exclusions)\\s*=.*")) {
                continue;
            }
            out.append(line).append('\n');
        }
        if (!settingsWritten) appendRoutingSettings(out, selective, domains);
        return out.toString();
    }

    private void appendRoutingSettings(StringBuilder out, boolean selective, Set<String> domains) {
        out.append("vpn_mode = ").append(selective ? "\"selective\"" : "\"general\"").append('\n');
        out.append("exclusions = [");
        boolean first = true;
        for (String domain : sorted(domains)) {
            if (!domain.matches("[a-zA-Z0-9.*:_/\\-]+")) continue;
            if (!first) out.append(", ");
            out.append('"').append(domain).append('"');
            first = false;
        }
        out.append("]\n");
    }

    private void configs(LinearLayout body) {
        LinearLayout header = row(); header.setGravity(Gravity.CENTER);
        FrameLayout add = configHeaderButton(R.drawable.ic_add, "Добавить конфиг");
        add.setOnClickListener(v -> addMenu()); header.addView(add, lp(40, 32));
        gapHorizontal(header, 12);
        FrameLayout ping = configHeaderButton(R.drawable.ic_ping, "Проверить пинг конфигов");
        ping.setOnClickListener(v -> pingConfigs()); header.addView(ping, lp(40, 32));
        body.addView(header, lp(-1, 44));
        if (configs.isEmpty()) {
            body.addView(new View(this), new LinearLayout.LayoutParams(1, 0, 1));
            TextView hint = label("Добавить конфиг", 15, textColor(), true); hint.setGravity(Gravity.CENTER);
            hint.setOnClickListener(v -> addMenu()); body.addView(hint, lp(-1, 40));
            body.addView(new View(this), new LinearLayout.LayoutParams(1, 0, 1));
        }
        for (int i = 0; i < configs.size(); i++) {
            final int index = i; Config config = configs.get(i); gap(body, 12);
            LinearLayout c = card(); LinearLayout top = row();
            TextView name = label(config.name, 16, textColor(), true); top.addView(name, new LinearLayout.LayoutParams(0, dp(35), 1));
            if (i == activeIndex) top.addView(label("●  выбран", 12, success(), false), lp(-2, 35));
            c.addView(top, lp(-1, 36));
            LinearLayout meta = row();
            meta.addView(label("TrustTunnel  ·  TOML", 12, dim(), false), new LinearLayout.LayoutParams(0, dp(25), 1));
            String pingText = pingResults.get(config);
            if (pingText != null) meta.addView(label(pingText, 12, dim(), false), lp(-2, 25));
            c.addView(meta, lp(-1, 25));
            LinearLayout actions = row();
            Button select = button("Выбрать", false); select.setOnClickListener(v -> { activeIndex = index; saveConfigs(); applyCurrentRules(); render(); });
            actions.addView(select, new LinearLayout.LayoutParams(0, dp(38), 1)); gapHorizontal(actions, 8);
            Button edit = button("Изменить", false); edit.setOnClickListener(v -> editConfig(index));
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
        if (configs.isEmpty()) { toast("Сначала добавьте конфиг"); return; }
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
        Matcher section = Pattern.compile("(?ms)^[ \\t]*\\[endpoint\\][ \\t]*\\r?\\n(.*?)(?=^[ \\t]*\\[|\\z)").matcher(toml);
        if (!section.find()) return "нет адреса";
        Matcher address = Pattern.compile("(?m)^\\s*addresses\\s*=\\s*\\[\\s*[\"']([^\"']+)[\"']").matcher(section.group(1));
        if (!address.find()) return "нет адреса";
        String target = address.group(1);
        int separator = target.lastIndexOf(':');
        if (separator <= 0) return "нет порта";
        String host = target.substring(0, separator);
        if (host.startsWith("[") && host.endsWith("]")) host = host.substring(1, host.length() - 1);
        try {
            int port = Integer.parseInt(target.substring(separator + 1));
            if (port < 1 || port > 65535) return "нет порта";
            long start = SystemClock.elapsedRealtime();
            try (Socket socket = new Socket()) { socket.connect(new InetSocketAddress(host, port), 2500); }
            return (SystemClock.elapsedRealtime() - start) + " мс";
        } catch (Exception e) { return "недоступен"; }
    }
    private void addMenu() {
        new AlertDialog.Builder(this).setTitle("Добавить конфиг")
                .setItems(new String[]{"Импортировать TOML-файл", "Вставить из буфера", "Создать вручную"}, (d, which) -> {
                    if (which == 0) {
                        Intent pick = new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("*/*").addCategory(Intent.CATEGORY_OPENABLE);
                        startActivityForResult(pick, PICK_CONFIG);
                    } else if (which == 1) {
                        ClipboardManager cm = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                        ClipData clip = cm.getPrimaryClip();
                        if (clip == null || clip.getItemCount() == 0) { toast("Буфер обмена пуст"); return; }
                        CharSequence value = clip.getItemAt(0).coerceToText(this);
                        String text = value == null ? "" : value.toString().trim();
                        if (DeepLinkImport.looksLikeLink(text)) importDeepLink(text);
                        else importToml("Из буфера", text);
                    } else editConfig(-1);
                }).show();
    }
    private void importDeepLink(String link) {
        final DeepLinkImport.Imported imported;
        try { imported = DeepLinkImport.parse(link); }
        catch (DeepLinkImport.ParseException e) { toast(e.getMessage()); return; }

        String message = "Добавить конфиг «" + imported.name + "»?\nСервер: " + imported.hostname;
        if (imported.skipVerification)
            message += "\nПроверка сертификата сервера отключена в этой ссылке.";
        new AlertDialog.Builder(this).setTitle("Импорт TrustTunnel")
                .setMessage(message).setNegativeButton("Отмена", null)
                .setPositiveButton("Добавить", (dialog, which) -> {
                    configs.add(new Config(imported.name, imported.toml));
                    if (configs.size() == 1) activeIndex = 0;
                    saveConfigs(); page = 1; render();
                }).show();
    }
    private void importToml(String name, String toml) {
        toml = stripBom(toml);
        if (!toml.contains("[endpoint]") || !toml.contains("[listener.tun]")) {
            toast("Ожидается TOML с секциями [endpoint] и [listener.tun]"); return;
        }
        configs.add(new Config(name, toml)); if (configs.size() == 1) activeIndex = 0;
        saveConfigs(); render();
    }
    private static String stripBom(String toml) {
        return toml.startsWith("\uFEFF") ? toml.substring(1) : toml;
    }
    private void editConfig(int index) {
        LinearLayout form = column(); form.setPadding(dp(20), dp(5), dp(20), 0);
        EditText name = new EditText(this); name.setSingleLine(true); name.setTextColor(textColor());
        name.setHint("Имя конфига"); name.setHintTextColor(faint());
        EditText raw = new EditText(this); raw.setTextColor(textColor()); raw.setHintTextColor(faint());
        raw.setHint("Вставьте полный TrustTunnel TOML"); raw.setMinLines(8); raw.setGravity(Gravity.TOP);
        raw.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_MULTI_LINE | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        if (index >= 0) { name.setText(configs.get(index).name); raw.setText(configs.get(index).toml); }
        form.addView(name, lp(-1, 54)); form.addView(raw, lp(-1, 230));
        ScrollView scroll = new ScrollView(this); scroll.addView(form);
        new AlertDialog.Builder(this).setTitle(index < 0 ? "Новый конфиг" : "Изменить конфиг")
                .setView(scroll).setNegativeButton("Отмена", null)
                .setPositiveButton("Сохранить", (dialog, which) -> {
                    String n = name.getText().toString().trim(), t = stripBom(raw.getText().toString().trim());
                    if (n.isEmpty()) { toast("Укажите имя конфига"); return; }
                    if (!t.contains("[endpoint]") || !t.contains("[listener.tun]")) { toast("Нужен полный TrustTunnel TOML"); return; }
                    if (index < 0) configs.add(new Config(n, t)); else configs.set(index, new Config(n, t));
                    saveConfigs(); render();
                }).show();
    }
    private void confirmRemove(int index) {
        new AlertDialog.Builder(this).setTitle("Удалить конфиг?").setMessage(configs.get(index).name)
                .setNegativeButton("Отмена", null).setPositiveButton("Удалить", (d, w) -> {
                    configs.remove(index); activeIndex = Math.max(0, Math.min(activeIndex, configs.size() - 1));
                    saveConfigs(); render();
                }).show();
    }
    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == PREPARE_VPN) {
            if (resultCode == RESULT_OK) startVpnWithPendingConfig();
            else { pendingConfig = null; toast("Разрешение на VPN не выдано"); }
            return;
        }
        if (requestCode != PICK_CONFIG || resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData(); if (uri == null) return;
        try (InputStream input = getContentResolver().openInputStream(uri); ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            if (input == null) return;
            byte[] bytes = new byte[8192]; int n; int total = 0;
            while ((n = input.read(bytes)) != -1) { total += n; if (total > 1024 * 1024) { toast("Файл слишком большой"); return; } out.write(bytes, 0, n); }
            String name = null;
            try (Cursor cursor = getContentResolver().query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
            } catch (Exception ignored) { }
            if (name == null || name.isEmpty()) name = uri.getLastPathSegment();
            if (name == null || name.isEmpty()) name = "Конфиг";
            int slash = name.lastIndexOf('/'); if (slash >= 0) name = name.substring(slash + 1);
            importToml(name, out.toString(StandardCharsets.UTF_8.name()));
        } catch (Exception e) { toast("Не удалось открыть файл"); }
    }

    private void split(LinearLayout body) {
        LinearLayout enabled = row();
        enabled.addView(label("Раздельное туннелирование", 14, textColor(), false),
                new LinearLayout.LayoutParams(0, dp(42), 1));
        Switch splitSwitch = new Switch(this); splitSwitch.setChecked(prefs.getBoolean("split_enabled", false));
        styleSwitch(splitSwitch);
        splitSwitch.setOnCheckedChangeListener((v, checked) -> {
            prefs.edit().putBoolean("split_enabled", checked).apply();
            applyCurrentRules();
        });
        enabled.addView(splitSwitch); body.addView(enabled, lp(-1, 42));
        boolean through = prefs.getBoolean("through", false);
        LinearLayout mode = row(); mode.addView(label("Режим", 14, textColor(), false),
                new LinearLayout.LayoutParams(0, dp(42), 1));
        TextView modeValue = label(through ? "Через VPN  ▾" : "В обход VPN  ▾", 14, dim(), false);
        mode.addView(modeValue, lp(-2, 42));
        mode.setOnClickListener(v -> new AlertDialog.Builder(this).setTitle("Режим")
                .setItems(new String[]{"В обход VPN", "Через VPN"}, (dialog, which) -> {
                    prefs.edit().putBoolean("through", which == 1).apply(); applyCurrentRules(); render();
                }).show());
        body.addView(mode, lp(-1, 42));
        section(body, "Профиль");
        LinearLayout profiles = row();
        TextView defaultChip = label("По умолчанию", 13, dark ? bg() : Color.WHITE, false);
        defaultChip.setGravity(Gravity.CENTER); defaultChip.setBackground(shape(accent(), 14));
        profiles.addView(defaultChip, lp(124, 28));
        body.addView(profiles, lp(-1, 30));
        gap(body, 14);
        LinearLayout domainHeader = row();
        domainHeader.addView(label(through ? "ПРАВИЛА — ЧЕРЕЗ VPN" : "ПРАВИЛА — В ОБХОД VPN", 11, faint(), true),
                new LinearLayout.LayoutParams(0, dp(29), 1));
        TextView clearDomains = label("Очистить", 12, accent(), false);
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
        domainInput.setHint("Домены или IP через запятую");
        domainInput.setPadding(dp(12), 0, dp(12), 0);
        domainInput.setBackground(shape(input(), 8));
        domainInput.setOnEditorActionListener((field, action, event) -> {
            addDomains(domainInput.getText().toString()); domainInput.setText(""); return true;
        });
        body.addView(domainInput, lp(-1, 36));
        gap(body, 15);
        LinearLayout appHeader = row();
        appHeader.addView(label(through ? "ПРИЛОЖЕНИЯ — ЧЕРЕЗ VPN" : "ПРИЛОЖЕНИЯ — В ОБХОД VPN", 11, faint(), true),
                new LinearLayout.LayoutParams(0, dp(29), 1));
        TextView chooseLink = label("Выбрать…", 12, accent(), false);
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
        TextView addApp = label("Выберите установленные приложения", 13, faint(), false);
        addApp.setPadding(dp(12), 0, dp(12), 0); addApp.setBackground(shape(input(), 8));
        addApp.setOnClickListener(v -> appPicker()); body.addView(addApp, lp(-1, 36));
    }
    private void addDomains(String inputText) {
        Set<String> rules = new HashSet<>(prefs.getStringSet("domains", Collections.emptySet()));
        boolean changed = false;
        for (String part : inputText.split(",")) {
            String rule = part.trim().toLowerCase();
            if (!rule.isEmpty() && rule.matches("[a-z0-9.*:_/\\-]+")) changed |= rules.add(rule);
        }
        if (changed) { prefs.edit().putStringSet("domains", rules).apply(); applyCurrentRules(); render(); }
    }
    private List<String> sorted(Set<String> set) { List<String> list = new ArrayList<>(set); Collections.sort(list); return list; }
    private void addDomain() {
        EditText field = new EditText(this); field.setTextColor(textColor()); field.setHint("example.com или 192.0.2.0/24");
        field.setHintTextColor(faint()); field.setSingleLine(true); field.setPadding(dp(22), 0, dp(22), 0);
        new AlertDialog.Builder(this).setTitle("Добавить правило").setView(field).setNegativeButton("Отмена", null)
                .setPositiveButton("Добавить", (d, w) -> {
                    String value = field.getText().toString().trim().toLowerCase();
                    if (!value.matches("[a-z0-9.*:_/\\-]+") || value.isEmpty()) { toast("Некорректный домен или адрес"); return; }
                    Set<String> rules = new HashSet<>(prefs.getStringSet("domains", Collections.emptySet()));
                    rules.add(value); prefs.edit().putStringSet("domains", rules).apply(); applyCurrentRules(); render();
                }).show();
    }
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
        new AlertDialog.Builder(this).setTitle("Выбрать приложения").setView(scroll)
                .setNegativeButton("Отмена", null).setPositiveButton("Готово", (d, w) -> {
                    prefs.edit().putStringSet("apps", chosen).apply(); applyCurrentRules(); render();
                }).show();
    }

    private void settings(LinearLayout body) {
        section(body, "Общие");
        LinearLayout appearance = row();
        appearance.addView(label("Тема", 14, textColor(), false), new LinearLayout.LayoutParams(0, dp(42), 1));
        appearance.addView(label(dark ? "Тёмная  ▾" : "Светлая  ▾", 14, dim(), false), lp(-2, 42));
        appearance.setOnClickListener(v -> new AlertDialog.Builder(this).setTitle("Тема")
                .setItems(new String[]{"Светлая", "Тёмная"}, (d, which) -> {
                    dark = which == 1; prefs.edit().putBoolean("dark", dark).apply(); render();
                }).show());
        body.addView(appearance, lp(-1, 42)); separator(body);
        addSwitch(body, "Подключать при запуске", "", "auto_connect", false);
        section(body, "Безопасность");
        LinearLayout systemVpn = row();
        systemVpn.addView(label("Параметры VPN Android", 14, textColor(), false),
                new LinearLayout.LayoutParams(0, dp(48), 1));
        systemVpn.addView(label("Открыть", 13, accent(), false), lp(-2, 48));
        systemVpn.setOnClickListener(v -> {
            try { startActivity(new Intent(Settings.ACTION_VPN_SETTINGS)); }
            catch (Exception e) { toast("Настройки VPN недоступны"); }
        });
        body.addView(systemVpn, lp(-1, 48));
        body.addView(label("Постоянный VPN и блокировку без соединения можно включить в системных настройках.",
                12, dim(), false), lp(-1, 46));
        section(body, "Журнал");
        addSwitch(body, "Включить журнал", "", "logging", true);
        section(body, "О приложении");
        body.addView(label("FreeTunnel для Android", 14, textColor(), false), lp(-1, 42));
        body.addView(label("Версия 0.3", 12, dim(), false), lp(-1, 28));
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
        sw.setOnCheckedChangeListener((v, enabled) -> prefs.edit().putBoolean(key, enabled).apply());
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
        TextView clear = label("Очистить", 13, accent(), false);
        actions.addView(clear, new LinearLayout.LayoutParams(0, dp(34), 1));
        TextView copy = label("Копировать", 13, accent(), false); actions.addView(copy, lp(-2, 34));
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
                toast(contents.isEmpty() ? "Журнал пуст" : "Журнал скопирован");
            } catch (Exception e) { toast("Не удалось прочитать журнал"); }
        });
        body.addView(actions, lp(-1, 34)); gap(body, 8);
        FrameLayout panel = new FrameLayout(this); panel.setBackground(shape(surface(), 8));
        String logText = "Подключите VPN, чтобы увидеть журнал";
        java.io.File logFile = new java.io.File(getFilesDir(), "freetunnel.log");
        if (logFile.exists()) {
            try { logText = new String(java.nio.file.Files.readAllBytes(logFile.toPath()), StandardCharsets.UTF_8).trim(); }
            catch (Exception ignored) { logText = "Не удалось прочитать журнал"; }
        }
        TextView placeholder = label(logText, 12, logFile.exists() ? dim() : faint(), false);
        placeholder.setGravity(Gravity.TOP | Gravity.LEFT);
        placeholder.setPadding(dp(12), dp(12), dp(12), dp(12));
        placeholder.setTextIsSelectable(true);
        panel.addView(placeholder, new FrameLayout.LayoutParams(-1, -1));
        body.addView(panel, new LinearLayout.LayoutParams(-1, 0, 1));
    }
    private void pageTitle(LinearLayout body, String title, String subtitle) {
        TextView h = label(title, 22, textColor(), true); body.addView(h, lp(-1, 34));
        TextView s = label(subtitle, 13, dim(), false); body.addView(s, lp(-1, 31)); gap(body, 14);
    }
    private void loadConfigs() {
        try {
            JSONArray array = new JSONArray(ConfigStore.read(prefs));
            for (int i = 0; i < array.length(); i++) {
                JSONObject o = array.getJSONObject(i); configs.add(new Config(o.getString("name"), o.getString("toml")));
            }
        } catch (Exception ignored) { configs.clear(); }
    }
    private void saveConfigs() {
        JSONArray array = new JSONArray();
        for (Config config : configs) {
            JSONObject o = new JSONObject();
            try { o.put("name", config.name); o.put("toml", config.toml); array.put(o); } catch (Exception ignored) { }
        }
        if (!ConfigStore.write(prefs, array.toString())) toast("Не удалось защитить конфиг в хранилище Android");
        prefs.edit().putInt("active", activeIndex).apply();
    }
}
