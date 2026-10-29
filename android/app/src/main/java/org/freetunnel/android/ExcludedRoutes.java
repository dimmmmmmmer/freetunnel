package org.freetunnel.android;

import android.content.Context;

import org.json.JSONArray;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/**
 * Settings > Excluded routes (qml/pages/SettingsPage.qml, Backend::addExcludedRoute and friends): subnets that bypass
 * the tunnel at the routing level, whatever the split rules say. They are the preference "excluded_routes", a JSON array
 * of subnets in the order they were listed; where it is absent, the list is the defaults, which are
 * data/split/default-excluded-routes.txt, one file for this app and the desktop (the copySharedData task of
 * app/build.gradle copies it into the assets, and cmake/SharedData.cmake compiles it into the desktop). A list that was
 * cleared is "[]", not absent.
 *
 * <p>A subnet is kept as {@link SplitRules#subnet} writes it, "10.0.0.0/8", a bare address being a /32 (or /128): the plan
 * needs the prefix, which the core's TOML has to carry. A /0 is every address of its kind, and excluded it would take
 * all of that traffic out of the tunnel while the app says Connected, so it is refused where it is typed and left out
 * wherever it is read.
 *
 * <p>All of this is plain Java, so that it is tested on a plain JVM (ExcludedRoutesTest).
 */
final class ExcludedRoutes {
    static final String KEY = "excluded_routes";
    static final String ASSET = "split/default-excluded-routes.txt";
    private static volatile List<String> cached;

    private ExcludedRoutes() { }

    // The defaults.

    /** The subnets in a file's text, in its order (a blank line and a line that begins with # are none). */
    static List<String> parse(String text) { return DataFile.parse(text); }

    /**
     * The defaults, from the assets. Read once. Where the assets cannot be read, which a real APK never is
     * (scripts/check-apk-data.sh), it is none: the VPN then excludes only what its config does.
     */
    static List<String> defaults(Context context) {
        List<String> list = cached;
        if (list != null) return list;
        try {
            list = sanitized(DataFile.read(context, ASSET));
        } catch (IOException unreadable) {
            return Collections.emptyList();
        }
        cached = list;
        return list;
    }

    // One subnet.

    /** The subnet as it is kept, or null if it is none or a /0. */
    static String normalize(String raw) {
        String subnet = raw == null ? null : SplitRules.INSTANCE.subnet(raw);
        return subnet == null || prefix(subnet) == 0 ? null : subnet;
    }

    /** True for a /0 written correctly: every address, which is refused for that, not for a typo. */
    static boolean isEveryAddress(String raw) {
        String subnet = raw == null ? null : SplitRules.INSTANCE.subnet(raw);
        return subnet != null && prefix(subnet) == 0;
    }

    private static int prefix(String subnet) { return Integer.parseInt(subnet.substring(subnet.lastIndexOf('/') + 1)); }

    // The list.

    /** Each of these as it is kept, in order, those that are no subnet or a /0 left out, and each once. */
    static List<String> sanitized(List<String> routes) {
        List<String> list = new ArrayList<>();
        for (String route : routes) {
            String subnet = normalize(route);
            if (subnet != null && !list.contains(subnet)) list.add(subnet);
        }
        return list;
    }

    /**
     * The saved list: the preference's JSON, or the defaults where there is none or it cannot be read. What is in it that is
     * no subnet is left out, as a /0 is.
     */
    static List<String> load(String saved, List<String> defaults) {
        if (saved == null) return new ArrayList<>(defaults);
        try {
            JSONArray array = new JSONArray(saved);
            List<String> routes = new ArrayList<>();
            for (int i = 0; i < array.length(); i++) {
                Object item = array.opt(i);
                if (item instanceof String) routes.add((String) item);
            }
            return sanitized(routes);
        } catch (org.json.JSONException | RuntimeException unreadable) {
            return new ArrayList<>(defaults);
        }
    }

    static String toJson(List<String> routes) {
        JSONArray array = new JSONArray();
        for (String route : routes) array.put(route);
        return array.toString();
    }

    /** What typing into the field did: the list as it is now, the subnets it added, and the words that were refused. */
    static final class Entered {
        final List<String> routes;
        final List<String> added;
        final List<String> refused;

        Entered(List<String> routes, List<String> added, List<String> refused) {
            this.routes = routes;
            this.added = added;
            this.refused = refused;
        }

        /** The refusal that says why: one of the words was a /0. */
        boolean refusedEveryAddress() {
            for (String word : refused) if (isEveryAddress(word)) return true;
            return false;
        }
    }

    /**
     * Adds what was typed to {@code current}: subnets, separated by spaces, commas or semicolons, as the desktop takes a list
     * pasted at once. The ones that are not subnets are named, a /0 among them for the reason it is refused for; one the list
     * has already is neither added nor refused.
     */
    static Entered add(List<String> current, String typed) {
        List<String> routes = new ArrayList<>(current);
        List<String> added = new ArrayList<>();
        List<String> refused = new ArrayList<>();
        for (String word : SplitRules.INSTANCE.tokens(typed == null ? "" : typed)) {
            String route = normalize(word);
            if (route == null) {
                if (!refused.contains(word)) refused.add(word);
            } else if (!routes.contains(route)) {
                routes.add(route);
                added.add(route);
            }
        }
        return new Entered(routes, added, refused);
    }

    /** The list without the route at this index; the same list for an index that is none. */
    static List<String> removeAt(List<String> routes, int index) {
        List<String> list = new ArrayList<>(routes);
        if (index >= 0 && index < list.size()) list.remove(index);
        return list;
    }

    // What the VPN is given.

    /**
     * What goes into the config's listener.tun.excluded_routes: the routes it has, as they are, and then these, each that is a
     * subnet and not a /0 and that the config does not exclude already. IPv6 ones only if the session has IPv6, as the VPN
     * service only gives the core those.
     */
    static List<String> forPlan(List<String> own, List<String> saved, boolean ipv6) {
        List<String> all = new ArrayList<>(own);
        List<String> have = new ArrayList<>();
        for (String route : own) {
            String subnet = SplitRules.INSTANCE.subnet(route);
            have.add(subnet == null ? route : subnet);
        }
        for (String route : saved) {
            String subnet = normalize(route);
            if (subnet == null || !ipv6 && subnet.indexOf(':') >= 0 || have.contains(subnet)) continue;
            have.add(subnet);
            all.add(subnet);
        }
        return all;
    }
}
