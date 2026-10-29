package org.freetunnel.android;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

/** The preferences SplitStore keeps the profiles in, in memory: what its tests, in Java and in Kotlin, use in their place. */
final class MemoryBacking implements SplitStore.Backing {
    final Map<String, Object> data = new HashMap<>();
    /** How many commits went through. */
    int commits;
    /** True to have every commit refused, as a full disk would. */
    boolean refuse;

    @Override public synchronized boolean contains(String key) { return data.containsKey(key); }

    @Override public synchronized String getString(String key) {
        Object value = data.get(key);
        return value instanceof String ? (String) value : null;
    }

    @Override public synchronized Set<String> getStringSet(String key) {
        Object value = data.get(key);
        if (!(value instanceof Set)) return null;
        Set<String> copy = new HashSet<>();
        for (Object item : (Set<?>) value) copy.add((String) item);
        return copy;
    }

    @Override public synchronized boolean commit(Map<String, Object> writes) {
        if (refuse) return false;
        commits++;
        for (Map.Entry<String, Object> write : writes.entrySet()) {
            Object value = write.getValue();
            data.put(write.getKey(), value instanceof Set ? new HashSet<Object>((Set<?>) value) : value);
        }
        return true;
    }

    synchronized Set<String> set(String key) { return getStringSet(key); }

    MemoryBacking with(String key, Object value) {
        data.put(key, value);
        return this;
    }
}
