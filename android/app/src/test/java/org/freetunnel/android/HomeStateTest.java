package org.freetunnel.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

// What Home shows for each state of the VPN: the logo's opacity, its pulse and the line under it.
public final class HomeStateTest {
    @Test public void theLogoIsDimWhenOffBrightWhenConnectedAndInBetweenOtherwise() {
        assertEquals(0.5f, HomeState.alpha(0, false), 0f);
        assertEquals(1.0f, HomeState.alpha(2, false), 0f);
        for (int state : new int[]{1, 3, 4, 5, 6}) assertEquals("state " + state, 0.7f, HomeState.alpha(state, false), 0f);
        // The desktop's disconnecting: the logo fades from connected to the middle one at once.
        assertEquals(0.7f, HomeState.alpha(2, true), 0f);
        assertEquals(0.7f, HomeState.alpha(0, true), 0f);
    }

    // The desktop's backend is "connecting" while its tunnel reconnects, and its logo pulses then: so does a blocked one here.
    @Test public void itPulsesWhileConnectingReconnectingAndDisconnectingButNotWhileOffOrUp() {
        for (int state : new int[]{1, 3, 4, 5, 6}) assertTrue("state " + state, HomeState.pulsing(state, false));
        for (int state : new int[]{0, 2}) assertFalse("state " + state, HomeState.pulsing(state, false));
        assertTrue(HomeState.pulsing(2, true));
        assertTrue(HomeState.pulsing(6, true));
    }

    @Test public void aSessionIsActiveFromTheFirstStateToTheLastAndWhileStopping() {
        assertFalse(HomeState.sessionActive(0, false));
        for (int state = 1; state <= 6; state++) assertTrue("state " + state, HomeState.sessionActive(state, false));
        assertTrue(HomeState.sessionActive(0, true));
    }

    @Test public void theLineSaysConnectingDisconnectingOrTheTimeAndTrafficBlockedIsConnectingAsOnTheDesktop() {
        assertEquals(HomeState.Line.NONE, HomeState.line(0, false));
        for (int state : new int[]{1, 3, 4, 5, 6}) assertEquals("state " + state, HomeState.Line.CONNECTING, HomeState.line(state, false));
        assertEquals(HomeState.Line.TIMER, HomeState.line(2, false));
        // Stopping wins over whatever state the service still reports.
        for (int state = 0; state <= 6; state++) assertEquals("state " + state, HomeState.Line.DISCONNECTING, HomeState.line(state, true));
    }
}
