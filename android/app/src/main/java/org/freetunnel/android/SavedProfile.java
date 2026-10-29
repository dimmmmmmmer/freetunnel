package org.freetunnel.android;

import android.content.SharedPreferences;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Single selected-profile lookup for launcher, tile, and Android Always-on: the config the app has selected, and the
 * split profile that config uses. The app, the tile and Always-on all run the VPN from this, so they route alike.
 */
final class SavedProfile {
    /** The selected config's TOML, and the name of the split profile it uses. */
    static final class Choice {
        final String toml;
        final String profile;
        Choice(String toml, String profile) { this.toml = toml; this.profile = profile; }
    }

    static Choice selected(SharedPreferences prefs) throws Exception {
        JSONArray configs = new JSONArray(ConfigStore.read(prefs));
        if (configs.length() == 0) throw new IllegalStateException("Сначала добавьте конфиг");
        int index = Math.max(0, Math.min(prefs.getInt("active", 0), configs.length() - 1));
        JSONObject chosen = configs.getJSONObject(index);
        return new Choice(chosen.getString("toml"), profileOf(chosen));
    }

    /**
     * The split profile the selected config uses, as it is named there; Default if there is no config, or the
     * configs cannot be read. A config saved before there were profiles names none, and uses Default.
     */
    static String profile(SharedPreferences prefs) {
        try { return selected(prefs).profile; }
        catch (Exception unreadable) { return SplitProfiles.DEFAULT; }
    }

    /** What a saved config says of its profile. */
    static String profileOf(JSONObject config) {
        String name = config.optString("profile", SplitProfiles.DEFAULT).trim();
        return name.isEmpty() ? SplitProfiles.DEFAULT : name;
    }
}
