package org.freetunnel.android;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * The split-tunnel profiles, as the desktop keeps them (src/core/AppSettings.cpp, src/app/BackendSplit.cpp): a name each,
 * and for each name the domain rules and the applications it lists, in the order they were added. "Default" is always
 * there, always first, and cannot be deleted; the others follow in the order they were made. A config uses one
 * profile (its own "profile" in the saved configs), and the Split page edits one, which has nothing to do with which
 * a config uses, so that editing another profile never restarts a session.
 *
 * <p>No Android in here, so the rules of it are tested on a plain JVM (SplitProfilesTest). SplitStore keeps it in the
 * preferences, as the JSON of {@link #toJson}.
 *
 * <p>One more name is kept: the profile the old global lists are a copy of. Version 1.0.0 had a single list of
 * domains and one of applications, "domains" and "apps" in the preferences, and still reads those, so that going back to
 * it works. They are written as a copy of the profile of the config that is selected, and this is which one that is.
 */
final class SplitProfiles {
    /** The built-in profile's stored name; the screen shows it in the language of the app. */
    static final String DEFAULT = "Default";
    /** A name has at most this many characters, as the desktop's name field allows. */
    static final int MAX_NAME = 16;
    private static final int VERSION = 1;

    private final List<String> order = new ArrayList<>();
    private final Map<String, List<String>> domains = new HashMap<>();
    private final Map<String, List<String>> apps = new HashMap<>();
    private String edited = DEFAULT;
    private String mirrored = DEFAULT;

    private SplitProfiles() {
        order.add(DEFAULT);
        domains.put(DEFAULT, new ArrayList<String>());
        apps.put(DEFAULT, new ArrayList<String>());
    }

    // Starting points.

    /** Only Default, with nothing in it. */
    static SplitProfiles empty() { return new SplitProfiles(); }

    /** A new installation: Default holds the recommended list, as it does on the desktop. */
    static SplitProfiles seeded(Collection<String> recommended) {
        SplitProfiles p = new SplitProfiles();
        p.addRecommended(DEFAULT, recommended);
        return p;
    }

    /**
     * What the first start of this version makes of the one list of domains and the one of applications that 1.0.0
     * kept: Default holds them, sorted, and is the profile being edited.
     */
    static SplitProfiles migrated(Collection<String> oldDomains, Collection<String> oldApps) {
        SplitProfiles p = new SplitProfiles();
        List<String> sortedDomains = new ArrayList<>(oldDomains);
        Collections.sort(sortedDomains);
        for (String rule : sortedDomains) if (!rule.trim().isEmpty() && !p.domains.get(DEFAULT).contains(rule)) p.domains.get(DEFAULT).add(rule);
        List<String> sortedApps = new ArrayList<>(oldApps);
        Collections.sort(sortedApps);
        for (String pkg : sortedApps) if (!pkg.trim().isEmpty() && !p.apps.get(DEFAULT).contains(pkg)) p.apps.get(DEFAULT).add(pkg);
        return p;
    }

    // Names.

    /** Every profile's name, in order: Default, then the others as they were made. */
    List<String> names() { return Collections.unmodifiableList(order); }
    boolean has(String name) { return name != null && domains.containsKey(name); }
    /** The profile the Split page is editing. */
    String edited() { return edited; }
    /** The profile whose lists the old global "domains" and "apps" are a copy of. */
    String mirrored() { return mirrored; }
    /** The name itself, or Default for one that is gone: what a config that names a deleted profile uses. */
    String effective(String name) { return has(name) ? name : DEFAULT; }

    /** What adding a name came to. */
    enum NameStatus {
        /** The profile was made, and is the one being edited. */
        ADDED,
        /** Nothing, or only spaces, was typed; nothing is said. */
        EMPTY,
        /** There is a profile of exactly this name; nothing is said, as on the desktop. */
        SAME,
        /** There is one that differs only in letter case; its name is in {@code existing}. */
        CASE_CLASH,
        /** Longer than MAX_NAME, which the field does not let be typed. */
        TOO_LONG
    }

    static final class NameResult {
        final NameStatus status;
        /** For CASE_CLASH, the profile it clashes with, as that is named. */
        final String existing;
        NameResult(NameStatus status, String existing) { this.status = status; this.existing = existing; }
    }

    /**
     * Makes a profile, empty, and makes it the one being edited. A name that differs from another only in letter case
     * is refused: a profile's name is the key its rules are kept under, and on Windows the desktop keeps them in
     * a registry where case is not distinguishing, so "Work" and "work" would be one list under two names.
     */
    NameResult add(String raw) {
        String name = raw == null ? "" : raw.trim();
        if (name.isEmpty()) return new NameResult(NameStatus.EMPTY, null);
        if (name.length() > MAX_NAME) return new NameResult(NameStatus.TOO_LONG, null);
        if (domains.containsKey(name)) return new NameResult(NameStatus.SAME, name);
        for (String existing : order) {
            if (existing.equalsIgnoreCase(name)) return new NameResult(NameStatus.CASE_CLASH, existing);
        }
        order.add(name);
        domains.put(name, new ArrayList<String>());
        apps.put(name, new ArrayList<String>());
        edited = name;
        return new NameResult(NameStatus.ADDED, name);
    }

    /** The Split page edits this one from now on. Nothing else changes. True if it was another. */
    boolean select(String name) {
        if (!has(name) || name.equals(edited)) return false;
        edited = name;
        return true;
    }

    /**
     * Deletes a profile and its lists. Default cannot be. The configs that used it use Default from now on, which the
     * caller sees to (see {@link #afterRemoval}).
     */
    boolean remove(String name) {
        if (DEFAULT.equals(name) || !has(name)) return false;
        order.remove(name);
        domains.remove(name);
        apps.remove(name);
        if (edited.equals(name)) edited = DEFAULT;
        if (mirrored.equals(name)) mirrored = DEFAULT;
        return true;
    }

    /** The profile a config that used {@code assigned} uses once {@code removed} is deleted. */
    static String afterRemoval(String assigned, String removed) {
        return assigned == null || assigned.equals(removed) ? DEFAULT : assigned;
    }

    /** Makes this the profile the old global lists copy; one that is not there is Default. */
    void setMirrored(String name) { mirrored = effective(name); }

    // Domains.

    /** The domain rules of a profile, in the order they were added; nothing for a name that is not one. */
    List<String> domains(String name) {
        List<String> list = domains.get(name);
        return list == null ? Collections.<String>emptyList() : Collections.unmodifiableList(list);
    }

    /** What typing domains came to: the rules added, and the words that are none. */
    static final class Entered {
        final List<String> added;
        final List<String> refused;
        Entered(List<String> added, List<String> refused) { this.added = added; this.refused = refused; }
    }

    /**
     * Adds what was typed or pasted: words separated by spaces, commas, semicolons or lines. A word that is a rule goes in,
     * lowercased and without a dot at its end, as SplitRules.normalize writes it, unless the profile has it already
     * (in another spelling too: ".example.com" and "*.example.com" are one rule). A word that is not one is in
     * {@code refused}, as it was typed, and nothing else is held back for it.
     */
    Entered addDomains(String profile, String raw) {
        List<String> added = new ArrayList<>();
        List<String> refused = new ArrayList<>();
        List<String> list = domains.get(profile);
        if (list == null) return new Entered(added, refused);
        Set<String> keys = keysOf(list);
        for (String word : SplitRules.INSTANCE.tokens(raw == null ? "" : raw)) {
            String core = SplitRules.INSTANCE.core(word);
            if (core == null) { refused.add(word); continue; }
            String rule = word.toLowerCase(Locale.ROOT);
            if (rule.endsWith(".")) rule = rule.substring(0, rule.length() - 1);
            if (keys.add(core)) { list.add(rule); added.add(rule); }
        }
        return new Entered(added, refused);
    }

    /** Adds the recommended rules the profile does not have, at its end. How many were new. */
    int addRecommended(String profile, Collection<String> rules) {
        List<String> list = domains.get(profile);
        if (list == null) return 0;
        Set<String> keys = keysOf(list);
        int count = 0;
        for (String rule : rules) {
            String core = SplitRules.INSTANCE.core(rule);
            if (core == null || !keys.add(core)) continue;
            list.add(rule.trim());
            count++;
        }
        return count;
    }

    boolean removeDomain(String profile, int index) {
        List<String> list = domains.get(profile);
        if (list == null || index < 0 || index >= list.size()) return false;
        list.remove(index);
        return true;
    }

    /**
     * Takes out the rules the core cannot use, which an older preview let be saved ("intranet", "example.com:443").
     * The rest stay as they are, in their places. How many went.
     */
    int dropUnusableDomains(String profile) {
        List<String> list = domains.get(profile);
        if (list == null) return 0;
        int before = list.size();
        for (int i = list.size() - 1; i >= 0; i--) if (SplitRules.INSTANCE.core(list.get(i)) == null) list.remove(i);
        return before - list.size();
    }

    boolean clearDomains(String profile) {
        List<String> list = domains.get(profile);
        if (list == null || list.isEmpty()) return false;
        list.clear();
        return true;
    }

    // Two spellings of one rule are one: the key is the form the core is given. A rule the core would not take (an old
    // one, waiting to be dropped) has none, and stands in the way of no other.
    private static Set<String> keysOf(List<String> rules) {
        Set<String> keys = new HashSet<>();
        for (String rule : rules) {
            String core = SplitRules.INSTANCE.core(rule);
            if (core != null) keys.add(core);
        }
        return keys;
    }

    // Applications.

    /** The packages of a profile, in the order they were added. A package that is no longer installed stays until it is removed. */
    List<String> apps(String name) {
        List<String> list = apps.get(name);
        return list == null ? Collections.<String>emptyList() : Collections.unmodifiableList(list);
    }

    /** False if it is there already, or is nothing. */
    boolean addApp(String profile, String pkg) {
        List<String> list = apps.get(profile);
        if (list == null || pkg == null || pkg.trim().isEmpty() || list.contains(pkg)) return false;
        list.add(pkg);
        return true;
    }

    boolean removeApp(String profile, int index) {
        List<String> list = apps.get(profile);
        if (list == null || index < 0 || index >= list.size()) return false;
        list.remove(index);
        return true;
    }

    boolean clearApps(String profile) {
        List<String> list = apps.get(profile);
        if (list == null || list.isEmpty()) return false;
        list.clear();
        return true;
    }

    // What a refusal says.

    /** Why words were refused: the plain "not a valid domain or subnet", or one of the two reasons that deserve saying. */
    enum Refusal { PLAIN, WILDCARD_ON_ADDRESS, EVERY_ADDRESS }

    /**
     * As the desktop words it (refusedRulesMessage): a wildcard on an address is worth more than "not valid", for the address
     * itself is fine, and so is a subnet of every address, which "Bypass VPN" would take wholly out of the tunnel.
     * The first reason wins when both are among the words.
     */
    static Refusal refusal(Collection<String> refused) {
        for (String word : refused) if (isWildcardOnAddress(word)) return Refusal.WILDCARD_ON_ADDRESS;
        for (String word : refused) if (isEveryAddress(word)) return Refusal.EVERY_ADDRESS;
        return Refusal.PLAIN;
    }

    // "*." or a dot before an address or a subnet that is not every address.
    private static boolean isWildcardOnAddress(String word) {
        int prefix = word.startsWith("*.") ? 2 : word.startsWith(".") ? 1 : 0;
        if (prefix == 0) return false;
        String subnet = SplitRules.INSTANCE.subnet(word.substring(prefix));
        return subnet != null && !subnet.endsWith("/0");
    }

    // A subnet written with a prefix of 0: 0.0.0.0/0, ::/0.
    private static boolean isEveryAddress(String word) {
        // A bare address is a subnet of one (/32, /128), so only one written with a prefix of 0 ends so.
        String subnet = SplitRules.INSTANCE.subnet(word);
        return subnet != null && subnet.endsWith("/0");
    }

    // Saved form.

    /**
     * The profiles as JSON: {"v":1,"edit":…,"mirror":…,"profiles":[{"name":…,"domains":[…],"apps":[…]},…]}. The
     * profiles are an array and not an object, because an object's keys have no order a reader has to keep.
     */
    String toJson() {
        try {
            JSONArray profiles = new JSONArray();
            for (String name : order) {
                JSONObject one = new JSONObject();
                one.put("name", name);
                one.put("domains", array(domains.get(name)));
                one.put("apps", array(apps.get(name)));
                profiles.put(one);
            }
            JSONObject root = new JSONObject();
            root.put("v", VERSION);
            root.put("edit", edited);
            root.put("mirror", mirrored);
            root.put("profiles", profiles);
            return root.toString();
        } catch (JSONException e) {
            // Strings and arrays of them: nothing here can fail.
            throw new IllegalStateException(e);
        }
    }

    private static JSONArray array(List<String> items) {
        JSONArray out = new JSONArray();
        for (String item : items) out.put(item);
        return out;
    }

    /**
     * Reads what {@link #toJson} wrote. Whatever it holds is made to fit what a profile list is, rather than refused: a
     * name that is there twice (in any letter case) keeps its first, Default is added or put first, a rule or a
     * package that is empty or listed twice is dropped, and the profile being edited, or mirrored, that is not there
     * is Default. Text that is no JSON, or has no list of profiles, throws.
     */
    static SplitProfiles fromJson(String json) throws JSONException {
        JSONObject root = new JSONObject(json);
        JSONArray profiles = root.getJSONArray("profiles");
        SplitProfiles p = new SplitProfiles();
        boolean defaultSeen = false;
        for (int i = 0; i < profiles.length(); i++) {
            JSONObject one = profiles.optJSONObject(i);
            if (one == null) continue;
            String name = one.optString("name", "").trim();
            if (name.isEmpty()) continue;
            String target = null;
            if (DEFAULT.equals(name)) {
                if (defaultSeen) continue;
                defaultSeen = true;
                target = DEFAULT;
            } else {
                boolean taken = false;
                for (String existing : p.order) if (existing.equalsIgnoreCase(name)) taken = true;
                if (taken) continue;
                p.order.add(name);
                p.domains.put(name, new ArrayList<String>());
                p.apps.put(name, new ArrayList<String>());
                target = name;
            }
            fill(p.domains.get(target), one.optJSONArray("domains"));
            fill(p.apps.get(target), one.optJSONArray("apps"));
        }
        p.edited = p.effective(root.optString("edit", DEFAULT));
        p.mirrored = p.effective(root.optString("mirror", DEFAULT));
        return p;
    }

    private static void fill(List<String> into, JSONArray from) {
        if (from == null) return;
        for (int i = 0; i < from.length(); i++) {
            String item = from.optString(i, "").trim();
            if (!item.isEmpty() && !into.contains(item)) into.add(item);
        }
    }
}
