package org.freetunnel.android;

import android.content.SharedPreferences;

import java.util.HashSet;
import java.util.HashMap;
import java.util.Map;
import java.util.Set;

/** The app's preferences, as SplitStore reaches them. */
final class SplitPrefs implements SplitStore.Backing {
    private final SharedPreferences prefs;

    SplitPrefs(SharedPreferences prefs) { this.prefs = prefs; }

    @Override public boolean contains(String key) { return prefs.contains(key); }

    @Override public String getString(String key) { return prefs.getString(key, null); }

    // A copy: a set the preferences hand out must not be changed.
    @Override public Set<String> getStringSet(String key) {
        Set<String> set = prefs.getStringSet(key, null);
        return set == null ? null : new HashSet<>(set);
    }

    @Override public boolean commit(Map<String, Object> writes) {
        Map<String, Object> previous = new HashMap<>();
        Set<String> absent = new HashSet<>();
        Map<String, ?> all = prefs.getAll();
        for (String key : writes.keySet()) {
            if (all.containsKey(key)) {
                Object value = all.get(key);
                if (value instanceof Set) value = new HashSet<>((Set<?>) value);
                previous.put(key, value);
            } else absent.add(key);
        }
        SharedPreferences.Editor editor = prefs.edit();
        for (Map.Entry<String, Object> write : writes.entrySet()) {
            Object value = write.getValue();
            if (value instanceof String) editor.putString(write.getKey(), (String) value);
            else if (value instanceof Boolean) editor.putBoolean(write.getKey(), (Boolean) value);
            else if (value instanceof Set) {
                Set<String> copy = new HashSet<>();
                for (Object item : (Set<?>) value) copy.add((String) item);
                editor.putStringSet(write.getKey(), copy);
            } else throw new IllegalArgumentException("Not a preference: " + write.getKey());
        }
        if (editor.commit()) return true;

        // Android updates SharedPreferences' in-memory map before writing to disk. A false commit result
        // therefore needs a matching in-memory rollback so callers can safely treat this write as rejected.
        SharedPreferences.Editor rollback = prefs.edit();
        for (String key : absent) rollback.remove(key);
        for (Map.Entry<String, Object> entry : previous.entrySet()) put(rollback, entry.getKey(), entry.getValue());
        rollback.commit();
        return false;
    }

    private static void put(SharedPreferences.Editor editor, String key, Object value) {
        if (value instanceof String) editor.putString(key, (String) value);
        else if (value instanceof Boolean) editor.putBoolean(key, (Boolean) value);
        else if (value instanceof Integer) editor.putInt(key, (Integer) value);
        else if (value instanceof Long) editor.putLong(key, (Long) value);
        else if (value instanceof Float) editor.putFloat(key, (Float) value);
        else if (value instanceof Set) {
            Set<String> copy = new HashSet<>();
            for (Object item : (Set<?>) value) copy.add((String) item);
            editor.putStringSet(key, copy);
        } else throw new IllegalArgumentException("Not a preference: " + key);
    }
}
