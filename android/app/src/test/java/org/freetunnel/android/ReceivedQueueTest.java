package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.concurrent.atomic.AtomicInteger;

// The texts Receive from phone took and the TV has not answered: whose turn it is, and the places they
// hold in the count that the receiver turns sends away by.
public final class ReceivedQueueTest {
    private final AtomicInteger waiting = new AtomicInteger();
    private final ReceivedQueue queue = new ReceivedQueue(waiting);

    // As the receiver does when it takes a text, before it hands it over.
    private void take(String text) {
        waiting.incrementAndGet();
        queue.add(text, null, "192.168.1.5");
    }

    @Test public void theCountIsTheOneTheReceiverIsGiven() {
        assertSame(waiting, queue.waiting());
    }

    @Test public void onlyOneIsAskedAboutAtATimeAndInTheOrderTheyCame() {
        assertNull("Nothing waits", queue.next());
        take("one"); take("two"); take("three");
        assertEquals(3, waiting.get());
        ReceivedQueue.Item first = queue.next();
        assertEquals("one", first.text);
        assertTrue(queue.busy());
        assertEquals("Two are waiting for their turn", 2, queue.queued());
        // A check bumps the number that cancels the one running, so the second is not taken up yet.
        assertNull(queue.next());
        assertNull(queue.next());
        assertEquals(3, waiting.get());
        assertTrue(queue.answered(first));
        assertFalse(queue.busy());
        ReceivedQueue.Item second = queue.next();
        assertEquals("two", second.text);
        assertNull(queue.next());
        assertTrue(queue.answered(second));
        ReceivedQueue.Item third = queue.next();
        assertEquals("three", third.text);
        assertTrue(queue.answered(third));
        assertNull(queue.next());
        assertFalse(queue.busy());
        assertEquals("Each answer gave its place back", 0, waiting.get());
    }

    @Test public void eachAnswerFreesOnePlaceAndAnAnswerIsCountedOnce() {
        for (int i = 0; i < LinkReceiver.MAX_WAITING; i++) take("text " + i);
        assertEquals(LinkReceiver.MAX_WAITING, waiting.get());
        ReceivedQueue.Item item = queue.next();
        assertEquals("The one asked about holds its place", LinkReceiver.MAX_WAITING, waiting.get());
        assertTrue(queue.answered(item));
        assertEquals(LinkReceiver.MAX_WAITING - 1, waiting.get());
        // A second answer to the same text, as when a confirmation is cancelled and also closed, frees nothing more.
        assertFalse(queue.answered(item));
        assertFalse(queue.answered(item));
        assertEquals(LinkReceiver.MAX_WAITING - 1, waiting.get());
        // And it does not free the place of the one that is asked about next.
        ReceivedQueue.Item next = queue.next();
        assertFalse(queue.answered(item));
        assertTrue("The next is still being asked about", queue.busy());
        assertEquals(LinkReceiver.MAX_WAITING - 1, waiting.get());
        assertTrue(queue.answered(next));
        assertEquals(LinkReceiver.MAX_WAITING - 2, waiting.get());
    }

    @Test public void leavingTheAppDropsWhatWaitsAndKeepsTheQuestionThatIsUp() {
        take("one"); take("two"); take("three");
        ReceivedQueue.Item asked = queue.next();
        assertEquals(2, queue.drop());
        assertEquals("Two places given back, the one being asked about holds its own", 1, waiting.get());
        assertEquals(0, queue.queued());
        assertTrue("Its question is still up", queue.busy());
        assertNull("and nothing follows it", queue.next());
        assertTrue(queue.answered(asked));
        assertEquals(0, waiting.get());
        assertNull(queue.next());
        // Nothing to drop is nothing to give back.
        assertEquals(0, queue.drop());
        assertEquals(0, waiting.get());
        // And the queue works after: the next session's texts are asked about as ever.
        take("four");
        assertEquals("four", queue.next().text);
    }

    @Test public void aTextHandedOverAfterTheAppWasLeftGivesItsPlaceBack() {
        // The receiver took it, and the main thread, which saw the app left first, did not queue it.
        waiting.incrementAndGet();
        queue.giveBack();
        assertEquals(0, waiting.get());
        assertNull(queue.next());
    }

    @Test public void theFileNameAndTheSenderComeWithTheText() {
        waiting.incrementAndGet();
        queue.add("[endpoint]", "home.toml", "10.0.0.7");
        ReceivedQueue.Item item = queue.next();
        assertEquals("[endpoint]", item.text);
        assertEquals("home.toml", item.fileName);
        assertEquals("10.0.0.7", item.sender);
    }
}
