package org.freetunnel.android;

import java.util.List;

/**
 * Moving a row of the Configs page by dragging it: where it would land, how the others make
 * way, and what the list is after. The desktop's drag (ConfigsPage.qml) works the same way.
 *
 * <p>No Android classes: a plain JVM can load and test it.
 */
final class Reorder {
    private Reorder() { }

    /** The slot a row dragged {@code dy} pixels from slot {@code index} is over, among {@code count}, rows being {@code pitch} apart. */
    static int slotOver(int index, float dy, int pitch, int count) {
        if (count <= 0 || pitch <= 0) return Math.max(0, index);
        return Math.max(0, Math.min(count - 1, index + Math.round(dy / pitch)));
    }

    /**
     * How many rows, up (-1), down (1) or none (0), the row at {@code index} moves while the
     * row at {@code from} is dragged over slot {@code to}: those between make way for it.
     */
    static int shift(int index, int from, int to) {
        if (from < 0 || index == from) return 0;
        if (from < to && index > from && index <= to) return -1;
        if (from > to && index < from && index >= to) return 1;
        return 0;
    }

    /**
     * Moves the item at {@code from} to {@code to} and returns where the item that was at
     * {@code active} is now: the selection stays on the same config when the order changes.
     */
    static <T> int move(List<T> list, int from, int to, int active) {
        if (from < 0 || to < 0 || from >= list.size() || to >= list.size() || from == to) return active;
        T moved = list.remove(from);
        list.add(to, moved);
        if (active == from) return to;
        if (from < to && active > from && active <= to) return active - 1;
        if (from > to && active >= to && active < from) return active + 1;
        return active;
    }
}
