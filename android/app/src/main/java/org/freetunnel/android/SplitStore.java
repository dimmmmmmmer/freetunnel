package org.freetunnel.android;

import org.json.JSONException;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;
import java.util.function.Supplier;

/**
 * Where the {@link SplitProfiles} are kept: the preferences' "split_profiles", as JSON, and the first time anything wants
 * them, made out of what is there.
 *
 * <ul>
 * <li><b>An update.</b> Version 1.0.0 kept one list of domains ("domains") and one of applications ("apps"), and
 * whether split tunnelling was on ("split_enabled") and which way it went ("through"). With no "split_profiles"
 * yet, Default is made of those lists, sorted, and is the profile being edited.</li>
 * <li><b>A new installation</b>: no configs, and none of those settings. Split tunnelling is on and Default holds the
 * recommended list for Russia, as the desktop's does. Only a caller that gives the list asks for this; the service,
 * which has configs by the time it runs, does not, and an empty preferences file read by it is an empty one.</li>
 * <li><b>The old lists stay</b>, written together with every save as a copy of the lists of the profile the config that
 * is selected uses ({@link SplitProfiles#mirrored}), so that going back to 1.0.0 finds a working set.</li>
 * </ul>
 *
 * <p>Every read and every change happens under one lock and a change is one {@code commit()}: the app and the VPN
 * service both save here, from different threads, and the one that reads the list to change it must not do so
 * while the other is half way through changing it. The preferences are reached through {@link Backing}, so that
 * this is tested on a plain JVM (SplitStoreTest).
 */
final class SplitStore {
    static final String KEY = "split_profiles";
    /** Set when rules the core could not use were dropped from a list: the Split page says so (legacy_rules_removed). */
    static final String RULES_MIGRATED = "rules_migrated";
    private static final String OLD_DOMAINS = "domains";
    private static final String OLD_APPS = "apps";
    private static final Object LOCK = new Object();
    // What a first start can tell a new installation by: none of these has ever been written.
    private static final String[] BEFORE = {KEY, OLD_DOMAINS, OLD_APPS, "split_enabled", "through", "configs_encrypted", "configs"};

    private SplitStore() { }

    /** The preferences, as much of them as this needs. A commit is all of its changes or none. */
    interface Backing {
        boolean contains(String key);
        String getString(String key);
        Set<String> getStringSet(String key);
        /** Writes each value (a String, a Boolean or a Set of Strings), all together or none. */
        boolean commit(Map<String, Object> writes);
    }

    // Reading.

    /**
     * The profiles as saved. If none are yet, they are made and saved now (see the class comment); with
     * {@code recommended} null the preferences never are those of a new installation.
     */
    static SplitProfiles load(Backing backing, Supplier<List<String>> recommended) {
        synchronized (LOCK) { return loadLocked(backing, recommended); }
    }

    private static SplitProfiles loadLocked(Backing backing, Supplier<List<String>> recommended) {
        String saved = backing.getString(KEY);
        if (saved != null) {
            try { return SplitProfiles.fromJson(saved); }
            catch (JSONException | RuntimeException unreadable) {
                // Nothing to build on. The old lists are written with every save as a copy of the profile in use, which makes
                // them the best there is; and the file is not left as it is to fail again.
                SplitProfiles rebuilt = SplitProfiles.migrated(setOf(backing, OLD_DOMAINS), setOf(backing, OLD_APPS));
                save(backing, rebuilt, Collections.<String, Object>emptyMap());
                return rebuilt;
            }
        }
        Map<String, Object> extra = new HashMap<>();
        SplitProfiles made;
        if (recommended != null && isNewInstallation(backing)) {
            made = SplitProfiles.seeded(recommended.get());
            extra.put("split_enabled", Boolean.TRUE);
        } else {
            made = SplitProfiles.migrated(setOf(backing, OLD_DOMAINS), setOf(backing, OLD_APPS));
        }
        // If this fails the next read makes it again, and gets the same.
        save(backing, made, extra);
        return made;
    }

    private static boolean isNewInstallation(Backing backing) {
        for (String key : BEFORE) if (backing.contains(key)) return false;
        return true;
    }

    private static Set<String> setOf(Backing backing, String key) {
        Set<String> set = backing.getStringSet(key);
        return set == null ? Collections.<String>emptySet() : set;
    }

    // Changing.

    /**
     * Runs {@code change} on the profiles as saved, and saves what it leaves. The old lists go with them. True if
     * nothing needed saving or it was saved; false if the preferences refused it.
     */
    static boolean edit(Backing backing, Supplier<List<String>> recommended, Consumer<SplitProfiles> change) {
        synchronized (LOCK) {
            SplitProfiles profiles = loadLocked(backing, recommended);
            String before = profiles.toJson();
            change.accept(profiles);
            if (before.equals(profiles.toJson())) return true;
            return save(backing, profiles, Collections.<String, Object>emptyMap());
        }
    }

    private static boolean save(Backing backing, SplitProfiles profiles, Map<String, Object> extra) {
        Map<String, Object> writes = new HashMap<>(extra);
        writes.put(KEY, profiles.toJson());
        writes.put(OLD_DOMAINS, new HashSet<>(profiles.domains(profiles.mirrored())));
        writes.put(OLD_APPS, new HashSet<>(profiles.apps(profiles.mirrored())));
        return backing.commit(writes);
    }

    // What the VPN is given.

    /** The rules of one profile, as the core is given them. */
    static final class Rules {
        /** The domain rules the core takes, in the profile's order. */
        final List<String> domains;
        /** The packages listed, installed or not. */
        final List<String> apps;
        Rules(List<String> domains, List<String> apps) { this.domains = domains; this.apps = apps; }
    }

    /**
     * The rules of a profile (Default for a name that is none) for a session. Domain rules that the core cannot
     * use, which an older preview let be saved, are taken out of the profile for good, and RULES_MIGRATED is
     * set, so that the Split page says they went. The applications are as listed: which of them are installed is
     * for the one that knows the device.
     */
    static Rules rules(Backing backing, Supplier<List<String>> recommended, String profile) {
        synchronized (LOCK) {
            SplitProfiles profiles = loadLocked(backing, recommended);
            String name = profiles.effective(profile);
            if (profiles.dropUnusableDomains(name) > 0) {
                Map<String, Object> flag = new HashMap<>();
                flag.put(RULES_MIGRATED, Boolean.TRUE);
                save(backing, profiles, flag);
            }
            return new Rules(new ArrayList<>(profiles.domains(name)), new ArrayList<>(profiles.apps(name)));
        }
    }
}
