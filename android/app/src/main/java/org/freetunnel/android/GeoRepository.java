package org.freetunnel.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import java.nio.file.Path;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArraySet;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Process-wide cache updates shared by the phone/TV screens and the running VPN. */
final class GeoRepository {
    static final String AUTO_UPDATE = "geo_auto_update";
    interface Listener { void changed(String key, boolean rulesChanged); }
    static final class State {
        final GeoCache.Result cached;
        final boolean busy;
        final String error;
        State(GeoCache.Result cached, boolean busy, String error) {
            this.cached = cached; this.busy = busy; this.error = error;
        }
    }
    private static GeoRepository instance;
    static synchronized GeoRepository get(Context context) {
        if (instance == null) instance = new GeoRepository(context.getApplicationContext());
        return instance;
    }
    static Path directory(Context context) { return context.getFilesDir().toPath().resolve("geo"); }
    static String key(GeoDefinition definition) {
        return definition.source + '\0' + definition.kind + ':' + definition.bypass;
    }
    private final Context context;
    private final SharedPreferences prefs;
    private final GeoCache cache;
    private final GeoDownloader downloader;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread thread = new Thread(r, "freetunnel-geo"); thread.setDaemon(true); return thread;
    });
    private final Map<String, State> states = new ConcurrentHashMap<>();
    private final Map<String, Long> attempts = new ConcurrentHashMap<>();
    private final Set<String> queued = ConcurrentHashMap.newKeySet();
    private final Set<Listener> listeners = new CopyOnWriteArraySet<>();
    private GeoRepository(Context context) {
        this.context = context;
        prefs = context.getSharedPreferences("freetunnel", Context.MODE_PRIVATE);
        cache = new GeoCache(directory(context));
        downloader = new GeoDownloader(directory(context));
        worker.scheduleAtFixedRate(this::refreshEnabled, 0, 1, TimeUnit.HOURS);
    }
    void observe(Listener listener) { listeners.add(listener); }
    void removeObserver(Listener listener) { listeners.remove(listener); }
    State state(GeoDefinition definition) {
        State state = states.get(key(definition));
        return state == null ? new State(null, false, "") : state;
    }
    void refreshEnabled() {
        SplitProfiles profiles = SplitStore.load(new SplitPrefs(prefs), RussiaList.seed(context));
        for (String profile : profiles.names())
            for (GeoDefinition definition : profiles.geoLists(profile))
                if (definition.enabled) refresh(definition, false);
    }
    void refresh(GeoDefinition definition, boolean force) {
        String identity = key(definition);
        if (!queued.add(identity)) return;
        worker.execute(() -> {
            State old = state(definition);
            GeoCache.Result cached = old.cached;
            if (cached == null) {
                try { cached = cache.load(definition.source, definition.kind, definition.bypass); }
                catch (Exception missing) { /* No valid cached rules yet. */ }
                if (cached != null) publish(identity, new State(cached, false, ""), true);
            }
            long now = System.currentTimeMillis();
            Long attempted = attempts.get(identity);
            boolean due = cached == null || now - cached.updatedMillis >= TimeUnit.DAYS.toMillis(1);
            boolean retry = attempted == null || now - attempted >= TimeUnit.HOURS.toMillis(1);
            if (!force && (!due || !retry || (cached != null && !prefs.getBoolean(AUTO_UPDATE, true)))) {
                queued.remove(identity); return;
            }
            attempts.put(identity, now);
            publish(identity, new State(cached, true, ""), false);
            try {
                GeoCache.Result downloaded = downloader.fetch(definition);
                boolean changed = cached == null || !cached.entries.equals(downloaded.entries);
                publish(identity, new State(downloaded, false, ""), changed);
            } catch (Exception error) {
                publish(identity, new State(cached, false, error.getMessage() == null ? "Download failed" : error.getMessage()), false);
            } finally {
                queued.remove(identity);
            }
        });
    }
    private void publish(String identity, State state, boolean changed) {
        states.put(identity, state);
        main.post(() -> { for (Listener listener : listeners) listener.changed(identity, changed); });
    }
    // Called by the VPN worker too, so Always-on works without ever opening the UI.
    static List<String> cachedEntries(Context context, GeoDefinition definition) {
        if (!definition.enabled) return Collections.emptyList();
        try { return new GeoCache(directory(context)).load(definition.source, definition.kind, definition.bypass).entries; }
        catch (Exception missing) { return Collections.emptyList(); }
    }
}
