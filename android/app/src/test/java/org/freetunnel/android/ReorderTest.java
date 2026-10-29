package org.freetunnel.android;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

// Dragging a config row: the slot it is over, how the others make way, and the list afterwards with
// the selection still on the same config.
public final class ReorderTest {
    private static List<String> list() { return new ArrayList<>(Arrays.asList("A", "B", "C", "D")); }

    @Test public void aRowIsOverTheSlotItHasBeenDraggedTo() {
        assertEquals(1, Reorder.slotOver(1, 10, 56, 4));
        assertEquals(1, Reorder.slotOver(1, -27, 56, 4));
        assertEquals(2, Reorder.slotOver(1, 29, 56, 4));
        assertEquals(0, Reorder.slotOver(1, -56, 56, 4));
        assertEquals("Never past the first", 0, Reorder.slotOver(1, -500, 56, 4));
        assertEquals("Never past the last", 3, Reorder.slotOver(1, 500, 56, 4));
        assertEquals(0, Reorder.slotOver(0, 100, 56, 1));
    }

    @Test public void theRowsBetweenMakeWayForTheOneBeingDragged() {
        // C (2) dragged up to the slot of A (0): A and B move down.
        assertEquals(1, Reorder.shift(0, 2, 0));
        assertEquals(1, Reorder.shift(1, 2, 0));
        assertEquals(0, Reorder.shift(2, 2, 0));
        assertEquals(0, Reorder.shift(3, 2, 0));
        // A (0) dragged down to the slot of C (2): B and C move up.
        assertEquals(0, Reorder.shift(0, 0, 2));
        assertEquals(-1, Reorder.shift(1, 0, 2));
        assertEquals(-1, Reorder.shift(2, 0, 2));
        assertEquals(0, Reorder.shift(3, 0, 2));
        // Nothing dragged, or dragged over its own slot.
        for (int i = 0; i < 4; i++) { assertEquals(0, Reorder.shift(i, -1, -1)); assertEquals(0, Reorder.shift(i, 1, 1)); }
    }

    @Test public void theListIsMovedAndTheSelectionStaysOnTheSameConfig() {
        for (int active = 0; active < 4; active++) {
            for (int from = 0; from < 4; from++) {
                for (int to = 0; to < 4; to++) {
                    List<String> items = list();
                    String selected = items.get(active);
                    int after = Reorder.move(items, from, to, active);
                    assertEquals("move " + from + " to " + to + ", selected " + active + ", gives " + items, selected, items.get(after));
                    if (from != to) assertEquals(list().get(from), items.get(to));
                }
            }
        }
    }

    @Test public void aMoveThatIsNoMoveChangesNothing() {
        List<String> items = list();
        assertEquals(2, Reorder.move(items, 1, 1, 2));
        assertEquals(2, Reorder.move(items, -1, 2, 2));
        assertEquals(2, Reorder.move(items, 0, 9, 2));
        assertEquals(list(), items);
    }
}
