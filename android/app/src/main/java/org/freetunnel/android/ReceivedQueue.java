package org.freetunnel.android;

import java.util.ArrayDeque;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * The texts Receive from phone took that the TV has not answered yet, in the order they came, and
 * which of them the TV is being asked about. Plain JDK, and used on the main thread only, except the
 * count, which the receiver adds to from its own thread.
 *
 * <p>One at a time: a second text is not taken up until the first is answered, so that they are asked about
 * in the order they came and the question on the screen is never replaced. A text holds a place in
 * {@link #waiting()} from the moment the receiver takes it until it is answered, dropped or given
 * back, whatever the answer: the receiver adds the place when it takes the text (LinkReceiver),
 * and turns a send away at LinkReceiver.MAX_WAITING of them. Nothing here adds a configuration: it
 * only says whose turn it is.
 */
final class ReceivedQueue {
    static final class Item {
        final String text;
        /** The name of the file the text came in; null when it was pasted or has none. */
        final String fileName;
        final String sender;
        private boolean answered;
        Item(String text, String fileName, String sender) { this.text = text; this.fileName = fileName; this.sender = sender; }
    }

    private final AtomicInteger waiting;
    private final ArrayDeque<Item> queue = new ArrayDeque<>();
    private Item current;

    /** waiting is what the receiver adds a place to for each text it takes. */
    ReceivedQueue(AtomicInteger waiting) { this.waiting = waiting; }

    AtomicInteger waiting() { return waiting; }

    /** A text the receiver took and handed over, to wait for its turn. */
    void add(String text, String fileName, String sender) { queue.add(new Item(text, fileName, sender)); }

    /**
     * The text to ask about now, if none is being asked about and one is waiting; null otherwise. It
     * stays the one being asked about until answered().
     */
    Item next() {
        if (current != null) return null;
        current = queue.poll();
        return current;
    }

    /** True while a text is being checked or asked about. */
    boolean busy() { return current != null; }

    /** How many are waiting for their turn, not counting the one being asked about. */
    int queued() { return queue.size(); }

    /**
     * The TV answered it, in whichever way: added, replaced, cancelled or refused. Frees its place,
     * and the next may be taken. Only the first call for an item does anything, and the result says
     * whether this was it.
     */
    boolean answered(Item item) {
        if (item.answered) return false;
        item.answered = true;
        if (current == item) current = null;
        waiting.decrementAndGet();
        return true;
    }

    /** A text the receiver took that is not going to be asked about, as when the app was left first: gives its place back. */
    void giveBack() { waiting.decrementAndGet(); }

    /**
     * The app is left: what waits for its turn is dropped, and its places given back. The one being
     * asked about stays, with its question, and its place until it is answered. Returns how many.
     */
    int drop() {
        int dropped = queue.size();
        queue.clear();
        if (dropped > 0) waiting.addAndGet(-dropped);
        return dropped;
    }
}
